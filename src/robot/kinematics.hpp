#pragma once

/// @file kinematics.hpp
/// @brief Forward and inverse kinematics for serial-link manipulators.
///
/// Forward kinematics composes the standard Denavit-Hartenberg transform of each
/// link. Inverse kinematics is solved numerically with damped least squares
/// (Levenberg-Marquardt), which stays well-behaved near singularities where a
/// plain Jacobian pseudo-inverse blows up.

#include <vector>

#include "common/types.hpp"
#include "robot/types.hpp"

namespace prodxcloud::robot {

/// The 6xN geometric Jacobian, stored row-major: 6 rows (vx vy vz wx wy wz).
struct Jacobian {
    int                 dof = 0;
    std::vector<double> data;  // 6 * dof

    double& at(int row, int col) { return data[static_cast<size_t>(row * dof + col)]; }
    [[nodiscard]] double at(int row, int col) const {
        return data[static_cast<size_t>(row * dof + col)];
    }
};

struct IKOptions {
    int    max_iterations   = 200;
    double position_tol_m   = 1e-4;   ///< 0.1 mm
    double orientation_tol_rad = 1e-3;
    double damping          = 0.05;   ///< λ in (JᵀJ + λ²I)Δq = Jᵀe
    double step_clamp_rad   = 0.20;   ///< max joint delta per iteration
    bool   respect_limits   = true;   ///< clamp iterates into the joint range
};

struct IKSolution {
    JointVector joints;
    int         iterations       = 0;
    double      position_error_m = 0.0;
    double      orientation_error_rad = 0.0;
    bool        converged        = false;
};

class Kinematics {
public:
    explicit Kinematics(DeviceSpec spec);

    [[nodiscard]] const DeviceSpec& spec() const { return spec_; }
    [[nodiscard]] int dof() const { return spec_.dof; }

    /// Transform of the tool centre point in the base frame.
    [[nodiscard]] Mat4 forward(const JointVector& q) const;

    /// Convenience wrapper returning position + RPY.
    [[nodiscard]] Pose forward_pose(const JointVector& q) const;

    /// Transform of every link frame, index 0 = base, index i = frame after link i.
    [[nodiscard]] std::vector<Mat4> link_transforms(const JointVector& q) const;

    /// Geometric Jacobian at configuration @p q.
    [[nodiscard]] Jacobian jacobian(const JointVector& q) const;

    /// Yoshikawa manipulability, sqrt(det(J Jᵀ)). Near zero means near-singular.
    [[nodiscard]] double manipulability(const JointVector& q) const;

    /// Solve IK for @p target, seeded from @p seed (typically the current pose).
    /// Fails (rather than returning a bad answer) when the target is unreachable.
    [[nodiscard]] Result<IKSolution> inverse(const Pose&        target,
                                             const JointVector& seed,
                                             const IKOptions&   opts = {}) const;

    /// True if @p q lies inside every joint's position limit.
    [[nodiscard]] bool within_limits(const JointVector& q) const;

    /// Clamp @p q into the joint position limits.
    [[nodiscard]] JointVector clamp_to_limits(const JointVector& q) const;

private:
    DeviceSpec spec_;
};

/// Solve the small dense system A x = b in place (Gaussian elimination with
/// partial pivoting). Returns false if A is singular to working precision.
bool solve_linear_system(std::vector<double>& a, std::vector<double>& b, int n);

}  // namespace prodxcloud::robot
