#include "robot/controller.hpp"

#include <algorithm>
#include <cmath>

#include <spdlog/spdlog.h>

#include "common/uuid.hpp"

namespace prodxcloud::robot {

RobotController::RobotController(ControllerConfig config, vxnode::NodeConfig node)
    : config_(config), node_(std::move(node)) {
    spdlog::info("robot controller up: {} Hz control loop, vxnode at {}",
                 config_.control_hz, node_.config().base_url);
}

// ─── Fleet ──────────────────────────────────────────────────────────────────

Result<std::string> RobotController::add_device(DeviceSpec spec, SimConfig sim, SafetyConfig safety) {
    if (spec.id.empty()) spec.id = "dev-" + generate_uuid().substr(0, 8);

    if (spec.dof < 1 || spec.dof > kMaxJoints) {
        return std::unexpected(Error::validation(
            "device '" + spec.id + "' has " + std::to_string(spec.dof) +
            " DOF; must be in [1, " + std::to_string(kMaxJoints) + "]"));
    }
    if (static_cast<int>(spec.limits.size()) != spec.dof) {
        return std::unexpected(Error::validation(
            "device '" + spec.id + "' declares " + std::to_string(spec.dof) +
            " DOF but supplies " + std::to_string(spec.limits.size()) + " joint limits"));
    }

    std::lock_guard lock(mutex_);
    if (devices_.count(spec.id)) {
        return std::unexpected(Error::bad_request("device '" + spec.id + "' already registered"));
    }

    ManagedDevice dev;
    dev.spec       = spec;
    dev.hardware   = std::make_unique<SimulatedDevice>(spec, sim);
    dev.kinematics = std::make_unique<Kinematics>(spec);
    dev.safety     = std::make_unique<SafetyMonitor>(spec, safety);
    dev.planner    = std::make_unique<TrajectoryPlanner>(spec);

    const std::string id = spec.id;
    devices_.emplace(id, std::move(dev));

    spdlog::info("registered device '{}' ({}, {} DOF, {})", id, spec.model, spec.dof,
                 device_kind_to_string(spec.kind));
    return id;
}

Result<void> RobotController::remove_device(const std::string& device_id) {
    std::lock_guard lock(mutex_);
    if (devices_.erase(device_id) == 0) {
        return std::unexpected(Error::not_found("no such device: " + device_id));
    }
    return {};
}

std::vector<std::string> RobotController::device_ids() const {
    std::lock_guard lock(mutex_);

    std::vector<std::string> ids;
    ids.reserve(devices_.size());
    for (const auto& [id, _] : devices_) ids.push_back(id);

    std::sort(ids.begin(), ids.end());
    return ids;
}

Result<DeviceState> RobotController::state(const std::string& device_id) const {
    std::lock_guard lock(mutex_);

    const auto it = devices_.find(device_id);
    if (it == devices_.end()) {
        return std::unexpected(Error::not_found("no such device: " + device_id));
    }
    return it->second.hardware->state();
}

Result<DeviceSpec> RobotController::spec(const std::string& device_id) const {
    std::lock_guard lock(mutex_);

    const auto it = devices_.find(device_id);
    if (it == devices_.end()) {
        return std::unexpected(Error::not_found("no such device: " + device_id));
    }
    return it->second.spec;
}

Result<void> RobotController::boot(const std::string& device_id) {
    std::lock_guard lock(mutex_);

    const auto it = devices_.find(device_id);
    if (it == devices_.end()) {
        return std::unexpected(Error::not_found("no such device: " + device_id));
    }

    it->second.hardware->boot();
    spdlog::info("device '{}' booted to home", device_id);
    return {};
}

void RobotController::estop_all(const std::string& reason) {
    std::lock_guard lock(mutex_);

    for (auto& [id, dev] : devices_) {
        dev.hardware->estop(reason);
        spdlog::error("E-STOP '{}': {}", id, reason);
    }

    AuditRecord r;
    r.timestamp    = now_iso8601();
    r.device_id    = "*";
    r.command_type = "estop";
    r.verdict      = "estop";
    r.rule         = "operator_estop";
    r.detail       = reason;
    audit_.push_back(r);
}

Result<void> RobotController::inject_disturbance(const std::string& device_id, int joint,
                                                 double torque_nm) {
    std::lock_guard lock(mutex_);

    const auto it = devices_.find(device_id);
    if (it == devices_.end()) {
        return std::unexpected(Error::not_found("no such device: " + device_id));
    }

    it->second.hardware->inject_disturbance(joint, torque_nm);
    return {};
}

// ─── Motion ─────────────────────────────────────────────────────────────────

void RobotController::record(const AuditRecord& r) {
    if (!config_.log_audit) return;

    audit_.push_back(r);
    if (audit_.size() > config_.audit_capacity) {
        audit_.erase(audit_.begin(), audit_.begin() + static_cast<long>(audit_.size() -
                                                                        config_.audit_capacity));
    }
}

Result<CommandResult> RobotController::run_trajectory(ManagedDevice&    dev,
                                                      const Command&    cmd,
                                                      const Trajectory& traj) {
    const double dt = 1.0 / std::max(1.0, dev.spec.control_hz);

    CommandResult res;
    res.command_id = cmd.id;
    res.device_id  = dev.spec.id;

    const auto total_ticks =
        static_cast<uint64_t>(std::ceil(traj.duration_s() / dt)) + 1;

    for (uint64_t t = 0; t < total_ticks; ++t) {
        const double now = static_cast<double>(t) * dt;
        const TrajectoryPoint p = traj.sample(now);

        // Feed the servo the position AND the feed-forward velocity: a pure
        // position loop lags a moving setpoint by design.
        dev.hardware->set_target(p.positions, p.velocities);
        dev.hardware->step(dt);

        const DeviceState s = dev.hardware->state();

        // The guard runs every tick, not just at admission. A collision mid-motion
        // is exactly the case the pre-flight check cannot see.
        if (const SafetyCheck check = dev.safety->check_state(s); !check.ok()) {
            dev.hardware->estop(check.message);

            AuditRecord a;
            a.timestamp    = now_iso8601();
            a.device_id    = dev.spec.id;
            a.command_id   = cmd.id;
            a.command_type = std::string(command_type_to_string(cmd.type));
            a.verdict      = "estop";
            a.rule         = check.rule;
            a.detail       = check.message;
            record(a);

            res.success        = false;
            res.message        = "E-STOP during motion [" + check.rule + "]: " + check.message;
            res.ticks_executed = t;
            res.duration_s     = now;
            res.final_joints   = dev.hardware->true_positions();
            res.final_pose     = s.tcp;
            return res;
        }

        ++res.ticks_executed;
    }

    // ── Settle: hold the goal until the arm has actually arrived ─────────────
    const JointVector goal = traj.points().back().positions;
    const auto settle_ticks = static_cast<uint64_t>(config_.settle_timeout_s / dt);

    bool     in_position = false;
    uint64_t settled_at  = 0;

    for (uint64_t t = 0; t < settle_ticks; ++t) {
        // Compute the error BEFORE stepping, so `settled_at` is the tick on which
        // the arm was first genuinely in position.
        double worst = 0.0;
        const JointVector& q = dev.hardware->true_positions();
        for (size_t i = 0; i < goal.size() && i < q.size(); ++i) {
            worst = std::max(worst, std::abs(q[i] - goal[i]));
        }

        if (worst <= config_.in_position_tolerance_rad) {
            in_position = true;
            settled_at  = t;
            break;
        }

        dev.hardware->set_target(goal);
        dev.hardware->step(dt);
        ++res.ticks_executed;

        if (const SafetyCheck check = dev.safety->check_state(dev.hardware->state()); !check.ok()) {
            dev.hardware->estop(check.message);

            AuditRecord a;
            a.timestamp    = now_iso8601();
            a.device_id    = dev.spec.id;
            a.command_id   = cmd.id;
            a.command_type = std::string(command_type_to_string(cmd.type));
            a.verdict      = "estop";
            a.rule         = check.rule;
            a.detail       = check.message;
            record(a);

            res.success      = false;
            res.message      = "E-STOP while settling [" + check.rule + "]: " + check.message;
            res.final_joints = dev.hardware->true_positions();
            res.final_pose   = dev.hardware->state().tcp;
            return res;
        }
    }

    const DeviceState final_state = dev.hardware->state();

    res.duration_s   = traj.duration_s() + static_cast<double>(settled_at) * dt;
    res.final_joints = dev.hardware->true_positions();
    res.final_pose   = final_state.tcp;

    if (!in_position) {
        // The servo could not converge on a goal it accepted. That is a real fault —
        // a jammed joint, a mistuned gain — and pretending the move succeeded would
        // hide it.
        res.success = false;
        res.message = "in-position timeout: the arm did not settle within " +
                      std::to_string(config_.settle_timeout_s) + " s of reaching the end of "
                      "its trajectory";
        return res;
    }

    res.success = true;
    res.message = "completed in " + std::to_string(res.duration_s) + " s (" +
                  std::to_string(res.ticks_executed) + " ticks, settled in " +
                  std::to_string(static_cast<double>(settled_at) * dt * 1000.0) + " ms)";
    return res;
}

Result<CommandResult> RobotController::execute(const Command& cmd) {
    std::lock_guard lock(mutex_);

    const auto it = devices_.find(cmd.device_id);
    if (it == devices_.end()) {
        return std::unexpected(Error::not_found("no such device: " + cmd.device_id));
    }
    ManagedDevice& dev = it->second;

    const DeviceState before = dev.hardware->state();

    // ── The gate. Nothing moves without passing here. ────────────────────────
    const SafetyCheck check = dev.safety->check_command(cmd, before);

    AuditRecord audit;
    audit.timestamp    = now_iso8601();
    audit.device_id    = cmd.device_id;
    audit.command_id   = cmd.id;
    audit.command_type = std::string(command_type_to_string(cmd.type));
    audit.rule         = check.rule;
    audit.detail       = check.message;

    if (!check.ok()) {
        audit.verdict = check.verdict == SafetyVerdict::ESTOP ? "estop" : "reject";
        record(audit);

        if (check.verdict == SafetyVerdict::ESTOP) {
            dev.hardware->estop(check.message);
        }

        spdlog::warn("REFUSED {} on '{}' [{}]: {}", audit.command_type, cmd.device_id,
                     check.rule, check.message);

        return std::unexpected(Error{422, "command refused by safety [" + check.rule + "]",
                                     check.message});
    }

    audit.verdict = "allow";
    record(audit);

    // ── Dispatch ─────────────────────────────────────────────────────────────
    switch (cmd.type) {
        case CommandType::ESTOP: {
            dev.hardware->estop("commanded e-stop");

            CommandResult r;
            r.command_id   = cmd.id;
            r.device_id    = cmd.device_id;
            r.success      = true;
            r.message      = "emergency stop latched";
            r.final_joints = dev.hardware->true_positions();
            return r;
        }

        case CommandType::RESET: {
            dev.hardware->reset();

            CommandResult r;
            r.command_id   = cmd.id;
            r.device_id    = cmd.device_id;
            r.success      = true;
            r.message      = "fault cleared; device is idle";
            r.final_joints = dev.hardware->true_positions();
            return r;
        }

        case CommandType::DWELL: {
            const auto traj = dev.planner->plan_dwell(dev.hardware->true_positions(), cmd.dwell_s);
            if (!traj) return std::unexpected(traj.error());
            return run_trajectory(dev, cmd, *traj);
        }

        case CommandType::HOME: {
            const JointVector goal = dev.spec.home.empty()
                                         ? JointVector(static_cast<size_t>(dev.spec.dof), 0.0)
                                         : dev.spec.home;
            const auto traj = dev.planner->plan_joint_move(dev.hardware->true_positions(), goal,
                                                           cmd.speed_scale, cmd.accel_scale);
            if (!traj) return std::unexpected(traj.error());
            return run_trajectory(dev, cmd, *traj);
        }

        case CommandType::MOVE_JOINT: {
            const auto traj = dev.planner->plan_joint_move(dev.hardware->true_positions(),
                                                           cmd.joint_goal, cmd.speed_scale,
                                                           cmd.accel_scale);
            if (!traj) return std::unexpected(traj.error());
            return run_trajectory(dev, cmd, *traj);
        }

        case CommandType::GRIP: {
            JointVector goal(static_cast<size_t>(dev.spec.dof), 0.0);
            goal[0] = cmd.grip_width_m;

            const auto traj = dev.planner->plan_joint_move(dev.hardware->true_positions(), goal,
                                                           cmd.speed_scale, cmd.accel_scale);
            if (!traj) return std::unexpected(traj.error());
            return run_trajectory(dev, cmd, *traj);
        }

        case CommandType::MOVE_LINEAR: {
            // Cartesian goals are solved into joint space here, seeded from where the
            // arm actually is — so the solution is the one closest to the current
            // configuration rather than an arbitrary branch of the IK.
            const auto sol = dev.kinematics->inverse(cmd.pose_goal, dev.hardware->true_positions());
            if (!sol) return std::unexpected(sol.error());

            // The IK answer is a *new* joint goal that safety has never seen. Check it.
            if (const SafetyCheck post = dev.safety->check_joint_goal(sol->joints); !post.ok()) {
                AuditRecord a = audit;
                a.verdict     = "reject";
                a.rule        = post.rule;
                a.detail      = "IK solution rejected: " + post.message;
                record(a);

                return std::unexpected(Error{422, "IK solution refused by safety [" + post.rule + "]",
                                             post.message});
            }

            const auto traj = dev.planner->plan_joint_move(dev.hardware->true_positions(),
                                                           sol->joints, cmd.speed_scale,
                                                           cmd.accel_scale);
            if (!traj) return std::unexpected(traj.error());

            auto result = run_trajectory(dev, cmd, *traj);
            if (result) {
                result->message += "; IK converged in " + std::to_string(sol->iterations) +
                                   " iterations (residual " +
                                   std::to_string(sol->position_error_m * 1000.0) + " mm)";
            }
            return result;
        }
    }

    return std::unexpected(Error::bad_request("unhandled command type"));
}

// ─── Brain ──────────────────────────────────────────────────────────────────

Result<size_t> RobotController::load_brain(const std::string& csv_path) {
    const auto n = brain_.load_csv(csv_path);
    if (!n) return std::unexpected(n.error());

    spdlog::info("brain online: {} knowledge entries, {} distinct skills", *n,
                 brain_.known_skills().size());
    return n;
}

Result<SkillPlan> RobotController::think(const std::string& request,
                                         const std::string& device_id) const {
    if (brain_.size() == 0) {
        return std::unexpected(Error::internal(
            "brain has no knowledge loaded — call load_brain(datasets/brain/robot_brain.csv)"));
    }

    DeviceSpec        spec_copy;
    const DeviceSpec* spec_ptr = nullptr;
    if (!device_id.empty()) {
        std::lock_guard lock(mutex_);

        const auto it = devices_.find(device_id);
        if (it == devices_.end()) {
            return std::unexpected(Error::not_found("no such device: " + device_id));
        }
        spec_copy = it->second.spec;
        spec_ptr  = &spec_copy;
    }

    return brain_.plan(request, spec_ptr);
}

Result<std::vector<CommandResult>> RobotController::think_and_act(const std::string& request,
                                                                  const std::string& device_id) {
    const auto plan = think(request, device_id);
    if (!plan) return std::unexpected(plan.error());

    if (!plan->understood) {
        return std::unexpected(Error::bad_request("I don't know how to: " + request +
                                                  " — " + plan->rationale));
    }

    // Provisioning is not motion. It leaves the robot entirely.
    if (plan->is_provisioning()) {
        return std::unexpected(Error::bad_request(
            "'" + plan->chosen.skill +
            "' is a provisioning skill — route it through request_node()/vxnode, not the "
            "motion path"));
    }

    if (plan->commands.empty()) {
        return std::unexpected(Error::bad_request(
            "skill '" + plan->chosen.skill +
            "' has no executable motion — it is a knowledge entry, not a command "
            "(params: " + plan->chosen.params + ")"));
    }

    std::vector<CommandResult> results;
    results.reserve(plan->commands.size());

    for (Command c : plan->commands) {
        c.device_id = device_id;

        auto r = execute(c);
        if (!r) return std::unexpected(r.error());

        results.push_back(*r);
    }
    return results;
}

// ─── Infrastructure ─────────────────────────────────────────────────────────

Result<vxnode::ProvisionResult> RobotController::request_node(const vxnode::ProvisionRequest& req) {
    spdlog::info("robot needs infrastructure ('{}') — asking vxnode, not building it",
                 req.purpose.empty() ? "unspecified" : req.purpose);
    return node_.provision_vm(req);
}

std::vector<AuditRecord> RobotController::audit_log() const {
    std::lock_guard lock(mutex_);
    return audit_;
}

}  // namespace prodxcloud::robot
