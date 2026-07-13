#include "robot/kinematics.hpp"

#include <algorithm>
#include <cmath>

namespace prodxcloud::robot {

// ─── Pose <-> Mat4 ──────────────────────────────────────────────────────────

Mat4 Pose::to_matrix() const {
    const double cr = std::cos(roll),  sr = std::sin(roll);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw),   sy = std::sin(yaw);

    Mat4 t = Mat4::identity();
    // Z-Y-X (yaw-pitch-roll) intrinsic rotation.
    t.at(0, 0) = cy * cp;
    t.at(0, 1) = cy * sp * sr - sy * cr;
    t.at(0, 2) = cy * sp * cr + sy * sr;
    t.at(1, 0) = sy * cp;
    t.at(1, 1) = sy * sp * sr + cy * cr;
    t.at(1, 2) = sy * sp * cr - cy * sr;
    t.at(2, 0) = -sp;
    t.at(2, 1) = cp * sr;
    t.at(2, 2) = cp * cr;
    t.at(0, 3) = position.x;
    t.at(1, 3) = position.y;
    t.at(2, 3) = position.z;
    return t;
}

Pose Pose::from_matrix(const Mat4& t) {
    Pose p;
    p.position = t.translation();
    // Guard the gimbal-lock case: |sp| == 1 leaves roll and yaw degenerate, so
    // fold the rotation into yaw and pin roll to zero.
    const double sp = -t.at(2, 0);
    if (std::abs(sp) > 0.999999) {
        p.pitch = std::copysign(kPi / 2.0, sp);
        p.roll  = 0.0;
        p.yaw   = std::atan2(-t.at(0, 1), t.at(1, 1));
    } else {
        p.pitch = std::asin(sp);
        p.roll  = std::atan2(t.at(2, 1), t.at(2, 2));
        p.yaw   = std::atan2(t.at(1, 0), t.at(0, 0));
    }
    return p;
}

// ─── Device specs ───────────────────────────────────────────────────────────

DeviceSpec DeviceSpec::vx_arm6(std::string id, std::string name) {
    DeviceSpec s;
    s.id    = std::move(id);
    s.name  = std::move(name);
    s.kind  = DeviceKind::ARM;
    s.model = "vx-arm6";
    s.dof   = 6;

    // Standard DH parameters of a UR5-class 6R manipulator.
    s.links = {
        {0.0,      0.089159,  kPi / 2.0, 0.0},
        {-0.425,   0.0,       0.0,       0.0},
        {-0.39225, 0.0,       0.0,       0.0},
        {0.0,      0.10915,   kPi / 2.0, 0.0},
        {0.0,      0.09465,  -kPi / 2.0, 0.0},
        {0.0,      0.0823,    0.0,       0.0},
    };

    JointLimits big;
    big.min_position_rad = -2.0 * kPi;
    big.max_position_rad = 2.0 * kPi;
    big.max_velocity     = kPi;      // 180 deg/s
    big.max_acceleration = 8.0;
    big.max_torque       = 150.0;

    JointLimits wrist = big;
    wrist.max_velocity     = 2.0 * kPi;  // wrists are faster and lighter
    wrist.max_acceleration = 20.0;
    wrist.max_torque       = 28.0;

    s.limits = {big, big, big, wrist, wrist, wrist};

    // The classic "ready" pose: elbow up, wrist folded, tool pointing down and well
    // clear of every singularity.
    s.home = {0.0, -kPi / 2.0, kPi / 2.0, -kPi / 2.0, -kPi / 2.0, 0.0};

    s.payload_kg = 5.0;
    s.reach_m    = 0.85;
    s.control_hz = kDefaultHz;
    return s;
}

DeviceSpec DeviceSpec::vx_gripper(std::string id, std::string name) {
    DeviceSpec s;
    s.id    = std::move(id);
    s.name  = std::move(name);
    s.kind  = DeviceKind::GRIPPER;
    s.model = "vx-grip2";
    s.dof   = 1;
    s.links = {{0.0, 0.05, 0.0, 0.0}};

    JointLimits jaw;
    jaw.min_position_rad = 0.0;    // fully closed
    jaw.max_position_rad = 0.085;  // 85 mm stroke, carried in the position slot
    jaw.max_velocity     = 0.15;
    jaw.max_acceleration = 1.0;
    jaw.max_torque       = 40.0;

    s.limits     = {jaw};
    s.home       = {0.085};  // parked open
    s.payload_kg = 5.0;
    s.reach_m    = 0.085;
    s.control_hz = 500.0;
    return s;
}

// ─── Kinematics ─────────────────────────────────────────────────────────────

Kinematics::Kinematics(DeviceSpec spec) : spec_(std::move(spec)) {}

/// Standard DH link transform for joint angle @p theta.
static Mat4 dh_transform(const DHParam& p, double theta) {
    const double t  = theta + p.theta_offset;
    const double ct = std::cos(t),        st = std::sin(t);
    const double ca = std::cos(p.alpha),  sa = std::sin(p.alpha);

    Mat4 m = Mat4::identity();
    m.at(0, 0) = ct;   m.at(0, 1) = -st * ca;  m.at(0, 2) = st * sa;   m.at(0, 3) = p.a * ct;
    m.at(1, 0) = st;   m.at(1, 1) = ct * ca;   m.at(1, 2) = -ct * sa;  m.at(1, 3) = p.a * st;
    m.at(2, 0) = 0.0;  m.at(2, 1) = sa;        m.at(2, 2) = ca;        m.at(2, 3) = p.d;
    return m;
}

std::vector<Mat4> Kinematics::link_transforms(const JointVector& q) const {
    std::vector<Mat4> frames;
    frames.reserve(static_cast<size_t>(spec_.dof) + 1);

    Mat4 acc = Mat4::identity();
    frames.push_back(acc);

    const int n = std::min(static_cast<int>(q.size()), spec_.dof);
    for (int i = 0; i < n; ++i) {
        acc = acc * dh_transform(spec_.links[static_cast<size_t>(i)], q[static_cast<size_t>(i)]);
        frames.push_back(acc);
    }
    return frames;
}

Mat4 Kinematics::forward(const JointVector& q) const {
    return link_transforms(q).back();
}

Pose Kinematics::forward_pose(const JointVector& q) const {
    return Pose::from_matrix(forward(q));
}

Jacobian Kinematics::jacobian(const JointVector& q) const {
    const int n = spec_.dof;
    Jacobian  j;
    j.dof  = n;
    j.data.assign(static_cast<size_t>(6 * n), 0.0);

    const auto frames = link_transforms(q);
    const Vec3 p_end  = frames.back().translation();

    for (int i = 0; i < n; ++i) {
        const Mat4& f = frames[static_cast<size_t>(i)];
        // Joint i rotates about the z-axis of frame i.
        const Vec3 z{f.at(0, 2), f.at(1, 2), f.at(2, 2)};
        const Vec3 p    = f.translation();
        const Vec3 lever = p_end - p;

        // Linear part: z × (p_end - p_i). Angular part: z.
        j.at(0, i) = z.y * lever.z - z.z * lever.y;
        j.at(1, i) = z.z * lever.x - z.x * lever.z;
        j.at(2, i) = z.x * lever.y - z.y * lever.x;
        j.at(3, i) = z.x;
        j.at(4, i) = z.y;
        j.at(5, i) = z.z;
    }
    return j;
}

double Kinematics::manipulability(const JointVector& q) const {
    const Jacobian j = jacobian(q);
    const int      n = j.dof;

    // JJᵀ is 6x6 regardless of the DOF count.
    std::vector<double> jjt(36, 0.0);
    for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 6; ++c) {
            double s = 0.0;
            for (int k = 0; k < n; ++k) s += j.at(r, k) * j.at(c, k);
            jjt[static_cast<size_t>(r * 6 + c)] = s;
        }
    }

    // det via LU with partial pivoting.
    double det  = 1.0;
    int    sign = 1;
    for (int col = 0; col < 6; ++col) {
        int    pivot = col;
        double best  = std::abs(jjt[static_cast<size_t>(col * 6 + col)]);
        for (int r = col + 1; r < 6; ++r) {
            const double v = std::abs(jjt[static_cast<size_t>(r * 6 + col)]);
            if (v > best) { best = v; pivot = r; }
        }
        if (best < 1e-12) return 0.0;  // singular
        if (pivot != col) {
            for (int c = 0; c < 6; ++c)
                std::swap(jjt[static_cast<size_t>(col * 6 + c)],
                          jjt[static_cast<size_t>(pivot * 6 + c)]);
            sign = -sign;
        }
        const double d = jjt[static_cast<size_t>(col * 6 + col)];
        det *= d;
        for (int r = col + 1; r < 6; ++r) {
            const double f = jjt[static_cast<size_t>(r * 6 + col)] / d;
            for (int c = col; c < 6; ++c)
                jjt[static_cast<size_t>(r * 6 + c)] -= f * jjt[static_cast<size_t>(col * 6 + c)];
        }
    }
    det *= sign;
    return det > 0.0 ? std::sqrt(det) : 0.0;
}

bool Kinematics::within_limits(const JointVector& q) const {
    const int n = std::min(static_cast<int>(q.size()), spec_.dof);
    for (int i = 0; i < n; ++i) {
        const auto& l = spec_.limits[static_cast<size_t>(i)];
        const double v = q[static_cast<size_t>(i)];
        if (v < l.min_position_rad || v > l.max_position_rad) return false;
    }
    return true;
}

JointVector Kinematics::clamp_to_limits(const JointVector& q) const {
    JointVector out = q;
    const int   n   = std::min(static_cast<int>(out.size()), spec_.dof);
    for (int i = 0; i < n; ++i) {
        const auto& l = spec_.limits[static_cast<size_t>(i)];
        out[static_cast<size_t>(i)] =
            std::clamp(out[static_cast<size_t>(i)], l.min_position_rad, l.max_position_rad);
    }
    return out;
}

bool solve_linear_system(std::vector<double>& a, std::vector<double>& b, int n) {
    for (int col = 0; col < n; ++col) {
        int    pivot = col;
        double best  = std::abs(a[static_cast<size_t>(col * n + col)]);
        for (int r = col + 1; r < n; ++r) {
            const double v = std::abs(a[static_cast<size_t>(r * n + col)]);
            if (v > best) { best = v; pivot = r; }
        }
        if (best < 1e-12) return false;

        if (pivot != col) {
            for (int c = 0; c < n; ++c)
                std::swap(a[static_cast<size_t>(col * n + c)],
                          a[static_cast<size_t>(pivot * n + c)]);
            std::swap(b[static_cast<size_t>(col)], b[static_cast<size_t>(pivot)]);
        }

        const double d = a[static_cast<size_t>(col * n + col)];
        for (int r = col + 1; r < n; ++r) {
            const double f = a[static_cast<size_t>(r * n + col)] / d;
            if (f == 0.0) continue;
            for (int c = col; c < n; ++c)
                a[static_cast<size_t>(r * n + c)] -= f * a[static_cast<size_t>(col * n + c)];
            b[static_cast<size_t>(r)] -= f * b[static_cast<size_t>(col)];
        }
    }

    // Back substitution.
    for (int r = n - 1; r >= 0; --r) {
        double s = b[static_cast<size_t>(r)];
        for (int c = r + 1; c < n; ++c) s -= a[static_cast<size_t>(r * n + c)] * b[static_cast<size_t>(c)];
        b[static_cast<size_t>(r)] = s / a[static_cast<size_t>(r * n + r)];
    }
    return true;
}

/// Shortest signed angular difference, wrapped to (-pi, pi].
static double wrap_angle(double a) {
    while (a > kPi) a -= 2.0 * kPi;
    while (a < -kPi) a += 2.0 * kPi;
    return a;
}

/// Orientation error as a rotation vector, from current transform to target.
static Vec3 orientation_error(const Mat4& current, const Mat4& target) {
    // R_err = R_target * R_currentᵀ, converted to axis-angle.
    double r[9];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double s = 0.0;
            for (int k = 0; k < 3; ++k) s += target.at(i, k) * current.at(j, k);
            r[i * 3 + j] = s;
        }
    }

    const double trace = r[0] + r[4] + r[8];
    const double cos_t = std::clamp((trace - 1.0) / 2.0, -1.0, 1.0);
    const double angle = std::acos(cos_t);

    if (angle < 1e-9) return {0.0, 0.0, 0.0};

    const double s = 2.0 * std::sin(angle);
    if (std::abs(s) < 1e-9) {
        // 180 degree rotation — axis is recovered from the diagonal.
        return {std::sqrt(std::max(0.0, (r[0] + 1.0) / 2.0)) * angle,
                std::sqrt(std::max(0.0, (r[4] + 1.0) / 2.0)) * angle,
                std::sqrt(std::max(0.0, (r[8] + 1.0) / 2.0)) * angle};
    }
    return {(r[7] - r[5]) / s * angle,
            (r[2] - r[6]) / s * angle,
            (r[3] - r[1]) / s * angle};
}

Result<IKSolution> Kinematics::inverse(const Pose&        target,
                                       const JointVector& seed,
                                       const IKOptions&   opts) const {
    const int n = spec_.dof;
    if (n < 1) return std::unexpected(Error::bad_request("device has no joints"));

    // Reject targets outside the reachable sphere before burning iterations on them.
    const double target_radius = target.position.norm();
    if (target_radius > spec_.reach_m * 1.001) {
        return std::unexpected(Error::validation(
            "target is outside the " + std::to_string(spec_.reach_m) + " m workspace (radius " +
            std::to_string(target_radius) + " m)"));
    }

    IKSolution sol;
    sol.joints.assign(static_cast<size_t>(n), 0.0);
    for (int i = 0; i < n && i < static_cast<int>(seed.size()); ++i)
        sol.joints[static_cast<size_t>(i)] = seed[static_cast<size_t>(i)];

    const Mat4   target_t = target.to_matrix();
    const double lambda2  = opts.damping * opts.damping;

    for (int iter = 0; iter < opts.max_iterations; ++iter) {
        const Mat4 current = forward(sol.joints);

        const Vec3 pos_err = target.position - current.translation();
        const Vec3 rot_err = orientation_error(current, target_t);

        sol.position_error_m      = pos_err.norm();
        sol.orientation_error_rad = rot_err.norm();
        sol.iterations            = iter;

        if (sol.position_error_m < opts.position_tol_m &&
            sol.orientation_error_rad < opts.orientation_tol_rad) {
            sol.converged = true;
            return sol;
        }

        const Jacobian j = jacobian(sol.joints);

        // Damped least squares: (JᵀJ + λ²I) Δq = Jᵀe.
        const double e[6] = {pos_err.x, pos_err.y, pos_err.z,
                             rot_err.x, rot_err.y, rot_err.z};

        std::vector<double> a(static_cast<size_t>(n * n), 0.0);
        std::vector<double> b(static_cast<size_t>(n), 0.0);

        for (int r = 0; r < n; ++r) {
            double s = 0.0;
            for (int k = 0; k < 6; ++k) s += j.at(k, r) * e[k];
            b[static_cast<size_t>(r)] = s;

            for (int c = 0; c < n; ++c) {
                double v = 0.0;
                for (int k = 0; k < 6; ++k) v += j.at(k, r) * j.at(k, c);
                if (r == c) v += lambda2;
                a[static_cast<size_t>(r * n + c)] = v;
            }
        }

        if (!solve_linear_system(a, b, n)) {
            return std::unexpected(Error::internal("IK failed: Jacobian system is singular"));
        }

        // Clamp the step so a large error near a singularity cannot fling the arm.
        double max_step = 0.0;
        for (int i = 0; i < n; ++i) max_step = std::max(max_step, std::abs(b[static_cast<size_t>(i)]));
        const double scale =
            max_step > opts.step_clamp_rad ? opts.step_clamp_rad / max_step : 1.0;

        for (int i = 0; i < n; ++i)
            sol.joints[static_cast<size_t>(i)] += b[static_cast<size_t>(i)] * scale;

        if (opts.respect_limits) sol.joints = clamp_to_limits(sol.joints);
    }

    return std::unexpected(Error::timeout(
        "IK did not converge in " + std::to_string(opts.max_iterations) +
        " iterations (position error " + std::to_string(sol.position_error_m) + " m)"));
}

}  // namespace prodxcloud::robot
