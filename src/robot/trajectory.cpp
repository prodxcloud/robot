#include "robot/trajectory.hpp"

#include <algorithm>
#include <cmath>

namespace prodxcloud::robot {

TrajectoryPoint Trajectory::sample(double t) const {
    if (points_.empty()) return {};
    if (t <= points_.front().time_s) return points_.front();
    if (t >= points_.back().time_s) return points_.back();

    // Binary search for the bracketing pair.
    size_t lo = 0, hi = points_.size() - 1;
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        if (points_[mid].time_s <= t) lo = mid;
        else hi = mid;
    }

    const TrajectoryPoint& a  = points_[lo];
    const TrajectoryPoint& b  = points_[hi];
    const double           dt = b.time_s - a.time_s;
    const double           u  = dt > 1e-12 ? (t - a.time_s) / dt : 0.0;

    TrajectoryPoint p;
    p.time_s = t;
    p.positions.resize(a.positions.size());
    p.velocities.resize(a.velocities.size());
    p.accelerations.resize(a.accelerations.size());

    for (size_t i = 0; i < a.positions.size(); ++i) {
        p.positions[i]     = a.positions[i] + (b.positions[i] - a.positions[i]) * u;
        p.velocities[i]    = a.velocities[i] + (b.velocities[i] - a.velocities[i]) * u;
        p.accelerations[i] = a.accelerations[i];
    }
    return p;
}

TrajectoryPlanner::TrajectoryPlanner(DeviceSpec spec) : spec_(std::move(spec)) {}

namespace {

/// Duration of a single-joint trapezoidal (or triangular) profile over @p distance.
double joint_duration(double distance, double vmax, double amax) {
    const double d = std::abs(distance);
    if (d < 1e-9 || vmax <= 0.0 || amax <= 0.0) return 0.0;

    // Distance needed to reach vmax and come back down again.
    const double d_ramp = vmax * vmax / amax;
    if (d < d_ramp) {
        // Triangular: never reaches vmax.
        return 2.0 * std::sqrt(d / amax);
    }
    // Trapezoidal: ramp up, cruise, ramp down.
    return vmax / amax + d / vmax;
}

/// Position/velocity/acceleration of a trapezoidal profile at time @p t.
struct Kin {
    double p, v, a;
};

Kin trapezoid_at(double t, double distance, double duration, double amax) {
    const double sign = distance < 0.0 ? -1.0 : 1.0;
    const double d    = std::abs(distance);

    if (d < 1e-9 || duration <= 1e-9) return {0.0, 0.0, 0.0};
    t = std::clamp(t, 0.0, duration);

    // Recover the peak velocity that fits this distance in this duration:
    //   d = v*(T - v/a)  =>  v² - a*T*v + a*d = 0
    const double disc = amax * amax * duration * duration - 4.0 * amax * d;
    double       v_peak;
    if (disc < 0.0) {
        // Numerically degenerate — fall back to the triangular peak.
        v_peak = amax * duration / 2.0;
    } else {
        v_peak = (amax * duration - std::sqrt(disc)) / 2.0;
    }
    const double t_ramp = v_peak / amax;

    double p, v, a;
    if (t < t_ramp) {                       // accelerating
        a = amax;
        v = amax * t;
        p = 0.5 * amax * t * t;
    } else if (t < duration - t_ramp) {     // cruising
        a = 0.0;
        v = v_peak;
        p = 0.5 * v_peak * t_ramp + v_peak * (t - t_ramp);
    } else {                                // decelerating
        const double td = duration - t;
        a = -amax;
        v = amax * td;
        p = d - 0.5 * amax * td * td;
    }
    return {sign * p, sign * v, sign * a};
}

}  // namespace

double TrajectoryPlanner::compute_duration(const JointVector& start,
                                           const JointVector& goal,
                                           double             speed_scale,
                                           double             accel_scale) const {
    speed_scale = std::clamp(speed_scale, 0.01, 1.0);
    accel_scale = std::clamp(accel_scale, 0.01, 1.0);

    const int n = std::min({static_cast<int>(start.size()), static_cast<int>(goal.size()), spec_.dof});

    double makespan = 0.0;
    for (int i = 0; i < n; ++i) {
        const auto&  lim  = spec_.limits[static_cast<size_t>(i)];
        const double dist = goal[static_cast<size_t>(i)] - start[static_cast<size_t>(i)];
        const double t    = joint_duration(dist,
                                           lim.max_velocity * speed_scale,
                                           lim.max_acceleration * accel_scale);
        makespan = std::max(makespan, t);
    }
    return makespan;
}

Result<Trajectory> TrajectoryPlanner::plan_joint_move(const JointVector& start,
                                                      const JointVector& goal,
                                                      double             speed_scale,
                                                      double             accel_scale,
                                                      double             sample_hz) const {
    const int n = spec_.dof;
    if (static_cast<int>(start.size()) < n || static_cast<int>(goal.size()) < n) {
        return std::unexpected(Error::bad_request(
            "joint vector size mismatch: expected " + std::to_string(n) + " values"));
    }
    if (sample_hz <= 0.0) {
        return std::unexpected(Error::bad_request("sample_hz must be positive"));
    }

    speed_scale = std::clamp(speed_scale, 0.01, 1.0);
    accel_scale = std::clamp(accel_scale, 0.01, 1.0);

    const double duration = compute_duration(start, goal, speed_scale, accel_scale);

    Trajectory traj;
    traj.duration_s_ = duration;

    // A zero-distance move still yields a single point, so callers can treat the
    // result uniformly instead of special-casing "already there".
    if (duration < 1e-9) {
        TrajectoryPoint p;
        p.time_s        = 0.0;
        p.positions     = goal;
        p.velocities.assign(static_cast<size_t>(n), 0.0);
        p.accelerations.assign(static_cast<size_t>(n), 0.0);
        traj.points_.push_back(std::move(p));
        return traj;
    }

    const double dt    = 1.0 / sample_hz;
    const auto   steps = static_cast<size_t>(std::ceil(duration / dt));
    traj.points_.reserve(steps + 1);

    for (size_t s = 0; s <= steps; ++s) {
        const double t = std::min(static_cast<double>(s) * dt, duration);

        TrajectoryPoint p;
        p.time_s = t;
        p.positions.resize(static_cast<size_t>(n));
        p.velocities.resize(static_cast<size_t>(n));
        p.accelerations.resize(static_cast<size_t>(n));

        for (int i = 0; i < n; ++i) {
            const auto&  lim  = spec_.limits[static_cast<size_t>(i)];
            const double dist = goal[static_cast<size_t>(i)] - start[static_cast<size_t>(i)];

            // Every joint is stretched to the common makespan, so they start and
            // stop together. The acceleration each joint needs is whatever its own
            // trapezoid requires over that shared duration — never above its limit,
            // since the makespan is the max over all joints' minimum durations.
            const double amax_i = lim.max_acceleration * accel_scale;
            const double d_tri  = amax_i * duration * duration / 4.0;
            const double amax_eff =
                std::abs(dist) > d_tri ? amax_i : 4.0 * std::abs(dist) / (duration * duration);

            const Kin k = trapezoid_at(t, dist, duration, std::max(amax_eff, 1e-9));

            p.positions[static_cast<size_t>(i)]     = start[static_cast<size_t>(i)] + k.p;
            p.velocities[static_cast<size_t>(i)]    = k.v;
            p.accelerations[static_cast<size_t>(i)] = k.a;
        }
        traj.points_.push_back(std::move(p));
    }

    // Pin the final point exactly on the goal — accumulated float error otherwise
    // leaves the arm microns short, which a position-tolerance check would flag.
    traj.points_.back().positions = goal;
    std::fill(traj.points_.back().velocities.begin(), traj.points_.back().velocities.end(), 0.0);
    std::fill(traj.points_.back().accelerations.begin(), traj.points_.back().accelerations.end(), 0.0);

    return traj;
}

Result<Trajectory> TrajectoryPlanner::plan_dwell(const JointVector& at,
                                                 double             duration_s,
                                                 double             sample_hz) const {
    if (duration_s < 0.0) return std::unexpected(Error::bad_request("dwell duration must be >= 0"));
    if (sample_hz <= 0.0) return std::unexpected(Error::bad_request("sample_hz must be positive"));

    Trajectory traj;
    traj.duration_s_ = duration_s;

    const double dt    = 1.0 / sample_hz;
    const auto   steps = static_cast<size_t>(std::ceil(duration_s / dt));

    for (size_t s = 0; s <= steps; ++s) {
        TrajectoryPoint p;
        p.time_s    = std::min(static_cast<double>(s) * dt, duration_s);
        p.positions = at;
        p.velocities.assign(at.size(), 0.0);
        p.accelerations.assign(at.size(), 0.0);
        traj.points_.push_back(std::move(p));
    }
    return traj;
}

}  // namespace prodxcloud::robot
