#!/usr/bin/env python3
"""Deterministic generator for the offline robot brain knowledge dataset.

Writes datasets/brain/robot_brain.csv : 1 header row + exactly 500 data rows.

Design constraints (driven by the C++ retrieval engine that loads this file):
  * The C++ CSV reader splits on ',' and does NOT handle quoting.
    => No field may contain a comma. Lists use '|' and params use ';'.
  * Output must be byte-identical on every run (no randomness no timestamps).

Run:  python tools/gen_brain_dataset.py
"""

import os
import re
import sys
from collections import Counter

HEADER = ["id", "domain", "intent", "utterance", "skill", "params",
          "preconditions", "safety", "keywords", "source"]

DOMAINS = {"manipulation", "gripper", "navigation", "perception", "safety",
           "calibration", "diagnostics", "maintenance", "teleop", "workcell",
           "computer_use", "shell", "filesystem", "network", "process",
           "provisioning"}

SKILLS = {"move_joint", "move_linear", "home", "grip", "release", "dwell",
          "estop", "reset", "calibrate", "teach_point", "scan_workspace",
          "detect_object", "follow_path", "navigate_to", "dock", "report_state",
          "run_diagnostic", "read_log", "tune_pid", "lubricate_check", "jog",
          "record_trajectory", "replay_trajectory", "shell_exec", "file_read",
          "file_write", "file_search", "dir_list", "process_list",
          "process_kill", "http_request", "port_check", "dns_lookup",
          "open_application", "click_ui", "type_text", "screenshot",
          "vxnode_provision", "vxnode_status", "vxnode_deploy", "vxnode_action"}

SOURCES = {"robot_manual", "ops_runbook", "computer_use", "safety_standard",
           "maintenance_guide", "vxnode_api"}

# Every provisioning row carries this clause: the robot is not an infra host.
VXCLAUSE = "robot never provisions locally - it always calls the vxnode node api"

ROWS = []  # (domain intent utterance skill params preconditions safety keywords source)


def add(domain, intent, utterance, skill, params, pre, safety, keywords, source):
    ROWS.append((domain, intent, utterance, skill, params, pre, safety, keywords, source))


def add_prov(intent, utterance, skill, params, pre, hazard, keywords):
    """Provisioning rows always delegate to the vxnode node API."""
    safety = hazard + " - " + VXCLAUSE if hazard != "-" else VXCLAUSE
    ROWS.append(("provisioning", intent, utterance, skill, params, pre, safety,
                 keywords, "vxnode_api"))


# ---------------------------------------------------------------------------
# ROBOTICS - manipulation (45)
# ---------------------------------------------------------------------------
M = "manipulation"
add(M, "move_to_pose", "move the arm to the pre-pick pose above the left bin", "move_linear",
    "pose=x0.42|y-0.18|z0.30|rz0;speed=0.15;frame=base_link", "arm homed and brakes released",
    "verify workspace clear before motion", "move|arm|pose|pre-pick|left bin", "robot_manual")
add(M, "move_to_pose", "can you send the robot to the handover position for the operator?", "move_linear",
    "pose=x0.55|y0.00|z0.45;speed=0.10;frame=base_link", "operator outside the fenced cell",
    "reduce speed to 250 mm/s inside the collaborative zone", "handover|position|operator|move|arm", "robot_manual")
add(M, "move_to_pose", "go to the joint configuration for the inspection station", "move_joint",
    "joints=0.00|-1.20|1.40|-1.75|-1.57|0.00;speed=0.20", "payload under 3 kg configured",
    "check for a wrist singularity before executing", "joint|configuration|inspection|station|move", "robot_manual")
add(M, "move_to_pose", "arm to camera pose", "move_joint",
    "joints=0.35|-1.05|1.10|-1.60|-1.57|0.35;speed=0.25", "camera mounted and powered",
    "keep the cable routing clear of joint 6 rotation", "camera|pose|arm|move|vision", "robot_manual")
add(M, "move_to_pose", "please move the tool center point straight down 50 millimetres", "move_linear",
    "axis=z;delta=-0.050;speed=0.05;frame=tool0", "tcp calibrated for the current tool",
    "stop on force above 20 N so the tool does not crash into the table", "tcp|down|linear|50mm|move", "robot_manual")
add(M, "move_to_pose", "shift the arm over to the right conveyor and hold there", "move_linear",
    "pose=x0.30|y0.52|z0.28;speed=0.18;frame=base_link;hold=true", "conveyor stopped or in sync mode",
    "confirm the conveyor guard interlock is closed", "conveyor|right|move|hold|arm", "ops_runbook")

add(M, "pick_object", "pick up the red block from the left bin", "grip",
    "object=red_block;source=left_bin;approach=top_down;force=35;width=0.045", "object detected and its pose published",
    "confirm the bin lid is open and no hand is inside the bin", "pick|red block|left bin|grasp|pick up", "robot_manual")
add(M, "pick_object", "grab the M8 bolt out of the parts tray", "grip",
    "object=m8_bolt;source=parts_tray;approach=pinch;force=20;width=0.009", "fine pinch fingers installed",
    "limit the force to 20 N so the threads are not deformed", "grab|bolt|m8|parts tray|pick", "robot_manual")
add(M, "pick_object", "can the arm pick the top-most carton off the pallet?", "grip",
    "object=carton;source=pallet;approach=top_down;vacuum=true;target_kpa=60", "vacuum pump on and holding 60 kPa",
    "verify the pallet stack is stable before lifting the top layer", "pick|carton|pallet|top|vacuum", "ops_runbook")
add(M, "pick_object", "pick the cylinder that the camera just found", "grip",
    "object=cylinder;pose_source=vision;approach=side;force=30;width=0.052", "detect_object returned a pose in the last 2 s",
    "reject the grasp if the vision confidence is below 0.8", "pick|cylinder|camera|vision|grasp", "robot_manual")
add(M, "pick_object", "take the sample tube from the rack", "grip",
    "object=sample_tube;source=rack_a;approach=top_down;force=12;width=0.016", "rack seated in its fixture",
    "cap the force at 12 N so the glass tube does not crack", "pick|sample tube|rack|lab|grasp", "robot_manual")
add(M, "pick_object", "lift the gearbox housing off the fixture", "grip",
    "object=gearbox_housing;source=fixture_2;approach=top_down;force=80;payload=4.2", "fixture clamps released",
    "the 4.2 kg payload is near the limit so keep the speed under 0.2 m/s", "lift|gearbox|housing|fixture|pick", "robot_manual")
add(M, "pick_object", "grasp the circuit board by its edges", "grip",
    "object=pcb;source=esd_tray;approach=edge_pinch;force=8;width=0.002", "esd grounding on the tooling verified",
    "edge grip only so the components are never touched", "grasp|pcb|circuit board|edges|esd", "robot_manual")
add(M, "pick_object", "pick the next widget from the infeed and wait for my go", "grip",
    "object=widget;source=infeed;approach=top_down;force=25;hold_after_pick=true", "infeed sensor reports a part present",
    "hold position until the operator confirms before transferring", "pick|widget|infeed|wait|hold", "ops_runbook")

add(M, "place_object", "place the block in the right bin", "release",
    "target=right_bin;pose=x0.38|y0.24|z0.12;retreat=0.08", "object currently held",
    "keep the drop height under 30 mm so the part is not damaged", "place|block|right bin|drop|release", "robot_manual")
add(M, "place_object", "put the bolt back where you got it", "release",
    "target=parts_tray;slot=origin;retreat=0.05", "the original slot pose was cached during the pick",
    "verify the slot is empty before releasing", "place|return|bolt|tray|release", "robot_manual")
add(M, "place_object", "set the carton down gently on the outfeed conveyor", "release",
    "target=outfeed;pose=x0.20|y0.60|z0.18;speed=0.05;vacuum_off=true", "conveyor running at 0.1 m/s",
    "release the vacuum only after the carton is fully supported", "place|carton|outfeed|conveyor|gentle", "ops_runbook")
add(M, "place_object", "drop the scrap part into the reject chute", "release",
    "target=reject_chute;pose=x-0.25|y0.40|z0.35;retreat=0.10", "part flagged as reject by inspection",
    "keep the chute area clear of personnel before dropping", "place|scrap|reject|chute|drop", "ops_runbook")
add(M, "place_object", "where do I tell it to place the finished assembly?", "release",
    "target=finished_goods;pose=x0.48|y-0.32|z0.15;retreat=0.06", "assembly inspection passed",
    "confirm the finished goods tray has a free slot", "place|finished|assembly|tray|output", "ops_runbook")
add(M, "place_object", "release the tube into slot B4 of the rack", "release",
    "target=rack_b;slot=b4;pose_z=0.09;force_limit=10", "slot b4 confirmed empty by vision",
    "insert slowly and abort if the contact force exceeds 10 N", "place|tube|rack|slot|b4", "robot_manual")

add(M, "stack_objects", "stack the boxes three high on the pallet", "release",
    "target=pallet;pattern=stack;layers=3;pitch_z=0.22", "pallet detected and squared to the base frame",
    "abort stacking if the layer tilt exceeds 2 degrees", "stack|boxes|pallet|three high|palletize", "ops_runbook")
add(M, "stack_objects", "build the next layer of the palletizing pattern", "follow_path",
    "path=pallet_layer_2;speed=0.25;blend=0.01", "previous layer complete and verified",
    "verify no operator is inside the pallet zone light curtain", "stack|layer|palletizing|pattern|pallet", "ops_runbook")
add(M, "stack_objects", "put this tray on top of the other one", "release",
    "target=tray_stack;pose_z=0.055;align=vision;force_limit=15", "lower tray pose known within 2 mm",
    "stop if the mating force exceeds 15 N", "stack|tray|on top|align|place", "robot_manual")
add(M, "stack_objects", "unstack the top plate and set it aside", "grip",
    "object=plate;source=plate_stack;approach=top_down;force=40;destination=staging", "stack height measured by the range sensor",
    "check the plate is not stuck to the one below before lifting", "unstack|plate|top|remove|staging", "ops_runbook")

add(M, "insert_peg", "insert the peg into the hole", "move_linear",
    "strategy=spiral_search;force_limit=25;depth=0.030;search_radius=0.003", "force torque sensor zeroed",
    "abort the insertion if the axial force exceeds 25 N", "insert|peg|hole|assembly|force", "robot_manual")
add(M, "insert_peg", "push the connector into the socket until it clicks", "move_linear",
    "strategy=push_until_force;force_target=18;max_depth=0.012", "connector aligned within 1 mm of the socket",
    "stop at 18 N so the connector housing is not cracked", "insert|connector|socket|click|push", "robot_manual")
add(M, "insert_peg", "seat the bearing into the housing bore", "move_linear",
    "strategy=press_fit;force_target=120;speed=0.005;depth=0.018", "bore chamfer cleaned and lubricated",
    "the press fit needs 120 N so verify the fixture is clamped down", "insert|bearing|housing|bore|press fit", "robot_manual")
add(M, "insert_peg", "the peg keeps jamming - can it wiggle it in?", "move_linear",
    "strategy=spiral_search;wiggle=true;amplitude=0.0005;force_limit=20", "force torque sensor healthy",
    "retract and re-approach after three failed insertion attempts", "insert|peg|jam|wiggle|search", "robot_manual")

add(M, "pour_liquid", "pour the beaker into the flask", "move_joint",
    "motion=pour;tilt_deg=95;rate_deg_s=15;joint=6", "beaker gripped and the liquid level known",
    "pour slowly to avoid splashing chemicals", "pour|beaker|flask|liquid|tilt", "robot_manual")
add(M, "pour_liquid", "tip the container slowly until it is empty", "move_joint",
    "motion=pour;tilt_deg=110;rate_deg_s=8;hold_s=3", "container held in the pour orientation",
    "keep the tilt rate under 10 deg/s to prevent spills", "pour|tip|container|empty|slow", "robot_manual")
add(M, "pour_liquid", "can the robot dispense 50 ml into each cup?", "move_joint",
    "motion=pour;volume_ml=50;tilt_deg=85;rate_deg_s=10;targets=cup_1|cup_2|cup_3", "the pour profile is volume calibrated",
    "return the container upright between cups so it does not drip", "pour|dispense|50 ml|cups|volume", "robot_manual")

add(M, "push_object", "push the box to the edge of the table", "move_linear",
    "motion=push;direction=y+;distance=0.15;force_limit=30;speed=0.08", "a flat pusher face is installed",
    "stop 30 mm short of the table edge so the box does not fall", "push|box|table|edge|slide", "robot_manual")
add(M, "push_object", "nudge the part into the fixture", "move_linear",
    "motion=push;direction=x+;distance=0.008;force_limit=15;speed=0.02", "part within 10 mm of the fixture datum",
    "limit the push force to 15 N to protect the fixture locators", "push|nudge|part|fixture|align", "robot_manual")
add(M, "push_object", "slide the tray toward the operator side", "move_linear",
    "motion=push;direction=y-;distance=0.25;force_limit=25;speed=0.10", "operator acknowledged the handover request",
    "never push toward a person faster than 0.10 m/s", "push|slide|tray|operator|handover", "ops_runbook")
add(M, "push_object", "close the drawer", "move_linear",
    "motion=push;target=drawer_1;distance=0.30;force_limit=40;speed=0.12", "drawer rail free of obstructions",
    "back off immediately if the force spikes above 40 N", "push|close|drawer|shut|slide", "robot_manual")

add(M, "home_arm", "send the robot home", "home",
    "profile=default;speed=0.30", "no part in the gripper",
    "confirm the path home is clear of fixtures", "home|robot|return|park|zero", "robot_manual")
add(M, "home_arm", "get back to the home position before I shut down", "home",
    "profile=shutdown;speed=0.20;park_brakes=true", "program stopped and the queue empty",
    "engage the brakes after homing before powering down", "home|shutdown|park|position|return", "ops_runbook")
add(M, "home_arm", "what is the command to home all the joints?", "home",
    "profile=all_joints;speed=0.15;search_limits=true", "encoders powered for at least 5 s",
    "homing sweeps every joint so clear the full envelope first", "home|joints|all|homing|zero", "robot_manual")
add(M, "home_arm", "reset the arm to the zero pose", "home",
    "profile=zero_pose;speed=0.25", "no collision reported in the last cycle",
    "verify the tool will clear the table at the zero pose", "home|zero|reset|pose|arm", "robot_manual")

add(M, "approach_object", "hover above the target before grabbing it", "move_linear",
    "pose_source=vision;offset_z=0.08;speed=0.10", "target pose available from perception",
    "keep an 80 mm standoff until the grasp is confirmed", "approach|hover|above|target|standoff", "robot_manual")
add(M, "approach_object", "line the gripper up with the part but do not touch it", "move_linear",
    "align=vision;offset_z=0.05;yaw_align=true;speed=0.06", "gripper open to the pre-grasp width",
    "do not close the fingers until the alignment error is under 2 mm", "approach|align|gripper|part|pre-grasp", "robot_manual")
add(M, "approach_object", "approach the weld seam slowly", "move_linear",
    "pose=seam_start;speed=0.02;force_guard=10", "seam tracking sensor enabled",
    "creep in at 20 mm/s and stop on any contact above 10 N", "approach|weld|seam|slow|contact", "robot_manual")

add(M, "retract", "back the arm away from the workpiece", "move_linear",
    "axis=z;delta=0.10;frame=tool0;speed=0.12", "gripper released or empty",
    "retract along the tool axis so the part is not dragged", "retract|back away|workpiece|withdraw|clear", "robot_manual")
add(M, "retract", "pull out and go to a safe standby spot", "move_linear",
    "pose=x0.10|y0.00|z0.55;speed=0.20;via=retract_z", "no motion command queued behind this one",
    "retract straight up before any lateral move", "retract|standby|safe|withdraw|clear", "ops_runbook")
add(M, "retract", "get the tool out of the hole", "move_linear",
    "axis=z;delta=0.040;frame=tool0;speed=0.03;force_limit=20", "insertion force released",
    "withdraw along the insertion axis only so the peg is not side loaded", "retract|withdraw|tool|hole|extract", "robot_manual")

# ---------------------------------------------------------------------------
# ROBOTICS - gripper (25)
# ---------------------------------------------------------------------------
G = "gripper"
add(G, "open_gripper", "open the gripper", "release",
    "width=0.080;speed=0.05", "-",
    "make sure nothing is held before opening or the part will drop", "open|gripper|release|fingers|jaws", "robot_manual")
add(G, "open_gripper", "open the fingers all the way", "release",
    "width=0.110;speed=0.08", "gripper powered and referenced",
    "check the fingers will not hit the fixture at full stroke", "open|fingers|full|wide|gripper", "robot_manual")
add(G, "open_gripper", "can you let go of the part?", "release",
    "width=0.070;speed=0.04;confirm_drop=true", "part supported by a surface or fixture",
    "verify the part is resting on a surface before letting go", "open|let go|release|part|drop", "robot_manual")
add(G, "open_gripper", "pop the vacuum off", "release",
    "mode=vacuum;vacuum=false;blow_off_ms=150", "vacuum currently holding a part",
    "use a short blow-off pulse so the part is not launched", "vacuum|release|blow off|suction|open", "robot_manual")
add(G, "open_gripper", "open just enough to clear the part", "release",
    "width=0.055;speed=0.03", "part width known from vision or the recipe",
    "partial opening keeps the fingers clear of neighbouring parts", "open|partial|clear|gripper|width", "robot_manual")

add(G, "close_gripper", "close the gripper", "grip",
    "width=0.030;force=40;speed=0.05", "gripper open and clear",
    "keep fingers and hands clear before closing", "close|gripper|grip|fingers|clamp", "robot_manual")
add(G, "close_gripper", "grip the part firmly", "grip",
    "width=0.028;force=90;speed=0.03;hold=true", "part centred between the fingers",
    "90 N is a hard grip so confirm the part can take the clamping load", "close|grip|firm|hard|clamp", "robot_manual")
add(G, "close_gripper", "clamp down but be gentle - it is fragile", "grip",
    "width=0.042;force=10;speed=0.01;slip_detect=true", "soft fingertips fitted",
    "10 N maximum on fragile parts and enable slip detection", "grip|gentle|fragile|soft|close", "robot_manual")
add(G, "close_gripper", "turn the suction on", "grip",
    "mode=vacuum;vacuum=true;target_kpa=60;settle_ms=300", "vacuum pump running",
    "verify the vacuum reaches 60 kPa before lifting or the part will fall", "vacuum|suction|on|grip|pick", "robot_manual")
add(G, "close_gripper", "hold the tube without crushing it", "grip",
    "width=0.016;force=12;speed=0.01;slip_detect=true", "fine fingers installed",
    "cap the force at 12 N so the glass tube does not crack", "grip|tube|gentle|crush|hold", "robot_manual")

add(G, "set_grip_force", "set the grip force to 30 newtons", "grip",
    "force=30;apply=setting_only", "the gripper supports force control",
    "confirm the new force is below the part crush limit", "grip force|set|30|newtons|force", "robot_manual")
add(G, "set_grip_force", "lower the clamping force - we are marking the parts", "grip",
    "force=18;profile=soft;apply=setting_only", "the current recipe is editable",
    "reducing the force raises slip risk so lower the transfer speed too", "grip force|lower|marking|soft|reduce", "ops_runbook")
add(G, "set_grip_force", "what force should it use for the aluminium bracket?", "grip",
    "force=55;object=alu_bracket;width=0.036", "part material known",
    "55 N holds the bracket without denting the soft aluminium", "grip force|aluminium|bracket|force|setting", "robot_manual")
add(G, "set_grip_force", "crank the grip up for the heavy casting", "grip",
    "force=140;width=0.062;payload=7.5;hold=true", "heavy duty fingers fitted and the payload configured",
    "verify the 7.5 kg payload is within the arm rating before lifting", "grip force|heavy|casting|increase|strong", "robot_manual")

add(G, "check_grip", "is the gripper actually holding the part?", "report_state",
    "signal=gripper;fields=width|force|object_present", "-",
    "never move at speed while the grip state is unknown", "gripper|holding|part|state|check", "ops_runbook")
add(G, "check_grip", "check if we dropped the part", "report_state",
    "signal=gripper;fields=object_present|slip_events;window_s=5", "gripper feedback wired to the controller",
    "stop the cycle and report if the part was dropped in transit", "dropped|part|check|gripper|slip", "ops_runbook")
add(G, "check_grip", "read back the current finger opening", "report_state",
    "signal=gripper;fields=width|target_width", "-",
    "-", "gripper|width|opening|fingers|read", "robot_manual")

add(G, "release_part", "let go and back off", "release",
    "width=0.075;speed=0.05;retreat_z=0.06", "part supported",
    "release then retract vertically so the part is not knocked over", "release|let go|back off|retreat|drop", "robot_manual")
add(G, "release_part", "release the part into my hand", "release",
    "width=0.070;speed=0.02;mode=handover", "operator hand detected under the part",
    "handover mode runs at 0.02 m/s so the operator is never pinched", "release|handover|hand|operator|give", "safety_standard")
add(G, "release_part", "drop whatever it is holding right now", "release",
    "width=max;speed=0.10;force=0", "recovery or emergency context",
    "only drop an unsupported part where the fall cannot injure anyone", "release|drop|now|emergency|let go", "safety_standard")

add(G, "calibrate_gripper", "calibrate the gripper stroke", "calibrate",
    "target=gripper;routine=stroke;cycles=3", "fingers clear of any obstruction",
    "the gripper runs its full stroke so keep hands out during calibration", "calibrate|gripper|stroke|zero|reference", "maintenance_guide")
add(G, "calibrate_gripper", "the jaws are off by a couple of millimetres - re-zero them", "calibrate",
    "target=gripper;routine=zero_offset;tolerance_mm=0.5", "fingers clean and free of debris",
    "verify the new zero offset before the next production cycle", "calibrate|jaws|zero|offset|gripper", "maintenance_guide")
add(G, "calibrate_gripper", "run the gripper force calibration", "calibrate",
    "target=gripper;routine=force;reference_load_n=50", "reference load cell mounted",
    "keep hands out of the jaws during the force sweep", "calibrate|gripper|force|load cell|routine", "maintenance_guide")

add(G, "change_tool", "swap to the vacuum tool", "reset",
    "target=tool_changer;tool=vacuum_cup;verify_lock=true", "arm at the tool change station with an empty gripper",
    "confirm the tool changer lock engaged before moving away from the dock", "tool change|vacuum|swap|tool|changer", "robot_manual")
add(G, "change_tool", "put the two-finger gripper back on", "reset",
    "target=tool_changer;tool=parallel_2f;verify_lock=true", "the current tool is docked",
    "never move with an unlocked tool on the flange", "tool change|two finger|gripper|attach|swap", "robot_manual")

# ---------------------------------------------------------------------------
# ROBOTICS - navigation (25)
# ---------------------------------------------------------------------------
N = "navigation"
add(N, "navigate_to_goal", "drive to the loading dock", "navigate_to",
    "goal=loading_dock;frame=map;tolerance_m=0.10;max_speed=0.8", "robot localized on the map",
    "sound the horn and slow to 0.3 m/s in pedestrian aisles", "navigate|drive|loading dock|goto|move", "robot_manual")
add(N, "navigate_to_goal", "take the mobile base to station 3", "navigate_to",
    "goal=station_3;frame=map;tolerance_m=0.05;max_speed=0.6", "the planner has a valid costmap",
    "stop if the lidar sees an obstacle within 0.5 m", "navigate|station 3|mobile base|goto|drive", "robot_manual")
add(N, "navigate_to_goal", "can it get to the paint shop from here?", "navigate_to",
    "goal=paint_shop;frame=map;plan_only=false;max_speed=0.7", "paint shop door interlock released",
    "do not enter the booth while the spray cycle is active", "navigate|paint shop|route|goto|drive", "ops_runbook")
add(N, "navigate_to_goal", "go to the charging area", "navigate_to",
    "goal=charge_zone;frame=map;tolerance_m=0.15;max_speed=0.5", "battery above 5 percent",
    "yield to forklift traffic crossing the charge aisle", "navigate|charging|battery|goto|charge", "robot_manual")
add(N, "navigate_to_goal", "move to the inspection cell and wait", "navigate_to",
    "goal=inspection_cell;frame=map;wait_on_arrival=true;max_speed=0.6", "inspection cell free",
    "hold outside the cell boundary until the light curtain clears", "navigate|inspection|cell|wait|goto", "ops_runbook")
add(N, "navigate_to_goal", "head over to aisle 7 bay 2", "navigate_to",
    "goal=aisle7_bay2;frame=map;tolerance_m=0.08;max_speed=0.8", "warehouse map current",
    "reduce speed to 0.4 m/s around blind corners", "navigate|aisle 7|bay 2|warehouse|goto", "ops_runbook")
add(N, "navigate_to_goal", "send the amr back to the staging area", "navigate_to",
    "goal=staging;frame=map;tolerance_m=0.20;max_speed=0.9", "payload unloaded",
    "verify the payload is unloaded before travelling at full speed", "navigate|amr|staging|return|goto", "ops_runbook")
add(N, "navigate_to_goal", "drive two metres forward", "navigate_to",
    "goal=relative;dx=2.0;dy=0.0;frame=base_link;max_speed=0.3", "2.5 m of clear space ahead",
    "relative moves ignore the global map so check the path visually", "navigate|forward|two metres|relative|drive", "robot_manual")

add(N, "dock_robot", "dock at the charger", "dock",
    "target=charger_1;approach=marker;marker_id=12;final_speed=0.05", "charger contacts clean and free",
    "abort docking if the marker is lost inside 0.3 m", "dock|charger|charge|station|dock in", "robot_manual")
add(N, "dock_robot", "back the robot into the charging cradle", "dock",
    "target=charger_2;approach=reverse;marker_id=14;final_speed=0.04", "rear clearance sensors healthy",
    "reverse docking is blind so rely on the rear bumper and the marker", "dock|reverse|cradle|charger|back in", "robot_manual")
add(N, "dock_robot", "undock and get clear of the station", "dock",
    "action=undock;retreat_m=0.60;speed=0.10", "charging contactor open",
    "confirm the charge contactor is open before pulling away", "undock|leave|charger|clear|station", "robot_manual")
add(N, "dock_robot", "line up with the conveyor dock", "dock",
    "target=conveyor_dock;approach=marker;marker_id=21;tolerance_mm=8", "conveyor dock marker visible",
    "transfer only after the dock reports mechanically latched", "dock|conveyor|align|marker|latch", "ops_runbook")

add(N, "follow_route", "follow the taped route to the pack line", "follow_path",
    "path=tape_route_pack;speed=0.5;line_follow=true", "line sensor calibrated to the tape colour",
    "stop if the line is lost for more than 0.5 s", "follow|route|tape|pack line|path", "robot_manual")
add(N, "follow_route", "run the standard patrol loop", "follow_path",
    "path=patrol_loop_a;speed=0.6;loops=1", "patrol waypoints loaded",
    "pause the patrol whenever a person is detected on the route", "patrol|loop|route|follow|path", "ops_runbook")
add(N, "follow_route", "take the long way round - the main aisle is blocked", "follow_path",
    "path=detour_b;speed=0.5;avoid=main_aisle", "the detour path exists in the map",
    "verify the detour clearance for the current payload width", "detour|route|blocked|aisle|path", "ops_runbook")
add(N, "follow_route", "repeat the delivery route five times", "follow_path",
    "path=delivery_route;speed=0.7;loops=5", "battery above 60 percent",
    "check the battery reserve before committing to a multi-loop run", "route|repeat|delivery|loops|follow", "ops_runbook")

add(N, "stop_base", "stop the base right now", "estop",
    "scope=base;mode=soft;decel=hard", "-",
    "a soft stop halts the drive but the arm may still be energised", "stop|base|halt|now|stop moving", "safety_standard")
add(N, "stop_base", "hold position - someone walked in front of it", "estop",
    "scope=base;mode=hold;reason=person_detected", "-",
    "hold until the person is clear then require an operator resume", "stop|hold|person|pause|halt", "safety_standard")

add(N, "set_travel_speed", "slow the robot down in the walkway", "navigate_to",
    "param=max_speed;value=0.3;zone=walkway", "the walkway zone is defined in the map",
    "0.3 m/s is the pedestrian zone limit in the site safety rules", "speed|slow|walkway|limit|reduce", "safety_standard")
add(N, "set_travel_speed", "let it run at full speed on the straight", "navigate_to",
    "param=max_speed;value=1.2;zone=main_straight", "the straight segment is clear of pedestrians",
    "full speed only where the aisle is fenced from foot traffic", "speed|full|fast|straight|increase", "ops_runbook")

add(N, "localize_robot", "the robot thinks it is somewhere else - relocalize it", "reset",
    "target=localization;method=amcl;seed=nearest_marker", "at least one fiducial marker in view",
    "do not drive until the localization covariance settles", "localize|lost|relocalize|position|map", "ops_runbook")
add(N, "localize_robot", "where is the robot on the map?", "report_state",
    "signal=pose;frame=map;fields=x|y|yaw|covariance", "-",
    "-", "where|position|map|pose|locate", "ops_runbook")
add(N, "localize_robot", "set the starting position at the dock", "reset",
    "target=localization;method=set_pose;pose=dock_1", "the robot is physically at the dock",
    "a wrong initial pose will send the robot down the wrong aisle", "localize|initial pose|dock|set|position", "ops_runbook")

add(N, "return_home", "bring the robot back to base", "navigate_to",
    "goal=home_base;frame=map;tolerance_m=0.10;max_speed=0.7", "no active transport job",
    "cancel any pending job before recalling the robot", "home|base|return|recall|navigate", "ops_runbook")
add(N, "return_home", "abort the trip and come home", "navigate_to",
    "goal=home_base;cancel_current=true;max_speed=0.5", "the current goal is cancellable",
    "drop the payload at the nearest safe drop point before returning", "abort|cancel|return|home|navigate", "ops_runbook")

# ---------------------------------------------------------------------------
# ROBOTICS - perception (25)
# ---------------------------------------------------------------------------
P = "perception"
add(P, "detect_object", "find the red block on the table", "detect_object",
    "class=block;color=red;roi=table;confidence_min=0.75", "camera streaming and the exposure set",
    "do not grasp on a detection below 0.75 confidence", "find|red block|detect|table|vision", "robot_manual")
add(P, "detect_object", "what objects can the camera see right now?", "detect_object",
    "class=any;roi=full_frame;top_k=10", "camera streaming",
    "-", "detect|objects|camera|see|list", "robot_manual")
add(P, "detect_object", "locate the bolts in the bin", "detect_object",
    "class=bolt;roi=bin_left;top_k=20;confidence_min=0.6", "bin lighting on",
    "bin poses can be occluded so rescan after every pick", "detect|bolts|bin|find|vision", "robot_manual")
add(P, "detect_object", "is there a part on the fixture?", "detect_object",
    "class=part;roi=fixture_2;confidence_min=0.8;return=boolean", "fixture in view",
    "-", "detect|part|fixture|present|check", "ops_runbook")
add(P, "detect_object", "pick out the defective units on the belt", "detect_object",
    "class=unit;filter=defect;roi=conveyor;confidence_min=0.85", "inspection model loaded",
    "flag borderline defects for human review instead of auto-rejecting", "detect|defect|belt|conveyor|inspect", "ops_runbook")
add(P, "detect_object", "get me the pose of the gearbox housing", "detect_object",
    "class=gearbox_housing;output=pose6d;frame=base_link;confidence_min=0.8", "3d camera calibrated to the base frame",
    "reject the pose if the hand-eye calibration is older than 30 days", "detect|pose|gearbox|housing|6dof", "robot_manual")
add(P, "detect_object", "count how many widgets are in the tray", "detect_object",
    "class=widget;roi=tray;output=count", "tray fully inside the field of view",
    "-", "count|widgets|tray|detect|inventory", "ops_runbook")
add(P, "detect_object", "spot anything that does not belong in the workspace", "detect_object",
    "class=foreign_object;roi=workspace;confidence_min=0.5", "reference background model captured",
    "treat any unexpected object as a person until proven otherwise", "detect|foreign|object|workspace|intruder", "safety_standard")

add(P, "scan_workspace", "scan the workspace", "scan_workspace",
    "mode=depth;resolution=2mm;area=workcell", "arm outside the camera field of view",
    "move the arm clear so it is not baked into the collision map", "scan|workspace|depth|map|survey", "robot_manual")
add(P, "scan_workspace", "build a fresh collision map of the cell", "scan_workspace",
    "mode=octomap;resolution=5mm;update=replace", "cell empty of people",
    "a stale collision map is the top cause of crashes so rescan after any fixture change", "scan|collision|map|cell|octomap", "robot_manual")
add(P, "scan_workspace", "sweep the bin and tell me where the parts are", "scan_workspace",
    "mode=bin_scan;roi=bin_left;output=pose_list", "bin inside the scan volume",
    "rescan after every pick because the pile shifts", "scan|bin|parts|sweep|survey", "robot_manual")
add(P, "scan_workspace", "take a 3d snapshot of the pallet", "scan_workspace",
    "mode=pointcloud;roi=pallet;resolution=4mm;save=true", "pallet lighting adequate",
    "-", "scan|3d|pallet|pointcloud|snapshot", "ops_runbook")
add(P, "scan_workspace", "check the table for obstacles before we move", "scan_workspace",
    "mode=obstacle_check;area=table;clearance_mm=30", "camera unobstructed",
    "always scan for obstacles before a long linear move", "scan|obstacles|table|clear|check", "safety_standard")

add(P, "locate_marker", "find the aruco marker on the fixture", "detect_object",
    "class=aruco;dict=4x4_50;roi=fixture;output=pose6d", "marker unobstructed and in focus",
    "a partially occluded marker gives a bad pose so check the reprojection error", "marker|aruco|fixture|detect|fiducial", "robot_manual")
add(P, "locate_marker", "which tag id is the camera looking at?", "detect_object",
    "class=apriltag;family=36h11;output=id", "camera streaming",
    "-", "tag|apriltag|id|marker|detect", "robot_manual")
add(P, "locate_marker", "the marker keeps flickering - can you check the detection?", "detect_object",
    "class=aruco;roi=fixture;diagnostics=true;frames=30", "30 frames of video buffered",
    "unstable marker detection means unreliable poses so stop automatic picking", "marker|flicker|detection|unstable|check", "ops_runbook")

add(P, "inspect_quality", "inspect the weld for cracks", "detect_object",
    "class=weld_defect;model=weld_v3;roi=seam;confidence_min=0.7", "seam lighting on and the part cooled",
    "do not inspect a weld above 60 C because heat haze corrupts the image", "inspect|weld|crack|defect|quality", "ops_runbook")
add(P, "inspect_quality", "check the surface finish on this part", "detect_object",
    "class=surface_defect;model=surface_v2;roi=part_face;confidence_min=0.75", "diffuse lighting active",
    "-", "inspect|surface|finish|scratch|quality", "ops_runbook")
add(P, "inspect_quality", "did the label go on straight?", "detect_object",
    "class=label;output=angle_offset;roi=bottle;tolerance_deg=2", "bottle presented to the camera",
    "reject any label skewed more than 2 degrees", "inspect|label|straight|skew|quality", "ops_runbook")
add(P, "inspect_quality", "verify all four screws are installed", "detect_object",
    "class=screw;roi=cover;expected_count=4;confidence_min=0.8", "cover in the inspection pose",
    "a missing screw must fail the unit and stop the line", "inspect|screws|count|missing|verify", "ops_runbook")

add(P, "measure_dimension", "measure the gap between the two plates", "detect_object",
    "measure=gap;roi=plate_pair;units=mm;tolerance=0.2", "depth camera calibrated",
    "-", "measure|gap|plates|distance|dimension", "robot_manual")
add(P, "measure_dimension", "how tall is the stack right now?", "detect_object",
    "measure=height;roi=stack;units=mm;reference=table", "stack fully in view",
    "stop stacking once the measured height passes 1200 mm", "measure|height|stack|tall|dimension", "ops_runbook")
add(P, "measure_dimension", "get the diameter of the bore", "detect_object",
    "measure=diameter;roi=bore;units=mm;tolerance=0.05", "bore centred and in focus",
    "-", "measure|diameter|bore|hole|dimension", "robot_manual")

add(P, "read_code", "read the barcode on the carton", "detect_object",
    "class=barcode;symbology=code128;roi=carton_face", "barcode within 300 mm of the camera",
    "-", "barcode|read|carton|scan|code128", "ops_runbook")
add(P, "read_code", "scan the qr code on the tote and tell me the id", "detect_object",
    "class=qr;roi=tote_face;output=payload", "tote label clean and unobscured",
    "-", "qr|scan|tote|code|read", "ops_runbook")


# ---------------------------------------------------------------------------
# ROBOTICS - safety (20)
# ---------------------------------------------------------------------------
S = "safety"
add(S, "emergency_stop", "stop everything now", "estop",
    "scope=cell;category=1;latch=true", "-",
    "a category 1 stop decelerates then removes drive power", "estop|stop|emergency|now|halt", "safety_standard")
add(S, "emergency_stop", "emergency stop", "estop",
    "scope=cell;category=0;latch=true", "-",
    "a category 0 stop cuts power at once and the arm may coast so stay clear", "emergency stop|estop|kill|halt|stop", "safety_standard")
add(S, "emergency_stop", "kill the arm - it is about to hit the fixture", "estop",
    "scope=arm;category=1;latch=true;reason=imminent_collision", "-",
    "expect the tool to drift a few millimetres during the controlled stop", "estop|kill|collision|stop|arm", "safety_standard")
add(S, "emergency_stop", "someone is in the cell - shut it down", "estop",
    "scope=cell;category=1;latch=true;reason=person_in_cell", "-",
    "keep the stop latched until the person is out and the cell is scanned", "estop|person|cell|shut down|stop", "safety_standard")
add(S, "emergency_stop", "pause the robot but do not cut the power", "estop",
    "scope=cell;mode=protective_stop;latch=false", "-",
    "a protective stop holds position with power on so the brakes stay released", "pause|stop|protective|hold|power", "safety_standard")

add(S, "reset_estop", "clear the emergency stop", "reset",
    "target=estop;require_ack=true;scan_cell=true", "all estop buttons physically released",
    "never reset an estop until you have confirmed the cell is empty", "reset|estop|clear|release|recover", "safety_standard")
add(S, "reset_estop", "we are ready to restart - release the safety stop", "reset",
    "target=estop;require_ack=true;operator_id=required", "the cause of the stop was found and fixed",
    "the reset must be acknowledged by a named operator standing outside the fence", "reset|restart|safety|release|estop", "safety_standard")
add(S, "reset_estop", "the estop will not clear - what is holding it?", "report_state",
    "signal=safety;fields=estop_sources|latched|door_interlocks", "-",
    "an estop that will not clear usually means a second device is still tripped", "estop|will not clear|stuck|latched|diagnose", "safety_standard")

add(S, "check_safety_state", "is it safe to enter the cell?", "report_state",
    "signal=safety;fields=power_state|motion_state|door_interlocks|estop", "-",
    "never enter on a report alone - apply lockout tagout before reaching in", "safe|enter|cell|state|check", "safety_standard")
add(S, "check_safety_state", "what is the current safety status?", "report_state",
    "signal=safety;fields=all", "-",
    "-", "safety|status|state|check|report", "safety_standard")
add(S, "check_safety_state", "are the light curtains active?", "report_state",
    "signal=safety;fields=light_curtain|muted|last_break", "-",
    "a muted light curtain offers no protection so verify the muting is intentional", "light curtain|active|safety|check|status", "safety_standard")
add(S, "check_safety_state", "check whether the fence door is closed", "report_state",
    "signal=safety;fields=door_interlocks", "-",
    "an open fence door must force a protective stop", "fence|door|interlock|closed|check", "safety_standard")

add(S, "enable_protective_stop", "put the robot in reduced speed mode - I am working nearby", "estop",
    "mode=protective_stop;then=reduced_speed;limit_mm_s=250", "collaborative mode supported by the controller",
    "250 mm/s is the hand guiding limit in the collaborative robot standard", "reduced speed|nearby|collaborative|slow|safety", "safety_standard")
add(S, "enable_protective_stop", "enable the safety zone around the operator station", "reset",
    "target=safety_zone;zone=operator_station;action=enable", "zone geometry configured in the safety controller",
    "verify the zone boundary with a test trip before relying on it", "safety zone|enable|operator|zone|protect", "safety_standard")
add(S, "enable_protective_stop", "trigger a protective stop if anyone crosses the curtain", "reset",
    "target=safety_config;rule=curtain_break_to_pstop;action=enable", "light curtain wired to the safety controller",
    "a curtain break must always force a stop and never merely a slowdown", "protective stop|curtain|rule|trigger|safety", "safety_standard")

add(S, "lockout_tagout", "lock out the robot so I can change the tool", "estop",
    "scope=cell;category=0;lockout=true;tag=maintenance", "maintenance work order open",
    "apply your personal lock and tag - a software stop alone is not lockout tagout", "lockout|tagout|lock|maintenance|tool change", "safety_standard")
add(S, "lockout_tagout", "how do I safely de-energize the cell for maintenance?", "estop",
    "scope=cell;category=0;lockout=true;verify_zero_energy=true", "maintenance authorised",
    "isolate the electrical and pneumatic supply then verify zero energy before working", "lockout|de-energize|maintenance|isolate|safe", "safety_standard")
add(S, "lockout_tagout", "remove the lockout and bring the cell back up", "reset",
    "target=lockout;require_ack=true;scan_cell=true", "all locks removed by their owners and tools cleared",
    "only the person who applied a lock may remove it", "lockout|remove|restore|power|restart", "safety_standard")

add(S, "limit_speed", "cap the arm speed while the guard is open", "estop",
    "mode=reduced_speed;limit_mm_s=250;condition=guard_open", "guard open detection wired",
    "with the guard open the arm must never exceed 250 mm/s", "speed|limit|guard|open|safety", "safety_standard")
add(S, "limit_speed", "restore full speed - the cell is clear and locked", "reset",
    "target=speed_limit;value=full;require_cell_scan=true", "cell scanned and the fence closed",
    "confirm the cell is empty before restoring full speed", "speed|full|restore|clear|safety", "safety_standard")

# ---------------------------------------------------------------------------
# ROBOTICS - calibration (20)
# ---------------------------------------------------------------------------
C = "calibration"
add(C, "calibrate_tcp", "calibrate the tool center point", "calibrate",
    "target=tcp;method=4point;tolerance_mm=0.5", "a sharp reference tip is mounted in the cell",
    "jog slowly during the touch-off points so the tool is not bent", "calibrate|tcp|tool center|tool|zero", "robot_manual")
add(C, "calibrate_tcp", "the tcp is off after the crash - redo it", "calibrate",
    "target=tcp;method=6point;tolerance_mm=0.3;reset_first=true", "tool inspected for damage after the crash",
    "inspect the tool for bending before trusting any new tcp", "calibrate|tcp|crash|redo|tool", "maintenance_guide")
add(C, "calibrate_tcp", "how do I set the tcp for the new vacuum cup?", "calibrate",
    "target=tcp;method=4point;tool=vacuum_cup;save_as=tcp_vac", "vacuum cup mounted and locked",
    "verify the new tcp with a dry run before production", "tcp|vacuum cup|set|calibrate|tool", "robot_manual")
add(C, "calibrate_tcp", "verify the tool center point is still accurate", "calibrate",
    "target=tcp;mode=verify;tolerance_mm=0.5;points=4", "reference tip in place",
    "a tcp error above 0.5 mm will cause insertion failures so recalibrate", "tcp|verify|accurate|check|calibrate", "maintenance_guide")

add(C, "hand_eye_calibration", "run the hand eye calibration", "calibrate",
    "target=hand_eye;method=tsai_lenz;poses=15;board=charuco", "calibration board fixed rigidly in the workspace",
    "the arm moves through 15 poses so clear the envelope first", "calibrate|hand eye|camera|robot|vision", "robot_manual")
add(C, "hand_eye_calibration", "the camera and the arm disagree by about 5 mm", "calibrate",
    "target=hand_eye;method=tsai_lenz;poses=20;reset_first=true", "camera mount checked for looseness",
    "check the camera mount is tight first or the error will come straight back", "hand eye|camera|offset|disagree|calibrate", "maintenance_guide")
add(C, "hand_eye_calibration", "recalibrate the camera to the base frame", "calibrate",
    "target=hand_eye;variant=eye_to_hand;frame=base_link;poses=18", "board visible from every sample pose",
    "verify the reprojection error is under 0.5 px before accepting", "calibrate|camera|base frame|eye to hand|vision", "robot_manual")
add(C, "hand_eye_calibration", "check the camera calibration is still good", "calibrate",
    "target=hand_eye;mode=verify;samples=8;max_error_mm=1.0", "charuco board available",
    "reject the calibration if the residual error exceeds 1 mm", "camera|calibration|verify|check|vision", "maintenance_guide")

add(C, "teach_point", "teach this position as the pick point", "teach_point",
    "name=pick_point;frame=base_link;capture=current_pose", "arm jogged to the desired pose",
    "verify the taught pose is reachable at production speed", "teach|position|pick point|save|waypoint", "robot_manual")
add(C, "teach_point", "save where the arm is right now as waypoint 4", "teach_point",
    "name=waypoint_4;frame=base_link;capture=current_pose;overwrite=true", "arm stationary",
    "overwriting a waypoint changes every program that uses it", "teach|save|waypoint|current|position", "robot_manual")
add(C, "teach_point", "record the drop-off spot for the tote", "teach_point",
    "name=tote_dropoff;frame=map;capture=current_pose;approach_z=0.10", "robot at the drop-off location",
    "include an approach offset so the point is always entered from above", "teach|drop off|tote|record|waypoint", "ops_runbook")
add(C, "teach_point", "let me hand guide the arm and capture points", "teach_point",
    "mode=hand_guide;capture=on_button;max_points=20", "hand guiding enabled and the payload configured",
    "hand guiding runs at reduced speed but keep clear of pinch points", "teach|hand guide|lead through|points|record", "robot_manual")
add(C, "teach_point", "update the taught place point - it is 3 mm too high", "teach_point",
    "name=place_point;adjust=z;delta=-0.003;overwrite=true", "the original place point exists",
    "re-run one cycle in dry run mode after adjusting a taught point", "teach|adjust|place point|offset|update", "ops_runbook")

add(C, "calibrate_force_sensor", "zero the force torque sensor", "calibrate",
    "target=ft_sensor;routine=bias_zero;samples=200", "no contact and no payload on the tool",
    "zeroing under load hides real contact forces", "calibrate|force|torque|zero|sensor", "robot_manual")
add(C, "calibrate_force_sensor", "the force readings are drifting", "calibrate",
    "target=ft_sensor;routine=drift_check;duration_s=120;then=bias_zero", "arm stationary for the whole check",
    "a drifting force sensor makes force limits unreliable so stop contact tasks", "force|drift|sensor|calibrate|torque", "maintenance_guide")
add(C, "calibrate_force_sensor", "run the load cell calibration with the reference weight", "calibrate",
    "target=ft_sensor;routine=gain;reference_mass_kg=2.0", "a certified 2 kg reference mass is available",
    "secure the reference mass so it cannot fall during the sweep", "calibrate|load cell|reference|weight|force", "maintenance_guide")

add(C, "calibrate_joints", "re-zero the joint encoders", "calibrate",
    "target=joints;routine=encoder_zero;joints=all", "arm parked on the mechanical zero marks",
    "joint zeroing invalidates every taught pose until they are verified", "calibrate|encoders|joints|zero|reference", "maintenance_guide")
add(C, "calibrate_joints", "joint 4 mastering looks off after the battery change", "calibrate",
    "target=joints;routine=mastering;joints=4;method=witness_mark", "witness marks visible on joint 4",
    "an unmastered joint can move unpredictably so keep the cell empty", "mastering|joint 4|battery|calibrate|zero", "maintenance_guide")

add(C, "calibrate_payload", "set the payload for the new gripper", "calibrate",
    "target=payload;mass_kg=1.85;cog=x0.001|y0.000|z0.058", "gripper mass and centre of gravity known",
    "a wrong payload breaks collision detection and force limiting", "payload|set|gripper|mass|calibrate", "robot_manual")
add(C, "calibrate_payload", "the robot thinks it is heavier than it is - run the payload identification", "calibrate",
    "target=payload;routine=identify;motion=wrist_sweep", "tool attached and the arm free to move",
    "the identification sweep moves the wrist through its full range so clear the area", "payload|identify|mass|estimate|calibrate", "maintenance_guide")

# ---------------------------------------------------------------------------
# ROBOTICS - diagnostics (15)
# ---------------------------------------------------------------------------
D = "diagnostics"
add(D, "run_diagnostic", "run a full self test on the robot", "run_diagnostic",
    "suite=full;timeout_s=300;report=json", "cell empty and the arm free to move",
    "the self test commands motion on every joint so clear the envelope", "diagnostic|self test|full|check|health", "robot_manual")
add(D, "run_diagnostic", "why is joint 3 making that noise?", "run_diagnostic",
    "suite=joint;joint=3;checks=backlash|current|vibration", "arm warmed up for 10 minutes",
    "stop the test immediately if the noise gets worse", "diagnostic|joint 3|noise|vibration|check", "maintenance_guide")
add(D, "run_diagnostic", "check the health of the drives", "run_diagnostic",
    "suite=drives;checks=bus_voltage|current|fault_codes", "-",
    "-", "diagnostic|drives|health|check|motors", "maintenance_guide")
add(D, "run_diagnostic", "test the communication with the controller", "run_diagnostic",
    "suite=comms;target=controller;packets=1000;timeout_s=30", "network link up",
    "-", "diagnostic|communication|controller|network|test", "ops_runbook")
add(D, "run_diagnostic", "is anything wrong with the robot right now?", "run_diagnostic",
    "suite=quick;checks=faults|warnings|temperature|estop", "-",
    "-", "diagnostic|wrong|fault|check|health", "ops_runbook")

add(D, "read_controller_log", "show me the last errors from the controller log", "read_log",
    "source=controller;level=error;lines=50", "-",
    "-", "log|errors|controller|read|last", "ops_runbook")
add(D, "read_controller_log", "pull the log from when the robot faulted this morning", "read_log",
    "source=controller;since=today_06:00;until=today_12:00;level=warn", "log retention covers the window",
    "-", "log|fault|morning|read|history", "ops_runbook")
add(D, "read_controller_log", "what does fault code e-0142 mean?", "read_log",
    "source=fault_table;code=E-0142;fields=meaning|cause|recovery", "-",
    "follow the documented recovery steps - do not simply reset and rerun", "fault code|e-0142|meaning|error|lookup", "robot_manual")
add(D, "read_controller_log", "tail the motion log while I run this", "read_log",
    "source=motion;follow=true;lines=20", "motion logging enabled",
    "-", "log|tail|motion|follow|live", "ops_runbook")

add(D, "report_state", "what is the robot doing right now?", "report_state",
    "signal=controller;fields=mode|program|speed|joint_positions", "-",
    "-", "state|status|doing|current|report", "ops_runbook")
add(D, "report_state", "give me the joint angles", "report_state",
    "signal=joints;fields=positions|velocities;units=rad", "-",
    "-", "joint angles|positions|read|state|joints", "robot_manual")
add(D, "report_state", "how many cycles has it run today?", "report_state",
    "signal=counters;fields=cycle_count|uptime_s|fault_count;window=today", "-",
    "-", "cycles|count|today|uptime|statistics", "ops_runbook")
add(D, "report_state", "report the current tcp pose and force reading", "report_state",
    "signal=tcp;fields=pose|force|torque;frame=base_link", "force sensor present",
    "-", "tcp|pose|force|report|state", "robot_manual")

add(D, "check_temperature", "are the motors running hot?", "report_state",
    "signal=temperature;fields=joint_temps|drive_temps;threshold_c=70", "-",
    "shut down and let the arm cool if any joint exceeds 70 C", "temperature|motors|hot|overheat|check", "maintenance_guide")
add(D, "check_temperature", "check the controller cabinet temperature", "report_state",
    "signal=temperature;source=cabinet;fields=temp_c|fan_rpm;threshold_c=45", "-",
    "a blocked cabinet filter causes thermal trips so inspect it if the fans run flat out", "temperature|cabinet|controller|fan|check", "maintenance_guide")

# ---------------------------------------------------------------------------
# ROBOTICS - maintenance (12)
# ---------------------------------------------------------------------------
MT = "maintenance"
add(MT, "lubricate_check", "check if the joints need greasing", "lubricate_check",
    "joints=all;interval_h=5000;report=due_list", "service hours counter available",
    "de-energize and lock out the robot before greasing any joint", "lubricate|grease|joints|check|service", "maintenance_guide")
add(MT, "lubricate_check", "when is joint 2 due for lubrication?", "lubricate_check",
    "joints=2;fields=hours_since|interval_h|due_in_h", "-",
    "-", "lubrication|joint 2|due|schedule|grease", "maintenance_guide")
add(MT, "lubricate_check", "log that I greased joints 1 through 3", "lubricate_check",
    "action=record;joints=1|2|3;grease=vg320;operator=required", "the lubrication was actually performed",
    "over-greasing a joint blows the seals so record the quantity used", "lubricate|record|log|grease|joints", "maintenance_guide")
add(MT, "lubricate_check", "the wrist sounds dry", "lubricate_check",
    "joints=5|6;checks=noise|current_draw|hours_since", "arm warmed up",
    "a dry joint that keeps running will destroy the gearbox so stop if it is overdue", "lubricate|wrist|dry|noise|grease", "maintenance_guide")

add(MT, "inspect_wear", "inspect the cable harness for wear", "run_diagnostic",
    "suite=harness;checks=insulation|continuity|flex_cycles", "robot locked out and de-energized",
    "a chafed harness can short the drive bus so never inspect it live", "inspect|cable|harness|wear|maintenance", "maintenance_guide")
add(MT, "inspect_wear", "how worn are the gripper fingers?", "run_diagnostic",
    "suite=tooling;component=fingers;checks=grip_cycles|slip_rate", "-",
    "worn fingertips cause dropped parts so replace them at 200000 cycles", "inspect|fingers|wear|gripper|maintenance", "maintenance_guide")
add(MT, "inspect_wear", "check the belt tension on the linear axis", "run_diagnostic",
    "suite=axis;component=belt;checks=tension_hz|backlash", "axis locked out and stationary",
    "a loose belt can jump teeth and drop the vertical axis so support the load first", "inspect|belt|tension|linear axis|maintenance", "maintenance_guide")

add(MT, "replace_part", "walk me through replacing the vacuum filter", "read_log",
    "source=maintenance_guide;topic=vacuum_filter_replacement;fields=steps|parts|torque", "replacement filter on hand",
    "isolate the pneumatic supply and bleed the pressure before opening the housing", "replace|vacuum|filter|procedure|maintenance", "maintenance_guide")
add(MT, "replace_part", "we need to swap the joint 6 motor - what is the procedure?", "read_log",
    "source=maintenance_guide;topic=joint6_motor_swap;fields=steps|parts|remastering", "replacement motor and mastering fixture available",
    "support the arm mechanically before removing the motor because the brake releases", "replace|motor|joint 6|procedure|maintenance", "maintenance_guide")

add(MT, "schedule_service", "what maintenance is due this month?", "lubricate_check",
    "action=schedule;window=30d;scope=all;fields=task|due_date|hours", "-",
    "-", "maintenance|due|schedule|month|service", "maintenance_guide")
add(MT, "schedule_service", "book the annual service for the cell", "lubricate_check",
    "action=schedule;task=annual_service;asset=cell_1;notify=maintenance_team", "the production schedule allows the downtime",
    "the annual service needs a full lockout so plan the production stop", "service|annual|schedule|book|maintenance", "maintenance_guide")
add(MT, "schedule_service", "how many hours until the next preventive maintenance?", "lubricate_check",
    "action=query;fields=hours_to_next_pm|last_pm_date", "-",
    "-", "maintenance|hours|preventive|next|pm", "maintenance_guide")

# ---------------------------------------------------------------------------
# ROBOTICS - teleop (8)
# ---------------------------------------------------------------------------
T = "teleop"
add(T, "jog_axis", "jog the arm 10 mm in x", "jog",
    "frame=base_link;axis=x;step_mm=10;speed=0.05", "teach pendant enabling switch held",
    "jogging is manual motion so keep your hand on the enabling switch", "jog|x|10mm|move|manual", "robot_manual")
add(T, "jog_axis", "nudge joint 5 a couple of degrees", "jog",
    "mode=joint;joint=5;step_deg=2;speed=0.10", "manual mode selected",
    "check the tool clearance before rotating the wrist", "jog|joint 5|degrees|nudge|manual", "robot_manual")
add(T, "jog_axis", "let me drive the arm with the joystick", "jog",
    "mode=cartesian;device=joystick;speed_scale=0.25;deadman=required", "joystick paired and the deadman switch working",
    "teleop runs at 25 percent speed and stops the moment the deadman is released", "jog|joystick|teleop|drive|manual", "robot_manual")
add(T, "jog_axis", "move the tool down a hair - it is not quite touching", "jog",
    "frame=tool0;axis=z;step_mm=-1;speed=0.01;force_guard=8", "manual mode with the force guard enabled",
    "stop on 8 N so the tool does not crash into the part", "jog|down|fine|touch|manual", "robot_manual")

add(T, "record_trajectory", "record what I am about to do with the arm", "record_trajectory",
    "name=demo_1;rate_hz=100;mode=hand_guide", "hand guiding enabled",
    "the recorded path will be replayed at full speed so keep the demonstration clean", "record|trajectory|demonstrate|teach|capture", "robot_manual")
add(T, "record_trajectory", "start capturing a path I can play back later", "record_trajectory",
    "name=path_a;rate_hz=50;include=tcp|joints|gripper", "storage available for the recording",
    "record the gripper state too or the replay will not grip", "record|path|capture|playback|trajectory", "robot_manual")

add(T, "replay_trajectory", "play back the trajectory I just recorded", "replay_trajectory",
    "name=demo_1;speed_scale=0.5;loops=1", "cell clear and the recording validated",
    "replay the first run at 50 percent speed and watch it the whole way", "replay|playback|trajectory|run|recorded", "robot_manual")
add(T, "replay_trajectory", "run path A three times at full speed", "replay_trajectory",
    "name=path_a;speed_scale=1.0;loops=3", "the path was already validated at reduced speed",
    "never run a new path at full speed until it has been proven slowly", "replay|path a|three times|full speed|run", "ops_runbook")

# ---------------------------------------------------------------------------
# ROBOTICS - workcell (5)
# ---------------------------------------------------------------------------
W = "workcell"
add(W, "coordinate_cell", "tell the conveyor to hold while the arm picks", "report_state",
    "signal=cell_io;set=conveyor_hold;value=true", "conveyor under cell control",
    "confirm the conveyor is stopped before the arm enters its envelope", "conveyor|hold|cell|coordinate|stop", "ops_runbook")
add(W, "coordinate_cell", "start the next cycle when both robots are ready", "report_state",
    "signal=cell_sync;wait_for=robot_a_ready|robot_b_ready;timeout_s=30", "both robots homed",
    "never start a cycle while either robot is still in the shared zone", "cycle|sync|both robots|ready|coordinate", "ops_runbook")
add(W, "coordinate_cell", "hand the part from arm A to arm B", "report_state",
    "signal=cell_sync;sequence=handover_ab;zone=shared;timeout_s=20", "the shared zone is reserved by the cell controller",
    "only one arm may occupy the shared zone at a time", "handover|arm a|arm b|shared zone|coordinate", "ops_runbook")
add(W, "wait_for_signal", "wait until the press finishes before loading", "dwell",
    "wait_for=press_cycle_done;timeout_s=60", "press status signal wired to the cell io",
    "never reach into the press until the cycle done signal is latched", "wait|press|cycle|load|signal", "ops_runbook")
add(W, "wait_for_signal", "pause for two seconds so the part settles", "dwell",
    "duration_s=2.0;reason=part_settle", "-",
    "-", "wait|pause|two seconds|settle|dwell", "robot_manual")


# ---------------------------------------------------------------------------
# COMPUTER - shell (45)
# ---------------------------------------------------------------------------
SH = "shell"
add(SH, "check_disk_usage", "how much disk space is left?", "shell_exec",
    "cmd=df -h;timeout_s=10", "-",
    "-", "disk|space|free|df|storage", "ops_runbook")
add(SH, "check_disk_usage", "which directory is eating all the space?", "shell_exec",
    "cmd=du -xh --max-depth=1 / | sort -h | tail -20;timeout_s=120", "read access to the mount",
    "a du walk of / is slow on network mounts so keep the timeout bounded", "disk|usage|du|biggest|directory", "ops_runbook")
add(SH, "check_disk_usage", "show me the biggest files on the disk", "shell_exec",
    "cmd=find / -xdev -type f -size +500M -exec ls -lh {} +;timeout_s=300", "read access to the filesystem",
    "this walk touches the whole filesystem so run it off peak", "big|files|large|disk|find", "ops_runbook")
add(SH, "check_memory", "how much memory is being used?", "shell_exec",
    "cmd=free -h;timeout_s=5", "-",
    "-", "memory|ram|free|usage|used", "ops_runbook")
add(SH, "check_cpu_load", "check the cpu load right now", "shell_exec",
    "cmd=top -b -n 1 | head -15;timeout_s=15", "-",
    "-", "cpu|load|top|usage|busy", "ops_runbook")
add(SH, "check_uptime", "what is the uptime on this box?", "shell_exec",
    "cmd=uptime;timeout_s=5", "-",
    "-", "uptime|load|boot|how long|server", "ops_runbook")
add(SH, "check_os_version", "print the kernel and os version", "shell_exec",
    "cmd=uname -a && cat /etc/os-release;timeout_s=10", "-",
    "-", "kernel|os|version|uname|release", "ops_runbook")

add(SH, "list_services", "show me the running services", "shell_exec",
    "cmd=systemctl list-units --type=service --state=running;timeout_s=15", "systemd host",
    "-", "services|running|systemctl|list|units", "ops_runbook")
add(SH, "restart_service", "restart the nginx service", "shell_exec",
    "cmd=systemctl restart nginx;sudo=true;timeout_s=30", "sudo rights on the host",
    "restarting nginx drops in-flight connections so drain the node first", "restart|nginx|service|systemctl|web", "ops_runbook")
add(SH, "check_service_status", "is the api service up?", "shell_exec",
    "cmd=systemctl is-active vxapi.service;timeout_s=10", "-",
    "-", "service|active|up|status|api", "ops_runbook")

add(SH, "list_files", "list everything in this folder including the hidden files", "shell_exec",
    "cmd=ls -la;cwd=.;timeout_s=10", "-",
    "-", "list|files|hidden|ls|folder", "computer_use")
add(SH, "extract_archive", "unzip the release archive into /opt/app", "shell_exec",
    "cmd=unzip -o release.zip -d /opt/app;timeout_s=120", "/opt/app writable",
    "the -o flag overwrites existing files so back up /opt/app first", "unzip|extract|archive|zip|install", "ops_runbook")
add(SH, "create_archive", "tar up the logs directory so I can send it", "shell_exec",
    "cmd=tar -czf logs.tgz /var/log/vx;timeout_s=180", "free disk space for the archive",
    "logs can contain secrets so review the bundle before sending it out", "tar|archive|logs|compress|bundle", "ops_runbook")
add(SH, "sync_files", "copy the build artifacts to the server", "shell_exec",
    "cmd=rsync -avz ./build/ deploy@node1:/srv/app/;timeout_s=600", "ssh key trusted on node1",
    "rsync without --delete leaves stale files so verify the target afterwards", "rsync|copy|deploy|artifacts|server", "ops_runbook")

add(SH, "build_project", "run the build", "shell_exec",
    "cmd=cmake --build build -j8;cwd=/srv/app;timeout_s=1800", "cmake already configured in build/",
    "-", "build|compile|cmake|make|build project", "ops_runbook")
add(SH, "run_tests", "run the unit tests and show me the failures", "shell_exec",
    "cmd=ctest --test-dir build --output-on-failure;timeout_s=900", "project built",
    "-", "test|ctest|unit tests|failures|run tests", "ops_runbook")
add(SH, "run_script", "run the deployment script", "shell_exec",
    "cmd=./deploy.sh --env=prod;cwd=/srv/app;timeout_s=1800", "deployment approved and the change window open",
    "this touches production so confirm the rollback plan before running it", "deploy|script|production|release|run", "ops_runbook")
add(SH, "run_as_user", "run this command as the service user", "shell_exec",
    "cmd=sudo -u vxapp /srv/app/bin/vxcli health;timeout_s=60", "vxapp is allowed to run the binary",
    "never run application binaries as root", "sudo|run as|service user|command|execute", "ops_runbook")

add(SH, "git_status", "what git branch am I on?", "shell_exec",
    "cmd=git rev-parse --abbrev-ref HEAD;cwd=repo;timeout_s=10", "inside a git repository",
    "-", "git|branch|current|which branch|repo", "ops_runbook")
add(SH, "git_status", "show the last five commits", "shell_exec",
    "cmd=git log -5 --oneline;cwd=repo;timeout_s=10", "inside a git repository",
    "-", "git|log|commits|history|last", "ops_runbook")
add(SH, "git_status", "what changed since the last commit?", "shell_exec",
    "cmd=git status --short && git diff --stat;cwd=repo;timeout_s=15", "inside a git repository",
    "-", "git|diff|changed|status|uncommitted", "ops_runbook")
add(SH, "git_pull", "pull the latest code", "shell_exec",
    "cmd=git pull --ff-only;cwd=repo;timeout_s=60", "clean working tree",
    "--ff-only refuses to merge so local work is never silently rewritten", "git|pull|update|latest|fetch", "ops_runbook")

add(SH, "docker_list", "list the docker containers", "shell_exec",
    "cmd=docker ps -a --format {{.Names}}|{{.Status}};timeout_s=20", "docker daemon reachable",
    "-", "docker|containers|list|ps|running", "ops_runbook")
add(SH, "docker_logs", "why did the container die?", "shell_exec",
    "cmd=docker logs --tail 100 vxapi;timeout_s=30", "the container still exists",
    "-", "docker|logs|container|died|crash", "ops_runbook")
add(SH, "docker_restart", "restart the docker stack", "shell_exec",
    "cmd=docker compose restart;cwd=/srv/app;timeout_s=180", "compose file present",
    "a restart interrupts live traffic so announce the maintenance window", "docker|compose|restart|stack|containers", "ops_runbook")
add(SH, "docker_prune", "clean up the dangling docker images", "shell_exec",
    "cmd=docker image prune -f;timeout_s=300", "-",
    "prune deletes untagged images permanently so confirm nothing depends on them", "docker|prune|cleanup|images|disk", "ops_runbook")

add(SH, "check_logins", "who is logged into this machine?", "shell_exec",
    "cmd=who -a;timeout_s=10", "-",
    "-", "who|logged in|users|sessions|login", "ops_runbook")
add(SH, "check_logins", "show me the last logins", "shell_exec",
    "cmd=last -n 20;timeout_s=15", "-",
    "repeated failed logins from one address may indicate an attack", "last|logins|history|auth|users", "ops_runbook")

add(SH, "change_permissions", "make this script executable", "shell_exec",
    "cmd=chmod +x ./deploy.sh;cwd=/srv/app;timeout_s=5", "the file exists",
    "only mark a script executable after you have read what it does", "chmod|executable|permissions|script|+x", "computer_use")
add(SH, "change_owner", "change the owner of the app directory to the service user", "shell_exec",
    "cmd=chown -R vxapp:vxapp /srv/app;sudo=true;timeout_s=60", "the vxapp user exists",
    "a recursive chown on the wrong path can break the whole system so check the target twice", "chown|owner|permissions|directory|user", "ops_runbook")

add(SH, "install_packages", "install the python dependencies", "shell_exec",
    "cmd=pip install -r requirements.txt;cwd=/srv/app;timeout_s=900", "virtualenv activated",
    "install into a virtualenv so the system python is not polluted", "pip|install|dependencies|requirements|python", "ops_runbook")
add(SH, "install_packages", "update the package index and list what can be upgraded", "shell_exec",
    "cmd=apt-get update && apt list --upgradable;sudo=true;timeout_s=300", "network access to the mirrors",
    "-", "apt|update|packages|upgradable|patch", "ops_runbook")

add(SH, "inspect_env", "what environment variables are set for the service?", "shell_exec",
    "cmd=systemctl show vxapi.service --property=Environment;timeout_s=10", "-",
    "the environment may expose secrets so do not paste the output into a ticket", "environment|variables|service|env|config", "ops_runbook")
add(SH, "set_env", "set the log level to debug and restart the service", "shell_exec",
    "cmd=export LOG_LEVEL=debug && systemctl restart vxapi;sudo=true;timeout_s=60", "the service reads LOG_LEVEL",
    "debug logging is noisy and can fill the disk so revert it when you are done", "log level|debug|env|restart|service", "ops_runbook")
add(SH, "check_shell", "what shell am I running?", "shell_exec",
    "cmd=echo $SHELL && $SHELL --version;timeout_s=5", "-",
    "-", "shell|which|bash|version|terminal", "computer_use")

add(SH, "list_cron", "check the crontab entries", "shell_exec",
    "cmd=crontab -l;timeout_s=10", "-",
    "-", "cron|crontab|schedule|jobs|list", "ops_runbook")
add(SH, "add_cron", "add a nightly backup job at 2am", "shell_exec",
    "cmd=crontab -l > cron.tmp && echo 0 2 * * * /srv/app/backup.sh >> cron.tmp && crontab cron.tmp;timeout_s=15", "backup.sh exists and is executable",
    "always append to the existing crontab - overwriting it silently drops other jobs", "cron|backup|nightly|schedule|job", "ops_runbook")

add(SH, "count_code", "count the lines of code in the src folder", "shell_exec",
    "cmd=find src -name *.cpp -o -name *.hpp | xargs wc -l | tail -1;cwd=repo;timeout_s=60", "src directory exists",
    "-", "count|lines|code|loc|source", "computer_use")
add(SH, "clean_temp", "empty the temp directory", "shell_exec",
    "cmd=rm -rf /tmp/vx/*;timeout_s=60", "no job is currently using /tmp/vx",
    "rm -rf is irreversible so confirm the path before you run it", "delete|temp|clean|rm|tmp", "ops_runbook")
add(SH, "check_certificate", "check the ssl certificate expiry on this host", "shell_exec",
    "cmd=openssl x509 -enddate -noout -in /etc/ssl/certs/vx.pem;timeout_s=15", "certificate file readable",
    "an expired certificate takes the api offline so renew at least 14 days ahead", "ssl|certificate|expiry|openssl|cert", "ops_runbook")
add(SH, "reboot_host", "reboot the machine", "shell_exec",
    "cmd=shutdown -r +1 maintenance reboot;sudo=true;timeout_s=15", "maintenance window open and workloads drained",
    "a reboot drops every running job so drain the node and warn users first", "reboot|restart|machine|shutdown|server", "ops_runbook")

add(SH, "read_app_log", "show the last 200 lines of the app log", "read_log",
    "source=file;path=/var/log/vx/app.log;lines=200", "log file readable",
    "-", "log|tail|app|last lines|read", "ops_runbook")
add(SH, "read_app_log", "follow the error log live", "read_log",
    "source=file;path=/var/log/vx/error.log;follow=true;filter=ERROR", "the log file exists",
    "-", "log|follow|live|error|tail", "ops_runbook")
add(SH, "read_auth_log", "grep the auth log for failed passwords", "read_log",
    "source=file;path=/var/log/auth.log;filter=Failed password;lines=100", "root or adm group membership",
    "repeated failures from one host suggest a brute force attempt - escalate to security", "log|auth|failed|password|grep", "ops_runbook")
add(SH, "read_journal", "check the journal for the api service since boot", "read_log",
    "source=journald;unit=vxapi.service;since=boot;level=warn", "systemd journal available",
    "-", "journal|journalctl|service|boot|log", "ops_runbook")

# ---------------------------------------------------------------------------
# COMPUTER - filesystem (45)
# ---------------------------------------------------------------------------
FS = "filesystem"
add(FS, "read_file", "read the config file", "file_read",
    "path=/etc/vx/config.yaml;encoding=utf-8", "file readable by the agent user",
    "-", "read|config|file|open|yaml", "computer_use")
add(FS, "read_file", "what is in the .env file?", "file_read",
    "path=/srv/app/.env;redact=secrets", "the file exists",
    "env files hold credentials so redact the secrets before displaying them", "read|env|file|secrets|config", "computer_use")
add(FS, "read_file", "open the first 40 lines of the readme", "file_read",
    "path=README.md;lines=1-40", "-",
    "-", "read|readme|open|head|file", "computer_use")
add(FS, "read_file", "show me the json payload we saved", "file_read",
    "path=/tmp/payload.json;parse=json", "the file is valid json",
    "-", "read|json|payload|file|open", "computer_use")
add(FS, "read_file", "cat the systemd unit for the api", "file_read",
    "path=/etc/systemd/system/vxapi.service", "-",
    "-", "read|unit|systemd|service|file", "ops_runbook")
add(FS, "read_file", "print the last 30 lines of the csv", "file_read",
    "path=/data/export.csv;lines=tail:30", "-",
    "-", "read|csv|tail|last lines|file", "computer_use")
add(FS, "read_file", "how big is the model file and can you read its header?", "file_read",
    "path=/models/brain.bin;bytes=0-256;mode=binary", "the file exists",
    "read the binary header only - do not dump the whole file to the terminal", "read|binary|header|model|file", "computer_use")
add(FS, "read_file", "load the yaml recipe for part 7", "file_read",
    "path=/srv/app/recipes/part_7.yaml;parse=yaml", "a recipe exists for part 7",
    "-", "read|recipe|yaml|part|load", "ops_runbook")
add(FS, "read_file", "check what is inside the docker compose file", "file_read",
    "path=/srv/app/docker-compose.yml;parse=yaml", "-",
    "-", "read|compose|docker|yaml|file", "ops_runbook")

add(FS, "write_file", "save this to notes.txt", "file_write",
    "path=/home/op/notes.txt;mode=overwrite;encoding=utf-8", "home directory writable",
    "overwrite mode destroys the existing file so use append if the content matters", "write|save|notes|file|text", "computer_use")
add(FS, "write_file", "append the result to the report", "file_write",
    "path=/srv/app/report.md;mode=append;newline=true", "the report file exists",
    "-", "write|append|report|file|add", "computer_use")
add(FS, "write_file", "create an empty log file with the right permissions", "file_write",
    "path=/var/log/vx/task.log;mode=create;perms=0640;owner=vxapp", "/var/log/vx exists",
    "log files must not be world readable when they can contain payload data", "write|create|log|file|permissions", "ops_runbook")
add(FS, "write_file", "write the config out as json", "file_write",
    "path=/etc/vx/config.json;format=json;mode=overwrite;backup=true", "config directory writable",
    "keep a backup of the previous config so a bad edit can be rolled back", "write|config|json|save|file", "ops_runbook")
add(FS, "write_file", "update the version string in the manifest", "file_write",
    "path=/srv/app/manifest.json;mode=patch;key=version;value=2.4.1", "the manifest is valid json",
    "a malformed manifest stops the service booting so validate it after writing", "write|version|manifest|update|json", "ops_runbook")
add(FS, "write_file", "dump the sensor readings to a csv", "file_write",
    "path=/data/sensors.csv;mode=append;format=csv;header_if_new=true", "/data writable",
    "-", "write|csv|sensor|export|dump", "ops_runbook")
add(FS, "write_file", "save the screenshot to the shared drive", "file_write",
    "path=/mnt/share/screens/cell_1.png;mode=overwrite;binary=true", "the share is mounted",
    "-", "write|save|screenshot|share|file", "computer_use")
add(FS, "write_file", "write a quick shell script that restarts the service", "file_write",
    "path=/srv/app/restart.sh;mode=create;perms=0750;shebang=/bin/sh", "the directory is writable",
    "review the script content before making it executable", "write|script|shell|create|restart", "ops_runbook")
add(FS, "write_file", "make a backup copy of the config before I edit it", "file_write",
    "path=/etc/vx/config.yaml.bak;mode=copy_from;source=/etc/vx/config.yaml", "the source file exists",
    "always take a backup before editing a production config", "backup|copy|config|before edit|save", "ops_runbook")

add(FS, "search_files", "find every file that mentions the api key", "file_search",
    "root=/srv/app;query=API_KEY;type=content;max_results=50", "read access to the tree",
    "if a key turns up in plain text rotate it and move it into the secret store", "search|api key|content|find|grep", "computer_use")
add(FS, "search_files", "where is the config file?", "file_search",
    "root=/;pattern=config.yaml;type=name;max_depth=6", "-",
    "-", "find|config|where|locate|file", "computer_use")
add(FS, "search_files", "look for all the python files in the project", "file_search",
    "root=/srv/app;pattern=*.py;type=name;max_results=200", "-",
    "-", "find|python|files|search|project", "computer_use")
add(FS, "search_files", "search the codebase for todo comments", "file_search",
    "root=repo;query=TODO;type=content;include=*.cpp|*.hpp|*.py", "-",
    "-", "search|todo|comments|code|find", "computer_use")
add(FS, "search_files", "which files did I change in the last hour?", "file_search",
    "root=/srv/app;filter=mtime<1h;type=name;max_results=100", "-",
    "-", "find|recent|changed|modified|files", "computer_use")
add(FS, "search_files", "find files bigger than 100 megabytes", "file_search",
    "root=/data;filter=size>100MB;type=name;max_results=50", "-",
    "check whether a large file is still in use before deleting it", "find|large|files|size|disk", "ops_runbook")
add(FS, "search_files", "locate the log that contains the crash trace", "file_search",
    "root=/var/log;query=SIGSEGV;type=content;include=*.log", "read access to /var/log",
    "-", "search|crash|trace|log|segfault", "ops_runbook")
add(FS, "search_files", "are there any leftover core dumps lying around?", "file_search",
    "root=/;pattern=core.*;type=name;max_depth=4", "-",
    "core dumps can contain in-memory secrets so delete them securely", "find|core dump|leftover|crash|cleanup", "ops_runbook")
add(FS, "search_files", "search for the function definition in the c++ sources", "file_search",
    "root=repo/src;query=void RetrievalEngine::;type=content;include=*.cpp", "-",
    "-", "search|function|definition|c++|code", "computer_use")
add(FS, "search_files", "find duplicate copies of the dataset", "file_search",
    "root=/data;pattern=robot_brain*.csv;type=name;hash_compare=true", "-",
    "confirm which copy is canonical before deleting any duplicate", "find|duplicate|dataset|csv|copies", "computer_use")

add(FS, "list_directory", "list what is in the datasets folder", "dir_list",
    "path=/srv/app/datasets;recursive=false;show_hidden=false", "-",
    "-", "list|directory|datasets|folder|contents", "computer_use")
add(FS, "list_directory", "show the folder tree two levels deep", "dir_list",
    "path=/srv/app;recursive=true;max_depth=2;format=tree", "-",
    "-", "list|tree|folders|depth|directory", "computer_use")
add(FS, "list_directory", "what is in my downloads folder?", "dir_list",
    "path=~/Downloads;recursive=false;sort=mtime_desc", "-",
    "-", "list|downloads|folder|files|directory", "computer_use")
add(FS, "list_directory", "list the log files sorted by size", "dir_list",
    "path=/var/log/vx;pattern=*.log;sort=size_desc;limit=20", "-",
    "-", "list|logs|size|sorted|directory", "ops_runbook")
add(FS, "list_directory", "how many files are in the images directory?", "dir_list",
    "path=/data/images;recursive=true;output=count", "-",
    "-", "count|files|directory|images|how many", "computer_use")
add(FS, "list_directory", "show me the newest files in the export folder", "dir_list",
    "path=/data/export;sort=mtime_desc;limit=10", "-",
    "-", "list|newest|recent|export|directory", "computer_use")
add(FS, "list_directory", "list the mounted volumes", "dir_list",
    "path=/mnt;recursive=false;include_mounts=true", "-",
    "-", "list|mounts|volumes|drives|directory", "ops_runbook")
add(FS, "list_directory", "what config directories exist under /etc/vx?", "dir_list",
    "path=/etc/vx;recursive=true;max_depth=2;only=dirs", "-",
    "-", "list|directories|etc|config|folders", "ops_runbook")
add(FS, "list_directory", "show the hidden files in my home directory", "dir_list",
    "path=~;show_hidden=true;recursive=false", "-",
    "-", "list|hidden|home|dotfiles|directory", "computer_use")

add(FS, "copy_file", "copy the dataset to the backup drive", "shell_exec",
    "cmd=cp -a /data/brain /mnt/backup/brain;timeout_s=600", "backup drive mounted with free space",
    "verify the copy with a checksum before deleting the source", "copy|dataset|backup|cp|drive", "ops_runbook")
add(FS, "move_file", "move the old exports into the archive folder", "shell_exec",
    "cmd=mv /data/export/2025-* /data/archive/;timeout_s=120", "the archive folder exists",
    "moving files breaks any job still reading them so check for open handles first", "move|archive|old|exports|mv", "ops_runbook")
add(FS, "delete_file", "delete the temporary build directory", "shell_exec",
    "cmd=rm -rf /srv/app/build;timeout_s=60", "no build is currently running",
    "rm -rf cannot be undone so confirm this is the build directory and not the source", "delete|remove|build|directory|clean", "ops_runbook")
add(FS, "rename_file", "rename the dataset so it includes today's date", "shell_exec",
    "cmd=mv robot_brain.csv robot_brain_2026-07-13.csv;cwd=/data/brain;timeout_s=10", "the file exists",
    "anything referencing the old filename will break so update the loader path", "rename|dataset|date|mv|file", "computer_use")
add(FS, "make_directory", "make a new folder for today's captures", "shell_exec",
    "cmd=mkdir -p /data/captures/2026-07-13;timeout_s=10", "-",
    "-", "mkdir|folder|create|directory|new", "computer_use")
add(FS, "change_permissions", "tighten the permissions on the secrets directory", "shell_exec",
    "cmd=chmod -R 700 /etc/vx/secrets;sudo=true;timeout_s=30", "the secrets directory exists",
    "secrets must never be group or world readable", "permissions|secrets|chmod|tighten|secure", "ops_runbook")
add(FS, "checksum_file", "checksum the dataset so I can verify the copy", "shell_exec",
    "cmd=sha256sum /data/brain/robot_brain.csv;timeout_s=60", "the file exists",
    "-", "checksum|sha256|verify|dataset|hash", "ops_runbook")
add(FS, "compare_files", "compare the local dataset with the one on the server", "shell_exec",
    "cmd=sha256sum ./robot_brain.csv && ssh node1 sha256sum /data/robot_brain.csv;timeout_s=120", "ssh access to node1",
    "-", "compare|diff|file|server|checksum", "ops_runbook")


# ---------------------------------------------------------------------------
# COMPUTER - process (30)
# ---------------------------------------------------------------------------
PR = "process"
add(PR, "list_processes", "what processes are running?", "process_list",
    "filter=all;sort=cpu_desc;limit=30", "-",
    "-", "processes|running|list|ps|top", "computer_use")
add(PR, "list_processes", "show me what is using the most cpu", "process_list",
    "sort=cpu_desc;limit=10;fields=pid|user|cpu|command", "-",
    "-", "cpu|top|process|hog|usage", "ops_runbook")
add(PR, "list_processes", "which process is eating all the memory?", "process_list",
    "sort=mem_desc;limit=10;fields=pid|user|rss|command", "-",
    "the top memory user may be the database so check before you kill anything", "memory|process|hog|rss|top", "ops_runbook")
add(PR, "list_processes", "is the api server process alive?", "process_list",
    "filter=name=vxapi;fields=pid|state|uptime", "-",
    "-", "process|alive|api|running|check", "ops_runbook")
add(PR, "list_processes", "find the process listening on port 8080", "process_list",
    "filter=port=8080;fields=pid|user|command", "-",
    "-", "process|port|8080|listening|find", "ops_runbook")
add(PR, "list_processes", "list all the python processes", "process_list",
    "filter=name~python;fields=pid|cpu|mem|command", "-",
    "-", "process|python|list|find|running", "computer_use")
add(PR, "list_processes", "how many worker processes are there?", "process_list",
    "filter=name=vxworker;output=count", "-",
    "-", "workers|count|processes|how many|list", "ops_runbook")
add(PR, "list_processes", "show me the process tree for the api", "process_list",
    "filter=name=vxapi;format=tree;include_children=true", "-",
    "-", "process|tree|children|api|hierarchy", "ops_runbook")

add(PR, "kill_process", "kill the hung process", "process_kill",
    "pid=required;signal=TERM;timeout_s=10;escalate=KILL", "the process is confirmed hung and not writing data",
    "send TERM first and only escalate to KILL if it refuses to exit", "kill|hung|process|stop|terminate", "ops_runbook")
add(PR, "kill_process", "kill process 4821", "process_kill",
    "pid=4821;signal=TERM;timeout_s=10", "pid 4821 still exists",
    "pids get reused so verify the pid still belongs to the process you mean", "kill|pid|4821|process|terminate", "ops_runbook")
add(PR, "kill_process", "force kill the frozen browser", "process_kill",
    "filter=name=chrome;signal=KILL;confirm=true", "-",
    "KILL gives no chance to save so unsaved tabs will be lost", "kill|force|browser|frozen|chrome", "computer_use")
add(PR, "kill_process", "stop all the stale worker processes", "process_kill",
    "filter=name=vxworker;filter2=cpu<1;signal=TERM;max_kills=8", "the workers are confirmed idle",
    "cap the number of kills so a bad filter cannot take down the whole pool", "kill|workers|stale|idle|stop", "ops_runbook")
add(PR, "kill_process", "the training job is stuck - end it", "process_kill",
    "filter=name=train.py;signal=TERM;timeout_s=30;escalate=KILL", "a checkpoint was saved recently",
    "killing a training job discards everything since the last checkpoint", "kill|training|stuck|job|stop", "ops_runbook")
add(PR, "kill_process", "kill whatever is holding port 5432", "process_kill",
    "filter=port=5432;signal=TERM;confirm=true", "-",
    "port 5432 is postgres so killing it drops every live database connection", "kill|port|5432|postgres|release", "ops_runbook")
add(PR, "signal_process", "send a hangup to the logger so it reopens its files", "process_kill",
    "filter=name=vxlogd;signal=HUP", "the logger supports a HUP reload",
    "HUP reloads rather than kills so the log stream is not interrupted", "signal|hup|reload|logger|process", "ops_runbook")
add(PR, "kill_process", "shut down the runaway process that is pinning the cpu", "process_kill",
    "filter=cpu>95;signal=TERM;max_kills=1;confirm=true", "the target is identified and is not a critical service",
    "a busy build looks the same as a runaway loop so confirm the target first", "kill|runaway|cpu|100 percent|stop", "ops_runbook")

add(PR, "inspect_process", "how long has the api process been running?", "process_list",
    "filter=name=vxapi;fields=pid|uptime|start_time", "-",
    "-", "process|uptime|how long|running|api", "ops_runbook")
add(PR, "inspect_process", "what files does the api process have open?", "shell_exec",
    "cmd=lsof -p $(pgrep -f vxapi | head -1);timeout_s=30", "lsof installed",
    "-", "open files|lsof|process|handles|api", "ops_runbook")
add(PR, "inspect_process", "show the memory footprint of the retrieval engine", "process_list",
    "filter=name=brain_engine;fields=rss|vsz|shared|swap", "-",
    "-", "memory|footprint|process|rss|engine", "ops_runbook")
add(PR, "inspect_process", "which user is running that process?", "process_list",
    "filter=pid=required;fields=pid|user|group|command", "-",
    "-", "process|user|owner|who|running", "computer_use")
add(PR, "inspect_process", "get a stack trace of the stuck process", "shell_exec",
    "cmd=gdb -p $(pgrep -f brain_engine) -batch -ex thread apply all bt;sudo=true;timeout_s=120", "gdb installed with debug symbols available",
    "attaching gdb pauses the process so never do this on a live production node", "stack trace|gdb|stuck|debug|process", "ops_runbook")
add(PR, "inspect_process", "what was the exit code of the last job?", "shell_exec",
    "cmd=echo $?;timeout_s=5", "run immediately after the job finished",
    "-", "exit code|status|last|job|result", "computer_use")
add(PR, "inspect_process", "check if the process is stuck in uninterruptible sleep", "process_list",
    "filter=state=D;fields=pid|state|command", "-",
    "processes stuck in D state usually mean a hung disk or network mount", "process|state|d state|hung|io", "ops_runbook")

add(PR, "start_service", "start the api service", "shell_exec",
    "cmd=systemctl start vxapi.service;sudo=true;timeout_s=60", "the config was validated",
    "check the config first or the service will crash loop on boot", "start|service|api|systemctl|launch", "ops_runbook")
add(PR, "stop_service", "stop the worker pool", "shell_exec",
    "cmd=systemctl stop vxworker.target;sudo=true;timeout_s=120", "the job queue is drained",
    "drain the queue first or in-flight jobs will be lost", "stop|workers|pool|service|shutdown", "ops_runbook")
add(PR, "restart_service", "restart the brain engine and watch it come up", "shell_exec",
    "cmd=systemctl restart brain-engine && journalctl -u brain-engine -f -n 20;sudo=true;timeout_s=180", "the dataset is present on disk",
    "the engine reloads the csv on start so validate the dataset before restarting", "restart|brain|engine|service|watch", "ops_runbook")
add(PR, "enable_service", "enable the service so it starts on boot", "shell_exec",
    "cmd=systemctl enable vxapi.service;sudo=true;timeout_s=30", "-",
    "-", "enable|boot|service|autostart|systemctl", "ops_runbook")
add(PR, "background_job", "run the batch job in the background", "shell_exec",
    "cmd=nohup ./batch.sh > /var/log/vx/batch.log 2>&1 &;cwd=/srv/app;timeout_s=10", "batch.sh is executable",
    "redirect the output or the job dies with the terminal", "background|job|nohup|batch|run", "ops_runbook")
add(PR, "set_priority", "lower the process priority so it stops hogging the cpu", "shell_exec",
    "cmd=renice +10 -p $(pgrep -f indexer);timeout_s=15", "the process is running",
    "renice cannot raise the priority back again without root", "priority|nice|renice|cpu|lower", "ops_runbook")
add(PR, "limit_resources", "limit the container to two cpus", "shell_exec",
    "cmd=docker update --cpus=2 vxapi;timeout_s=30", "the container is running",
    "throttling cpu can push request latency past the client timeout", "limit|cpu|container|docker|throttle", "ops_runbook")

# ---------------------------------------------------------------------------
# COMPUTER - network (40)
# ---------------------------------------------------------------------------
NW = "network"
add(NW, "http_get", "check if the api is responding", "http_request",
    "method=GET;url=https://api.vxcloud.io/health;timeout_s=10;expect=200", "-",
    "-", "http|health|api|check|responding", "ops_runbook")
add(NW, "http_get", "get the version from the service endpoint", "http_request",
    "method=GET;url=http://localhost:8080/version;timeout_s=5", "the service is listening on 8080",
    "-", "http|get|version|endpoint|api", "ops_runbook")
add(NW, "http_get", "fetch the json from the inventory api", "http_request",
    "method=GET;url=https://api.vxcloud.io/api/v2/inventory;header=Authorization: Bearer $TOKEN;timeout_s=15", "the token is exported in the environment",
    "never log the bearer token from the request headers", "http|get|json|inventory|api", "ops_runbook")
add(NW, "http_get", "is the website up?", "http_request",
    "method=GET;url=https://vxcloud.io;timeout_s=10;follow_redirects=true", "-",
    "-", "website|up|http|check|status", "ops_runbook")
add(NW, "http_get", "what response headers does the endpoint return?", "http_request",
    "method=HEAD;url=https://api.vxcloud.io/health;timeout_s=10;output=headers", "-",
    "-", "http|headers|head|response|endpoint", "ops_runbook")
add(NW, "http_get", "how slow is the api right now?", "http_request",
    "method=GET;url=https://api.vxcloud.io/health;timeout_s=20;measure=latency_ms;samples=5", "-",
    "-", "latency|slow|api|response time|http", "ops_runbook")
add(NW, "http_get", "check the metrics endpoint", "http_request",
    "method=GET;url=http://localhost:9090/metrics;timeout_s=10;format=prometheus", "the metrics exporter is enabled",
    "-", "metrics|prometheus|endpoint|http|monitoring", "ops_runbook")
add(NW, "http_get", "call the robot state api and show me the pose", "http_request",
    "method=GET;url=http://robot-1.local:8000/api/state;timeout_s=5;jsonpath=$.tcp.pose", "the robot is reachable on the cell network",
    "-", "http|robot|state|pose|api", "ops_runbook")

add(NW, "http_post", "post the job to the queue api", "http_request",
    "method=POST;url=https://api.vxcloud.io/api/v2/jobs;body=@job.json;content_type=application/json;timeout_s=20", "job.json is valid",
    "posting a job starts real work so check the payload before sending it", "http|post|job|queue|api", "ops_runbook")
add(NW, "http_post", "send the sensor batch to the ingest endpoint", "http_request",
    "method=POST;url=https://ingest.vxcloud.io/v1/telemetry;body=@batch.json;retries=3;timeout_s=30", "the batch file is written",
    "-", "http|post|telemetry|ingest|upload", "ops_runbook")
add(NW, "http_post", "trigger the webhook", "http_request",
    "method=POST;url=$WEBHOOK_URL;body=@cycle_done.json;content_type=application/json;timeout_s=10", "the webhook url is configured",
    "webhooks fan out to other systems so never fire test events at production", "webhook|post|trigger|http|event", "ops_runbook")
add(NW, "http_patch", "update the asset record with a patch request", "http_request",
    "method=PATCH;url=https://api.vxcloud.io/api/v2/assets/robot-1;body=status=maintenance;format=json;timeout_s=15", "the asset exists",
    "a patch writes to production data so double check the asset id", "http|patch|update|record|api", "ops_runbook")
add(NW, "http_delete", "delete the stale session through the api", "http_request",
    "method=DELETE;url=https://api.vxcloud.io/api/v2/sessions/expired;timeout_s=15;confirm=true", "the session is confirmed expired",
    "deleting an active session logs the operator straight out", "http|delete|session|stale|api", "ops_runbook")

add(NW, "check_port", "is port 22 open on that host?", "port_check",
    "host=node1.local;port=22;protocol=tcp;timeout_s=5", "-",
    "-", "port|22|ssh|open|check", "ops_runbook")
add(NW, "check_port", "check whether the database port is reachable", "port_check",
    "host=db1.internal;port=5432;protocol=tcp;timeout_s=5", "-",
    "-", "port|5432|database|reachable|check", "ops_runbook")
add(NW, "check_port", "scan the common ports on the robot controller", "port_check",
    "host=robot-1.local;ports=22|80|443|502|8000;protocol=tcp;timeout_s=10", "scanning is permitted on the cell network",
    "only scan hosts you own - scanning production networks trips intrusion detection", "port|scan|controller|open|check", "ops_runbook")
add(NW, "check_port", "what ports is this machine listening on?", "port_check",
    "mode=local_listeners;protocol=tcp|udp;fields=port|pid|process", "-",
    "an unexpected listener may be a backdoor so investigate anything you do not recognise", "ports|listening|local|open|netstat", "ops_runbook")
add(NW, "check_port", "can we reach the modbus port on the plc?", "port_check",
    "host=plc-1.cell;port=502;protocol=tcp;timeout_s=3", "the plc is powered and on the cell vlan",
    "never write to modbus registers while the line is running", "port|modbus|502|plc|check", "ops_runbook")
add(NW, "check_port", "is the mqtt broker accepting connections?", "port_check",
    "host=mqtt.internal;port=1883;protocol=tcp;timeout_s=5", "-",
    "-", "port|mqtt|1883|broker|check", "ops_runbook")
add(NW, "check_port", "verify the https port is open through the firewall", "port_check",
    "host=api.vxcloud.io;port=443;protocol=tcp;timeout_s=8;from=external", "-",
    "-", "port|443|https|firewall|check", "ops_runbook")
add(NW, "check_port", "nothing seems to be listening on 8080 - can you confirm?", "port_check",
    "host=localhost;port=8080;protocol=tcp;timeout_s=3;expect=closed", "-",
    "-", "port|8080|listening|closed|confirm", "ops_runbook")

add(NW, "dns_lookup", "resolve the api hostname", "dns_lookup",
    "name=api.vxcloud.io;type=A;timeout_s=5", "-",
    "-", "dns|resolve|hostname|api|lookup", "ops_runbook")
add(NW, "dns_lookup", "what ip does the robot hostname point to?", "dns_lookup",
    "name=robot-1.local;type=A;resolver=mdns;timeout_s=5", "mdns enabled on the cell network",
    "-", "dns|ip|robot|hostname|resolve", "ops_runbook")
add(NW, "dns_lookup", "check the mx records for the domain", "dns_lookup",
    "name=vxcloud.io;type=MX;timeout_s=5", "-",
    "-", "dns|mx|mail|records|lookup", "ops_runbook")
add(NW, "dns_lookup", "do a reverse lookup on that ip", "dns_lookup",
    "name=10.20.4.17;type=PTR;timeout_s=5", "-",
    "-", "dns|reverse|ptr|ip|lookup", "ops_runbook")
add(NW, "dns_lookup", "dns seems broken - which resolver are we using?", "dns_lookup",
    "mode=config;fields=nameservers|search_domains|resolv_conf", "-",
    "-", "dns|resolver|broken|nameserver|config", "ops_runbook")
add(NW, "dns_lookup", "check the txt record for the domain verification", "dns_lookup",
    "name=_vxverify.vxcloud.io;type=TXT;timeout_s=5", "-",
    "-", "dns|txt|verification|record|lookup", "ops_runbook")

add(NW, "ping_host", "can we ping the controller?", "shell_exec",
    "cmd=ping -c 4 robot-1.local;timeout_s=20", "-",
    "-", "ping|controller|reachable|network|check", "ops_runbook")
add(NW, "trace_route", "trace the route to the cloud api", "shell_exec",
    "cmd=traceroute -n api.vxcloud.io;timeout_s=60", "-",
    "-", "traceroute|route|network|path|api", "ops_runbook")
add(NW, "show_interfaces", "what is my ip address?", "shell_exec",
    "cmd=ip -brief addr show;timeout_s=10", "-",
    "-", "ip|address|network|interface|mine", "ops_runbook")
add(NW, "show_routes", "show the routing table", "shell_exec",
    "cmd=ip route show;timeout_s=10", "-",
    "-", "route|routing|table|network|gateway", "ops_runbook")
add(NW, "show_interfaces", "is the network link actually up?", "shell_exec",
    "cmd=ip -brief link show;timeout_s=10", "-",
    "-", "link|network|interface|up|status", "ops_runbook")
add(NW, "measure_packet_loss", "measure the packet loss to the robot", "shell_exec",
    "cmd=ping -c 100 -i 0.2 robot-1.local | tail -3;timeout_s=60", "-",
    "packet loss above 1 percent will cause realtime control dropouts", "packet loss|ping|robot|network|quality", "ops_runbook")
add(NW, "measure_latency", "check the network latency to the cell switch", "shell_exec",
    "cmd=ping -c 20 switch-cell.local | tail -2;timeout_s=40", "-",
    "latency spikes above 10 ms break the realtime motion bus", "latency|ping|switch|cell|network", "ops_runbook")

add(NW, "check_tls", "check the tls certificate on the api", "shell_exec",
    "cmd=openssl s_client -connect api.vxcloud.io:443 -servername api.vxcloud.io < /dev/null | openssl x509 -noout -dates;timeout_s=30", "-",
    "an expired certificate breaks every client so renew well in advance", "tls|certificate|expiry|https|check", "ops_runbook")
add(NW, "check_proxy", "why is the request going through the proxy?", "shell_exec",
    "cmd=env | grep -i proxy;timeout_s=5", "-",
    "-", "proxy|env|http_proxy|request|config", "ops_runbook")
add(NW, "list_firewall", "list the firewall rules", "shell_exec",
    "cmd=iptables -L -n -v;sudo=true;timeout_s=30", "-",
    "never flush firewall rules on a remote host or you will lock yourself out", "firewall|rules|iptables|list|network", "ops_runbook")
add(NW, "open_firewall_port", "open port 8443 in the firewall", "shell_exec",
    "cmd=ufw allow 8443/tcp;sudo=true;timeout_s=20", "the change is approved by security",
    "every open port widens the attack surface so close it when it is no longer needed", "firewall|open|port|8443|allow", "ops_runbook")
add(NW, "read_http_log", "the api call keeps timing out - what does the request log say?", "read_log",
    "source=file;path=/var/log/vx/http.log;filter=timeout;lines=100", "http logging enabled",
    "-", "timeout|http|log|api|request", "ops_runbook")
add(NW, "read_http_log", "show me the recent 5xx responses", "read_log",
    "source=file;path=/var/log/nginx/access.log;filter=status_5xx;lines=200", "the access log is readable",
    "a spike in 5xx errors means the backend is failing so check upstream health", "5xx|errors|http|access log|nginx", "ops_runbook")


# ---------------------------------------------------------------------------
# COMPUTER - computer_use / GUI automation (40)
# ---------------------------------------------------------------------------
CU = "computer_use"
add(CU, "open_application", "open the browser", "open_application",
    "app=firefox;wait_ready_s=10", "a desktop session is active",
    "-", "open|browser|firefox|launch|application", "computer_use")
add(CU, "open_application", "launch the robot teach pendant software", "open_application",
    "app=teachpendant;window=maximized;wait_ready_s=20", "pendant software installed and licensed",
    "the pendant takes motion control so confirm nobody else is jogging the arm", "open|teach pendant|launch|software|app", "computer_use")
add(CU, "open_application", "start the terminal", "open_application",
    "app=gnome-terminal;cwd=/srv/app", "-",
    "-", "open|terminal|shell|launch|console", "computer_use")
add(CU, "open_application", "bring up the vxcloud dashboard in chrome", "open_application",
    "app=chrome;url=https://app.vxcloud.io/dashboard;wait_ready_s=15", "the browser session is authenticated",
    "-", "open|dashboard|chrome|vxcloud|browser", "computer_use")
add(CU, "open_application", "open the csv in the spreadsheet app", "open_application",
    "app=libreoffice-calc;file=/data/brain/robot_brain.csv", "the file exists",
    "do not let the spreadsheet re-save the csv - it will quote fields and break the parser", "open|csv|spreadsheet|excel|libreoffice", "computer_use")
add(CU, "open_application", "get the file manager up so I can see the folder", "open_application",
    "app=nautilus;path=/data/brain", "-",
    "-", "open|file manager|folder|explorer|browse", "computer_use")
add(CU, "open_application", "start the camera viewer", "open_application",
    "app=rqt_image_view;topic=/camera/color/image_raw", "the camera node is publishing",
    "-", "open|camera|viewer|image|app", "computer_use")
add(CU, "open_application", "open the log viewer for the controller", "open_application",
    "app=logviewer;file=/var/log/vx/controller.log;follow=true", "-",
    "-", "open|log viewer|controller|logs|app", "computer_use")
add(CU, "focus_window", "can you bring the simulation window back to the front?", "open_application",
    "app=gazebo;action=focus", "the simulator is already running",
    "-", "focus|window|simulation|front|gazebo", "computer_use")
add(CU, "close_application", "close the browser - I am done", "open_application",
    "app=firefox;action=quit;save_state=false", "-",
    "quitting discards unsaved form data in open tabs", "close|quit|browser|exit|application", "computer_use")

add(CU, "click_ui", "click the start button", "click_ui",
    "target=button;label=Start;wait_visible_s=5", "the target window has focus",
    "confirm the button really is Start before clicking inside a control ui", "click|start|button|press|ui", "computer_use")
add(CU, "click_ui", "hit the run program button on the pendant", "click_ui",
    "target=button;label=Run;app=teachpendant;confirm=true", "a program is selected and the cell is clear",
    "clicking run starts real robot motion so verify the cell is empty first", "click|run|program|pendant|start", "computer_use")
add(CU, "click_ui", "press ok on the dialog", "click_ui",
    "target=button;label=OK;role=dialog;wait_visible_s=5", "-",
    "-", "click|ok|dialog|confirm|button", "computer_use")
add(CU, "click_ui", "click the deploy button in the dashboard", "click_ui",
    "target=button;label=Deploy;app=chrome;selector=#deploy-btn", "the dashboard is loaded and a stack is selected",
    "deploy pushes to a live environment so check the target environment first", "click|deploy|dashboard|button|ui", "computer_use")
add(CU, "click_ui", "select the second row in the results table", "click_ui",
    "target=row;index=2;app=chrome;selector=table.results", "-",
    "-", "click|row|table|select|ui", "computer_use")
add(CU, "click_ui", "check the enable checkbox", "click_ui",
    "target=checkbox;label=Enable;state=checked", "-",
    "-", "click|checkbox|enable|tick|ui", "computer_use")
add(CU, "click_ui", "open the settings menu", "click_ui",
    "target=menu;label=Settings;path=Edit|Preferences", "-",
    "-", "click|menu|settings|preferences|open", "computer_use")
add(CU, "click_ui", "double click the dataset file to open it", "click_ui",
    "target=file;name=robot_brain.csv;action=double_click;app=nautilus", "-",
    "-", "double click|file|open|dataset|ui", "computer_use")
add(CU, "click_ui", "right click on the node and pick restart", "click_ui",
    "target=node;name=node-1;action=context_menu;then=Restart", "the node is visible in the tree",
    "restarting a node interrupts anything running on it", "right click|context menu|restart|node|ui", "computer_use")
add(CU, "click_ui", "scroll down and click load more", "click_ui",
    "target=button;label=Load more;scroll_into_view=true;app=chrome", "-",
    "-", "click|scroll|load more|button|ui", "computer_use")

add(CU, "type_text", "type my username into the login field", "type_text",
    "target=field;label=Username;text=$VX_USER;clear_first=true", "the login page is loaded",
    "never type a password into a field that is not masked", "type|username|login|field|input", "computer_use")
add(CU, "type_text", "enter the ip address of the controller", "type_text",
    "target=field;label=Controller IP;text=192.168.10.21;clear_first=true", "the settings dialog is open",
    "-", "type|ip|controller|address|input", "computer_use")
add(CU, "type_text", "fill in the part number", "type_text",
    "target=field;label=Part number;text=WX-4471;submit=false", "-",
    "-", "type|part number|fill|field|input", "computer_use")
add(CU, "type_text", "type the search query and press enter", "type_text",
    "target=field;role=search;text=robot brain dataset;submit=true", "-",
    "-", "type|search|query|enter|input", "computer_use")
add(CU, "type_text", "write the command into the terminal and run it", "type_text",
    "target=terminal;text=systemctl status brain-engine;submit=true", "the terminal window is focused",
    "read the command before submitting - typed commands execute immediately", "type|command|terminal|run|input", "computer_use")
add(CU, "type_text", "paste the token into the api key box", "type_text",
    "target=field;label=API key;text=$VX_TOKEN;mask=true;clear_first=true", "the token is available in the environment",
    "mask the token so it never lands in a screenshot or a log", "type|token|api key|paste|secret", "computer_use")
add(CU, "type_text", "put today's date in the date field", "type_text",
    "target=field;label=Date;text=2026-07-13;format=iso", "-",
    "-", "type|date|field|today|input", "computer_use")
add(CU, "type_text", "enter the speed override as 25 percent", "type_text",
    "target=field;label=Speed override;text=25;units=percent;submit=true", "the pendant is in manual mode",
    "verify the override took effect before the next motion command", "type|speed|override|percent|input", "computer_use")
add(CU, "type_text", "type the ticket number into the comment box", "type_text",
    "target=field;label=Comment;text=OPS-2291 robot recovered;submit=false", "-",
    "-", "type|ticket|comment|note|input", "computer_use")
add(CU, "type_text", "clear the field and type the new waypoint name", "type_text",
    "target=field;label=Waypoint;text=pick_point_b;clear_first=true;submit=true", "-",
    "renaming a waypoint updates every program that references it", "type|waypoint|rename|field|input", "computer_use")

add(CU, "screenshot", "take a screenshot", "screenshot",
    "scope=screen;path=/tmp/shot.png;format=png", "-",
    "-", "screenshot|capture|screen|image|shot", "computer_use")
add(CU, "screenshot", "capture the pendant screen so I can attach it to the ticket", "screenshot",
    "scope=window;app=teachpendant;path=/tmp/pendant.png", "the pendant window is visible",
    "redact any credentials visible on screen before attaching the image", "screenshot|pendant|window|ticket|capture", "computer_use")
add(CU, "screenshot", "grab a picture of the error dialog", "screenshot",
    "scope=window;role=dialog;path=/tmp/error.png;include_cursor=false", "-",
    "-", "screenshot|error|dialog|capture|image", "computer_use")
add(CU, "screenshot", "screenshot the dashboard and save it to the share", "screenshot",
    "scope=window;app=chrome;path=/mnt/share/screens/dashboard.png", "the share is mounted",
    "-", "screenshot|dashboard|save|share|capture", "computer_use")
add(CU, "screenshot", "take a shot of just the camera view region", "screenshot",
    "scope=region;region=x100|y120|w640|h480;path=/tmp/camera.png", "-",
    "-", "screenshot|region|crop|camera|capture", "computer_use")
add(CU, "screenshot", "capture the screen every 5 seconds while the cycle runs", "screenshot",
    "scope=screen;interval_s=5;count=24;path=/tmp/cycle/", "disk space for 24 images",
    "continuous capture may record operator faces so check the site privacy policy", "screenshot|interval|timelapse|cycle|capture", "computer_use")
add(CU, "screenshot", "what is on the screen right now?", "screenshot",
    "scope=screen;path=/tmp/now.png;annotate=ocr", "-",
    "-", "screenshot|what|screen|now|capture", "computer_use")
add(CU, "screenshot", "snapshot both monitors", "screenshot",
    "scope=all_displays;path=/tmp/desktop.png;format=png", "two displays are connected",
    "-", "screenshot|monitors|displays|both|capture", "computer_use")
add(CU, "screenshot", "screenshot the fault code shown on the controller hmi", "screenshot",
    "scope=window;app=hmi;path=/tmp/fault.png;then=ocr", "the hmi window is visible",
    "record the fault code before clearing it - the hmi discards it on reset", "screenshot|fault|hmi|controller|capture", "ops_runbook")
add(CU, "screenshot", "capture the browser page as a full length image", "screenshot",
    "scope=page;app=chrome;full_page=true;path=/tmp/page.png", "-",
    "-", "screenshot|full page|browser|capture|image", "computer_use")


# ---------------------------------------------------------------------------
# PROVISIONING (100) - always delegated to the vxnode node API
#   POST /api/v2/provision/vm
#   GET  /api/v2/provision/vm/status
#   POST /api/v2/provision/vm/action
#   POST /api/v2/infrastructure/services/<stack>/deploy
# ---------------------------------------------------------------------------
EP_VM = "endpoint=/api/v2/provision/vm;method=POST;"
EP_ST = "endpoint=/api/v2/provision/vm/status;method=GET;"
EP_AC = "endpoint=/api/v2/provision/vm/action;method=POST;"


def EP_DEPLOY(stack):
    return "endpoint=/api/v2/infrastructure/services/" + stack + "/deploy;method=POST;"


# --- vxnode_provision (35) -------------------------------------------------
add_prov("provision_vm", "spin up a new vm for the fastapi service", "vxnode_provision",
         EP_VM + "stack=fastapi;cpu=2;ram=4096;disk=40;name=vx-fastapi-01",
         "vxnode api reachable and the token valid",
         "check the quota before creating so the request is not rejected mid-flight",
         "provision|vm|fastapi|create|vxnode")
add_prov("provision_vm", "provision a node server for the web app", "vxnode_provision",
         EP_VM + "stack=nodejs;cpu=2;ram=4096;disk=30;name=vx-node-01",
         "vxnode api token exported",
         "confirm the stack name before the call - a wrong stack rebuilds the wrong image",
         "provision|vm|nodejs|node|create")
add_prov("provision_vm", "I need a python box for the data scripts", "vxnode_provision",
         EP_VM + "stack=python;cpu=2;ram=8192;disk=60;name=vx-py-data",
         "project quota has at least 2 vcpu free",
         "size the disk up front because resizing later needs a reboot",
         "provision|vm|python|data|create")
add_prov("provision_vm", "create a django vm for the admin portal", "vxnode_provision",
         EP_VM + "stack=django;cpu=4;ram=8192;disk=80;name=vx-django-admin",
         "database endpoint reachable from the target subnet",
         "the admin portal is internet facing so restrict the security group on creation",
         "provision|vm|django|admin|create")
add_prov("provision_vm", "put up a small flask instance for the internal tool", "vxnode_provision",
         EP_VM + "stack=flask;cpu=1;ram=2048;disk=20;name=vx-flask-tool",
         "vxnode api reachable",
         "a 1 vcpu box will throttle under load so keep it for internal traffic only",
         "provision|vm|flask|internal|create")
add_prov("provision_vm", "we need a go service host", "vxnode_provision",
         EP_VM + "stack=golang;cpu=2;ram=4096;disk=40;name=vx-go-svc",
         "vxnode api token valid",
         "-",
         "provision|vm|golang|go|create")
add_prov("provision_vm", "provision a rust build machine", "vxnode_provision",
         EP_VM + "stack=rust;cpu=8;ram=16384;disk=120;name=vx-rust-build",
         "build quota approved for 8 vcpu",
         "rust builds are cpu bound so an undersized box will time out the pipeline",
         "provision|vm|rust|build|create")
add_prov("provision_vm", "spin up a java vm for the batch jobs", "vxnode_provision",
         EP_VM + "stack=java;cpu=4;ram=16384;disk=80;name=vx-java-batch",
         "heap sizing agreed with the app team",
         "the jvm heap must fit inside the vm ram or the kernel will kill the process",
         "provision|vm|java|batch|create")
add_prov("provision_vm", "stand up a springboot server for the orders api", "vxnode_provision",
         EP_VM + "stack=springboot;cpu=4;ram=8192;disk=60;name=vx-spring-orders",
         "orders database credentials in the secret store",
         "-",
         "provision|vm|springboot|orders|create")
add_prov("provision_vm", "create a php host for the legacy site", "vxnode_provision",
         EP_VM + "stack=php;cpu=2;ram=4096;disk=40;name=vx-php-legacy",
         "legacy site archive available",
         "the legacy stack is unpatched so keep it off the public subnet",
         "provision|vm|php|legacy|create")
add_prov("provision_vm", "provision a laravel vm for the customer portal", "vxnode_provision",
         EP_VM + "stack=laravel;cpu=2;ram=4096;disk=50;name=vx-laravel-portal",
         "database and redis endpoints known",
         "-",
         "provision|vm|laravel|portal|create")
add_prov("provision_vm", "get a nextjs front end machine ready", "vxnode_provision",
         EP_VM + "stack=nextjs;cpu=2;ram=4096;disk=40;name=vx-next-web",
         "vxnode api reachable",
         "-",
         "provision|vm|nextjs|frontend|create")
add_prov("provision_vm", "we need a react dev box", "vxnode_provision",
         EP_VM + "stack=reactjs;cpu=2;ram=8192;disk=40;name=vx-react-dev",
         "dev environment quota available",
         "-",
         "provision|vm|reactjs|dev|create")
add_prov("provision_vm", "spin up an angular host for the admin ui", "vxnode_provision",
         EP_VM + "stack=angular;cpu=2;ram=4096;disk=40;name=vx-ng-admin",
         "vxnode api token valid",
         "-",
         "provision|vm|angular|admin ui|create")
add_prov("provision_vm", "create a streamlit vm so the team can see the dashboards", "vxnode_provision",
         EP_VM + "stack=streamlit;cpu=2;ram=4096;disk=30;name=vx-streamlit-dash",
         "data source reachable from the vm subnet",
         "streamlit has no auth by default so put it behind the sso proxy",
         "provision|vm|streamlit|dashboard|create")
add_prov("provision_vm", "put the marketing static site on its own vm", "vxnode_provision",
         EP_VM + "stack=staticwebsite;cpu=1;ram=1024;disk=10;name=vx-static-mkt",
         "site bundle built",
         "-",
         "provision|vm|staticwebsite|marketing|create")
add_prov("provision_vm", "provision a c++ build node for the robot firmware", "vxnode_provision",
         EP_VM + "stack=cpp;cpu=16;ram=32768;disk=200;name=vx-cpp-build",
         "build quota approved for 16 vcpu",
         "-",
         "provision|vm|cpp|build|firmware")
add_prov("provision_vm", "give me a bigger vm - the current one keeps running out of memory", "vxnode_provision",
         EP_VM + "stack=python;cpu=4;ram=16384;disk=80;name=vx-py-large;replaces=vx-py-data",
         "data migrated off the old vm",
         "provisioning a replacement does not delete the old vm so retire it afterwards",
         "provision|vm|bigger|memory|resize")
add_prov("provision_vm", "provision two identical workers for the queue", "vxnode_provision",
         EP_VM + "stack=python;cpu=2;ram=4096;disk=40;count=2;name_prefix=vx-worker",
         "queue endpoint reachable",
         "-",
         "provision|vm|workers|two|create")
add_prov("provision_vm", "create the vm in the eu region", "vxnode_provision",
         EP_VM + "stack=fastapi;region=eu-west-1;cpu=2;ram=4096;disk=40;name=vx-api-eu",
         "eu region enabled for the project",
         "eu data residency rules apply so keep customer data inside the region",
         "provision|vm|eu|region|create")
add_prov("provision_vm", "spin up a machine in the us east region for the demo", "vxnode_provision",
         EP_VM + "stack=nextjs;region=us-east-1;cpu=2;ram=4096;disk=40;name=vx-demo-use1",
         "demo account has quota in us-east-1",
         "-",
         "provision|vm|us east|region|demo")
add_prov("provision_vm", "provision a vm with my ssh key on it", "vxnode_provision",
         EP_VM + "stack=golang;cpu=2;ram=4096;disk=40;ssh_key=ops-philip;name=vx-go-ops",
         "the public key is registered with vxnode",
         "only inject public keys - a private key must never be passed to the api",
         "provision|vm|ssh key|access|create")
add_prov("provision_vm", "create a vm and tag it for the robotics project", "vxnode_provision",
         EP_VM + "stack=python;cpu=4;ram=8192;disk=60;tags=project|robotics;name=vx-py-robotics",
         "the robotics project exists in vxnode",
         "-",
         "provision|vm|tag|robotics|create")
add_prov("provision_vm", "spin up a staging copy of the api server", "vxnode_provision",
         EP_VM + "stack=fastapi;cpu=2;ram=4096;disk=40;env=staging;name=vx-api-staging",
         "staging config and secrets prepared",
         "point staging at the staging database - never at production data",
         "provision|vm|staging|api|copy")
add_prov("provision_vm", "we need a production sized instance for the api", "vxnode_provision",
         EP_VM + "stack=fastapi;cpu=8;ram=16384;disk=100;env=production;name=vx-api-prod",
         "change approved for a production resource",
         "production resources are billed and audited so confirm the change ticket",
         "provision|vm|production|api|create")
add_prov("provision_vm", "provision a gpu box for the vision model", "vxnode_provision",
         EP_VM + "stack=python;cpu=8;ram=32768;disk=200;gpu=1;name=vx-vision-gpu",
         "gpu quota available in the region",
         "gpu instances are expensive so set a budget alert when you create one",
         "provision|vm|gpu|vision|create")
add_prov("provision_vm", "can you check the quota before creating the vm?", "vxnode_provision",
         EP_VM + "stack=java;cpu=4;ram=8192;disk=60;dry_run=true",
         "vxnode api reachable",
         "a dry run validates the request without creating anything",
         "provision|dry run|quota|check|vm")
add_prov("provision_vm", "set up a vm for the ci runners", "vxnode_provision",
         EP_VM + "stack=nodejs;cpu=4;ram=8192;disk=80;name=vx-ci-runner",
         "ci registration token available",
         "ci runners execute untrusted code so isolate them on their own subnet",
         "provision|vm|ci|runner|create")
add_prov("provision_vm", "provision the database host", "vxnode_provision",
         EP_VM + "stack=python;cpu=4;ram=16384;disk=250;disk_type=ssd;name=vx-db-1",
         "backup policy defined for the volume",
         "attach the backup policy at creation time - data added before it is unprotected",
         "provision|vm|database|host|create")
add_prov("provision_vm", "create a throwaway sandbox vm I can break", "vxnode_provision",
         EP_VM + "stack=python;cpu=2;ram=4096;disk=20;ttl_h=8;name=vx-sandbox",
         "sandbox quota free",
         "the ttl destroys the vm after 8 hours so do not leave anything valuable on it",
         "provision|vm|sandbox|temporary|create")
add_prov("provision_vm", "spin up a vm with the smallest footprint you can", "vxnode_provision",
         EP_VM + "stack=staticwebsite;cpu=1;ram=512;disk=10;name=vx-tiny",
         "-",
         "512 MB of ram will oom under any real workload",
         "provision|vm|small|cheap|create")
add_prov("provision_vm", "make a new vm from the golden image", "vxnode_provision",
         EP_VM + "stack=cpp;image=golden-2026-06;cpu=4;ram=8192;disk=60;name=vx-cpp-golden",
         "the golden image exists in the vxnode image registry",
         "verify the golden image is patched before cloning it into new hosts",
         "provision|vm|golden image|clone|create")
add_prov("provision_vm", "provision a host for the mqtt bridge to the robot cell", "vxnode_provision",
         EP_VM + "stack=golang;cpu=2;ram=2048;disk=20;name=vx-mqtt-bridge",
         "cell vlan route approved",
         "the bridge touches the robot cell network so lock the security group to the cell subnet",
         "provision|vm|mqtt|bridge|cell")
add_prov("provision_vm", "we need a vm to host the retrieval engine for the brain", "vxnode_provision",
         EP_VM + "stack=cpp;cpu=8;ram=16384;disk=100;name=vx-brain-engine",
         "dataset bundle available for upload",
         "the brain engine loads the csv at boot so provision enough disk for the dataset",
         "provision|vm|brain|engine|create")
add_prov("provision_vm", "create the vm but do not start it yet", "vxnode_provision",
         EP_VM + "stack=django;cpu=2;ram=4096;disk=40;start_after_create=false;name=vx-django-idle",
         "-",
         "a stopped vm still consumes disk quota even though it is not billed for cpu",
         "provision|vm|create|do not start|stopped")

# --- vxnode_status (20) ----------------------------------------------------
add_prov("check_vm_status", "is the vm ready yet?", "vxnode_status",
         EP_ST + "vm=vx-fastapi-01;fields=state|ip|progress",
         "the provisioning job was submitted",
         "-",
         "vm|status|ready|check|provision")
add_prov("check_vm_status", "what is the status of the provisioning job?", "vxnode_status",
         EP_ST + "job_id=required;fields=state|step|eta_s",
         "the job id was returned by the provision call",
         "-",
         "provision|job|status|progress|check")
add_prov("list_vms", "list all the vms we have running", "vxnode_status",
         EP_ST + "scope=all;fields=name|state|ip|stack",
         "vxnode api token valid",
         "-",
         "vm|list|all|running|inventory")
add_prov("check_vm_status", "did the build node come up?", "vxnode_status",
         EP_ST + "vm=vx-cpp-build;fields=state|ip|boot_time_s",
         "-",
         "-",
         "vm|build node|up|status|check")
add_prov("check_vm_status", "what ip did the new vm get?", "vxnode_status",
         EP_ST + "vm=vx-node-01;fields=ip|private_ip|dns",
         "the vm has finished booting",
         "-",
         "vm|ip|address|status|check")
add_prov("check_vm_status", "check whether the gpu box is provisioned", "vxnode_status",
         EP_ST + "vm=vx-vision-gpu;fields=state|gpu|driver_version",
         "-",
         "a missing gpu driver looks like a healthy vm so always check the driver version",
         "vm|gpu|provisioned|status|check")
add_prov("check_vm_status", "how far along is the vm creation?", "vxnode_status",
         EP_ST + "job_id=required;fields=progress|step|started_at;poll_s=5",
         "-",
         "-",
         "provision|progress|creation|status|poll")
add_prov("check_vm_status", "the provisioning seems stuck - what step is it on?", "vxnode_status",
         EP_ST + "job_id=required;fields=step|state|last_error;verbose=true",
         "the job is still in a running state",
         "do not resubmit a stuck job - a duplicate request creates a second vm",
         "provision|stuck|step|error|status")
add_prov("list_vms", "show me the state of every vm in the eu region", "vxnode_status",
         EP_ST + "scope=region;region=eu-west-1;fields=name|state|cpu|ram",
         "-",
         "-",
         "vm|list|eu|region|status")
add_prov("list_vms", "which vms are stopped?", "vxnode_status",
         EP_ST + "scope=all;filter=state=stopped;fields=name|stack|stopped_at",
         "-",
         "-",
         "vm|stopped|list|idle|status")
add_prov("check_vm_status", "how much cpu and memory does the api vm have?", "vxnode_status",
         EP_ST + "vm=vx-api-prod;fields=cpu|ram|disk|stack",
         "-",
         "-",
         "vm|cpu|memory|size|status")
add_prov("check_vm_status", "is the staging api reachable yet?", "vxnode_status",
         EP_ST + "vm=vx-api-staging;fields=state|ip|health",
         "-",
         "-",
         "vm|staging|reachable|health|status")
add_prov("check_vm_status", "get the health of all the robotics project vms", "vxnode_status",
         EP_ST + "scope=tag;tag=robotics;fields=name|state|health",
         "the robotics tag exists",
         "-",
         "vm|health|robotics|tag|status")
add_prov("check_vm_status", "what stack is running on that vm?", "vxnode_status",
         EP_ST + "vm=vx-brain-engine;fields=stack|services|version",
         "-",
         "-",
         "vm|stack|services|version|status")
add_prov("check_quota", "check the provisioning quota we have left", "vxnode_status",
         EP_ST + "scope=quota;fields=vcpu_used|vcpu_limit|vm_count",
         "-",
         "a provision call fails outright once the quota is exhausted",
         "quota|limit|provision|remaining|status")
add_prov("check_vm_status", "did the sandbox vm expire?", "vxnode_status",
         EP_ST + "vm=vx-sandbox;fields=state|ttl_remaining_h",
         "-",
         "the ttl deletes the vm and its disk without warning",
         "vm|sandbox|expired|ttl|status")
add_prov("list_vms", "show the vm creation history for today", "vxnode_status",
         EP_ST + "scope=all;filter=created>today;fields=name|created_at|state",
         "-",
         "-",
         "vm|history|created|today|status")
add_prov("check_vm_status", "how long has the api vm been up?", "vxnode_status",
         EP_ST + "vm=vx-api-prod;fields=uptime_s|state|last_restart",
         "-",
         "-",
         "vm|uptime|how long|running|status")
add_prov("check_vm_status", "what went wrong with the failed vm?", "vxnode_status",
         EP_ST + "vm=vx-java-batch;fields=state|last_error|events;verbose=true",
         "the vm is in a failed state",
         "read the failure reason before retrying or the retry will fail the same way",
         "vm|failed|error|why|status")
add_prov("check_vm_status", "tell me the private address of the mqtt bridge", "vxnode_status",
         EP_ST + "vm=vx-mqtt-bridge;fields=private_ip|port|state",
         "-",
         "-",
         "vm|private ip|mqtt|bridge|status")

# --- vxnode_deploy (30) ----------------------------------------------------
add_prov("deploy_service", "deploy the fastapi service", "vxnode_deploy",
         EP_DEPLOY("fastapi") + "vm=vx-fastapi-01;repo=vxcloud/api;branch=main;port=8000",
         "the vm is running and reachable",
         "-",
         "deploy|fastapi|service|release|vxnode")
add_prov("deploy_service", "deploy the node app to the web vm", "vxnode_deploy",
         EP_DEPLOY("nodejs") + "vm=vx-node-01;repo=vxcloud/web;branch=main;port=3000",
         "node build artefacts available",
         "-",
         "deploy|nodejs|web|app|release")
add_prov("deploy_service", "push the django admin to production", "vxnode_deploy",
         EP_DEPLOY("django") + "vm=vx-django-admin;branch=release;migrate=true;port=8000",
         "database backup taken before the migration",
         "migrations are not automatically reversible so back the database up first",
         "deploy|django|production|migrate|release")
add_prov("deploy_service", "deploy the flask internal tool", "vxnode_deploy",
         EP_DEPLOY("flask") + "vm=vx-flask-tool;branch=main;port=5000",
         "-",
         "-",
         "deploy|flask|internal|tool|release")
add_prov("deploy_service", "roll out the go service", "vxnode_deploy",
         EP_DEPLOY("golang") + "vm=vx-go-svc;branch=main;port=8080;build=true",
         "the go module builds cleanly",
         "-",
         "deploy|golang|service|rollout|release")
add_prov("deploy_service", "deploy the rust binary to the build node", "vxnode_deploy",
         EP_DEPLOY("rust") + "vm=vx-rust-build;branch=main;profile=release",
         "cargo build passes locally",
         "-",
         "deploy|rust|binary|build node|release")
add_prov("deploy_service", "deploy the java batch app", "vxnode_deploy",
         EP_DEPLOY("java") + "vm=vx-java-batch;artifact=batch.jar;jvm_opts=-Xmx8g",
         "the jar is published to the artifact store",
         "the heap flag must stay below the vm ram or the jvm will be oom killed",
         "deploy|java|batch|jar|release")
add_prov("deploy_service", "get springboot up on the orders host", "vxnode_deploy",
         EP_DEPLOY("springboot") + "vm=vx-spring-orders;branch=main;port=8080;profile=prod",
         "prod profile config present",
         "-",
         "deploy|springboot|orders|service|release")
add_prov("deploy_service", "deploy the php legacy site", "vxnode_deploy",
         EP_DEPLOY("php") + "vm=vx-php-legacy;branch=main;docroot=/var/www/html",
         "-",
         "the legacy stack has no automated tests so verify the site by hand after deploy",
         "deploy|php|legacy|site|release")
add_prov("deploy_service", "deploy the laravel portal and run the migrations", "vxnode_deploy",
         EP_DEPLOY("laravel") + "vm=vx-laravel-portal;branch=main;migrate=true;queue_restart=true",
         "database backup taken",
         "restart the queue workers after deploy or they keep running the old code",
         "deploy|laravel|portal|migrate|release")
add_prov("deploy_service", "ship the nextjs front end", "vxnode_deploy",
         EP_DEPLOY("nextjs") + "vm=vx-next-web;branch=main;build=true;port=3000",
         "the build passes in ci",
         "-",
         "deploy|nextjs|frontend|ship|release")
add_prov("deploy_service", "deploy the react dashboard", "vxnode_deploy",
         EP_DEPLOY("reactjs") + "vm=vx-react-dev;branch=develop;build=true",
         "-",
         "-",
         "deploy|reactjs|dashboard|build|release")
add_prov("deploy_service", "put the angular admin ui live", "vxnode_deploy",
         EP_DEPLOY("angular") + "vm=vx-ng-admin;branch=main;build=true;base_href=/admin",
         "-",
         "a wrong base href breaks every route so verify the app after deploy",
         "deploy|angular|admin ui|live|release")
add_prov("deploy_service", "deploy the streamlit dashboard for the team", "vxnode_deploy",
         EP_DEPLOY("streamlit") + "vm=vx-streamlit-dash;branch=main;port=8501",
         "-",
         "streamlit exposes the data it loads so confirm the dashboard is behind sso",
         "deploy|streamlit|dashboard|team|release")
add_prov("deploy_service", "publish the static marketing site", "vxnode_deploy",
         EP_DEPLOY("staticwebsite") + "vm=vx-static-mkt;source=./dist;cdn_invalidate=true",
         "the site bundle is built",
         "invalidate the cdn or users keep seeing the old page",
         "deploy|static|marketing|publish|release")
add_prov("deploy_service", "deploy the c++ retrieval engine to the brain vm", "vxnode_deploy",
         EP_DEPLOY("cpp") + "vm=vx-brain-engine;branch=main;build=cmake;binary=brain_engine",
         "the dataset csv is uploaded to the vm",
         "the engine parses the csv on boot so an invalid dataset takes the service down",
         "deploy|cpp|brain|engine|release")
add_prov("deploy_service", "deploy the python worker to the queue vm", "vxnode_deploy",
         EP_DEPLOY("python") + "vm=vx-worker-1;branch=main;entrypoint=worker.py",
         "the queue is reachable from the vm",
         "-",
         "deploy|python|worker|queue|release")
add_prov("redeploy_service", "redeploy the api - the last deploy failed", "vxnode_deploy",
         EP_DEPLOY("fastapi") + "vm=vx-fastapi-01;branch=main;force=true;clean=true",
         "the cause of the previous failure was identified",
         "a clean redeploy wipes the release directory so confirm nothing is stored there",
         "deploy|redeploy|failed|retry|release")
add_prov("rollback_service", "roll back the api to the previous release", "vxnode_deploy",
         EP_DEPLOY("fastapi") + "vm=vx-api-prod;version=previous;strategy=rollback",
         "the previous release artefact is still retained",
         "a rollback does not undo database migrations so check the schema first",
         "rollback|previous|release|deploy|revert")
add_prov("deploy_service", "deploy the api with zero downtime", "vxnode_deploy",
         EP_DEPLOY("fastapi") + "vm=vx-api-prod;strategy=blue_green;health_path=/health",
         "the health endpoint returns 200 on the new slot",
         "traffic only cuts over once the new slot is healthy",
         "deploy|zero downtime|blue green|api|release")
add_prov("deploy_service", "deploy to staging first before we touch production", "vxnode_deploy",
         EP_DEPLOY("fastapi") + "vm=vx-api-staging;branch=main;env=staging",
         "staging vm running",
         "never promote to production until the staging deploy is verified",
         "deploy|staging|first|verify|release")
add_prov("deploy_service", "can you deploy the tag v2.4.1 instead of main?", "vxnode_deploy",
         EP_DEPLOY("nodejs") + "vm=vx-node-01;tag=v2.4.1;build=true",
         "tag v2.4.1 exists in the repository",
         "-",
         "deploy|tag|version|specific|release")
add_prov("deploy_service", "deploy and run the smoke tests afterwards", "vxnode_deploy",
         EP_DEPLOY("fastapi") + "vm=vx-api-staging;branch=main;post_deploy=smoke_tests",
         "the smoke test suite exists",
         "a failing smoke test must block promotion to production",
         "deploy|smoke tests|verify|post deploy|release")
add_prov("deploy_service", "deploy the app with the new environment variables", "vxnode_deploy",
         EP_DEPLOY("django") + "vm=vx-django-admin;env_file=@prod.env;restart=true",
         "the env file is in the secret store",
         "never commit the env file to the repository - pass it from the secret store",
         "deploy|environment|variables|env|release")
add_prov("deploy_service", "do a dry run of the deployment first", "vxnode_deploy",
         EP_DEPLOY("springboot") + "vm=vx-spring-orders;branch=main;dry_run=true",
         "-",
         "a dry run validates the plan without touching the running service",
         "deploy|dry run|plan|check|release")
add_prov("deploy_service", "deploy the vision service to the gpu box", "vxnode_deploy",
         EP_DEPLOY("python") + "vm=vx-vision-gpu;branch=main;gpu=true;entrypoint=serve.py",
         "the gpu driver and cuda runtime are installed",
         "verify the model loads onto the gpu - a silent cpu fallback will miss the latency target",
         "deploy|vision|gpu|model|release")
add_prov("deploy_service", "deploy the mqtt bridge that talks to the robot cell", "vxnode_deploy",
         EP_DEPLOY("golang") + "vm=vx-mqtt-bridge;branch=main;port=1883",
         "the cell broker credentials are configured",
         "the bridge can command robots so restrict its topic permissions to read only",
         "deploy|mqtt|bridge|robot cell|release")
add_prov("deploy_service", "update the deployment to use two replicas", "vxnode_deploy",
         EP_DEPLOY("nodejs") + "vm=vx-node-01;replicas=2;strategy=rolling",
         "the app is stateless",
         "-",
         "deploy|replicas|scale|rolling|release")
add_prov("deploy_service", "deploy the docs site", "vxnode_deploy",
         EP_DEPLOY("staticwebsite") + "vm=vx-static-mkt;source=./docs/_build;path=/docs",
         "the docs build succeeded",
         "-",
         "deploy|docs|site|static|release")
add_prov("deploy_service", "how do I deploy the flask app without dropping requests?", "vxnode_deploy",
         EP_DEPLOY("flask") + "vm=vx-flask-tool;strategy=rolling;drain_s=30;health_path=/healthz",
         "the app handles SIGTERM cleanly",
         "drain for 30 s so in-flight requests finish before the old process exits",
         "deploy|rolling|no downtime|drain|release")

# --- vxnode_action (15) ----------------------------------------------------
add_prov("vm_action", "restart the api vm", "vxnode_action",
         EP_AC + "vm=vx-api-prod;action=restart",
         "traffic drained from the node",
         "a restart drops in-flight requests so drain the load balancer first",
         "restart|vm|api|action|reboot")
add_prov("vm_action", "stop the sandbox vm - I am done with it", "vxnode_action",
         EP_AC + "vm=vx-sandbox;action=stop",
         "-",
         "-",
         "stop|vm|sandbox|shutdown|action")
add_prov("vm_action", "start the build node back up", "vxnode_action",
         EP_AC + "vm=vx-cpp-build;action=start",
         "-",
         "-",
         "start|vm|build node|power on|action")
add_prov("vm_action", "reboot the node that is not responding", "vxnode_action",
         EP_AC + "vm=vx-node-01;action=restart;force=false",
         "the vm is confirmed unresponsive",
         "try a graceful restart before forcing it or the filesystem may be left dirty",
         "reboot|vm|unresponsive|restart|action")
add_prov("vm_action", "shut down all the dev vms for the weekend", "vxnode_action",
         EP_AC + "scope=tag;tag=dev;action=stop",
         "no dev jobs are running",
         "a tag wide stop hits every tagged vm so check the tag membership first",
         "stop|dev|vms|weekend|action")
add_prov("vm_action", "bring the staging environment back online", "vxnode_action",
         EP_AC + "vm=vx-api-staging;action=start;wait_healthy=true",
         "-",
         "-",
         "start|staging|online|vm|action")
add_prov("vm_action", "restart the streamlit service on that vm", "vxnode_action",
         EP_AC + "vm=vx-streamlit-dash;action=restart;service=streamlit",
         "-",
         "restarting the service drops every open dashboard session",
         "restart|streamlit|service|vm|action")
add_prov("vm_action", "power cycle the gpu box - the driver hung", "vxnode_action",
         EP_AC + "vm=vx-vision-gpu;action=restart;force=true",
         "the gpu driver is confirmed hung",
         "a forced power cycle loses any unsaved work on the vm",
         "restart|gpu|power cycle|hung|action")
add_prov("check_action_status", "check the action status - did the restart finish?", "vxnode_action",
         EP_AC + "action=status;vm=vx-api-prod;fields=last_action|state",
         "an action was submitted",
         "-",
         "action|status|restart|finished|check")
add_prov("vm_action", "stop the java vm overnight to save cost", "vxnode_action",
         EP_AC + "vm=vx-java-batch;action=stop;schedule=22:00",
         "no batch job is scheduled overnight",
         "a scheduled stop kills any job still running at that time",
         "stop|vm|overnight|cost|schedule")
add_prov("vm_action", "start the worker vms - the queue is backing up", "vxnode_action",
         EP_AC + "scope=name_prefix;prefix=vx-worker;action=start",
         "-",
         "-",
         "start|workers|queue|scale|action")
add_prov("vm_action", "restart everything tagged robotics", "vxnode_action",
         EP_AC + "scope=tag;tag=robotics;action=restart;serial=true",
         "the robotics tag membership was reviewed",
         "restart serially so the whole robotics fleet is never down at once",
         "restart|tag|robotics|fleet|action")
add_prov("vm_action", "stop the legacy php host - we migrated off it", "vxnode_action",
         EP_AC + "vm=vx-php-legacy;action=stop;confirm=true",
         "traffic confirmed migrated off the host",
         "check the access log is quiet before stopping - a live client may still be pointed at it",
         "stop|legacy|php|decommission|action")
add_prov("vm_action", "can you restart the brain engine vm without losing the dataset?", "vxnode_action",
         EP_AC + "vm=vx-brain-engine;action=restart;preserve_disk=true",
         "the dataset lives on the persistent volume",
         "restart preserves the disk but a rebuild would not so never use rebuild here",
         "restart|brain|engine|dataset|preserve")
add_prov("check_action_status", "what actions have been run on this vm today?", "vxnode_action",
         EP_AC + "action=status;vm=vx-api-prod;fields=action_history;window=today",
         "-",
         "-",
         "action|history|audit|vm|today")


# ---------------------------------------------------------------------------
# Validation + writer
# ---------------------------------------------------------------------------
INTENT_RE = re.compile(r"^[a-z][a-z0-9_]*$")


def validate(rows):
    errs = []
    if len(rows) != 500:
        errs.append("expected 500 data rows got %d" % len(rows))
    seen_utt = set()
    for i, r in enumerate(rows):
        rid = "BRN-%04d" % (i + 1)
        if len(r) != 9:
            errs.append("%s wrong field count" % rid)
            continue
        domain, intent, utt, skill, params, pre, safety, kw, src = r
        for name, val in zip(HEADER[1:], r):
            if "," in val:
                errs.append("%s comma in %s: %s" % (rid, name, val))
            if "\n" in val or "\r" in val:
                errs.append("%s newline in %s" % (rid, name))
            if val == "":
                errs.append("%s empty %s" % (rid, name))
        if domain not in DOMAINS:
            errs.append("%s bad domain %s" % (rid, domain))
        if skill not in SKILLS:
            errs.append("%s bad skill %s" % (rid, skill))
        if src not in SOURCES:
            errs.append("%s bad source %s" % (rid, src))
        if not INTENT_RE.match(intent):
            errs.append("%s intent not snake_case: %s" % (rid, intent))
        n_kw = len(kw.split("|"))
        if n_kw < 3 or n_kw > 8:
            errs.append("%s keyword count %d" % (rid, n_kw))
        if kw != kw.lower():
            errs.append("%s keywords not lowercase" % rid)
        if utt in seen_utt:
            errs.append("%s duplicate utterance: %s" % (rid, utt))
        seen_utt.add(utt)
        if domain == "provisioning":
            if skill not in ("vxnode_provision", "vxnode_status", "vxnode_deploy", "vxnode_action"):
                errs.append("%s provisioning row must use a vxnode skill" % rid)
            if "vxnode" not in safety:
                errs.append("%s provisioning safety must reference vxnode" % rid)
            if "/api/v2/" not in params:
                errs.append("%s provisioning params must call a vxnode endpoint" % rid)
    return errs


def main():
    errs = validate(ROWS)
    if errs:
        for e in errs[:40]:
            print("ERROR: " + e, file=sys.stderr)
        print("total errors: %d" % len(errs), file=sys.stderr)
        return 1

    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = os.path.join(os.path.dirname(here), "datasets", "brain")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "robot_brain.csv")

    lines = [",".join(HEADER)]
    for i, r in enumerate(ROWS):
        lines.append(",".join(("BRN-%04d" % (i + 1),) + tuple(r)))

    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")

    dist = Counter(r[0] for r in ROWS)
    print("wrote %s" % out_path)
    print("data rows: %d" % len(ROWS))
    print("unique utterances: %d" % len({r[2] for r in ROWS}))
    print("skills used: %d" % len({r[3] for r in ROWS}))
    print("domain distribution:")
    for d, c in sorted(dist.items(), key=lambda kv: (-kv[1], kv[0])):
        print("  %-14s %3d" % (d, c))
    return 0


if __name__ == "__main__":
    sys.exit(main())
