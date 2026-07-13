#pragma once

/// @file controller.hpp
/// @brief The robot: brain + safety + planner + devices, wired together.
///
/// The controller is the seam where thinking becomes moving. It owns the fleet of
/// devices, runs their control loops, and is the single component allowed to make
/// a device act. Every path into motion goes through the same gate:
///
///   request ──▶ brain (retrieve + plan) ──▶ safety (allow/reject) ──▶ trajectory
///                                                   │                      │
///                                                   ▼                      ▼
///                                              refuse, log            servo loop
///
/// Provisioning is deliberately *not* on that diagram. When a plan needs
/// infrastructure, the controller hands it to VxNodeClient and gets back a node.
/// The robot never provisions anything itself — see src/vxnode/vxnode_client.hpp.

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/types.hpp"
#include "robot/brain.hpp"
#include "robot/kinematics.hpp"
#include "robot/safety.hpp"
#include "robot/simulator.hpp"
#include "robot/trajectory.hpp"
#include "robot/types.hpp"
#include "vxnode/vxnode_client.hpp"

namespace prodxcloud::robot {

/// One managed device: its spec, its simulated (or real) hardware, and the
/// per-device policy objects that guard and plan for it.
struct ManagedDevice {
    DeviceSpec                       spec;
    std::unique_ptr<SimulatedDevice> hardware;
    std::unique_ptr<Kinematics>      kinematics;
    std::unique_ptr<SafetyMonitor>   safety;
    std::unique_ptr<TrajectoryPlanner> planner;
};

/// An entry in the controller's audit log. Every accepted *and* every refused
/// command lands here — a robot that only records what it did is useless in the
/// post-mortem of what it refused to do.
struct AuditRecord {
    std::string timestamp;
    std::string device_id;
    std::string command_id;
    std::string command_type;
    std::string verdict;   ///< allow | reject | estop
    std::string rule;      ///< which safety rule fired, if any
    std::string detail;
};

struct ControllerConfig {
    double control_hz    = kDefaultHz;
    bool   log_audit     = true;
    size_t audit_capacity = 1000;

    /// A servo tracking a moving setpoint always lags it slightly, so the instant
    /// the trajectory ends the arm is still a few milliradians short. Declaring the
    /// move "complete" there would be a lie — the next command would then plan from
    /// a position the arm is not at, and the error would compound down the program.
    ///
    /// So the move is not done until the arm has actually *arrived*: hold the final
    /// setpoint and wait for the in-position check, exactly as an industrial
    /// controller does.
    double in_position_tolerance_rad = 5e-4;   ///< ~0.03 deg
    double settle_timeout_s          = 1.0;    ///< a servo that cannot settle is faulted
};

class RobotController {
public:
    RobotController(ControllerConfig config = {},
                    vxnode::NodeConfig node = vxnode::NodeConfig::from_env());

    // ── Fleet ───────────────────────────────────────────────────────────────

    /// Register a device and bring up its simulated hardware.
    Result<std::string> add_device(DeviceSpec spec, SimConfig sim = {}, SafetyConfig safety = {});

    Result<void> remove_device(const std::string& device_id);

    [[nodiscard]] std::vector<std::string> device_ids() const;
    [[nodiscard]] Result<DeviceState> state(const std::string& device_id) const;
    [[nodiscard]] Result<DeviceSpec>  spec(const std::string& device_id) const;

    /// Power a device on (it lands at its home configuration).
    Result<void> boot(const std::string& device_id);

    // ── Motion ──────────────────────────────────────────────────────────────

    /// Run a command to completion against the device's control loop.
    /// This is the ONLY way a device moves.
    Result<CommandResult> execute(const Command& cmd);

    /// Latch the emergency stop on one device, or on the whole fleet.
    void estop_all(const std::string& reason);

    /// Inject an external torque on a joint — a simulated collision.
    /// This exists so the tick-by-tick safety guard can be *proven* to fire, rather
    /// than merely asserted to exist. On real hardware the disturbance arrives by
    /// itself, and this call has no counterpart.
    Result<void> inject_disturbance(const std::string& device_id, int joint, double torque_nm);

    // ── Brain ───────────────────────────────────────────────────────────────

    /// Load the knowledge corpus. Without it the robot can still be driven by
    /// explicit commands — it just cannot be *asked* for anything.
    Result<size_t> load_brain(const std::string& csv_path);

    [[nodiscard]] const Brain& brain() const { return brain_; }

    /// Understand a natural-language request, then act on it: motion skills run on
    /// @p device_id, provisioning skills are delegated to vxnode.
    Result<SkillPlan> think(const std::string& request, const std::string& device_id = "") const;

    /// think() + execute() — the full loop from a sentence to a moved joint.
    Result<std::vector<CommandResult>> think_and_act(const std::string& request,
                                                     const std::string& device_id);

    // ── Infrastructure (delegated — never done here) ─────────────────────────

    /// Ask vxnode for an instance. The robot does not know how to make one.
    Result<vxnode::ProvisionResult> request_node(const vxnode::ProvisionRequest& req);

    [[nodiscard]] const vxnode::VxNodeClient& node() const { return node_; }

    // ── Introspection ───────────────────────────────────────────────────────

    [[nodiscard]] std::vector<AuditRecord> audit_log() const;

private:
    void record(const AuditRecord& r);

    /// Drive @p dev along @p traj, closing the loop each tick and aborting the
    /// instant a safety rule fires.
    Result<CommandResult> run_trajectory(ManagedDevice&    dev,
                                         const Command&    cmd,
                                         const Trajectory& traj);

    ControllerConfig       config_;
    vxnode::VxNodeClient   node_;
    Brain                  brain_;

    mutable std::mutex                                      mutex_;
    std::unordered_map<std::string, ManagedDevice>          devices_;
    std::vector<AuditRecord>                                audit_;
};

}  // namespace prodxcloud::robot
