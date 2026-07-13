<div align="center">

# 🦾 robot

### A robot that thinks on-device, moves at 1 kHz, and refuses to hurt you.

**C++23 · zero cloud SDKs · 500-row onboard brain · deterministic simulator · [vxnode](https://github.com/prodxcloud/vxnode)-delegated infrastructure**

[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=flat-square&logo=cplusplus)](https://en.cppreference.com/w/cpp/23)
[![Build](https://img.shields.io/badge/build-CMake-064F8C?style=flat-square&logo=cmake)](CMakeLists.txt)
[![Tests](https://img.shields.io/badge/tests-149%20passing-2ea44f?style=flat-square)](tests/test_robot.cpp)
[![Cloud SDKs](https://img.shields.io/badge/cloud%20SDKs-0-ff4b4b?style=flat-square)](#the-one-rule-this-repo-provisions-nothing)
[![Provisioning](https://img.shields.io/badge/provisioning-vxnode-6f42c1?style=flat-square)](https://github.com/prodxcloud/vxnode)

</div>

---

```
$ robotsim demo

  [3/9] descend onto the part
       tool  ( 0.450, -0.150,  0.220) m
       joints[   -6.2  -203.1   114.5    88.7   -96.2   -90.0 ] deg
  ok   reached in 0.91 s, 912 ticks, 0.213 mm from target

  ok   9-step pick-and-place completed
       cycle time      7.53 s
       control ticks   9812 @ 1 kHz
       safety verdicts 11 logged
```

No hardware. No cloud account. No API key. That ran on a laptop, in a terminal, in 7.5 seconds.

---

## The one rule: this repo provisions nothing

Most "AI robot" repos smuggle in an AWS SDK, a Terraform binary and a set of provider
credentials, and then quietly become a way to spend money. This one cannot.

There is **no cloud SDK, no Terraform, no CloudFormation, no provider credential**
anywhere in this tree. Grep for it. When the robot needs a machine, it *asks* for one:

```
  robot                          vxnode                     the cloud
  ─────                          ──────                     ─────────
  plans, controls devices        owns the credentials       AWS · Azure · GCP
  decides it needs compute  ──▶  owns the drivers      ──▶  Linode · DigitalOcean
  has no way to make one         owns the blast radius
                    POST /api/v2/provision/vm
```

That split is the security property. The robot ships no secrets, so a bad plan cannot
leak one. Everything it can do to infrastructure is exactly what the node's API
permits — and every request is one auditable HTTP call, tagged with the robot that
made it.

```bash
$ robotsim provision

   the robot decides it needs compute:
       "offload stereo depth inference for cell A"

   what leaves the robot:
{
  "url": "http://127.0.0.1:8744/api/v2/provision/vm",
  "payload": {
    "provider": "aws",  "instance_type": "t3.large",  "count": 2,
    "purpose": "offload stereo depth inference for cell A",
    "requested_by": "prodxcloud-robot"
  }
}
```

That is the *entire* provisioning surface of this repository — a typed wrapper around
[vxnode](https://github.com/prodxcloud/vxnode)'s node API. See
[`src/vxnode/`](src/vxnode/). Related: [vxcloud](https://github.com/prodxcloud/vxcloud),
the multi-cloud SDK vxnode is built on.

---

## It has its own brain

The robot does not phone home to think. It carries a **500-row knowledge corpus**
([`datasets/brain/robot_brain.csv`](datasets/brain/robot_brain.csv)) covering robotics,
computer usage and provisioning, indexed into a BM25 retrieval engine that turns a
sentence into an executable skill — in microseconds, deterministically, offline.

```bash
$ robotsim brain "the arm is about to hit something stop it now"

   skill estop  (safety/emergency_stop, confidence 0.71)
   exec  1 command(s): estop
   also considered: halt_motion(4.1) pause_program(2.8)
```

```bash
$ robotsim brain "spin up a vm to run the perception workload"

   skill vxnode_provision  (provisioning/provision_vm, confidence 0.63)
   route provisioning skill -> delegated to vxnode
         (this repo builds no infrastructure itself)
```

There is no model server in that path, no embedding API, no token budget. An LLM
([`src/ai/`](src/ai/) — Anthropic, OpenAI, Ollama) can sit *on top* to paraphrase or to
handle what the corpus misses. But the robot is never **dependent** on one to act, and
it will tell you when it doesn't know rather than guess:

```
$ robotsim brain "xyzzy plugh quux"
   unsure  best match scored 0.11 — below the confidence floor; refusing to guess
```

---

## It refuses

The safety layer is deliberately paranoid and deliberately dumb. It has no plan, no
memory, and no opinion about intent. It answers one question — *would executing this
violate a limit?* — and it **refuses rather than repairs**. Silently clamping a bad goal
into a legal one would turn a rejected motion into a *different* motion, which is how
robots hurt people.

```bash
$ robotsim safety

  ok   refused: motion commanded on an OFFLINE device
  ok   refused: joint goal outside its position limit
  ok   refused: cartesian target beyond the arm's reach
  ok   refused: target below the workspace floor (z < 0)
  ok   refused: speed scale above 100%

── COLLISION — the guard that runs on every one of the 1000 ticks/second ──
  ok   the same goal, in free space: accepted and completed
       injecting a 400 Nm reaction torque on joint 2 (the arm hits a fixture)...
  ok   collision detected and the arm stopped
       E-STOP: joint 2 is saturated at 150 Nm (>135 Nm, 90% of its rating)
              — the arm is pushing against something
       note the asymmetry: the SAME goal was accepted a moment ago in free space.
       Admission passed. What stopped the arm was the guard inside the control loop.

  ok   refused: motion while the e-stop is latched
  ok   RESET cleared the latch — and RESET is the one command allowed while latched

  ok   9/9 safety assertions held
```

Two properties worth calling out, because they are the ones that are usually wrong:

- **A collision is torque *saturation*, not "torque > limit".** A motor cannot exceed
  its own rating — it saturates. So `effort > max_torque` is a test that can never fire.
  What a collision actually looks like is the servo pinned at its limit while it fails
  to track: the arm pushing as hard as it can against something that will not move.
- **`RESET` is allowed precisely when the device is unsafe.** It is the one command that
  runs *because* the e-stop is latched — otherwise a tripped robot could never be
  recovered, and you would have to power-cycle a machine that is holding a part.

Every verdict — allowed **and refused** — lands in an audit log. A robot that only
records what it did is useless in the post-mortem of what it refused to do.

---

## Quick start

```bash
git clone https://github.com/prodxcloud/robot && cd robot

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

./build/apps/robot_sim/robotsim demo         # pick and place, end to end
./build/apps/robot_sim/robotsim safety       # watch it refuse
./build/apps/robot_sim/robotsim kinematics   # FK/IK round-trip, with an ASCII arm
./build/apps/robot_sim/robotsim brain "pick up the block"
./build/apps/robot_sim/robotsim provision    # delegation to vxnode, dry-run
./build/apps/robot_sim/robotsim node         # is your vxnode node alive?

ctest --test-dir build --output-on-failure   # 149 assertions
```

**Dependencies: `spdlog`, `nlohmann_json`, a C++23 compiler.** That is the whole list.
A control loop should not need a web framework, a cloud SDK or a model runtime in order
to run, so it doesn't. The HTTP server is optional and builds only if Crow is present
(`-DENABLE_SERVER=OFF` to skip it).

<details>
<summary><b>Ubuntu / WSL from scratch</b></summary>

```bash
sudo apt update && sudo apt install -y build-essential cmake \
     libspdlog-dev nlohmann-json3-dev

cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
./build/apps/robot_sim/robotsim demo
```
</details>

---

## Architecture

```
        "pick up the block and put it in the bin"
                        │
                        ▼
   ┌────────────────────────────────────────────┐
   │  BRAIN            src/robot/brain.cpp      │   500-row corpus, BM25,
   │  retrieve → rank → compile to commands     │   offline, deterministic
   └────────────────────────────────────────────┘
                        │
                        ▼
   ┌────────────────────────────────────────────┐
   │  SAFETY           src/robot/safety.cpp     │   limits · workspace · reach
   │  allow / reject / e-stop                   │   singularity · collision
   └────────────────────────────────────────────┘
              │ allow                  │ reject
              ▼                        └──▶ refuse + audit
   ┌────────────────────────────────────────────┐
   │  PLANNER          src/robot/trajectory.cpp │   trapezoidal profiles,
   │  synchronised joint-space trajectories     │   all joints arrive together
   └────────────────────────────────────────────┘
                        │
                        ▼
   ┌────────────────────────────────────────────┐
   │  CONTROL LOOP     src/robot/controller.cpp │   1 kHz · safety re-checked
   │  servo · in-position settle · audit        │   EVERY tick · then settle
   └────────────────────────────────────────────┘
                        │
                        ▼
   ┌────────────────────────────────────────────┐
   │  DEVICE           src/robot/simulator.cpp  │   semi-implicit Euler,
   │  simulated now, fieldbus later             │   deterministic, seeded
   └────────────────────────────────────────────┘

   ╭─ needs infrastructure? ──────────────────────────────────────╮
   │  src/vxnode/  ──▶  POST vxnode /api/v2/provision/vm          │
   │  (the robot does not know how to make a machine — by design) │
   ╰──────────────────────────────────────────────────────────────╯
```

| Module | What it is |
|---|---|
| [`src/robot/kinematics`](src/robot/kinematics.cpp) | DH forward kinematics; damped-least-squares IK that stays sane at singularities. Converges in ~5 iterations to 13 µm. |
| [`src/robot/trajectory`](src/robot/trajectory.cpp) | Trapezoidal velocity profiles, time-synchronised so every joint starts and stops together. Degenerates to triangular when it can't cruise. |
| [`src/robot/safety`](src/robot/safety.cpp) | The gate. Joint limits, workspace envelope, reach, singularity, thermal, collision. Refuses; never repairs. |
| [`src/robot/simulator`](src/robot/simulator.cpp) | A deterministic servo model — the device driver's stunt double. Same seed, same run, every time. |
| [`src/robot/brain`](src/robot/brain.cpp) | BM25 over the onboard corpus. Field-weighted (a curated keyword outranks incidental prose), with a confidence floor below which it admits it doesn't know. |
| [`src/robot/controller`](src/robot/controller.cpp) | Where thinking becomes moving. The only component allowed to make a device act. |
| [`src/vxnode/`](src/vxnode/) | The only route to infrastructure. Dependency-free HTTP; TLS via libcurl when the node isn't on loopback. |

---

## Why the zero pose is not home

A 6R arm at all-zeros is fully outstretched — which is a **boundary singularity**, where
the Jacobian loses rank and the IK solver's step blows up. So `HOME` targets a tucked,
well-conditioned pose (`0, -90°, 90°, -90°, -90°, 0`), and the safety layer will refuse
to drive into the zero pose at all. The test suite asserts this explicitly:

```cpp
check(w_zero < w, "the fully-outstretched zero pose is closer to singular than home");
```

This is the kind of thing that only shows up when you *run* the robot, which is why
there is a simulator and 149 tests rather than a README full of promises.

---

## Configuration

| Variable | Default | What |
|---|---|---|
| `VXNODE_URL` | `http://127.0.0.1:8744` | Your vxnode node |
| `VXNODE_API_KEY` | — | Sent as `X-API-Key` |
| `VXNODE_DRY_RUN` | `0` | Print the call, don't send it |
| `ROBOT_BRAIN_CSV` | `datasets/brain/robot_brain.csv` | The corpus |

Bring up a node:

```bash
docker run -d -p 8744:8744 vxcloud/vxnode        # see prodxcloud/vxnode
export VXNODE_URL=http://127.0.0.1:8744
./build/apps/robot_sim/robotsim node             # confirm it's alive
```

---

## Extending it to real hardware

`SimulatedDevice` exists to be replaced. It exposes exactly the interface a real driver
would — write a joint setpoint, read back the measured state — so a fieldbus
implementation (EtherCAT, CANopen, a vendor SDK) drops into the same slot and every
layer above it, safety included, is unchanged:

```cpp
void set_target(const JointVector& q, const JointVector& qd = {});
void step(double dt);
DeviceState state() const;   // measured, with encoder noise — never ground truth
```

The controller never reads ground truth. It closes the loop on the *measured* state,
because that is all real hardware will ever give it.

---

<div align="center">

**[prodxcloud/vxnode](https://github.com/prodxcloud/vxnode)** · **[prodxcloud/vxcloud](https://github.com/prodxcloud/vxcloud)** · [vxcloud.io](https://vxcloud.io)

<sub>© 2026 PRODXCLOUD — Joel O. Wembo</sub>

</div>
