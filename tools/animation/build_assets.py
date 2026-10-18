"""Blender-side BVH retargeting and deterministic glTF export for Besktop G1-G5.

Run with Blender --background --factory-startup --python this_file -- --source DIR --output DIR.
Only explicitly configured local CMU clips are read; no desktop or network access.
"""
import argparse
import base64
import hashlib
import json
import math
import re
import struct
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

# Blender does not consistently add a --python script's directory to sys.path.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from anthropometry import audit_profile, load as load_reference

JOINTS = [
    ("root", -1, None), ("pelvis", 0, "Hips"),
    ("spine_low", 1, "LowerBack"), ("spine", 2, "Spine"),
    ("chest", 3, "Spine1"), ("neck_base", 4, "Neck"),
    ("neck", 5, "Neck1"), ("head", 6, "Head"),
    ("clavicle_l", 4, "LeftShoulder"), ("upperarm_l", 8, "LeftArm"),
    ("forearm_l", 9, "LeftForeArm"), ("hand_l", 10, "LeftHand"),
    ("clavicle_r", 4, "RightShoulder"), ("upperarm_r", 12, "RightArm"),
    ("forearm_r", 13, "RightForeArm"), ("hand_r", 14, "RightHand"),
    ("thigh_l", 1, "LeftUpLeg"), ("shin_l", 16, "LeftLeg"),
    ("foot_l", 17, "LeftFoot"), ("toe_l", 18, "LeftToeBase"),
    ("thigh_r", 1, "RightUpLeg"), ("shin_r", 20, "RightLeg"),
    ("foot_r", 21, "RightFoot"), ("toe_r", 22, "RightToeBase"),
]
CMU_UNIT = 2.54 / 100 / .45
IDENTITY = Quaternion((1, 0, 0, 0))
AXES = {"X": Vector((1, 0, 0)), "Y": Vector((0, 1, 0)), "Z": Vector((0, 0, 1))}


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\r\n")


def read_bvh(path):
    text = path.read_text(encoding="utf-8")
    hierarchy, motion = text.split("MOTION", 1)
    tokens = re.findall(r"[{}]|[^\s{}]+", hierarchy)
    nodes = []
    cursor = 1
    channel_count = 0

    def node(parent):
        nonlocal cursor, channel_count
        kind = tokens[cursor]
        cursor += 1
        name = tokens[cursor]
        cursor += 1
        if kind == "End":
            name = nodes[parent]["name"] + "_end"
        assert tokens[cursor] == "{"
        cursor += 1
        idx = len(nodes)
        item = {"name": name, "parent": parent, "offset": Vector((0, 0, 0)), "channels": [], "start": channel_count}
        nodes.append(item)
        while tokens[cursor] != "}":
            key = tokens[cursor]
            if key == "OFFSET":
                item["offset"] = Vector([float(x) for x in tokens[cursor + 1:cursor + 4]])
                cursor += 4
            elif key == "CHANNELS":
                count = int(tokens[cursor + 1])
                item["channels"] = tokens[cursor + 2:cursor + 2 + count]
                channel_count += count
                cursor += count + 2
            else:
                node(idx)
        cursor += 1
    node(-1)
    lines = motion.strip().splitlines()
    frame_count = int(lines[0].split(":")[1])
    dt = float(lines[1].split(":")[1])
    frames = [[float(x) for x in line.split()] for line in lines[2:] if line.strip()]
    if len(frames) != frame_count or any(len(f) != channel_count for f in frames):
        raise ValueError(f"Invalid BVH frame/channel count: {path}")
    if not .00832 < dt < .00834:
        raise ValueError("Expected the 120 Hz MotionBuilder 2010 conversion")
    return {"nodes": nodes, "frames": frames, "fps": 120, "path": path}


def bvh_world(source, frame):
    positions, rotations = [], []
    for node in source["nodes"]:
        pos, rot = node["offset"].copy(), IDENTITY.copy()
        for channel, value in zip(node["channels"], frame[node["start"]:]):
            if channel.endswith("position"):
                pos["XYZ".index(channel[0])] += value
            else:
                rot = rot @ Quaternion(AXES[channel[0]], math.radians(value))
        parent = node["parent"]
        if parent >= 0:
            pos = positions[parent] + rotations[parent] @ pos
            rot = rotations[parent] @ rot
        positions.append(pos)
        rotations.append(rot.normalized())
    return positions, rotations


def fk(offsets, positions, rotations):
    p, q = [], []
    for i, (_, parent, _) in enumerate(JOINTS):
        if parent < 0:
            p.append(positions[i].copy())
            q.append(rotations[i].copy())
        else:
            p.append(p[parent] + q[parent] @ positions[i])
            q.append((q[parent] @ rotations[i]).normalized())
    return p, q


def make_reference(source, profile):
    names = {n["name"]: i for i, n in enumerate(source["nodes"])}
    pos, _ = bvh_world(source, source["frames"][0])
    floor = min(pos[names[n]].y for n in ("LeftToeBase", "RightToeBase", "LeftFoot", "RightFoot"))
    # The rendered head crown, not just the Head joint, defines the target height.
    scale = (profile["crown_height"] - profile["presentation"]["head_radius"]) / (pos[names["Head"]].y - floor)
    origin = Vector((pos[0].x, floor, pos[0].z))
    rest = [Vector((0, 0, 0))] + [(pos[names[s]] - origin) * scale for _, _, s in JOINTS[1:]]
    offsets = [rest[i] - rest[parent] if parent >= 0 else rest[i] for i, (_, parent, _) in enumerate(JOINTS)]
    # Symmetric target proportions are independent of marker-fit asymmetry in the source.
    offsets[1].y = profile["pelvis_height"]
    offsets[3].y = profile["spine_height"]
    offsets[4].y = profile["chest_height"]
    for joint, sign in ((9, 1), (13, -1)):
        offsets[joint] = Vector((sign * profile["shoulder_half_width"],
                                 profile["shoulder_height_above_chest"], profile["shoulder_depth"]))
        arm_axis = Vector((sign * math.cos(math.radians(8)), -math.sin(math.radians(8)), 0))
        offsets[joint + 1] = arm_axis * profile["upperarm_length"]
        offsets[joint + 2] = arm_axis * profile["forearm_length"]
    for joint, sign in ((16, 1), (20, -1)):
        offsets[joint] = Vector((sign * profile["hip_half_width"], -profile["hip_drop"], 0))
        offsets[joint + 1] = Vector((0, -profile["thigh_length"], 0))
        offsets[joint + 2] = Vector((0, -profile["shin_length"], 0))
        offsets[joint + 3] = Vector((0, -.025, .145))
    return offsets, scale


def retarget(source, offsets, fps=60, limit_seconds=None):
    names = {n["name"]: i for i, n in enumerate(source["nodes"])}
    refp, refq = bvh_world(source, source["frames"][0])
    # Both legs contribute to displacement scale; source asymmetry must not bias the stride.
    source_leg = sum((refp[names[side + "UpLeg"]] - refp[names[side + "Leg"]]).length +
                     (refp[names[side + "Leg"]] - refp[names[side + "Foot"]]).length for side in ("Left", "Right")) / 2
    scale = (offsets[17].length + offsets[18].length) / source_leg
    firstp, firstq = bvh_world(source, source["frames"][1])
    forward = (firstq[0] @ refq[0].inverted()) @ AXES["Z"]
    orient = Quaternion(AXES["Y"], -math.atan2(forward.x, forward.z))
    duration = (len(source["frames"]) - 2) / source["fps"]
    if limit_seconds is not None:
        duration = min(duration, limit_seconds)
    count = int(duration * fps) + 1
    frames = []
    for sample in range(count):
        # Source 120 Hz, output 60 Hz: sample original integer frames, exclude synthetic T pose.
        source_frame = min(1 + sample * 2, len(source["frames"]) - 1)
        sp, sq = bvh_world(source, source["frames"][source_frame])
        globals_q = [IDENTITY.copy()]
        for _, _, name in JOINTS[1:]:
            globals_q.append((orient @ sq[names[name]] @ refq[names[name]].inverted()).normalized())
        hip_forward = globals_q[1] @ AXES["Z"]
        globals_q[0] = Quaternion(AXES["Y"], math.atan2(hip_forward.x, hip_forward.z))
        localq = [globals_q[i] if parent < 0 else globals_q[parent].inverted() @ globals_q[i]
                  for i, (_, parent, _) in enumerate(JOINTS)]
        localp = [o.copy() for o in offsets]
        displacement = orient @ ((sp[0] - firstp[0]) * scale)
        localp[0] = Vector((displacement.x, 0, displacement.z))
        localp[1] = Vector((0, offsets[1].y + (sp[0].y - refp[0].y) * scale, 0))
        frames.append((localp, localq))
    return frames, fps


def foot_bottom(position, rotation, presentation):
    radii = Vector((presentation["foot_half_width"], presentation["foot_half_height"], presentation["foot_half_length"]))
    center = position + rotation @ Vector((0, presentation["foot_center_y"], presentation["foot_center_z"]))
    down = rotation.inverted() @ Vector((0, -1, 0))
    denominator = math.sqrt(sum((radii[i] * down[i]) ** 2 for i in range(3)))
    support = Vector(tuple(radii[i] ** 2 * down[i] / denominator for i in range(3)))
    return center + rotation @ support


def ground_clip(frames, offsets, presentation):
    # Align after trimming and loop sealing, using the visible foot rather than the ankle joint.
    floor = min(foot_bottom(wp[j], wq[j], presentation).y
                for positions, rotations in frames for wp, wq in [fk(offsets, positions, rotations)] for j in (18, 22))
    for positions, _ in frames:
        positions[1].y -= floor
    return floor


def smooth(t):
    t = max(0., min(1., t))
    return t * t * (3 - 2 * t)


def authored_celebration(idle, offsets):
    """Authored gestures are explicitly labelled, not passed off as captured performance."""
    frames = []
    names = {name: i for i, (name, _, _) in enumerate(JOINTS)}
    keys = [
        (0., {}), (.4, {"chest": (0, 0, -5), "upperarm_l": (0, 0, -78), "upperarm_r": (0, 0, 78)}),
        (1.1, {"upperarm_l": (0, -12, 48), "upperarm_r": (0, 12, -48), "forearm_l": (0, 0, 18), "forearm_r": (0, 0, -18), "chest": (-8, 0, 0)}),
        (1.65, {"upperarm_l": (0, -12, 28), "upperarm_r": (0, 12, -35), "chest": (-6, 0, 5)}),
        (2.2, {"upperarm_l": (0, -12, 48), "upperarm_r": (0, 12, -48), "chest": (-8, 0, -5)}),
        (3.0, {}), (3.6, {}),
    ]
    basep, baseq = idle[0]
    for frame in range(217):
        time = frame / 60
        p, q = [x.copy() for x in basep], [x.copy() for x in baseq]
        p[0] = Vector((0, 0, 0)); q[0] = IDENTITY.copy()
        a, b = next(((a, b) for a, b in zip(keys, keys[1:]) if a[0] <= time <= b[0]), (keys[-2], keys[-1]))
        weight = smooth((time - a[0]) / (b[0] - a[0]))
        for name in set(a[1]) | set(b[1]):
            idx = names[name]
            def keyrot(key):
                if name not in key[1]:
                    return baseq[idx]
                from mathutils import Euler
                return Euler([math.radians(v) for v in key[1][name]], "XYZ").to_quaternion()
            q[idx] = keyrot(a).slerp(keyrot(b), weight)
        frames.append((p, q))
    return frames, 60


def gltf_export(path, offsets, clips):
    binary = bytearray()
    doc = {"asset": {"version": "2.0", "generator": "Besktop animation foundation v1", "copyright": "CMU Graphics Lab / Bruce Hahne; authored celebration: Besktop"},
           "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [], "bufferViews": [], "accessors": [], "animations": []}
    def accessor(rows, kind):
        start = len(binary)
        for row in rows:
            binary.extend(struct.pack("<" + "f" * len(row), *row))
        view = len(doc["bufferViews"])
        doc["bufferViews"].append({"buffer": 0, "byteOffset": start, "byteLength": len(binary) - start})
        value = {"bufferView": view, "componentType": 5126, "count": len(rows), "type": kind}
        if kind == "SCALAR":
            value.update(min=[rows[0][0]], max=[rows[-1][0]])
        doc["accessors"].append(value)
        return len(doc["accessors"]) - 1
    for i, (name, parent, _) in enumerate(JOINTS):
        node = {"name": name, "translation": list(offsets[i])}
        children = [j for j, (_, p, _) in enumerate(JOINTS) if p == i]
        if children:
            node["children"] = children
        doc["nodes"].append(node)
    for name, (frames, fps, metadata) in clips.items():
        time_accessor = accessor([[i / fps] for i in range(len(frames))], "SCALAR")
        animation = {"name": name, "channels": [], "samplers": [], "extras": metadata}
        for joint in range(len(JOINTS)):
            for prop in ("rotation", "translation"):
                if prop == "translation" and joint > 1:
                    continue
                rows = []
                previous = None
                for p, q in frames:
                    if prop == "rotation":
                        value = q[joint].normalized()
                        if previous is not None and previous.dot(value) < 0:
                            value.negate()
                        previous = value
                        rows.append([value.x, value.y, value.z, value.w])
                    else:
                        rows.append(list(p[joint]))
                out = accessor(rows, "VEC4" if prop == "rotation" else "VEC3")
                sampler = len(animation["samplers"])
                animation["samplers"].append({"input": time_accessor, "output": out, "interpolation": "LINEAR"})
                animation["channels"].append({"sampler": sampler, "target": {"node": joint, "path": prop}})
        doc["animations"].append(animation)
    doc["buffers"] = [{"byteLength": len(binary), "uri": "data:application/octet-stream;base64," + base64.b64encode(binary).decode()}]
    write_json(path, doc)


def build_blender(offsets, clips, output, presentation):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    # The scene uses Blender's Z-up, the exported glTF/runtime stays right-handed Y-up.
    convert = Matrix.Rotation(math.pi / 2, 4, "X")
    rest, _ = fk(offsets, offsets, [IDENTITY.copy() for _ in JOINTS])
    armature = bpy.data.armatures.new("BesktopHumanoid_v1")
    rig = bpy.data.objects.new("BesktopHumanoid_v1", armature)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    for i, (name, parent, _) in enumerate(JOINTS):
        bone = armature.edit_bones.new(name)
        bone.head = convert @ rest[i]
        bone.tail = bone.head + Vector((0, 0, .08))
        if parent >= 0:
            bone.parent = armature.edit_bones[JOINTS[parent][0]]
        bone.use_connect = False
    bpy.ops.object.mode_set(mode="OBJECT")
    rig.show_in_front = True
    rig["skeleton_id"] = "besktop.humanoid.v1"
    rig["notice"] = "CMU mocap + Besktop retargeting; G1 human approval pending"
    rest_matrices = [armature.bones[n].matrix_local.copy() for n, _, _ in JOINTS]
    for name, (frames, fps, _) in clips.items():
        action = bpy.data.actions.new(name)
        action.use_fake_user = True
        rig.animation_data_create()
        rig.animation_data.action = action
        for frame, (positions, rotations) in enumerate(frames):
            wp, wq = fk(offsets, positions, rotations)
            for i, (joint, _, _) in enumerate(JOINTS):
                # Same world-space matrices as runtime FK, including each bone's Blender rest orientation.
                pose = rig.pose.bones[joint]
                pose.rotation_mode = "QUATERNION"
                rotation = convert @ wq[i].to_matrix().to_4x4() @ convert.inverted()
                pose.matrix = Matrix.Translation(convert @ wp[i]) @ rotation @ rest_matrices[i].to_3x3().to_4x4()
                bpy.context.view_layer.update()
                pose.keyframe_insert("location", frame=frame + 1)
                pose.keyframe_insert("rotation_quaternion", frame=frame + 1)
        if hasattr(action, "fcurves"):
            for curve in action.fcurves:
                for key in curve.keyframe_points:
                    key.interpolation = "LINEAR"

    def material(name, color):
        m = bpy.data.materials.new(name)
        m.diffuse_color = (*color, 1)
        return m
    blue = material("Body - glacier blue", (.055, .42, .7))
    light = material("Joints - porcelain", (.78, .91, .97))
    dark = material("Floor - slate", (.045, .07, .11))
    def bind(obj, joint, center):
        group = obj.vertex_groups.new(name=joint)
        group.add(list(range(len(obj.data.vertices))), 1, "REPLACE")
        modifier = obj.modifiers.new("Rigid bone binding", "ARMATURE")
        modifier.object = rig
        obj.data.materials.append(blue)
    for i, (name, parent, _) in enumerate(JOINTS):
        if i == 0:
            continue
        point = convert @ rest[i]
        radius = presentation["head_radius"] if name == "head" else presentation["joint_radius"]
        if name.startswith(("hand", "foot")):
            radius *= .7
        if name not in ("pelvis", "spine_low", "spine", "chest", "neck_base", "clavicle_l", "clavicle_r", "toe_l", "toe_r"):
            bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, radius=radius, location=point)
            sphere = bpy.context.object
            sphere.name = "Joint_" + name
            bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
            bind(sphere, name, point)
            sphere.data.materials.clear(); sphere.data.materials.append(light if name != "head" else blue)
            for polygon in sphere.data.polygons:
                polygon.use_smooth = True
        if parent <= 0 or (rest[i] - rest[parent]).length < .015 or name in ("spine", "chest", "thigh_l", "thigh_r", "toe_l", "toe_r"):
            continue
        a, b = convert @ rest[parent], point
        radius = presentation["torso_radius"] if name in ("spine", "chest") else presentation["limb_radius"]
        bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=radius, depth=(b - a).length, location=(a + b) / 2)
        limb = bpy.context.object
        limb.name = "Limb_" + name
        limb.rotation_mode = "QUATERNION"
        limb.rotation_quaternion = (b - a).to_track_quat("Z", "Y")
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        bind(limb, JOINTS[parent][0], (a + b) / 2)
        for polygon in limb.data.polygons:
            polygon.use_smooth = True
    def ellipsoid(name, joint, center, radii, rotation=IDENTITY):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=12, radius=1, location=convert @ center)
        obj = bpy.context.object
        obj.name = name
        obj.rotation_mode = "QUATERNION"
        obj.rotation_quaternion = (convert.to_quaternion() @ rotation).normalized()
        obj.scale = radii
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        bind(obj, joint, center)
        for polygon in obj.data.polygons:
            polygon.use_smooth = True
    for a, b, label, prefix in ((3, 4, "Chest", "chest"), (1, 3, "Abdomen", "waist")):
        span = rest[b] - rest[a]
        ellipsoid(label, JOINTS[a][0], (rest[a] + rest[b]) / 2,
                  (presentation[prefix + "_half_width"], span.length * .65, presentation[prefix + "_half_depth"]),
                  Vector((0, 1, 0)).rotation_difference(span))
    ellipsoid("Pelvis", "pelvis", rest[1] + Vector((0, presentation["pelvis_center_y"], 0)),
              tuple(presentation["pelvis_half_" + axis] for axis in ("width", "height", "depth")))
    for foot, hand, sign in ((18, 11, 1), (22, 15, -1)):
        ellipsoid("Foot_" + JOINTS[foot][0], JOINTS[foot][0], rest[foot] + Vector((0, presentation["foot_center_y"], presentation["foot_center_z"])),
                  tuple(presentation["foot_half_" + axis] for axis in ("width", "height", "length")))
        ellipsoid("Palm_" + JOINTS[hand][0], JOINTS[hand][0], rest[hand] + Vector((sign * presentation["palm_center_offset"], 0, 0)),
                  tuple(presentation["palm_half_" + axis] for axis in ("length", "width", "thickness")))
    bpy.ops.mesh.primitive_plane_add(size=200)
    bpy.context.object.name = "Ground"
    bpy.context.object.data.materials.append(dark)
    # Thin grid geometry provides stable depth/foot-contact cues in rendered previews.
    for axis in range(2):
        for i in range(-20, 21):
            bpy.ops.mesh.primitive_cube_add(size=1, location=(i * .25 if axis == 0 else 0, 0 if axis == 0 else i * .25, -.002))
            grid = bpy.context.object
            grid.location.z = .002
            grid.scale = (.002, 5, .002) if axis == 0 else (5, .002, .002)
            grid.data.materials.append(light)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.background_type = "WORLD"
    scene.world.color = (.025, .04, .065)
    scene.render.resolution_x = 640
    scene.render.resolution_y = 640
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.fps = 60
    rig.animation_data.action = bpy.data.actions["walk"]
    scene.frame_start = 1
    scene.frame_end = len(clips["walk"][0])
    scene.frame_set(40)
    bpy.context.view_layer.update()
    wp, _ = fk(offsets, *clips["walk"][0][39])
    blender_error = max((rig.pose.bones[name].head - convert @ wp[i]).length for i, (name, _, _) in enumerate(JOINTS))
    write_json(output / "blender-validation.json", {"evaluated_frame": 40, "clip": "walk", "max_joint_error_meters": blender_error, "tolerance_meters": .0001})
    if blender_error > .0001:
        raise ValueError(f"Blender evaluated bone heads differ from exported FK: {blender_error}")
    cameras = {"front": (0, -5, 1.35), "side": (5, 0, 1.35), "back": (0, 5, 1.35), "three_quarter": (3.8, -4, 2.2)}
    for name, location in cameras.items():
        bpy.ops.object.camera_add(location=location)
        camera = bpy.context.object
        camera.name = "Camera_" + name
        camera.rotation_euler = (Vector((0, 0, .85)) - camera.location).to_track_quat("-Z", "Y").to_euler()
        camera.data.type = "ORTHO"; camera.data.ortho_scale = 2.25
    scene.camera = bpy.data.objects["Camera_three_quarter"]
    bpy.ops.object.select_all(action="DESELECT")
    rig.select_set(True); bpy.context.view_layer.objects.active = rig
    bpy.ops.wm.save_as_mainfile(filepath=str(output / "G1-stick-figure.blend"))
    # Four views at identical time; translate cameras by horizontal root displacement.
    root = convert @ clips["walk"][0][39][0][0]
    for name in cameras:
        scene.camera = bpy.data.objects["Camera_" + name]
        scene.camera.location += root
        scene.render.filepath = str(output / ("G1-" + name + ".png"))
        bpy.ops.render.render(write_still=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--skip-blend", action="store_true")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    args.source = args.source.resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    clip_sources = json.loads((Path(__file__).parent / "clip_sources.json").read_text(encoding="utf-8"))
    profile = json.loads((Path(__file__).parent / "rig_profile.json").read_text(encoding="utf-8"))
    reference = load_reference(Path(__file__).parent / "anthropometry_reference.json")
    proportion_audit = audit_profile(reference, profile)
    proportion_audit["profile_sha256"] = hashlib.sha256((Path(__file__).parent / "rig_profile.json").read_bytes()).hexdigest()
    write_json(args.output / "anthropometry-audit.json", proportion_audit)
    names = sorted({entry["source"] for entry in clip_sources["clips"]} | {clip_sources["reference_source"]})
    sources = {name: read_bvh(args.source / (name + ".bvh")) for name in names}
    offsets, scale = make_reference(sources[clip_sources["reference_source"]], profile)
    clips = {}
    for entry in clip_sources["clips"]:
        name, source = entry["name"], entry["source"]
        if name in clips or not name.replace("_", "").isalnum():
            raise ValueError("Duplicate or invalid clip name: " + name)
        frames, fps = retarget(sources[source], offsets, limit_seconds=entry.get("limit_seconds"))
        if "trim_seconds" in entry:
            start_time, end_time = entry["trim_seconds"]
            first, last = round(start_time * fps), round(end_time * fps)
            if first < 0 or last >= len(frames) or last <= first:
                raise ValueError("Invalid clip trim: " + name)
            frames = frames[first:last + 1]
            # Each trimmed take starts at a canonical origin/heading; playback applies placement once.
            origin, rotation = frames[0][0][0].copy(), frames[0][1][0].inverted()
            for positions, rotations in frames:
                positions[0] = rotation @ (positions[0] - origin)
                rotations[0] = rotation @ rotations[0]
        clips[name] = (frames, fps, {"origin": "CMU " + source, "description": entry["description"], "source_trim_seconds": entry.get("trim_seconds"), "loop": entry.get("loop", False), "root_motion": "single-root-track", "contact_correction": "runtime optional"})
    clips["walk_source"] = clips["walk"]
    walk = clips["walk"][0]
    # Pick a full gait cycle by pose similarity; keep the original take for comparison.
    def seam_cost(a, b):
        return sum((1 - abs(walk[a][1][j].dot(walk[b][1][j]))) for j in (1, 4, 9, 10, 13, 14, 16, 17, 18, 20, 21, 22))
    a, b = min(((a, b) for a in range(8, 48) for b in range(a + 55, min(a + 86, len(walk)))), key=lambda ab: seam_cost(*ab))
    cycle = [([v.copy() for v in p], [q.copy() for q in r]) for p, r in walk[a:b + 1]]
    start = cycle[0][0][0].copy()
    for p, _ in cycle:
        p[0] -= start
    # Seal the local pose over the final 100 ms; horizontal displacement/yaw remain recorded.
    for i in range(max(0, len(cycle) - 7), len(cycle)):
        w = smooth((i - (len(cycle) - 7)) / 6)
        for j in range(1, len(JOINTS)):
            cycle[i][1][j] = cycle[i][1][j].slerp(cycle[0][1][j], w)
        cycle[i][0][1] = cycle[i][0][1].lerp(cycle[0][0][1], w)
    clips["walk"] = (cycle, 60, {**clips["walk"][2], "loop": True, "source_frames_60hz": [a, b], "seam_cost_before": seam_cost(a, b), "tail_blend_seconds": .1})
    clips["idle"][2]["loop"] = True
    idle = clips["idle"][0]
    for i in range(len(idle) - 25, len(idle)):
        w = smooth((i - (len(idle) - 25)) / 24)
        for j in range(len(JOINTS)):
            idle[i][1][j] = idle[i][1][j].slerp(idle[0][1][j], w)
            idle[i][0][j] = idle[i][0][j].lerp(idle[0][0][j], w)
    frames, fps = authored_celebration(clips["idle"][0], offsets)
    clips["celebrate"] = (frames, fps, {"origin": "Besktop authored prototype; NOT motion capture", "loop": False, "root_motion": "single-root-track"})
    for frames, _, meta in clips.values():
        meta["ground_offset_meters"] = -ground_clip(frames, offsets, profile["presentation"])
    gltf_export(args.output / "foundation.gltf", offsets, clips)
    skeleton = {"id": "besktop.humanoid.v1", "version": 1, "units": "meters", "handedness": "right", "up": "+Y", "forward": "+Z", "quaternion": "xyzw", "joint_count": len(JOINTS),
                "reference": "CMU 08_01 orientation basis; v4 leg-span allocation from pinned OpenSim neutral joint frames; other dimensions engineering choices", "cmu_unit_meters": CMU_UNIT, "reference_scale": scale,
                "joint_reference": reference["id"], "joint_reference_commit": reference["source"]["commit"],
                "proportion_profile": profile["id"], "presentation": profile["presentation"],
                "joints": [{"name": n, "parent": p, "source": s, "offset": list(offsets[i])} for i, (n, p, s) in enumerate(JOINTS)]}
    write_json(args.output / "skeleton.json", skeleton)
    manifest = {"skeleton": skeleton["id"], "clips": []}
    golden = {"skeleton": skeleton["id"], "samples": []}
    for name, (frames, fps, meta) in clips.items():
        # Golden FK reference is sampled independently in Blender, then compared against ozz C++.
        for frame in sorted({0, len(frames) // 3, len(frames) // 2, len(frames) - 1}):
            wp, _ = fk(offsets, *frames[frame])
            golden["samples"].append({"clip": name, "time": frame / fps, "positions": [list(p) for p in wp]})
        contacts = []
        worlds = [fk(offsets, *f)[0] for f in frames]
        for joint in (18, 22):
            active = None
            foot_floor = min(world[joint].y for world in worlds)
            for f, world in enumerate(worlds):
                velocity = Vector((0, 0, 0)) if f == 0 else (world[joint] - worlds[f - 1][joint]) * fps
                speed = math.hypot(velocity.x, velocity.z)
                touching = world[joint].y < foot_floor + .065 and speed < .5 and abs(velocity.y) < .25
                if touching and active is None:
                    active = f
                if active is not None and (not touching or f == len(worlds) - 1):
                    if f - active >= 4:
                        contacts.append({"joint": JOINTS[joint][0], "start": active / fps, "end": f / fps})
                    active = None
        manifest["clips"].append({"name": name, "file": name + ".ozz", "duration": (len(frames) - 1) / fps, "fps": fps, "loop": False, "next": "idle", "blend_seconds": .3, "contacts": contacts, **meta})
    write_json(args.output / "manifest.json", manifest)
    write_json(args.output / "golden.json", golden)
    write_json(args.output / "provenance.json", {"blender": bpy.app.version_string, "source": "https://mocap.cs.cmu.edu/", "conversion": "cgspeed MotionBuilder 2010", "files": [{"name": s["path"].name, "sha256": hashlib.sha256(s["path"].read_bytes()).hexdigest(), "frames": len(s["frames"]), "fps": 120} for s in sources.values()]})
    if not args.skip_blend:
        build_blender(offsets, clips, args.output, profile["presentation"])
    print("BESKTOP_ASSETS_READY", args.output)


if __name__ == "__main__":
    main()
