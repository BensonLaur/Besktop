"""Reproduce a limited joint-span adaptation; never equate it with population anthropometry.

The small endpoint snapshot records numerical facts only. Neither the OpenSim model,
its geometry, nor a copyrighted standard PDF is required or redistributed by the build.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def segment_lengths(reference):
    if reference["units"] != "meters":
        raise ValueError("Reference must use meters")
    lengths = {}
    for name in ("thigh_l", "thigh_r", "shin_l", "shin_r"):
        segment = reference["segments"][name]
        for key in ("proximal", "distal"):
            if len(segment[key]) != 3 or not all(math.isfinite(v) for v in segment[key]):
                raise ValueError("Invalid endpoint: " + name)
        lengths[name] = math.dist(segment["proximal"], segment["distal"])
        if not 0.1 < lengths[name] < 1.0:
            raise ValueError("Invalid segment length: " + name)
    return lengths


def audit_profile(reference, profile):
    lengths = segment_lengths(reference)
    thigh = (lengths["thigh_l"] + lengths["thigh_r"]) / 2
    shin = (lengths["shin_l"] + lengths["shin_r"]) / 2
    span = reference["adaptation"]["target_leg_span_m"]
    if not math.isfinite(span) or not 0.5 < span < 1.3:
        raise ValueError("Invalid engineering target leg span")
    scale = span / (thigh + shin)
    derived = {"thigh_length": thigh * scale, "shin_length": shin * scale}
    if profile.get("joint_reference") != reference["id"]:
        raise ValueError("Profile/reference version mismatch")
    for key, expected in derived.items():
        if not math.isfinite(profile[key]) or abs(profile[key] - expected) > 1e-8:
            raise ValueError(f"{key} drift: expected {expected:.9f}, got {profile[key]}")
    return {"schema_version": 1, "profile": profile["id"], "reference": reference,
            "source_segment_lengths_m": lengths, "uniform_leg_scale": scale,
            "source_thigh_shin_ratio": thigh / shin, "derived_profile_m": derived,
            "not_population_average": True,
            "unmodified_engineering_fields": sorted(k for k in profile if k not in
                ("id", "joint_reference", "thigh_length", "shin_length"))}


def verify_osim(reference, path):
    """Optional independent extraction from the pinned original XML, not needed at runtime."""
    raw = Path(path).read_bytes()
    if hashlib.sha256(raw).hexdigest() != reference["source"]["sha256"]:
        raise ValueError("OpenSim source checksum mismatch")
    joints = ET.fromstring(raw).find("Model/JointSet/objects")

    def joint(name):
        return next(j for j in joints if j.get("name") == name)

    def endpoint(j, body):
        frame = next(f for f in j.findall("frames/PhysicalOffsetFrame")
                     if f.findtext("socket_parent") == "/bodyset/" + body)
        return [float(v) for v in frame.findtext("translation").split()]

    for side in ("l", "r"):
        hip, knee, ankle = (joint(n + side) for n in ("hip_", "walker_knee_", "ankle_"))
        # A moving knee has no universal fixed center. Only the explicit q=0 origins
        # below are adopted; we do not import its angle-dependent rolling/sliding model.
        for axis in knee.findall("SpatialTransform/TransformAxis"):
            spline = axis.find("SimmSpline")
            if spline is not None:
                if float(spline.findtext("x").split()[0]) != 0 or float(spline.findtext("y").split()[0]) != 0:
                    raise ValueError("Unsupported nonzero neutral knee transform")
        pairs = (("thigh_", hip, knee, "femur_"), ("shin_", knee, ankle, "tibia_"))
        for name, proximal, distal, body in pairs:
            record = reference["segments"][name + side]
            if record["body"] != body + side:
                raise ValueError("Reference body mismatch")
            for label, j in (("proximal", proximal), ("distal", distal)):
                if math.dist(endpoint(j, body + side), record[label]) > 1e-12:
                    raise ValueError("Snapshot/source endpoint mismatch: " + name + side)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify-osim", type=Path)
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent
    reference = load(directory / "anthropometry_reference.json")
    profile = load(directory / "rig_profile.json")
    audit = audit_profile(reference, profile)
    if args.verify_osim:
        verify_osim(reference, args.verify_osim)
        print("Pinned OpenSim XML endpoints verified")
    print(json.dumps(audit["derived_profile_m"], indent=2))
    print("Joint-span ratio:", audit["source_thigh_shin_ratio"])


if __name__ == "__main__":
    main()
