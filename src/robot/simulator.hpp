#pragma once

/// @file simulator.hpp
/// @brief A deterministic, fixed-step simulation of a physical device.
///
/// The simulator is the device driver's stunt double: it exposes exactly the
/// interface the controller would use against real hardware — write a joint
/// setpoint, read back the measured state — but resolves it with a servo model
/// instead of a fieldbus. Everything is deterministic given the seed, so a
/// misbehaving motion replays identically in a test.
///
/// Servo model, per joint, integrated with semi-implicit Euler:
///   torque      = kp·(q_target − q) + kd·(q̇_target − q̇) − friction·q̇ − gravity(q)
///   q̈           = clamp(torque / inertia, ±a_max)
///   q̇          += q̈·dt          (then clamped to ±v_max)
///   q          += q̇·dt

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "common/types.hpp"
#include "robot/kinematics.hpp"
#include "robot/types.hpp"

namespace prodxcloud::robot {

/// Tunables of the simulated servo. Defaults are a stiff, well-damped industrial
/// joint that tracks a trajectory to well under a milliradian.
struct SimConfig {
    double kp            = 400.0;  ///< proportional gain
    double kd            = 40.0;   ///< derivative gain
    /// Effective link inertia. Sized so that even the most aggressive rated
    /// acceleration draws only a few Nm — leaving a wide, unambiguous gap between
    /// normal effort and the torque saturation that signals a collision.
    double inertia       = 0.5;    ///< kg·m²
    double friction      = 2.0;    ///< viscous friction coefficient
    double gravity_gain  = 0.0;    ///< 0 disables gravity loading
    double encoder_noise_rad = 0.0;   ///< std-dev of measurement noise, 0 = perfect encoder
    double thermal_rise_per_watt = 0.004;
    double ambient_c     = 30.0;
    uint64_t seed        = 42;     ///< fixed seed keeps runs reproducible
};

/// The simulated device: state, dynamics, and the fault injection used to prove
/// the safety layer actually fires.
class SimulatedDevice {
public:
    SimulatedDevice(DeviceSpec spec, SimConfig config = {});

    [[nodiscard]] const DeviceSpec& spec() const { return spec_; }
    [[nodiscard]] const std::string& id() const { return spec_.id; }

    /// Power on. The device lands at its home configuration, ready to move.
    void boot();
    void shutdown();

    /// Latch/clear the emergency stop. A latched e-stop zeroes velocity instantly
    /// and refuses every motion until reset.
    void estop(const std::string& reason);
    void reset();

    /// Command the servo loop to hold @p q (and, optionally, track @p qd).
    void set_target(const JointVector& q, const JointVector& qd = {});

    /// Advance the simulation by @p dt seconds.
    void step(double dt);

    /// Measured state, including encoder noise.
    [[nodiscard]] DeviceState state() const;

    /// Ground-truth joint positions, bypassing the encoder model. Tests use this;
    /// the controller never does, because real hardware has no such oracle.
    [[nodiscard]] const JointVector& true_positions() const { return q_; }

    /// Inject an external torque disturbance (a collision) on @p joint.
    void inject_disturbance(int joint, double torque_nm);

    [[nodiscard]] uint64_t tick() const { return tick_; }
    [[nodiscard]] double   uptime_s() const { return uptime_s_; }

private:
    DeviceSpec  spec_;
    SimConfig   config_;
    Kinematics  kin_;

    JointVector q_;            // true position
    JointVector qd_;           // true velocity
    JointVector effort_;       // applied torque
    JointVector q_target_;
    JointVector qd_target_;
    JointVector disturbance_;

    DeviceStatus status_ = DeviceStatus::OFFLINE;
    bool         estop_  = false;
    std::string  fault_message_;

    uint64_t tick_          = 0;
    double   uptime_s_      = 0.0;
    double   temperature_c_ = 30.0;

    mutable std::mt19937_64 rng_;
};

}  // namespace prodxcloud::robot
