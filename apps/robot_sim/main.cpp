/// @file main.cpp
/// @brief `robotsim` — drive the robot without any hardware, and without any cloud.
///
/// Subcommands:
///   demo        full pick-and-place: boot, home, approach, grip, transfer, place
///   brain       ask the onboard brain a question, see what it retrieves and plans
///   safety      prove the safety layer refuses what it should
///   kinematics  forward/inverse kinematics round-trip
///   provision   show how a provisioning request is delegated to vxnode
///   node        check the vxnode node's health

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "robot/controller.hpp"
#include "robot/kinematics.hpp"
#include "vxnode/vxnode_client.hpp"

using namespace prodxcloud;
using namespace prodxcloud::robot;

namespace {

// ─── Pretty printing ────────────────────────────────────────────────────────

constexpr const char* kReset  = "\033[0m";
constexpr const char* kBold   = "\033[1m";
constexpr const char* kDim    = "\033[2m";
constexpr const char* kGreen  = "\033[32m";
constexpr const char* kRed    = "\033[31m";
constexpr const char* kYellow = "\033[33m";
constexpr const char* kCyan   = "\033[36m";

void banner(const std::string& title) {
    // Subtract in signed arithmetic. `66 - title.size()` is unsigned, so a title
    // longer than 66 bytes wraps to a colossal value and std::string(n, '-')
    // throws length_error — and a std::max<size_t>(0, ...) guard cannot save it,
    // because the wrap has already happened by the time max sees the value.
    constexpr int kWidth = 66;
    const int     pad    = kWidth - static_cast<int>(title.size());

    std::cout << "\n" << kBold << kCyan << "── " << title << " "
              << std::string(static_cast<size_t>(std::max(3, pad)), '-') << kReset << "\n";
}

void ok(const std::string& msg)   { std::cout << kGreen << "  ok   " << kReset << msg << "\n"; }
void fail(const std::string& msg) { std::cout << kRed   << "  FAIL " << kReset << msg << "\n"; }
void info(const std::string& msg) { std::cout << kDim   << "       " << msg << kReset << "\n"; }

std::string fmt_joints(const JointVector& q) {
    std::ostringstream s;
    s << "[";
    for (size_t i = 0; i < q.size(); ++i) {
        if (i) s << " ";
        s << std::fixed << std::setprecision(1) << std::setw(7) << rad_to_deg(q[i]);
    }
    s << " ] deg";
    return s.str();
}

std::string fmt_pose(const Pose& p) {
    std::ostringstream s;
    s << std::fixed << std::setprecision(3) << "(" << std::setw(6) << p.position.x << ", "
      << std::setw(6) << p.position.y << ", " << std::setw(6) << p.position.z << ") m";
    return s.str();
}

/// A crude side-on view of the arm — enough to see that a motion is doing what
/// the numbers say it is.
void draw_arm(const Kinematics& kin, const JointVector& q) {
    constexpr int kW = 60, kH = 16;
    std::vector<std::string> canvas(kH, std::string(kW, ' '));

    const auto frames = kin.link_transforms(q);

    // Project x-z (side view). The arm's reach maps onto the canvas width.
    const double scale = 0.9;
    const auto   plot  = [&](const Vec3& p, char c) {
        const int cx = static_cast<int>(kW / 2.0 + p.x / scale * (kW / 2.0));
        const int cy = static_cast<int>(kH - 2 - p.z / scale * (kH - 2));
        if (cx >= 0 && cx < kW && cy >= 0 && cy < kH) canvas[static_cast<size_t>(cy)][static_cast<size_t>(cx)] = c;
    };

    // Draw each link as a line of dots between consecutive frame origins.
    for (size_t i = 1; i < frames.size(); ++i) {
        const Vec3 a = frames[i - 1].translation();
        const Vec3 b = frames[i].translation();

        for (int s = 0; s <= 20; ++s) {
            const double u = s / 20.0;
            plot({a.x + (b.x - a.x) * u, 0.0, a.z + (b.z - a.z) * u}, '.');
        }
        plot(b, 'o');
    }
    plot(frames.back().translation(), '#');   // tool
    plot({0, 0, 0}, 'B');                      // base

    std::cout << kDim;
    for (const auto& row : canvas) std::cout << "   |" << row << "|\n";
    std::cout << "   +" << std::string(kW, '-') << "+  " << kReset << "\n";
}

// ─── Fixtures ───────────────────────────────────────────────────────────────

std::string brain_path() {
    if (const char* p = std::getenv("ROBOT_BRAIN_CSV"); p && *p) return p;
    return "datasets/brain/robot_brain.csv";
}

// ─── Subcommands ────────────────────────────────────────────────────────────

int cmd_kinematics() {
    banner("KINEMATICS — forward and inverse, vx-arm6");

    const DeviceSpec spec = DeviceSpec::vx_arm6("arm-1", "Demo Arm");
    const Kinematics kin(spec);

    const JointVector home = spec.home;
    const Pose        p    = kin.forward_pose(home);

    info("home configuration  " + fmt_joints(home));
    ok("forward kinematics  tool at " + fmt_pose(p));
    info("manipulability      " + std::to_string(kin.manipulability(home)) +
         "  (0 would mean singular)");

    draw_arm(kin, home);

    // Round-trip: perturb the arm, take the resulting pose, and ask IK to find its
    // way back. If FK and IK disagree, this is where it shows.
    banner("IK ROUND-TRIP — can the solver recover a known pose?");

    const JointVector truth = {0.3, -1.2, 1.1, -1.4, -1.5, 0.2};
    const Pose        target = kin.forward_pose(truth);

    info("ground truth        " + fmt_joints(truth));
    info("its tool pose       " + fmt_pose(target));

    const auto sol = kin.inverse(target, home);
    if (!sol) {
        fail("IK failed: " + sol.error().message);
        return 1;
    }

    info("IK solution         " + fmt_joints(sol->joints));
    const Pose achieved = kin.forward_pose(sol->joints);

    const double err_mm = target.position.dist(achieved.position) * 1000.0;
    if (sol->converged && err_mm < 1.0) {
        ok("converged in " + std::to_string(sol->iterations) + " iterations, residual " +
           std::to_string(err_mm) + " mm");
    } else {
        fail("did not converge: residual " + std::to_string(err_mm) + " mm");
        return 1;
    }

    draw_arm(kin, sol->joints);

    // Unreachable targets must fail loudly rather than return a bogus pose.
    banner("UNREACHABLE TARGET — the solver must refuse, not improvise");
    Pose far;
    far.position = {2.5, 0.0, 1.0};

    if (const auto bad = kin.inverse(far, home); !bad) {
        ok("refused a target 2.5 m out on a 0.85 m arm");
        info(bad.error().message);
    } else {
        fail("solver returned a solution for an unreachable target");
        return 1;
    }
    return 0;
}

int cmd_demo() {
    banner("PICK AND PLACE — boot, home, approach, grip, transfer, place");

    RobotController robot;

    const auto arm_id = robot.add_device(DeviceSpec::vx_arm6("arm-1", "Line A Arm"));
    if (!arm_id) {
        fail(arm_id.error().message);
        return 1;
    }
    const auto grip_id = robot.add_device(DeviceSpec::vx_gripper("grip-1", "Line A Gripper"));
    if (!grip_id) {
        fail(grip_id.error().message);
        return 1;
    }

    ok("fleet: " + *arm_id + " (6-DOF arm) + " + *grip_id + " (2-finger gripper)");

    if (const auto r = robot.boot(*arm_id); !r) { fail(r.error().message); return 1; }
    if (const auto r = robot.boot(*grip_id); !r) { fail(r.error().message); return 1; }
    ok("both devices booted to home");

    const DeviceSpec spec = DeviceSpec::vx_arm6("arm-1", "Line A Arm");
    const Kinematics kin(spec);

    struct Step {
        std::string label;
        Command     cmd;
    };

    const auto pose_cmd = [&](const std::string& device, double x, double y, double z,
                              double speed) {
        Command c;
        c.id          = "cmd-" + device + "-" + std::to_string(static_cast<int>(x * 1000));
        c.device_id   = device;
        c.type        = CommandType::MOVE_LINEAR;
        c.pose_goal.position = {x, y, z};
        c.pose_goal.pitch    = kPi / 2.0;  // tool pointing down
        c.speed_scale = speed;
        c.accel_scale = speed;
        return c;
    };

    const auto grip_cmd = [&](const std::string& id, double width) {
        Command c;
        c.id           = id;
        c.device_id    = "grip-1";
        c.type         = CommandType::GRIP;
        c.grip_width_m = width;
        c.speed_scale  = 0.8;
        c.accel_scale  = 0.8;
        return c;
    };

    std::vector<Step> program = {
        {"approach the pick point (above the part)", pose_cmd("arm-1",  0.45, -0.15, 0.35, 0.6)},
        {"open the jaws",                            grip_cmd("open-1", 0.080)},
        {"descend onto the part",                    pose_cmd("arm-1",  0.45, -0.15, 0.22, 0.25)},
        {"close on the part",                        grip_cmd("close-1", 0.030)},
        {"lift clear",                               pose_cmd("arm-1",  0.45, -0.15, 0.40, 0.4)},
        {"transfer across the cell",                 pose_cmd("arm-1",  0.35,  0.30, 0.40, 0.7)},
        {"descend onto the place target",            pose_cmd("arm-1",  0.35,  0.30, 0.24, 0.25)},
        {"release",                                  grip_cmd("open-2", 0.080)},
        {"retract",                                  pose_cmd("arm-1",  0.35,  0.30, 0.42, 0.6)},
    };

    double total_time = 0.0;
    uint64_t total_ticks = 0;
    int step_no = 0;

    for (const auto& step : program) {
        ++step_no;
        std::cout << "\n" << kBold << "  [" << step_no << "/" << program.size() << "] "
                  << step.label << kReset << "\n";

        const auto r = robot.execute(step.cmd);
        if (!r) {
            fail(r.error().message + "  " + r.error().detail);
            return 1;
        }

        total_time  += r->duration_s;
        total_ticks += r->ticks_executed;

        if (step.cmd.device_id == "arm-1") {
            info("tool  " + fmt_pose(r->final_pose));
            info("joints" + fmt_joints(r->final_joints));

            const double err_mm =
                step.cmd.pose_goal.position.dist(r->final_pose.position) * 1000.0;
            ok("reached in " + std::to_string(r->duration_s) + " s, " +
               std::to_string(r->ticks_executed) + " ticks, " + std::to_string(err_mm) +
               " mm from target");
        } else {
            info("jaw width " + std::to_string(r->final_joints[0] * 1000.0) + " mm");
            ok("done in " + std::to_string(r->duration_s) + " s");
        }
    }

    banner("RESULT");
    ok("9-step pick-and-place completed");
    info("cycle time      " + std::to_string(total_time) + " s");
    info("control ticks   " + std::to_string(total_ticks) + " @ 1 kHz");
    info("safety verdicts " + std::to_string(robot.audit_log().size()) + " logged (all allowed)");

    const auto final_state = robot.state("arm-1");
    if (final_state) {
        info("arm temperature " + std::to_string(final_state->temperature_c) + " C");
        draw_arm(kin, final_state->joints.empty()
                          ? spec.home
                          : [&] {
                                JointVector q;
                                for (const auto& j : final_state->joints) q.push_back(j.position);
                                return q;
                            }());
    }
    return 0;
}

int cmd_safety() {
    banner("SAFETY — the layer that refuses");

    RobotController robot;
    const auto      id = robot.add_device(DeviceSpec::vx_arm6("arm-1", "Test Arm"));
    if (!id) { fail(id.error().message); return 1; }

    int passed = 0, total = 0;

    const auto expect_refusal = [&](const std::string& what, const Command& c) {
        ++total;
        const auto r = robot.execute(c);
        if (!r) {
            ok("refused: " + what);
            info(r.error().message);
            ++passed;
        } else {
            fail("ACCEPTED a command it should have refused: " + what);
        }
    };

    // A command on an un-booted device must not move anything.
    {
        Command c;
        c.id        = "c0";
        c.device_id = "arm-1";
        c.type      = CommandType::MOVE_JOINT;
        c.joint_goal = {0.0, -1.0, 1.0, -1.0, -1.0, 0.0};
        expect_refusal("motion commanded on an OFFLINE device", c);
    }

    if (const auto r = robot.boot("arm-1"); !r) { fail(r.error().message); return 1; }
    ok("device booted — now testing the limits");

    {
        Command c;
        c.id         = "c1";
        c.device_id  = "arm-1";
        c.type       = CommandType::MOVE_JOINT;
        c.joint_goal = {0.0, -1.0, 1.0, -1.0, -1.0, 99.0};  // joint 6 way past its stop
        expect_refusal("joint goal outside its position limit", c);
    }

    {
        Command c;
        c.id                 = "c2";
        c.device_id          = "arm-1";
        c.type               = CommandType::MOVE_LINEAR;
        c.pose_goal.position = {2.0, 0.0, 0.5};  // 2 m away, on a 0.85 m arm
        expect_refusal("cartesian target beyond the arm's reach", c);
    }

    {
        Command c;
        c.id                 = "c3";
        c.device_id          = "arm-1";
        c.type               = CommandType::MOVE_LINEAR;
        c.pose_goal.position = {0.35, 0.0, -0.30};  // below the floor
        expect_refusal("target below the workspace floor (z < 0)", c);
    }

    {
        Command c;
        c.id          = "c4";
        c.device_id   = "arm-1";
        c.type        = CommandType::MOVE_JOINT;
        c.joint_goal  = {0.0, -1.0, 1.0, -1.0, -1.0, 0.0};
        c.speed_scale = 5.0;  // 500% of the rated speed
        expect_refusal("speed scale above 100%", c);
    }

    // A collision mid-motion must trip the loop, not just the pre-flight check.
    // This is the case the admission gate structurally cannot see: the goal is
    // legal, and the arm hits something on the way to it.
    banner("COLLISION — the guard that runs on every one of the 1000 ticks/second");
    {
        Command clean;
        clean.id          = "c5";
        clean.device_id   = "arm-1";
        clean.type        = CommandType::MOVE_JOINT;
        clean.joint_goal  = {0.5, -1.2, 1.2, -1.5, -1.5, 0.5};
        clean.speed_scale = 0.5;
        clean.accel_scale = 0.5;

        ++total;
        if (const auto r = robot.execute(clean); r && r->success) {
            ok("the same goal, in free space: accepted and completed");
            ++passed;
        } else {
            fail("a legal motion in free space was refused");
        }

        // Now put an obstacle in the way. A 400 Nm reaction torque on the elbow
        // saturates a 150 Nm joint — the signature of an arm pushing on something
        // solid.
        info("injecting a 400 Nm reaction torque on joint 2 (the arm hits a fixture)...");
        if (const auto d = robot.inject_disturbance("arm-1", 2, 400.0); !d) {
            fail(d.error().message);
            return 1;
        }

        Command into_obstacle    = clean;
        into_obstacle.id         = "c6";
        into_obstacle.joint_goal = {0.0, -1.57, 1.57, -1.57, -1.57, 0.0};

        ++total;
        const auto r = robot.execute(into_obstacle);
        if (!r) {
            fail("the command was refused at admission — but the collision should be "
                 "caught *during* motion, not before it");
        } else if (!r->success && r->message.find("E-STOP") != std::string::npos) {
            ok("collision detected and the arm stopped");
            info(r->message);
            info("note the asymmetry: the SAME goal was accepted a moment ago in free "
                 "space. Admission passed. What stopped the arm was the guard inside the "
                 "control loop, on tick " + std::to_string(r->ticks_executed) + ".");
            ++passed;
        } else {
            fail("the arm drove through a collision without stopping");
        }
    }

    // Once latched, EVERYTHING is refused until an explicit reset.
    {
        Command c;
        c.id         = "c8";
        c.device_id  = "arm-1";
        c.type       = CommandType::MOVE_JOINT;
        c.joint_goal = {0.0, -1.5, 1.5, -1.5, -1.5, 0.0};
        expect_refusal("motion while the e-stop is latched", c);
    }

    {
        ++total;
        Command c;
        c.id        = "c9";
        c.device_id = "arm-1";
        c.type      = CommandType::RESET;

        const auto r = robot.execute(c);
        if (r && r->success) {
            ok("RESET cleared the latch — and RESET is the one command allowed while latched");
            ++passed;
        } else {
            fail("RESET was refused; the robot would be permanently bricked");
        }
    }

    banner("AUDIT LOG — every verdict, allowed or refused");
    for (const auto& a : robot.audit_log()) {
        const char* colour = a.verdict == "allow" ? kGreen : kYellow;
        std::cout << "   " << colour << std::setw(6) << a.verdict << kReset << "  "
                  << std::setw(11) << a.command_type << "  " << kDim << a.rule
                  << (a.rule.empty() ? "" : ": ") << a.detail << kReset << "\n";
    }

    std::cout << "\n";
    if (passed == total) {
        ok(std::to_string(passed) + "/" + std::to_string(total) + " safety assertions held");
        return 0;
    }
    fail(std::to_string(passed) + "/" + std::to_string(total) + " safety assertions held");
    return 1;
}

int cmd_brain(const std::vector<std::string>& args) {
    banner("BRAIN — offline retrieval over the onboard corpus");

    RobotController robot;
    const auto      n = robot.load_brain(brain_path());
    if (!n) {
        fail(n.error().message);
        info("set ROBOT_BRAIN_CSV, or run from the repo root");
        return 1;
    }

    ok("loaded " + std::to_string(*n) + " knowledge entries from " + brain_path());

    const auto counts = robot.brain().domain_counts();
    std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::cout << "\n" << kDim << "   what this robot knows:\n" << kReset;
    for (const auto& [domain, c] : sorted) {
        std::cout << "   " << std::setw(14) << domain << "  " << std::string(static_cast<size_t>(c / 2), '#')
                  << " " << c << "\n";
    }
    info(std::to_string(robot.brain().known_skills().size()) + " distinct skills dispatchable");

    const auto arm = robot.add_device(DeviceSpec::vx_arm6("arm-1", "Arm"));
    if (arm) (void)robot.boot(*arm);

    // Either answer the user's question, or run a sampler across the domains.
    std::vector<std::string> queries;
    if (args.size() > 1) {
        std::string q;
        for (size_t i = 1; i < args.size(); ++i) {
            if (i > 1) q += " ";
            q += args[i];
        }
        queries.push_back(q);
    } else {
        queries = {
            "pick up the block and put it in the bin",
            "the arm is about to hit something stop it now",
            "send the robot back to its home position",
            "how much disk space is left on the machine",
            "kill the process that is eating all the cpu",
            "spin up a vm to run the perception workload",
            "deploy the fastapi service to the node",
            "take a screenshot of the operator screen",
        };
    }

    for (const auto& q : queries) {
        std::cout << "\n" << kBold << "   ask: " << kReset << "\"" << q << "\"\n";

        const auto plan = robot.think(q, "arm-1");
        if (!plan) {
            fail(plan.error().message);
            continue;
        }

        if (!plan->understood) {
            std::cout << "   " << kYellow << "unsure" << kReset << "  " << plan->rationale << "\n";
            continue;
        }

        std::cout << "   " << kGreen << "skill " << kReset << plan->chosen.skill << kDim << "  ("
                  << plan->chosen.domain << "/" << plan->chosen.intent << ", confidence "
                  << std::fixed << std::setprecision(2) << plan->confidence << ")" << kReset << "\n";
        std::cout << "   " << kDim << "params " << plan->chosen.params << kReset << "\n";

        if (plan->is_provisioning()) {
            std::cout << "   " << kCyan << "route " << kReset
                      << "provisioning skill -> delegated to vxnode (this repo builds no "
                         "infrastructure itself)\n";
        } else if (!plan->commands.empty()) {
            std::cout << "   " << kCyan << "exec  " << kReset << plan->commands.size()
                      << " command(s): ";
            for (const auto& c : plan->commands)
                std::cout << command_type_to_string(c.type) << " ";
            std::cout << "\n";
        }

        if (!plan->alternatives.empty()) {
            std::cout << "   " << kDim << "also considered: ";
            for (const auto& alt : plan->alternatives)
                std::cout << alt.entry.intent << "(" << std::setprecision(1) << alt.score << ") ";
            std::cout << kReset << "\n";
        }
    }
    return 0;
}

int cmd_provision() {
    banner("PROVISIONING — delegated to vxnode, never done here");

    // Dry-run prints the exact HTTP call that would go to the node, without
    // sending it. This is the whole provisioning surface of the robot.
    vxnode::NodeConfig cfg = vxnode::NodeConfig::from_env();
    cfg.dry_run            = true;

    RobotController robot({}, cfg);

    info("node          " + cfg.base_url);
    info("api key       " + std::string(cfg.api_key.empty() ? "(unset — set VXNODE_API_KEY)"
                                                            : "(set)"));
    info("mode          DRY RUN — nothing will be created");

    vxnode::ProvisionRequest req;
    req.provider      = "aws";
    req.region        = "us-east-1";
    req.instance_type = "t3.large";
    req.image         = "ubuntu-24.04";
    req.name          = "robot-perception-worker";
    req.count         = 2;
    req.purpose       = "offload stereo depth inference for cell A";

    std::cout << "\n" << kBold << "   the robot decides it needs compute:\n" << kReset;
    info("\"" + req.purpose + "\"");

    const auto res = robot.request_node(req);
    if (!res) {
        fail(res.error().message);
        if (!res.error().detail.empty()) info(res.error().detail);
        return 1;
    }

    std::cout << "\n" << kBold << "   what leaves the robot:\n" << kReset;
    std::cout << kDim << res->raw_response << kReset << "\n";

    ok("request handed to vxnode — the node owns the cloud credentials, the drivers "
       "and the blast radius");
    info("the robot has no AWS SDK, no Terraform, no provider keys, and cannot create "
         "an instance on its own");

    banner("DEPLOY — same story");
    vxnode::DeployRequest dep;
    dep.stack    = "fastapi";
    dep.host     = "10.0.4.21";
    dep.repo_url = "https://github.com/prodxcloud/perception-service";
    dep.domain   = "perception.vxcloud.click";
    dep.port     = 8000;

    const auto d = robot.node().deploy(dep);
    if (!d) {
        fail(d.error().message);
        return 1;
    }
    std::cout << kDim << *d << kReset << "\n";
    ok("POST /api/v2/infrastructure/services/fastapi/deploy");

    return 0;
}

int cmd_node() {
    banner("VXNODE — is the node reachable?");

    const vxnode::VxNodeClient client;
    info("node " + client.config().base_url);

    const auto h = client.health();
    if (!h) {
        fail(h.error().message);
        info("start a node:  docker run -d -p 8744:8744 vxcloud/vxnode");
        info("or point at one:  export VXNODE_URL=https://<your-node>.vxcloud.click");
        return 1;
    }

    if (h->reachable) {
        ok("node is up: status=" + h->status + " version=" + h->version + " (" +
           std::to_string(h->latency_ms) + " ms)");
    } else {
        fail("node answered but is not healthy: " + h->raw_response);
        return 1;
    }
    return 0;
}

void usage() {
    std::cout << kBold << "\nrobotsim" << kReset
              << " — the PRODXCLOUD robot, running with no hardware and no cloud\n\n"
              << "  robotsim demo         pick-and-place, end to end\n"
              << "  robotsim brain [q]    ask the onboard brain something\n"
              << "  robotsim safety       prove the safety layer refuses what it should\n"
              << "  robotsim kinematics   forward/inverse kinematics round-trip\n"
              << "  robotsim provision    show provisioning being delegated to vxnode\n"
              << "  robotsim node         check the vxnode node's health\n\n"
              << kDim
              << "  env: VXNODE_URL (default http://127.0.0.1:8744), VXNODE_API_KEY,\n"
              << "       VXNODE_DRY_RUN=1, ROBOT_BRAIN_CSV\n"
              << kReset << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    spdlog::set_pattern("%^%l%$ %v");
    spdlog::set_level(std::getenv("ROBOT_DEBUG") ? spdlog::level::debug : spdlog::level::warn);

    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        usage();
        return 0;
    }

    const std::string& cmd = args[0];

    if (cmd == "demo")       return cmd_demo();
    if (cmd == "brain")      return cmd_brain(args);
    if (cmd == "safety")     return cmd_safety();
    if (cmd == "kinematics") return cmd_kinematics();
    if (cmd == "provision")  return cmd_provision();
    if (cmd == "node")       return cmd_node();

    if (cmd == "-h" || cmd == "--help" || cmd == "help") {
        usage();
        return 0;
    }

    std::cerr << "unknown command: " << cmd << "\n";
    usage();
    return 2;
}
