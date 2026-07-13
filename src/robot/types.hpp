#pragma once

/// @file types.hpp
/// @brief Core value types for the robot control plane: joints, poses, devices,
///        commands and telemetry.
///
/// Everything here is a plain value type — no I/O, no allocation in the hot path —
/// so it is safe to pass through the real-time control loop.

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "common/types.hpp"

namespace prodxcloud::robot {

// ─── Constants ──────────────────────────────────────────────────────────────

inline constexpr int    kMaxJoints  = 8;
inline constexpr double kPi         = 3.14159265358979323846;
inline constexpr double kDefaultHz  = 1000.0;  // 1 kHz control loop

constexpr double deg_to_rad(double d) { return d * kPi / 180.0; }
constexpr double rad_to_deg(double r) { return r * 180.0 / kPi; }

// ─── Linear algebra (fixed-size, stack-allocated) ───────────────────────────

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }

    [[nodiscard]] double norm() const { return std::sqrt(x * x + y * y + z * z); }
    [[nodiscard]] double dist(const Vec3& o) const { return (*this - o).norm(); }
};

/// Row-major 4x4 homogeneous transform.
struct Mat4 {
    std::array<double, 16> m{};

    static Mat4 identity() {
        Mat4 r;
        r.m = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        return r;
    }

    double& at(int row, int col) { return m[static_cast<size_t>(row * 4 + col)]; }
    [[nodiscard]] double at(int row, int col) const { return m[static_cast<size_t>(row * 4 + col)]; }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                double s = 0.0;
                for (int k = 0; k < 4; ++k) s += at(i, k) * o.at(k, j);
                r.at(i, j) = s;
            }
        }
        return r;
    }

    [[nodiscard]] Vec3 translation() const { return {at(0, 3), at(1, 3), at(2, 3)}; }
};

/// Cartesian pose: position + RPY orientation (radians).
struct Pose {
    Vec3   position;
    double roll  = 0.0;
    double pitch = 0.0;
    double yaw   = 0.0;

    static Pose from_matrix(const Mat4& t);
    [[nodiscard]] Mat4 to_matrix() const;
};

// ─── Joints ─────────────────────────────────────────────────────────────────

/// Per-joint kinematic and dynamic limits. The safety layer treats these as hard
/// bounds — a command that violates them is rejected, never silently truncated
/// into a different motion.
struct JointLimits {
    double min_position_rad = -kPi;
    double max_position_rad = kPi;
    double max_velocity     = 3.14;   // rad/s
    double max_acceleration = 10.0;   // rad/s^2
    double max_torque       = 150.0;  // Nm
};

/// Instantaneous state of a single joint.
struct JointState {
    double position = 0.0;  // rad
    double velocity = 0.0;  // rad/s
    double effort   = 0.0;  // Nm
};

/// Joint-space vector sized to the device's DOF.
using JointVector = std::vector<double>;

// ─── Device model ───────────────────────────────────────────────────────────

enum class DeviceKind { ARM, GRIPPER, MOBILE_BASE, SENSOR, CONVEYOR };

constexpr std::string_view device_kind_to_string(DeviceKind k) {
    switch (k) {
        case DeviceKind::ARM:         return "arm";
        case DeviceKind::GRIPPER:     return "gripper";
        case DeviceKind::MOBILE_BASE: return "mobile_base";
        case DeviceKind::SENSOR:      return "sensor";
        case DeviceKind::CONVEYOR:    return "conveyor";
    }
    return "unknown";
}

enum class DeviceStatus { OFFLINE, BOOTING, IDLE, MOVING, FAULTED, ESTOPPED };

constexpr std::string_view device_status_to_string(DeviceStatus s) {
    switch (s) {
        case DeviceStatus::OFFLINE:  return "offline";
        case DeviceStatus::BOOTING:  return "booting";
        case DeviceStatus::IDLE:     return "idle";
        case DeviceStatus::MOVING:   return "moving";
        case DeviceStatus::FAULTED:  return "faulted";
        case DeviceStatus::ESTOPPED: return "estopped";
    }
    return "unknown";
}

/// Denavit-Hartenberg parameters for one link.
struct DHParam {
    double a     = 0.0;  // link length (m)
    double d     = 0.0;  // link offset (m)
    double alpha = 0.0;  // link twist (rad)
    double theta_offset = 0.0;  // constant offset added to the joint variable (rad)
};

/// Static description of a device. Immutable once registered.
struct DeviceSpec {
    std::string              id;
    std::string              name;
    DeviceKind               kind = DeviceKind::ARM;
    std::string              model;      // e.g. "vx-arm6"
    int                      dof  = 6;
    std::vector<DHParam>     links;      // size == dof for arms
    std::vector<JointLimits> limits;     // size == dof
    /// The pose HOME returns to. Not the zero configuration: a 6R arm at all-zeros
    /// is fully outstretched, which is a boundary singularity the safety layer
    /// (rightly) refuses to drive into. Home is a tucked, well-conditioned pose.
    JointVector              home;
    double                   payload_kg    = 5.0;
    double                   reach_m       = 0.85;
    double                   control_hz    = kDefaultHz;
    /// Node this device is bound to. Empty means "not yet provisioned" — the
    /// controller will ask vxnode for a node before the device can go online.
    std::string              node_id;

    /// A UR5-class 6-DOF arm — the default simulated device.
    static DeviceSpec vx_arm6(std::string id, std::string name);
    /// A parallel-jaw gripper (1 DOF, prismatic, modelled as a revolute stand-in).
    static DeviceSpec vx_gripper(std::string id, std::string name);
};

/// Live state of a device. Snapshotted out of the control loop under a lock.
struct DeviceState {
    std::string             device_id;
    DeviceStatus            status = DeviceStatus::OFFLINE;
    std::vector<JointState> joints;
    Pose                    tcp;             // tool centre point (forward kinematics)
    bool                    estop = false;
    std::string             fault_message;
    uint64_t                tick        = 0;
    double                  uptime_s    = 0.0;
    double                  temperature_c = 30.0;
};

// ─── Commands ───────────────────────────────────────────────────────────────

enum class CommandType {
    MOVE_JOINT,   ///< joint-space goal
    MOVE_LINEAR,  ///< cartesian goal, solved through IK
    HOME,         ///< return to the zero configuration
    GRIP,         ///< open/close a gripper
    DWELL,        ///< hold position for a duration
    ESTOP,        ///< latch emergency stop
    RESET         ///< clear a fault or a latched e-stop
};

constexpr std::string_view command_type_to_string(CommandType c) {
    switch (c) {
        case CommandType::MOVE_JOINT:  return "move_joint";
        case CommandType::MOVE_LINEAR: return "move_linear";
        case CommandType::HOME:        return "home";
        case CommandType::GRIP:        return "grip";
        case CommandType::DWELL:       return "dwell";
        case CommandType::ESTOP:       return "estop";
        case CommandType::RESET:       return "reset";
    }
    return "unknown";
}

struct Command {
    std::string id;
    std::string device_id;
    CommandType type = CommandType::MOVE_JOINT;

    JointVector joint_goal;                ///< MOVE_JOINT
    Pose        pose_goal;                 ///< MOVE_LINEAR
    double      grip_width_m   = 0.0;      ///< GRIP
    double      dwell_s        = 0.0;      ///< DWELL

    double speed_scale = 1.0;  ///< fraction of max velocity, (0, 1]
    double accel_scale = 1.0;  ///< fraction of max acceleration, (0, 1]
};

/// Outcome of executing a command.
struct CommandResult {
    std::string command_id;
    std::string device_id;
    bool        success = false;
    std::string message;
    double      duration_s      = 0.0;
    uint64_t    ticks_executed  = 0;
    Pose        final_pose;
    JointVector final_joints;
};

}  // namespace prodxcloud::robot
