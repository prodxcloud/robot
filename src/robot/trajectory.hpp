#pragma once

/// @file trajectory.hpp
/// @brief Time-parameterised motion planning in joint space.
///
/// Motions are planned as trapezoidal velocity profiles (accelerate, cruise,
/// decelerate), scaled so that every joint arrives at the goal at the same
/// instant — the slowest joint sets the duration and the rest are slaved to it.
/// A profile that cannot cruise degenerates to a triangular one automatically.

#include <vector>

#include "common/types.hpp"
#include "robot/types.hpp"

namespace prodxcloud::robot {

/// One sampled point on a trajectory.
struct TrajectoryPoint {
    double      time_s = 0.0;
    JointVector positions;
    JointVector velocities;
    JointVector accelerations;
};

/// A fully time-parameterised joint-space motion.
class Trajectory {
public:
    Trajectory() = default;

    [[nodiscard]] bool   empty() const { return points_.empty(); }
    [[nodiscard]] size_t size() const { return points_.size(); }
    [[nodiscard]] double duration_s() const { return duration_s_; }
    [[nodiscard]] const std::vector<TrajectoryPoint>& points() const { return points_; }

    /// Sample the trajectory at @p t seconds, interpolating between waypoints.
    /// Times outside [0, duration] clamp to the endpoints.
    [[nodiscard]] TrajectoryPoint sample(double t) const;

    friend class TrajectoryPlanner;

private:
    std::vector<TrajectoryPoint> points_;
    double                       duration_s_ = 0.0;
};

class TrajectoryPlanner {
public:
    explicit TrajectoryPlanner(DeviceSpec spec);

    /// Plan a synchronised joint-space move from @p start to @p goal.
    /// @p speed_scale and @p accel_scale are fractions of the joint limits in (0, 1].
    [[nodiscard]] Result<Trajectory> plan_joint_move(const JointVector& start,
                                                     const JointVector& goal,
                                                     double             speed_scale = 1.0,
                                                     double             accel_scale = 1.0,
                                                     double             sample_hz   = 100.0) const;

    /// Hold the current configuration for @p duration_s.
    [[nodiscard]] Result<Trajectory> plan_dwell(const JointVector& at,
                                                double             duration_s,
                                                double             sample_hz = 100.0) const;

    /// Duration of the slowest joint's trapezoidal profile — the motion's makespan.
    [[nodiscard]] double compute_duration(const JointVector& start,
                                          const JointVector& goal,
                                          double             speed_scale,
                                          double             accel_scale) const;

private:
    DeviceSpec spec_;
};

}  // namespace prodxcloud::robot
