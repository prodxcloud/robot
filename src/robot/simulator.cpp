#include "robot/simulator.hpp"

#include <algorithm>
#include <cmath>

namespace prodxcloud::robot {

SimulatedDevice::SimulatedDevice(DeviceSpec spec, SimConfig config)
    : spec_(std::move(spec)),
      config_(config),
      kin_(spec_),
      rng_(config.seed) {
    const auto n = static_cast<size_t>(spec_.dof);
    q_.assign(n, 0.0);
    qd_.assign(n, 0.0);
    effort_.assign(n, 0.0);
    q_target_.assign(n, 0.0);
    qd_target_.assign(n, 0.0);
    disturbance_.assign(n, 0.0);
    temperature_c_ = config_.ambient_c;
}

void SimulatedDevice::boot() {
    const auto n = static_cast<size_t>(spec_.dof);

    q_ = spec_.home.empty() ? JointVector(n, 0.0) : spec_.home;
    q_.resize(n, 0.0);

    std::fill(qd_.begin(), qd_.end(), 0.0);
    std::fill(effort_.begin(), effort_.end(), 0.0);
    std::fill(disturbance_.begin(), disturbance_.end(), 0.0);

    q_target_  = q_;
    qd_target_.assign(n, 0.0);

    estop_         = false;
    fault_message_.clear();
    status_        = DeviceStatus::IDLE;
    temperature_c_ = config_.ambient_c;
}

void SimulatedDevice::shutdown() {
    std::fill(qd_.begin(), qd_.end(), 0.0);
    std::fill(effort_.begin(), effort_.end(), 0.0);
    status_ = DeviceStatus::OFFLINE;
}

void SimulatedDevice::estop(const std::string& reason) {
    estop_ = true;
    // A real e-stop cuts drive power: velocity goes to zero, the arm stops holding
    // a target, and nothing moves again until a human clears the latch.
    std::fill(qd_.begin(), qd_.end(), 0.0);
    std::fill(effort_.begin(), effort_.end(), 0.0);
    q_target_      = q_;
    status_        = DeviceStatus::ESTOPPED;
    fault_message_ = reason;
}

void SimulatedDevice::reset() {
    estop_ = false;
    fault_message_.clear();
    std::fill(disturbance_.begin(), disturbance_.end(), 0.0);
    q_target_ = q_;
    std::fill(qd_target_.begin(), qd_target_.end(), 0.0);
    status_ = status_ == DeviceStatus::OFFLINE ? DeviceStatus::OFFLINE : DeviceStatus::IDLE;
}

void SimulatedDevice::set_target(const JointVector& q, const JointVector& qd) {
    if (estop_ || status_ == DeviceStatus::OFFLINE) return;

    const auto n = static_cast<size_t>(spec_.dof);
    for (size_t i = 0; i < n && i < q.size(); ++i) q_target_[i] = q[i];

    if (qd.empty()) {
        std::fill(qd_target_.begin(), qd_target_.end(), 0.0);
    } else {
        for (size_t i = 0; i < n && i < qd.size(); ++i) qd_target_[i] = qd[i];
    }
}

void SimulatedDevice::inject_disturbance(int joint, double torque_nm) {
    if (joint >= 0 && joint < spec_.dof) {
        disturbance_[static_cast<size_t>(joint)] += torque_nm;
    }
}

void SimulatedDevice::step(double dt) {
    if (status_ == DeviceStatus::OFFLINE || dt <= 0.0) return;

    ++tick_;
    uptime_s_ += dt;

    if (estop_) {
        // Power is cut — nothing integrates, but the device still cools down.
        temperature_c_ += (config_.ambient_c - temperature_c_) * std::min(1.0, dt * 0.05);
        return;
    }

    const auto n = static_cast<size_t>(spec_.dof);
    double     total_power = 0.0;
    bool       moving      = false;

    for (size_t i = 0; i < n; ++i) {
        const auto& lim = spec_.limits[i];

        const double pos_err = q_target_[i] - q_[i];
        const double vel_err = qd_target_[i] - qd_[i];

        // Gravity load is a crude sin(q) term — enough to make the model non-trivial
        // without pretending to be a real rigid-body dynamics solver.
        const double gravity = config_.gravity_gain * std::sin(q_[i]);

        const double torque = config_.kp * pos_err + config_.kd * vel_err -
                              config_.friction * qd_[i] - gravity + disturbance_[i];

        effort_[i] = std::clamp(torque, -lim.max_torque, lim.max_torque);

        double accel = effort_[i] / config_.inertia;
        accel        = std::clamp(accel, -lim.max_acceleration, lim.max_acceleration);

        // Semi-implicit Euler: integrate velocity first, then use the *new* velocity
        // for position. More stable than explicit Euler at a 1 ms step.
        qd_[i] += accel * dt;
        qd_[i] = std::clamp(qd_[i], -lim.max_velocity, lim.max_velocity);

        q_[i] += qd_[i] * dt;
        q_[i] = std::clamp(q_[i], lim.min_position_rad, lim.max_position_rad);

        total_power += std::abs(effort_[i] * qd_[i]);
        if (std::abs(qd_[i]) > 1e-4) moving = true;

        // A disturbance is an impulse, not a permanent field — it decays away.
        disturbance_[i] *= 0.95;
    }

    // Thermal model: heat with mechanical power, cool toward ambient.
    temperature_c_ += (total_power * config_.thermal_rise_per_watt -
                       (temperature_c_ - config_.ambient_c) * 0.5) * dt;

    if (status_ != DeviceStatus::FAULTED) {
        status_ = moving ? DeviceStatus::MOVING : DeviceStatus::IDLE;
    }
}

DeviceState SimulatedDevice::state() const {
    DeviceState s;
    s.device_id     = spec_.id;
    s.status        = status_;
    s.estop         = estop_;
    s.fault_message = fault_message_;
    s.tick          = tick_;
    s.uptime_s      = uptime_s_;
    s.temperature_c = temperature_c_;

    const auto n = static_cast<size_t>(spec_.dof);
    s.joints.resize(n);

    JointVector measured(n, 0.0);
    std::normal_distribution<double> noise(0.0, config_.encoder_noise_rad);

    for (size_t i = 0; i < n; ++i) {
        // Real encoders are quantised and noisy; the controller must close the loop
        // on this, not on the ground truth.
        const double m = config_.encoder_noise_rad > 0.0 ? q_[i] + noise(rng_) : q_[i];
        measured[i]    = m;

        s.joints[i].position = m;
        s.joints[i].velocity = qd_[i];
        s.joints[i].effort   = effort_[i];
    }

    if (spec_.kind == DeviceKind::ARM) {
        s.tcp = kin_.forward_pose(measured);
    }
    return s;
}

}  // namespace prodxcloud::robot
