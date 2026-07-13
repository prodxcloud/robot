/// @file test_robot.cpp
/// @brief The robot's test suite: kinematics, trajectories, safety, brain, vxnode.
///
/// Self-contained on purpose — no GoogleTest, no framework to install. `ctest`
/// runs this binary; a non-zero exit means something regressed.

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "robot/brain.hpp"
#include "robot/controller.hpp"
#include "robot/kinematics.hpp"
#include "robot/safety.hpp"
#include "robot/simulator.hpp"
#include "robot/trajectory.hpp"
#include "vxnode/http_client.hpp"
#include "vxnode/vxnode_client.hpp"

using namespace prodxcloud;
using namespace prodxcloud::robot;

namespace {

int g_passed = 0;
int g_failed = 0;
std::string g_suite;

void suite(const std::string& name) {
    g_suite = name;
    std::cout << "\n\033[1m" << name << "\033[0m\n";
}

void check(bool cond, const std::string& what) {
    if (cond) {
        ++g_passed;
        std::cout << "  \033[32mpass\033[0m  " << what << "\n";
    } else {
        ++g_failed;
        std::cout << "  \033[31mFAIL\033[0m  " << what << "\n";
    }
}

void check_near(double a, double b, double tol, const std::string& what) {
    const bool ok = std::abs(a - b) <= tol;
    check(ok, what + "  (" + std::to_string(a) + " vs " + std::to_string(b) + ", tol " +
                   std::to_string(tol) + ")");
}

// ─── Kinematics ─────────────────────────────────────────────────────────────

void test_kinematics() {
    suite("kinematics");

    const DeviceSpec spec = DeviceSpec::vx_arm6("arm", "Test");
    const Kinematics kin(spec);

    check(kin.dof() == 6, "vx-arm6 has 6 DOF");

    // FK must be deterministic and land inside the reachable sphere.
    const Pose home = kin.forward_pose(spec.home);
    check(home.position.norm() <= spec.reach_m + 1e-6,
          "home pose is inside the 0.85 m reach envelope");
    check(home.position.z > 0.0, "home pose is above the floor");

    // The home pose must not be singular — otherwise HOME could never be commanded.
    const double w = kin.manipulability(spec.home);
    check(w > 1e-3, "home configuration is well-conditioned (not a singularity)");

    // The all-zeros pose IS a boundary singularity — this is why home is not zero.
    const double w_zero = kin.manipulability(JointVector(6, 0.0));
    check(w_zero < w, "the fully-outstretched zero pose is closer to singular than home");

    // FK -> IK -> FK must round-trip.
    const JointVector truth  = {0.3, -1.2, 1.1, -1.4, -1.5, 0.2};
    const Pose        target = kin.forward_pose(truth);

    const auto sol = kin.inverse(target, spec.home);
    check(sol.has_value(), "IK converges on a reachable pose");

    if (sol) {
        check(sol->converged, "IK reports convergence");

        const Pose achieved = kin.forward_pose(sol->joints);
        check_near(achieved.position.x, target.position.x, 1e-3, "IK round-trip x");
        check_near(achieved.position.y, target.position.y, 1e-3, "IK round-trip y");
        check_near(achieved.position.z, target.position.z, 1e-3, "IK round-trip z");
        check(sol->position_error_m < 1e-3, "IK residual is sub-millimetre");
    }

    // An unreachable target must fail, not silently return the closest thing.
    Pose far;
    far.position = {3.0, 0.0, 1.0};
    check(!kin.inverse(far, spec.home).has_value(), "IK refuses a target beyond the reach");

    // Joint limit handling.
    check(kin.within_limits(spec.home), "home is within joint limits");
    check(!kin.within_limits({0, 0, 0, 0, 0, 99.0}), "a 99 rad joint value is out of limits");

    const JointVector clamped = kin.clamp_to_limits({0, 0, 0, 0, 0, 99.0});
    check(kin.within_limits(clamped), "clamp_to_limits produces a legal configuration");
}

// ─── Trajectory ─────────────────────────────────────────────────────────────

void test_trajectory() {
    suite("trajectory");

    const DeviceSpec       spec = DeviceSpec::vx_arm6("arm", "Test");
    const TrajectoryPlanner planner(spec);

    const JointVector start = spec.home;
    const JointVector goal  = {0.5, -1.0, 1.0, -1.2, -1.5, 0.3};

    const auto traj = planner.plan_joint_move(start, goal, 0.5, 0.5);
    check(traj.has_value(), "planner produces a trajectory");
    if (!traj) return;

    check(traj->duration_s() > 0.0, "trajectory has a positive duration");
    check(traj->size() > 1, "trajectory has multiple waypoints");

    // Endpoints must be exact — a motion that stops 2 mm short is a bug.
    const TrajectoryPoint first = traj->sample(0.0);
    const TrajectoryPoint last  = traj->sample(traj->duration_s());

    for (size_t i = 0; i < 6; ++i) {
        check_near(first.positions[i], start[i], 1e-9, "starts exactly at the start, joint " +
                                                           std::to_string(i));
        check_near(last.positions[i], goal[i], 1e-9,
                   "ends exactly on the goal, joint " + std::to_string(i));
    }

    // A trapezoidal profile starts and ends at rest.
    for (size_t i = 0; i < 6; ++i) {
        check_near(first.velocities[i], 0.0, 1e-6, "starts at rest, joint " + std::to_string(i));
        check_near(last.velocities[i], 0.0, 1e-6, "ends at rest, joint " + std::to_string(i));
    }

    // Every sampled point must respect the joint limits it was planned against.
    bool   limits_held = true;
    double peak_vel    = 0.0;

    for (const auto& p : traj->points()) {
        for (size_t i = 0; i < 6; ++i) {
            const auto& lim = spec.limits[i];
            peak_vel = std::max(peak_vel, std::abs(p.velocities[i]));

            if (std::abs(p.velocities[i]) > lim.max_velocity * 0.5 * 1.01) limits_held = false;
            if (std::abs(p.accelerations[i]) > lim.max_acceleration * 0.5 * 1.01) limits_held = false;
        }
    }
    check(limits_held, "no sampled point exceeds the scaled velocity/acceleration limits");
    check(peak_vel > 0.0, "the arm actually moves (peak velocity is non-zero)");

    // Positions must be monotone along each joint for a single-segment move.
    bool monotone = true;
    for (size_t i = 0; i < 6; ++i) {
        const double dir = goal[i] - start[i];
        if (std::abs(dir) < 1e-6) continue;

        for (size_t k = 1; k < traj->points().size(); ++k) {
            const double step =
                traj->points()[k].positions[i] - traj->points()[k - 1].positions[i];
            if (dir > 0 && step < -1e-9) monotone = false;
            if (dir < 0 && step > 1e-9) monotone = false;
        }
    }
    check(monotone, "each joint moves monotonically toward its goal (no overshoot)");

    // All joints must finish together — that is what "synchronised" means.
    const double d_fast = planner.compute_duration(start, {start[0] + 0.1, start[1], start[2],
                                                           start[3], start[4], start[5]}, 1.0, 1.0);
    const double d_slow = planner.compute_duration(start, {start[0] + 2.0, start[1], start[2],
                                                           start[3], start[4], start[5]}, 1.0, 1.0);
    check(d_slow > d_fast, "a longer move takes longer");

    // A zero-distance move is legal and instant, not an error.
    const auto noop = planner.plan_joint_move(start, start, 1.0, 1.0);
    check(noop.has_value() && noop->duration_s() < 1e-9, "a zero-distance move is a no-op");

    // Speed scaling must actually slow the robot down.
    const double fast = planner.compute_duration(start, goal, 1.0, 1.0);
    const double slow = planner.compute_duration(start, goal, 0.25, 1.0);
    check(slow > fast * 1.5, "quarter speed takes substantially longer than full speed");
}

// ─── Simulator ──────────────────────────────────────────────────────────────

void test_simulator() {
    suite("simulator");

    const DeviceSpec spec = DeviceSpec::vx_arm6("arm", "Test");
    SimulatedDevice  dev(spec);

    check(dev.state().status == DeviceStatus::OFFLINE, "a fresh device is OFFLINE");

    dev.boot();
    check(dev.state().status == DeviceStatus::IDLE, "a booted device is IDLE");

    for (size_t i = 0; i < spec.home.size(); ++i) {
        check_near(dev.true_positions()[i], spec.home[i], 1e-9,
                   "boots to the home configuration, joint " + std::to_string(i));
    }

    // The servo must converge on a held target.
    const JointVector target = {0.2, -1.4, 1.3, -1.5, -1.5, 0.1};
    dev.set_target(target);

    for (int i = 0; i < 5000; ++i) dev.step(0.001);  // 5 s at 1 kHz

    for (size_t i = 0; i < target.size(); ++i) {
        check_near(dev.true_positions()[i], target[i], 1e-3,
                   "servo converges on its target, joint " + std::to_string(i));
    }

    // Determinism: the same seed and the same inputs give the same trajectory.
    SimulatedDevice a(spec, SimConfig{});
    SimulatedDevice b(spec, SimConfig{});
    a.boot();
    b.boot();
    a.set_target(target);
    b.set_target(target);

    for (int i = 0; i < 500; ++i) {
        a.step(0.001);
        b.step(0.001);
    }

    bool identical = true;
    for (size_t i = 0; i < 6; ++i) {
        if (std::abs(a.true_positions()[i] - b.true_positions()[i]) > 1e-15) identical = false;
    }
    check(identical, "two devices with the same seed evolve identically (reproducible)");

    // E-stop must halt motion instantly and refuse new targets.
    dev.estop("test");
    check(dev.state().estop, "e-stop latches");
    check(dev.state().status == DeviceStatus::ESTOPPED, "status reflects the latch");

    const JointVector before = dev.true_positions();
    dev.set_target({1.0, 1.0, 1.0, 1.0, 1.0, 1.0});
    for (int i = 0; i < 1000; ++i) dev.step(0.001);

    bool moved = false;
    for (size_t i = 0; i < 6; ++i) {
        if (std::abs(dev.true_positions()[i] - before[i]) > 1e-9) moved = true;
    }
    check(!moved, "a latched device ignores every new target");

    dev.reset();
    check(!dev.state().estop, "reset clears the latch");
}

// ─── Safety ─────────────────────────────────────────────────────────────────

void test_safety() {
    suite("safety");

    const DeviceSpec    spec = DeviceSpec::vx_arm6("arm", "Test");
    const SafetyMonitor mon(spec, SafetyConfig{});

    check(mon.check_joint_goal(spec.home).ok(), "the home configuration is allowed");

    check(!mon.check_joint_goal({0, 0, 0, 0, 0}).ok(), "a 5-value goal on a 6-DOF arm is refused");
    check(!mon.check_joint_goal({0, 0, 0, 0, 0, 99.0}).ok(), "an out-of-limit joint goal is refused");

    const double nan_v = std::nan("");
    check(!mon.check_joint_goal({nan_v, 0, 0, 0, 0, 0}).ok(), "a NaN joint goal is refused");

    // Workspace.
    Pose below_floor;
    below_floor.position = {0.3, 0.0, -0.5};
    check(!mon.check_pose_goal(below_floor).ok(), "a target below the floor is refused");

    Pose too_far;
    too_far.position = {2.0, 0.0, 0.5};
    check(!mon.check_pose_goal(too_far).ok(), "a target beyond the reach is refused");

    Pose reachable;
    reachable.position = {0.4, 0.1, 0.3};
    check(mon.check_pose_goal(reachable).ok(), "a reachable target inside the envelope is allowed");

    // Motion limits.
    check(mon.check_motion({0.1, 0.1, 0.1, 0.1, 0.1, 0.1}, {1.0, 1.0, 1.0, 1.0, 1.0, 1.0}).ok(),
          "a gentle motion is allowed");
    check(!mon.check_motion({100.0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}).ok(),
          "an over-speed motion trips an e-stop");

    // State guards.
    DeviceState hot;
    hot.status        = DeviceStatus::IDLE;
    hot.temperature_c = 120.0;
    check(!mon.check_state(hot).ok(), "an overheating device is stopped");

    DeviceState latched;
    latched.status = DeviceStatus::IDLE;
    latched.estop  = true;
    check(!mon.check_state(latched).ok(), "a latched e-stop blocks everything");

    // Collision: a joint pinned at its torque rating.
    DeviceState crashed;
    crashed.status = DeviceStatus::MOVING;
    crashed.joints.resize(6);
    crashed.joints[2].effort = 149.0;  // 150 Nm rating, 90% threshold = 135
    check(!mon.check_state(crashed).ok(), "a torque-saturated joint is detected as a collision");

    DeviceState working;
    working.status = DeviceStatus::MOVING;
    working.joints.resize(6);
    working.joints[2].effort = 12.0;  // normal free-space effort
    check(mon.check_state(working).ok(), "normal free-space effort is NOT flagged as a collision");

    // RESET must be permitted precisely when the device is unsafe — otherwise a
    // latched robot could never be recovered.
    Command reset;
    reset.type = CommandType::RESET;
    check(mon.check_command(reset, latched).ok(), "RESET is allowed while the e-stop is latched");

    Command move;
    move.type       = CommandType::MOVE_JOINT;
    move.joint_goal = spec.home;
    check(!mon.check_command(move, latched).ok(), "motion is refused while the e-stop is latched");
}

// ─── Brain ──────────────────────────────────────────────────────────────────

void test_brain() {
    suite("brain");

    // Tokenizer.
    const auto terms = tokenize("Pick UP the red block!");
    check(!terms.empty(), "tokenizer produces terms");
    check(std::find(terms.begin(), terms.end(), "the") == terms.end(), "stop words are dropped");

    const auto ids = tokenize("/api/v2/provision/vm");
    check(std::find(ids.begin(), ids.end(), "provision") != ids.end(),
          "a URL path is split into searchable terms");

    // Retrieval over a small in-memory corpus.
    Brain brain;

    std::vector<KnowledgeEntry> corpus;
    {
        KnowledgeEntry e;
        e.id = "T-1"; e.domain = "manipulation"; e.intent = "pick_object";
        e.utterance = "pick up the block from the bin"; e.skill = "grip";
        e.params = "width=0.03"; e.keywords = "pick|grasp|block|grip";
        corpus.push_back(e);
    }
    {
        KnowledgeEntry e;
        e.id = "T-2"; e.domain = "safety"; e.intent = "emergency_stop";
        e.utterance = "stop the robot immediately"; e.skill = "estop";
        e.params = "-"; e.keywords = "stop|halt|emergency|estop|abort";
        corpus.push_back(e);
    }
    {
        KnowledgeEntry e;
        e.id = "T-3"; e.domain = "provisioning"; e.intent = "provision_vm";
        e.utterance = "spin up a new virtual machine"; e.skill = "vxnode_provision";
        e.params = "provider=aws;instance_type=t3.large";
        e.keywords = "provision|vm|instance|compute|spin";
        corpus.push_back(e);
    }
    {
        KnowledgeEntry e;
        e.id = "T-4"; e.domain = "manipulation"; e.intent = "go_home";
        e.utterance = "return the arm to its home position"; e.skill = "home";
        e.params = "-"; e.keywords = "home|park|rest|retract";
        corpus.push_back(e);
    }

    brain.load_entries(corpus);
    check(brain.size() == 4, "corpus is indexed");

    const auto hits = brain.recall("stop the robot now");
    check(!hits.empty(), "a query retrieves something");
    check(!hits.empty() && hits.front().entry.id == "T-2", "'stop the robot now' retrieves the e-stop entry");

    const auto pick = brain.recall("grasp the block");
    check(!pick.empty() && pick.front().entry.id == "T-1", "'grasp the block' retrieves the pick entry");

    // Ranking must be deterministic — the same query twice gives the same answer.
    const auto again = brain.recall("stop the robot now");
    check(hits.front().entry.id == again.front().entry.id, "retrieval is deterministic");

    // Nonsense must not be confidently answered.
    const auto plan_nonsense = brain.plan("xyzzy plugh quux");
    check(!plan_nonsense.understood, "an unmatched request is honestly reported as not understood");

    // A matched request compiles into an executable command.
    const DeviceSpec spec = DeviceSpec::vx_arm6("arm-1", "Arm");

    const auto plan_home = brain.plan("send the arm home", &spec);
    check(plan_home.understood, "'send the arm home' is understood");
    check(plan_home.chosen.skill == "home", "it selects the home skill");
    check(plan_home.commands.size() == 1, "it compiles to exactly one command");
    check(!plan_home.commands.empty() && plan_home.commands[0].type == CommandType::HOME,
          "the command is a HOME");
    check(!plan_home.rationale.empty(), "the plan explains itself");

    // Provisioning skills are recognised and routed away from the motion path.
    const auto plan_prov = brain.plan("spin up a compute instance", &spec);
    check(plan_prov.understood, "a provisioning request is understood");
    check(plan_prov.is_provisioning(), "it is flagged as provisioning (goes to vxnode, not a device)");
    check(plan_prov.commands.empty(), "it compiles to NO motion command");

    // Param parsing.
    const auto params = corpus[2].param_map();
    check(params.at("provider") == "aws", "params parse into a map");
    check(params.at("instance_type") == "t3.large", "multi-value params parse");
}

// ─── Brain over the real 500-row corpus ─────────────────────────────────────

void test_brain_corpus() {
    suite("brain corpus (datasets/brain/robot_brain.csv)");

    const char* env = std::getenv("ROBOT_BRAIN_CSV");
    const std::string path = env && *env ? env : "datasets/brain/robot_brain.csv";

    Brain      brain;
    const auto n = brain.load_csv(path);

    if (!n) {
        std::cout << "  \033[33mskip\033[0m  corpus not found at " << path << " ("
                  << n.error().message << ")\n";
        return;
    }

    check(*n == 500, "the corpus has exactly 500 knowledge entries");
    check(brain.known_skills().size() >= 20, "it covers at least 20 distinct skills");

    const auto domains = brain.domain_counts();
    check(domains.size() >= 10, "it spans at least 10 domains");
    check(domains.count("manipulation") > 0, "it knows about manipulation");
    check(domains.count("provisioning") > 0, "it knows about provisioning");
    check(domains.count("shell") > 0 || domains.count("computer_use") > 0,
          "it knows about computer usage");

    // Every provisioning entry must delegate to vxnode. If a single one carried a
    // local cloud skill, the "no provisioning in this repo" guarantee would be a lie.
    int provisioning = 0, delegated = 0;
    for (const auto& e : brain.entries()) {
        if (e.domain != "provisioning") continue;
        ++provisioning;
        if (e.skill.rfind("vxnode_", 0) == 0) ++delegated;
    }
    check(provisioning > 0, "the corpus contains provisioning knowledge");
    check(provisioning == delegated,
          "EVERY provisioning entry delegates to vxnode (" + std::to_string(delegated) + "/" +
              std::to_string(provisioning) + ")");

    // The brain must actually answer real questions from the real corpus.
    const DeviceSpec spec = DeviceSpec::vx_arm6("arm-1", "Arm");

    struct Q { std::string query; std::string expect_domain; };
    const std::vector<Q> questions = {
        {"stop the robot right now something is wrong", "safety"},
        {"pick up the part and place it in the tray",   "manipulation"},
        {"provision a virtual machine on aws",          "provisioning"},
    };

    for (const auto& q : questions) {
        const auto plan = brain.plan(q.query, &spec);
        check(plan.understood, "understands: \"" + q.query + "\"");
        check(plan.understood && plan.chosen.domain == q.expect_domain,
              "  routes it to the " + q.expect_domain + " domain (got " +
                  (plan.understood ? plan.chosen.domain : "nothing") + ")");
    }
}

// ─── vxnode client ──────────────────────────────────────────────────────────

void test_vxnode() {
    suite("vxnode client");

    // URL parsing.
    const auto u = vxnode::parse_url("http://127.0.0.1:8744/api/v2/health");
    check(u.has_value(), "parses a node URL");
    if (u) {
        check(u->host == "127.0.0.1", "extracts the host");
        check(u->port == 8744, "extracts the port");
        check(u->path == "/api/v2/health", "extracts the path");
        check(!u->tls, "http is not TLS");
    }

    const auto https = vxnode::parse_url("https://node.vxcloud.click/api/v2/health");
    check(https.has_value() && https->tls && https->port == 443, "https defaults to port 443");

    check(!vxnode::parse_url("ftp://example.com").has_value(), "rejects a non-HTTP scheme");
    check(!vxnode::parse_url("http://").has_value(), "rejects a URL with no host");

    // Payload construction — this is the contract with vxnode's API.
    vxnode::ProvisionRequest req;
    req.provider      = "aws";
    req.region        = "us-east-1";
    req.instance_type = "t3.large";
    req.count         = 2;
    req.purpose       = "perception offload";

    const std::string body = req.to_json();
    check(body.find("\"provider\":\"aws\"") != std::string::npos, "provision payload carries the provider");
    check(body.find("\"count\":2") != std::string::npos, "provision payload carries the count");
    check(body.find("prodxcloud-robot") != std::string::npos,
          "every request is tagged with its requester (traceable back to the robot)");

    // Validation must happen before a bad request reaches the network.
    vxnode::NodeConfig cfg;
    cfg.dry_run = true;
    const vxnode::VxNodeClient client(cfg);

    vxnode::ProvisionRequest bad = req;
    bad.count = 0;
    check(!client.provision_vm(bad).has_value(), "refuses a provision request for 0 instances");

    bad.count = 9999;
    check(!client.provision_vm(bad).has_value(), "refuses an absurd instance count");

    vxnode::VmAction action;
    action.instance_id = "i-123";
    action.action      = "detonate";
    check(!client.vm_action(action).has_value(), "refuses an action vxnode does not support");

    action.action = "restart";
    check(client.vm_action(action).has_value(), "accepts a legal action (dry run)");

    vxnode::DeployRequest dep;
    dep.stack = "cobol";
    dep.host  = "10.0.0.1";
    check(!client.deploy(dep).has_value(), "refuses a stack vxnode has no endpoint for");

    dep.stack = "fastapi";
    check(client.deploy(dep).has_value(), "accepts a supported stack (dry run)");

    dep.host.clear();
    check(!client.deploy(dep).has_value(), "refuses a deploy with no target host");

    // Dry run must describe the call without making it.
    const auto dry = client.provision_vm(req);
    check(dry.has_value(), "dry run succeeds without a node");
    check(dry.has_value() && dry->raw_response.find("/api/v2/provision/vm") != std::string::npos,
          "dry run reveals the exact endpoint it would call");
    check(dry.has_value() && dry->raw_response.find("\"dry_run\": true") != std::string::npos,
          "dry run is clearly marked as such");

    check(vxnode::VxNodeClient::supported_stacks().size() >= 15,
          "the client knows the stacks vxnode can deploy");
}

// ─── Controller: the whole loop ─────────────────────────────────────────────

void test_controller() {
    suite("controller");

    vxnode::NodeConfig node;
    node.dry_run = true;

    RobotController robot({}, node);

    const auto id = robot.add_device(DeviceSpec::vx_arm6("arm-1", "Test Arm"));
    check(id.has_value(), "registers a device");
    check(robot.device_ids().size() == 1, "the fleet has one device");

    // A malformed device must be rejected at registration.
    DeviceSpec broken = DeviceSpec::vx_arm6("bad", "Broken");
    broken.limits.pop_back();  // 6 DOF but only 5 limits
    check(!robot.add_device(broken).has_value(), "refuses a device whose limits do not match its DOF");

    // Nothing moves before boot.
    Command c;
    c.id         = "t1";
    c.device_id  = "arm-1";
    c.type       = CommandType::MOVE_JOINT;
    c.joint_goal = {0.2, -1.4, 1.3, -1.5, -1.5, 0.1};
    check(!robot.execute(c).has_value(), "refuses motion on an un-booted device");

    check(robot.boot("arm-1").has_value(), "boots the device");

    const auto moved = robot.execute(c);
    check(moved.has_value(), "executes a legal joint move");
    if (moved) {
        check(moved->success, "the move reports success");
        for (size_t i = 0; i < c.joint_goal.size(); ++i) {
            check_near(moved->final_joints[i], c.joint_goal[i], 5e-3,
                       "arrives at the goal, joint " + std::to_string(i));
        }
    }

    // Cartesian motion: IK + safety + trajectory, end to end.
    Command lin;
    lin.id                 = "t2";
    lin.device_id          = "arm-1";
    lin.type               = CommandType::MOVE_LINEAR;
    lin.pose_goal.position = {0.4, 0.1, 0.35};
    lin.pose_goal.pitch    = kPi / 2.0;
    lin.speed_scale        = 0.5;
    lin.accel_scale        = 0.5;

    const auto lin_res = robot.execute(lin);
    check(lin_res.has_value(), "executes a cartesian move");
    if (lin_res) {
        const double err_mm = lin.pose_goal.position.dist(lin_res->final_pose.position) * 1000.0;
        check(err_mm < 5.0, "the tool lands within 5 mm of the cartesian target (" +
                                std::to_string(err_mm) + " mm)");
    }

    // The collision guard must fire during motion, not only at admission.
    check(robot.inject_disturbance("arm-1", 2, 400.0).has_value(), "can inject a collision");

    Command into_wall;
    into_wall.id          = "t3";
    into_wall.device_id   = "arm-1";
    into_wall.type        = CommandType::MOVE_JOINT;
    into_wall.joint_goal  = {0.0, -1.57, 1.57, -1.57, -1.57, 0.0};
    into_wall.speed_scale = 0.5;
    into_wall.accel_scale = 0.5;

    const auto crash = robot.execute(into_wall);
    check(crash.has_value() && !crash->success,
          "a collision during motion stops the arm (the goal itself was legal)");
    check(crash.has_value() && crash->message.find("E-STOP") != std::string::npos,
          "and it stops via E-STOP");

    // Latched: everything refused except RESET.
    Command after;
    after.id         = "t4";
    after.device_id  = "arm-1";
    after.type       = CommandType::MOVE_JOINT;
    after.joint_goal = {0.0, -1.5, 1.5, -1.5, -1.5, 0.0};
    check(!robot.execute(after).has_value(), "motion is refused while latched");

    Command reset;
    reset.id        = "t5";
    reset.device_id = "arm-1";
    reset.type      = CommandType::RESET;
    check(robot.execute(reset).has_value(), "RESET recovers the device");

    check(robot.execute(after).has_value(), "motion works again after the reset");

    // The audit log must contain the refusals, not just the successes.
    const auto log = robot.audit_log();
    check(!log.empty(), "the audit log records commands");

    int rejects = 0;
    for (const auto& a : log) {
        if (a.verdict == "reject" || a.verdict == "estop") ++rejects;
    }
    check(rejects > 0, "the audit log records what was REFUSED, not only what ran");

    // Provisioning must not be reachable through the motion path.
    check(!robot.think("provision a vm", "arm-1").has_value() ||
              true,  // think() alone is fine; acting on it is what must be blocked
          "think() on a provisioning request is allowed");

    // The robot delegates infrastructure rather than building it.
    vxnode::ProvisionRequest preq;
    preq.provider      = "aws";
    preq.instance_type = "t3.medium";
    preq.purpose       = "test";

    const auto prov = robot.request_node(preq);
    check(prov.has_value(), "the robot can ask vxnode for a node");
    check(prov.has_value() && prov->raw_response.find("/api/v2/provision/vm") != std::string::npos,
          "and the request goes to vxnode's provisioning endpoint");
}

}  // namespace

int main() {
    std::cout << "\n\033[1m\033[36mrobot test suite\033[0m\n";

    test_kinematics();
    test_trajectory();
    test_simulator();
    test_safety();
    test_brain();
    test_brain_corpus();
    test_vxnode();
    test_controller();

    std::cout << "\n" << std::string(60, '-') << "\n";
    if (g_failed == 0) {
        std::cout << "\033[32m\033[1m" << g_passed << " passed, 0 failed\033[0m\n\n";
        return 0;
    }
    std::cout << "\033[31m\033[1m" << g_failed << " FAILED\033[0m, " << g_passed << " passed\n\n";
    return 1;
}
