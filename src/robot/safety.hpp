#pragma once

/// @file safety.hpp
/// @brief The safety envelope every command crosses before it reaches a device.
///
/// This layer is deliberately paranoid and deliberately dumb: it has no plan, no
/// memory and no opinion about intent. It answers one question — "would executing
/// this violate a limit?" — and it refuses rather than repairs. Silently clamping
/// a bad goal would turn a rejected motion into a *different* motion, which is how
/// robots hurt people.

#include <string>
#include <vector>

#include "common/types.hpp"
#include "robot/types.hpp"

namespace prodxcloud::robot {

enum class SafetyVerdict { ALLOW, REJECT, ESTOP };

struct SafetyCheck {
    SafetyVerdict verdict = SafetyVerdict::ALLOW;
    std::string   rule;     ///< which rule fired, empty when allowed
    std::string   message;

    [[nodiscard]] bool ok() const { return verdict == SafetyVerdict::ALLOW; }

    static SafetyCheck allow() { return {}; }
    static SafetyCheck reject(std::string rule, std::string msg) {
        return {SafetyVerdict::REJECT, std::move(rule), std::move(msg)};
    }
    static SafetyCheck estop(std::string rule, std::string msg) {
        return {SafetyVerdict::ESTOP, std::move(rule), std::move(msg)};
    }
};

/// A keep-out / keep-in region in the base frame.
struct WorkspaceBounds {
    double min_x = -1.0, max_x = 1.0;
    double min_y = -1.0, max_y = 1.0;
    double min_z =  0.0, max_z = 1.5;  ///< floor at z = 0 by default

    [[nodiscard]] bool contains(const Vec3& p) const {
        return p.x >= min_x && p.x <= max_x && p.y >= min_y && p.y <= max_y &&
               p.z >= min_z && p.z <= max_z;
    }
};

struct SafetyConfig {
    WorkspaceBounds workspace;
    double singularity_threshold = 1e-3;  ///< min manipulability before a move is refused
    double max_temperature_c     = 85.0;
    double max_speed_scale       = 1.0;
    bool   require_homed         = false;

    /// Collision detection threshold, as a fraction of each joint's rated torque.
    ///
    /// A motor cannot physically exceed its own torque limit — it saturates. So
    /// "effort > max_torque" is a test that can never fire. What a collision
    /// actually looks like is the servo *pinned at* its limit while it fails to
    /// track: the arm is pushing as hard as it can against something that will not
    /// move. Free-space motion never comes close (a few Nm), so the gap between
    /// normal effort and saturation is wide and unambiguous.
    double collision_torque_fraction = 0.90;
};

class SafetyMonitor {
public:
    SafetyMonitor(DeviceSpec spec, SafetyConfig config);

    /// Validate a joint-space goal: limits, workspace, singularity.
    [[nodiscard]] SafetyCheck check_joint_goal(const JointVector& goal) const;

    /// Validate a cartesian goal against the workspace envelope.
    [[nodiscard]] SafetyCheck check_pose_goal(const Pose& goal) const;

    /// Validate a sampled trajectory point against velocity/acceleration limits.
    [[nodiscard]] SafetyCheck check_motion(const JointVector& velocities,
                                           const JointVector& accelerations) const;

    /// Validate live device state — the continuous guard that runs every tick.
    [[nodiscard]] SafetyCheck check_state(const DeviceState& state) const;

    /// Full pre-flight for a command. This is the single entry point the
    /// controller uses; the finer-grained checks above exist for tests.
    [[nodiscard]] SafetyCheck check_command(const Command& cmd, const DeviceState& state) const;

    [[nodiscard]] const SafetyConfig& config() const { return config_; }

private:
    DeviceSpec   spec_;
    SafetyConfig config_;
};

}  // namespace prodxcloud::robot
