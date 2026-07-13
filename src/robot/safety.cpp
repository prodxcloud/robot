#include "robot/safety.hpp"

#include <cmath>

#include "robot/kinematics.hpp"

namespace prodxcloud::robot {

SafetyMonitor::SafetyMonitor(DeviceSpec spec, SafetyConfig config)
    : spec_(std::move(spec)), config_(std::move(config)) {}

SafetyCheck SafetyMonitor::check_joint_goal(const JointVector& goal) const {
    if (static_cast<int>(goal.size()) != spec_.dof) {
        return SafetyCheck::reject("joint_count",
                                   "expected " + std::to_string(spec_.dof) + " joint values, got " +
                                       std::to_string(goal.size()));
    }

    for (int i = 0; i < spec_.dof; ++i) {
        const double v = goal[static_cast<size_t>(i)];

        if (!std::isfinite(v)) {
            return SafetyCheck::reject("joint_finite",
                                       "joint " + std::to_string(i) + " goal is not finite");
        }

        const auto& lim = spec_.limits[static_cast<size_t>(i)];
        if (v < lim.min_position_rad || v > lim.max_position_rad) {
            return SafetyCheck::reject(
                "joint_limit",
                "joint " + std::to_string(i) + " goal " + std::to_string(rad_to_deg(v)) +
                    " deg is outside [" + std::to_string(rad_to_deg(lim.min_position_rad)) + ", " +
                    std::to_string(rad_to_deg(lim.max_position_rad)) + "] deg");
        }
    }

    // Arms have a tool that must stay inside the envelope; other kinds have no TCP.
    if (spec_.kind == DeviceKind::ARM) {
        const Kinematics kin(spec_);

        const Pose tcp = kin.forward_pose(goal);
        if (!config_.workspace.contains(tcp.position)) {
            return SafetyCheck::reject(
                "workspace",
                "goal drives the tool to (" + std::to_string(tcp.position.x) + ", " +
                    std::to_string(tcp.position.y) + ", " + std::to_string(tcp.position.z) +
                    ") m, outside the workspace envelope");
        }

        const double w = kin.manipulability(goal);
        if (w < config_.singularity_threshold) {
            return SafetyCheck::reject(
                "singularity",
                "goal is within a singularity (manipulability " + std::to_string(w) + " < " +
                    std::to_string(config_.singularity_threshold) + ")");
        }
    }

    return SafetyCheck::allow();
}

SafetyCheck SafetyMonitor::check_pose_goal(const Pose& goal) const {
    if (!std::isfinite(goal.position.x) || !std::isfinite(goal.position.y) ||
        !std::isfinite(goal.position.z)) {
        return SafetyCheck::reject("pose_finite", "target pose is not finite");
    }

    if (!config_.workspace.contains(goal.position)) {
        return SafetyCheck::reject(
            "workspace",
            "target (" + std::to_string(goal.position.x) + ", " + std::to_string(goal.position.y) +
                ", " + std::to_string(goal.position.z) + ") m is outside the workspace envelope");
    }

    const double radius = goal.position.norm();
    if (radius > spec_.reach_m) {
        return SafetyCheck::reject("reach",
                                   "target at " + std::to_string(radius) + " m exceeds the " +
                                       std::to_string(spec_.reach_m) + " m reach");
    }

    return SafetyCheck::allow();
}

SafetyCheck SafetyMonitor::check_motion(const JointVector& velocities,
                                        const JointVector& accelerations) const {
    const int n = std::min(static_cast<int>(velocities.size()), spec_.dof);

    for (int i = 0; i < n; ++i) {
        const auto&  lim = spec_.limits[static_cast<size_t>(i)];
        const double v   = std::abs(velocities[static_cast<size_t>(i)]);

        // A 1% tolerance absorbs the float noise of trajectory sampling without
        // letting a genuinely over-speed point through.
        if (v > lim.max_velocity * 1.01) {
            return SafetyCheck::estop("velocity_limit",
                                      "joint " + std::to_string(i) + " velocity " +
                                          std::to_string(v) + " rad/s exceeds limit " +
                                          std::to_string(lim.max_velocity) + " rad/s");
        }

        if (i < static_cast<int>(accelerations.size())) {
            const double a = std::abs(accelerations[static_cast<size_t>(i)]);
            if (a > lim.max_acceleration * 1.01) {
                return SafetyCheck::estop("acceleration_limit",
                                          "joint " + std::to_string(i) + " acceleration " +
                                              std::to_string(a) + " rad/s^2 exceeds limit " +
                                              std::to_string(lim.max_acceleration) + " rad/s^2");
            }
        }
    }

    return SafetyCheck::allow();
}

SafetyCheck SafetyMonitor::check_state(const DeviceState& state) const {
    if (state.estop) {
        return SafetyCheck::estop("estop_latched", "emergency stop is latched");
    }
    if (state.status == DeviceStatus::FAULTED) {
        return SafetyCheck::reject("faulted", "device is faulted: " + state.fault_message);
    }
    if (state.temperature_c > config_.max_temperature_c) {
        return SafetyCheck::estop("thermal",
                                  "device temperature " + std::to_string(state.temperature_c) +
                                      " C exceeds limit " +
                                      std::to_string(config_.max_temperature_c) + " C");
    }

    for (size_t i = 0; i < state.joints.size() && i < spec_.limits.size(); ++i) {
        const auto&  lim       = spec_.limits[i];
        const double e         = std::abs(state.joints[i].effort);
        const double threshold = lim.max_torque * config_.collision_torque_fraction;

        if (e > threshold) {
            return SafetyCheck::estop(
                "collision",
                "joint " + std::to_string(i) + " is saturated at " + std::to_string(e) +
                    " Nm (>" + std::to_string(threshold) + " Nm, " +
                    std::to_string(config_.collision_torque_fraction * 100.0) +
                    "% of its " + std::to_string(lim.max_torque) +
                    " Nm rating) — the arm is pushing against something");
        }
    }

    return SafetyCheck::allow();
}

SafetyCheck SafetyMonitor::check_command(const Command& cmd, const DeviceState& state) const {
    // RESET is the one command that is allowed to run *because* the device is
    // unsafe — it is how an operator clears the latch. ESTOP is always allowed.
    if (cmd.type == CommandType::RESET || cmd.type == CommandType::ESTOP) {
        return SafetyCheck::allow();
    }

    if (const SafetyCheck s = check_state(state); !s.ok()) return s;

    if (state.status == DeviceStatus::OFFLINE) {
        return SafetyCheck::reject("offline", "device is offline — provision and boot it first");
    }

    if (cmd.speed_scale <= 0.0 || cmd.speed_scale > config_.max_speed_scale) {
        return SafetyCheck::reject("speed_scale",
                                   "speed_scale must be in (0, " +
                                       std::to_string(config_.max_speed_scale) + "]");
    }
    if (cmd.accel_scale <= 0.0 || cmd.accel_scale > 1.0) {
        return SafetyCheck::reject("accel_scale", "accel_scale must be in (0, 1]");
    }

    switch (cmd.type) {
        case CommandType::MOVE_JOINT:
            return check_joint_goal(cmd.joint_goal);

        case CommandType::MOVE_LINEAR:
            return check_pose_goal(cmd.pose_goal);

        case CommandType::GRIP: {
            if (spec_.kind != DeviceKind::GRIPPER) {
                return SafetyCheck::reject("wrong_device_kind",
                                           "GRIP is only valid on a gripper, not a " +
                                               std::string(device_kind_to_string(spec_.kind)));
            }
            const auto& lim = spec_.limits[0];
            if (cmd.grip_width_m < lim.min_position_rad || cmd.grip_width_m > lim.max_position_rad) {
                return SafetyCheck::reject("grip_stroke",
                                           "grip width " + std::to_string(cmd.grip_width_m) +
                                               " m is outside the jaw stroke");
            }
            return SafetyCheck::allow();
        }

        case CommandType::DWELL:
            if (cmd.dwell_s < 0.0 || cmd.dwell_s > 3600.0) {
                return SafetyCheck::reject("dwell", "dwell must be in [0, 3600] s");
            }
            return SafetyCheck::allow();

        case CommandType::HOME:
            return check_joint_goal(spec_.home.empty()
                                        ? JointVector(static_cast<size_t>(spec_.dof), 0.0)
                                        : spec_.home);

        case CommandType::ESTOP:
        case CommandType::RESET:
            return SafetyCheck::allow();
    }

    return SafetyCheck::reject("unknown_command", "unrecognised command type");
}

}  // namespace prodxcloud::robot
