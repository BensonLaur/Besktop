#include "besktop/anim3d/runtime.h"
#include "besktop/core/json_value.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace besktop::anim3d;
namespace
{
int checks = 0;
void Require(bool condition, const std::string &message)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
const besktop::JsonValue &Field(const besktop::JsonValue &o, const char *key)
{
    const auto *v = o.Find(key);
    if (!v)
        throw std::runtime_error(key);
    return *v;
}
Vec3 Position(const besktop::JsonValue &a)
{
    return {static_cast<float>(a.arrayValue[0].numberValue), static_cast<float>(a.arrayValue[1].numberValue),
            static_cast<float>(a.arrayValue[2].numberValue)};
}
Vec3 LocalNeckDirection(const Library &library, const Pose &pose, bool upper)
{
    const int base = library.Joint(upper ? "neck" : "neck_base"), tip = library.Joint(upper ? "head" : "neck");
    return Normal(pose.world[0].rotation.Inverse().Rotate(pose.world[tip].position - pose.world[base].position));
}
float FootHalfExtentZ(const BodyEllipsoid &foot)
{
    const auto &q = foot.transform.rotation;
    const float x = q.Rotate({foot.radii.x, 0, 0}).z, y = q.Rotate({0, foot.radii.y, 0}).z,
                z = q.Rotate({0, 0, foot.radii.z}).z;
    return std::sqrt(x * x + y * y + z * z);
}
void BoneLengths(const Library &lib, const Pose &p, float scale = 1)
{
    for (size_t j = 2; j < p.world.size(); ++j)
    {
        const float expected = Length(lib.Rest().local[j].position) * scale;
        const float actual = Length(p.world[j].position - p.world[lib.Parents()[j]].position);
        Require(std::abs(expected - actual) < .0002f, "Bone length changed at " + lib.Names()[j]);
        Require(Finite(p.world[j].position), "Non-finite pose");
    }
}
float ArmTorsoClearance(const Library &lib, const Pose &actor, const Pose &partner, float actorScale,
                        float partnerScale)
{
    float clearance = 100;
    for (const char *side : {"_l", "_r"})
        for (const char *bone : {"forearm", "hand"})
        {
            const int tip = lib.Joint(std::string(bone) + side), parent = lib.Parents()[tip];
            // Sample the rendered arm capsules against the partner's visible torso ellipsoids.
            for (int i = 0; i <= 20; ++i)
            {
                const Vec3 p = Lerp(actor.world[parent].position, actor.world[tip].position, i / 20.f);
                for (const auto &shape : TorsoShapes(lib, partner, partnerScale))
                {
                    const Vec3 local = shape.transform.rotation.Inverse().Rotate(p - shape.transform.position);
                    const Vec3 r = shape.radii;
                    const float normalized = Length({local.x / r.x, local.y / r.y, local.z / r.z});
                    float distance = (normalized - 1) * std::min({r.x, r.y, r.z});
                    if (normalized >= 1)
                    {
                        // Closest point on the actual ellipsoid; a bounding capsule falsely rejects wrapping arms.
                        auto equation = [&](float t) {
                            return Length({r.x * local.x / (t + r.x * r.x), r.y * local.y / (t + r.y * r.y),
                                           r.z * local.z / (t + r.z * r.z)});
                        };
                        float lo = 0, hi = .1f;
                        while (equation(hi) > 1)
                            hi *= 2;
                        for (int iteration = 0; iteration < 24; ++iteration)
                        {
                            const float middle = (lo + hi) * .5f;
                            if (equation(middle) > 1)
                                lo = middle;
                            else
                                hi = middle;
                        }
                        const Vec3 closest{r.x * r.x * local.x / (hi + r.x * r.x),
                                           r.y * r.y * local.y / (hi + r.y * r.y),
                                           r.z * r.z * local.z / (hi + r.z * r.z)};
                        distance = Length(local - closest);
                    }
                    clearance = std::min(clearance, distance - lib.Presentation().limbRadius * actorScale);
                }
            }
        }
    return clearance;
}
} // namespace
int main(int argc, char **argv)
{
    try
    {
        Require(argc == 2, "Usage: anim3d_tests ASSET_DIRECTORY");
        const std::filesystem::path directory = argv[1];
        Library lib;
        lib.Load(directory);
        Require(lib.Names().size() == 24, "24-joint contract");
        const auto &rest = lib.Rest();
        // Joint-frame spans are not CT femur/tibia bone lengths. Independently pin
        // the neutral reference ratio so exporter and runtime cannot drift together.
        for (const char *side : {"_l", "_r"})
        {
            const float thigh = Length(rest.local[lib.Joint(std::string("shin") + side)].position);
            const float shin = Length(rest.local[lib.Joint(std::string("foot") + side)].position);
            Require(std::abs(thigh / shin - 1.02919972317514f) < .00001f, "Reference leg ratio drift");
            Require(std::abs(thigh + shin - .87f) < .00001f, "Engineering leg span drift");
        }
        const int head = lib.Joint("head"), chest = lib.Joint("chest");
        const int shoulders[] = {lib.Joint("upperarm_l"), lib.Joint("upperarm_r")};
        const float floor =
            std::min(FootBottom(lib, rest, lib.Joint("foot_l")).y, FootBottom(lib, rest, lib.Joint("foot_r")).y);
        Require(std::abs(rest.world[head].position.y + lib.Presentation().headRadius - floor - 1.75f) < .002f,
                "Reference height must include the rendered head crown");
        for (int shoulder : shoulders)
            Require(std::abs(rest.world[shoulder].position.y - rest.world[chest].position.y) < .03f,
                    "Reference shoulder girdle rises too steeply from chest");
        for (const char *joint : {"forearm", "hand", "thigh", "shin", "foot", "toe"})
            Require(std::abs(Length(rest.local[lib.Joint(std::string(joint) + "_l")].position) -
                             Length(rest.local[lib.Joint(std::string(joint) + "_r")].position)) < .0001f,
                    std::string("Asymmetric reference bone: ") + joint);
        Require(Length(rest.local[lib.Joint("hand_l")].position) / Length(rest.local[lib.Joint("forearm_l")].position) >
                    .8f,
                "Forearm below prototype reach constraint (not a population anthropometric limit)");
        float minNeckClearance = 100;
        for (const char *clipName : {"idle", "walk", "walk_source"})
        {
            const int clip = lib.FindClip(clipName);
            for (int frame = 0; frame <= 200; ++frame)
            {
                Pose pose;
                lib.Sample(clip, lib.Clips()[clip].duration * frame / 200, pose);
                const Vec3 bodyUp = pose.world[chest].rotation.Rotate({0, 1, 0});
                for (int shoulder : shoulders)
                    minNeckClearance = std::min(minNeckClearance,
                                                Dot(pose.world[head].position - pose.world[shoulder].position, bodyUp) -
                                                    lib.Presentation().headRadius - lib.Presentation().jointRadius);
            }
        }
        std::cout << "Relaxed shoulder/head surface vertical clearance (m): " << minNeckClearance << "\n";
        // Only relaxed takes are checked: overhead gestures must retain real shoulder elevation.
        Require(minNeckClearance > .025f, "Relaxed shoulders crowd the rendered chin");
        float overheadReach = 0;
        const int celebrate = lib.FindClip("celebrate");
        for (int frame = 0; frame <= 120; ++frame)
        {
            Pose pose;
            lib.Sample(celebrate, lib.Clips()[celebrate].duration * frame / 120, pose);
            for (const char *hand : {"hand_l", "hand_r"})
                overheadReach =
                    std::max(overheadReach, pose.world[lib.Joint(hand)].position.y - pose.world[head].position.y);
        }
        Require(overheadReach > .1f, "Proportion calibration restricted overhead gestures");
        std::ifstream goldenFile(directory / "golden.json");
        std::ostringstream contents;
        contents << goldenFile.rdbuf();
        const auto golden = besktop::ParseJson(contents.str());
        float maxError = 0;
        for (const auto &sample : Field(golden, "samples").arrayValue)
        {
            Pose p;
            lib.Sample(lib.FindClip(Field(sample, "clip").stringValue),
                       static_cast<float>(Field(sample, "time").numberValue), p);
            const auto &expected = Field(sample, "positions").arrayValue;
            for (size_t j = 0; j < expected.size(); ++j)
                maxError = std::max(maxError, Length(p.world[j].position - Position(expected[j])));
        }
        std::cout << "Blender/glTF -> ozz FK max error (m): " << maxError << "\n";
        Require(maxError < .002f, "Runtime does not match Blender golden FK");
        for (size_t c = 0; c < lib.Clips().size(); ++c)
            for (int frame = 0; frame <= 200; ++frame)
            {
                Pose p;
                lib.Sample(static_cast<int>(c), lib.Clips()[c].duration * frame / 200, p);
                BoneLengths(lib, p);
            }
        Pose start, end, boundary;
        const int walk = lib.FindClip("walk");
        lib.Sample(walk, -10, start);
        lib.Sample(walk, 0, boundary);
        Require(Length(start.world[11].position - boundary.world[11].position) < 1e-5f, "Negative sample must clamp");
        lib.Sample(walk, 999, end);
        lib.Sample(walk, lib.Clips()[walk].duration, boundary);
        Require(Length(end.world[0].position - boundary.world[0].position) < 1e-5f, "End sample must clamp");
        Player player(lib);
        player.Select(walk, false);
        player.SetPlacement({});
        player.SetCorrection(false);
        player.SetAutoNext(false);
        const float duration = lib.Clips()[walk].duration;
        for (int i = 0; i < static_cast<int>(duration * 60) + 1; ++i)
            player.Update(1 / 60.f);
        Pose wrap;
        lib.Sample(walk, player.Time(), wrap);
        const Transform expected = end.local[0] * start.local[0].Inverse() * wrap.local[0];
        Require(Length(player.Raw().world[0].position - expected.position) < .002f,
                "Root motion must apply exactly once over a loop");
        const Vec3 beforeTurn = player.Raw().world[0].position;
        player.TurnBy(Pi * .5f);
        Require(Length(beforeTurn - player.Raw().world[0].position) < .0001f,
                "Scene steering must not teleport the root");
        BoneLengths(lib, player.Current());
        for (size_t c = 0; c < lib.Clips().size(); ++c)
        {
            const Pose before = player.Current();
            player.Select(static_cast<int>(c), true);
            Require(Length(player.Current().world[11].position - before.world[11].position) < .0001f,
                    "Transition first frame jumped");
            for (int i = 0; i < 50; ++i)
            {
                player.Update(1 / 60.f);
                BoneLengths(lib, player.Current());
            }
        }
        float lockedDrift = 0, rawDrift = 0, soleHeightError = 0;
        int contactSamples = 0;
        player.Select(walk, false);
        player.SetPlacement({});
        player.SetCorrection(true);
        for (const auto &contact : lib.Clips()[walk].contacts)
        {
            if (contact.end - contact.start < .16f)
                continue;
            player.Seek(contact.start + .06f);
            const Vec3 anchor = FootBottom(lib, player.Current(), contact.joint);
            const Vec3 rawAnchor = FootBottom(lib, player.Raw(), contact.joint);
            for (float t = contact.start + .075f; t < contact.end - .06f; t += 1 / 120.f)
            {
                player.Seek(t);
                const Vec3 sole = FootBottom(lib, player.Current(), contact.joint);
                lockedDrift = std::max(lockedDrift, Length(sole - anchor));
                rawDrift = std::max(rawDrift, Length(FootBottom(lib, player.Raw(), contact.joint) - rawAnchor));
                soleHeightError = std::max(soleHeightError, std::abs(sole.y));
                const Quat a = player.Current().world[contact.joint].rotation,
                           b = player.Raw().world[contact.joint].rotation;
                Require(std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) > .99999f,
                        "Grounding removed captured foot roll");
                ++contactSamples;
            }
        }
        std::cout << "Support-foot drift corrected/raw (m): " << lockedDrift << " / " << rawDrift
                  << " samples=" << contactSamples << "\n";
        Require(contactSamples > 0, "Need measurable stance interval");
        Require(lockedDrift < .008f, "Support foot still drifts");
        std::cout << "Planted visible sole height error (m): " << soleHeightError << "\n";
        Require(soleHeightError < .003f, "Planted visible sole floats above or penetrates floor");
        float minSoleHeight = 100, kneeForward = 100;
        for (const char *name : {"idle", "walk", "stop", "turn", "celebrate"})
        {
            const int clip = lib.FindClip(name);
            player.Select(clip, false);
            player.SetPlacement({});
            for (int frame = 0; frame <= 240; ++frame)
            {
                player.Seek(lib.Clips()[clip].duration * frame / 240);
                const auto &pose = player.Current();
                for (const char *side : {"_l", "_r"})
                {
                    const int hip = lib.Joint(std::string("thigh") + side),
                              knee = lib.Joint(std::string("shin") + side),
                              foot = lib.Joint(std::string("foot") + side);
                    minSoleHeight = std::min(minSoleHeight, FootBottom(lib, pose, foot).y);
                    if (std::string(name) == "idle" || std::string(name) == "walk")
                    {
                        const Vec3 a = pose.world[hip].position, b = pose.world[foot].position, axis = b - a;
                        const Vec3 onAxis =
                            a + axis * std::clamp(Dot(pose.world[knee].position - a, axis) / Dot(axis, axis), 0.f, 1.f);
                        kneeForward = std::min(kneeForward, Dot(pose.world[knee].position - onAxis,
                                                                pose.world[0].rotation.Rotate({0, 0, 1})));
                    }
                }
            }
        }
        std::cout << "All-motion minimum visible sole height / relaxed knee-forward projection (m): " << minSoleHeight
                  << " / " << kneeForward << "\n";
        Require(minSoleHeight > -.003f, "Visible foot penetrates ground during motion");
        Require(kneeForward > -.01f, "Relaxed knee bends backward");
        for (const char *side : {"_l", "_r"})
            for (bool leg : {true, false})
            {
                Pose pose = rest;
                const int upper = lib.Joint(std::string(leg ? "thigh" : "upperarm") + side),
                          lower = lib.Joint(std::string(leg ? "shin" : "forearm") + side),
                          tip = lib.Joint(std::string(leg ? "foot" : "hand") + side);
                const float reach = Length(pose.world[lower].position - pose.world[upper].position) +
                                    Length(pose.world[tip].position - pose.world[lower].position);
                const Vec3 startPoint = pose.world[upper].position, unreachable = startPoint + Vec3{10, -10, 0};
                const float residual = SolveLimb(lib, pose, upper, lower, tip, unreachable, unreachable);
                BoneLengths(lib, pose);
                Require(residual > 10, "Unreachable IK target was falsely satisfied");
                Require(Length(pose.world[tip].position - startPoint) < reach - .00005f,
                        "IK joint hyperextended at the reach limit");
            }
        Pose originalIdle;
        lib.Sample(lib.FindClip("idle"), 0, originalIdle);
        float pairError = 0, torsoClearance = 100, pairSoleMinimum = 100, maxNeckTilt = 0;
        float pairFootTilt = 0, holdFootDrift = 0, holdKneeStep = 0, allKneeStep = 0, maxKneeAcceleration = 0;
        std::string worstPair;
        for (PairKind kind : {PairKind::Handshake, PairKind::Hug})
            for (float a : {.9f, 1.f, 1.1f})
                for (float b : {.9f, 1.f, 1.1f})
                    for (float distance : {.6f, .9f, 1.2f})
                    {
                        PairSession pair(lib);
                        Require(pair.Start(kind, a, b, distance), "Valid pair rejected");
                        Require(!pair.Start(kind), "Must not acquire occupied actors");
                        for (const Pose *pose : {&pair.Left(), &pair.Right()})
                            for (bool upper : {false, true})
                                Require(Length(LocalNeckDirection(lib, *pose, upper) - Vec3{0, 1, 0}) < .0001f,
                                        "Pair must start with a neutral neck, not a captured idle head gesture");
                        float combinationError = 0;
                        Pose previousLeft = pair.Left(), previousRight = pair.Right();
                        Pose holdLeft, holdRight;
                        bool haveHoldReference = false;
                        Vec3 previousKneeDelta[2][2]{};
                        for (int frame = 0; frame < 312; ++frame)
                        {
                            pair.Update(1 / 60.f);
                            BoneLengths(lib, pair.Left(), a);
                            BoneLengths(lib, pair.Right(), b);
                            for (const char *leftFoot : {"foot_l", "foot_r"})
                                for (const char *rightFoot : {"foot_l", "foot_r"})
                                {
                                    const auto first = FootShape(lib, pair.Left(), lib.Joint(leftFoot), a);
                                    const auto second = FootShape(lib, pair.Right(), lib.Joint(rightFoot), b);
                                    Require(first.transform.position.z + FootHalfExtentZ(first) + .005f <
                                                second.transform.position.z - FootHalfExtentZ(second),
                                            "Pair feet overlap along the approach axis");
                                }
                            for (const auto &[pose, previous] :
                                 {std::pair{&pair.Left(), &previousLeft}, std::pair{&pair.Right(), &previousRight}})
                            {
                                for (int side = 0; side < 2; ++side)
                                {
                                    const std::string suffix = side == 0 ? "_l" : "_r";
                                    const int foot = lib.Joint("foot" + suffix), knee = lib.Joint("shin" + suffix),
                                              hip = lib.Joint("thigh" + suffix);
                                    const Quat rotation = pose->world[foot].rotation;
                                    const Vec3 up = rotation.Rotate({0, 1, 0});
                                    pairFootTilt = std::max(
                                        pairFootTilt, std::atan2(Length(Cross(up, {0, 1, 0})), Dot(up, {0, 1, 0})));
                                    Require(Length(up - Vec3{0, 1, 0}) < .0001f,
                                            "Pair sole must be flat, not just touching at one point");
                                    const Vec3 expectedForward =
                                        (pose->world[0].rotation * AxisAngle({0, 1, 0}, side == 0 ? .06f : -.06f))
                                            .Rotate({0, 0, 1});
                                    Require(Length(rotation.Rotate({0, 0, 1}) - expectedForward) < .0001f,
                                            "Pair foot heading drifts away from its support pose");
                                    const Vec3 kneeDelta = pose->world[knee].position - previous->world[knee].position;
                                    const float kneeStep = Length(kneeDelta);
                                    allKneeStep = std::max(allKneeStep, kneeStep);
                                    Require(kneeStep < .03f, "Pair knee jumps during a short approach step");
                                    const int actor = pose == &pair.Left() ? 0 : 1;
                                    const float kneeAcceleration =
                                        Length(kneeDelta - previousKneeDelta[actor][side]) * 3600;
                                    maxKneeAcceleration = std::max(maxKneeAcceleration, kneeAcceleration);
                                    Require(kneeAcceleration < 50, "Pair knee has a discontinuous acceleration spike");
                                    previousKneeDelta[actor][side] = kneeDelta;
                                    const Vec3 leg = Normal(pose->world[foot].position - pose->world[hip].position);
                                    const Vec3 bend = pose->world[knee].position - pose->world[hip].position;
                                    Require(Dot(bend - leg * Dot(bend, leg),
                                                pose->world[0].rotation.Rotate({0, 0, 1})) > .01f,
                                            "Pair knee loses its forward bend or nears the straight-leg singularity");
                                    if (pair.Holding() && haveHoldReference)
                                    {
                                        const Pose &anchor = pose == &pair.Left() ? holdLeft : holdRight;
                                        const float scale = pose == &pair.Left() ? a : b;
                                        const float drift = Length(FootBottom(lib, *pose, foot, scale) -
                                                                   FootBottom(lib, anchor, foot, scale));
                                        holdFootDrift = std::max(holdFootDrift, drift);
                                        holdKneeStep = std::max(holdKneeStep, kneeStep);
                                        Require(drift < .0001f, "Standing pair support foot drifts");
                                        Require(Length(pose->world[foot].position - anchor.world[foot].position) <
                                                    .0001f,
                                                "Standing pair ankle rocks around a fixed sole point");
                                        Require(kneeStep < .0001f, "Standing pair knee jitters despite planted feet");
                                    }
                                }
                                const Vec3 direction = LocalNeckDirection(lib, *pose, true);
                                const float tilt = std::acos(std::clamp(direction.y, -1.f, 1.f));
                                maxNeckTilt = std::max(maxNeckTilt, tilt);
                                Require(tilt < .22f, "Interaction neck exceeds prototype bend envelope");
                                Require(LocalNeckDirection(lib, *pose, false).y > std::cos(.08f),
                                        "Lower neck carries too much of the head bend");
                                const float headStep = Length(direction - LocalNeckDirection(lib, *previous, true));
                                if (headStep >= .02f)
                                    std::cout << "Neck step diagnostic kind=" << static_cast<int>(kind)
                                              << " t=" << pair.Time() << " scales=" << a << "," << b
                                              << " step=" << headStep << "\n";
                                Require(headStep < .02f, "Interaction neck changes abruptly between frames");
                                if (kind == PairKind::Handshake)
                                    Require(std::abs(direction.x) < .0001f, "Handshake inherited sideways idle lean");
                                if (pair.Holding() && kind == PairKind::Hug)
                                    Require(std::abs(direction.x) > .10f, "Hug lost its intentional bounded head tilt");
                            }
                            if (pair.Holding() && !haveHoldReference)
                            {
                                holdLeft = pair.Left();
                                holdRight = pair.Right();
                                haveHoldReference = true;
                            }
                            previousLeft = pair.Left();
                            previousRight = pair.Right();
                            for (const char *side : {"foot_l", "foot_r"})
                                pairSoleMinimum =
                                    std::min({pairSoleMinimum, FootBottom(lib, pair.Left(), lib.Joint(side), a).y,
                                              FootBottom(lib, pair.Right(), lib.Joint(side), b).y});
                            if (pair.Holding())
                            {
                                if (kind == PairKind::Handshake)
                                {
                                    const auto leftPalm = PalmShape(lib, pair.Left(), lib.Joint("hand_r"), a);
                                    const auto rightPalm = PalmShape(lib, pair.Right(), lib.Joint("hand_r"), b);
                                    const Vec3 leftNormal = leftPalm.transform.rotation.Rotate({0, 0, 1});
                                    const Vec3 rightNormal = rightPalm.transform.rotation.Rotate({0, 0, 1});
                                    Require(Dot(leftNormal, rightNormal) < -.999f,
                                            "Handshake palms do not face each other");
                                    Require(Length(leftPalm.transform.position + leftNormal * leftPalm.radii.z -
                                                   rightPalm.transform.position - rightNormal * rightPalm.radii.z) <
                                                .003f,
                                            "Handshake palm surfaces miss contact");
                                }
                                pairError = std::max(pairError, pair.Residual());
                                combinationError = std::max(combinationError, pair.Residual());
                                const float clearance =
                                    std::min(ArmTorsoClearance(lib, pair.Left(), pair.Right(), a, b),
                                             ArmTorsoClearance(lib, pair.Right(), pair.Left(), b, a));
                                if (clearance < torsoClearance)
                                {
                                    torsoClearance = clearance;
                                    worstPair = std::string(kind == PairKind::Hug ? "hug" : "handshake") +
                                                " scale=" + std::to_string(a) + "/" + std::to_string(b) +
                                                " t=" + std::to_string(pair.Time());
                                }
                            }
                        }
                        if (combinationError > .035f)
                            std::cout << "Contact diagnostic kind=" << (kind == PairKind::Hug ? "hug" : "handshake")
                                      << " scales=" << a << "," << b << " d=" << distance
                                      << " residual=" << combinationError << "\n";
                        pair.Update(.02f);
                        Require(!pair.Active(), "Pair ownership not released");
                        for (const Pose *pose : {&pair.Left(), &pair.Right()})
                            Require(Length(LocalNeckDirection(lib, *pose, true) - Vec3{0, 1, 0}) < .0001f,
                                    "Pair must recover to a neutral neck");
                    }
        std::cout << "Pair contact residual (m), 54 combinations: " << pairError << "\n";
        std::cout << "Pair maximum upper-neck tilt (degrees): " << maxNeckTilt * 180 / Pi << "\n";
        std::cout << "Pair maximum sole tilt (degrees): " << pairFootTilt * 180 / Pi << "\n";
        std::cout << "Pair hold foot drift / knee frame step (m): " << holdFootDrift << " / " << holdKneeStep << "\n";
        std::cout << "Pair all-motion maximum knee frame step at 60Hz (m): " << allKneeStep << "\n";
        std::cout << "Pair maximum sampled knee acceleration at 60Hz (m/s^2): " << maxKneeAcceleration << "\n";
        Require(pairError < .035f, "Pair contact unreachable in declared range");
        std::cout << "Pair minimum visible sole height (m): " << pairSoleMinimum << "\n";
        Require(pairSoleMinimum > -.003f, "Pair approach foot penetrates ground");
        std::cout << "Sampled arm / opposite torso clearance (m): " << torsoClearance << "\n";
        std::cout << "Minimum clearance combination: " << worstPair << "\n";
        Require(torsoClearance >= -.005f, "Interaction arms penetrate the opposite simplified torso");
        for (PairKind cancelKind : {PairKind::Handshake, PairKind::Hug})
            for (const auto &[cancelScaleA, cancelScaleB] :
                 {std::pair{.9f, 1.1f}, std::pair{1.1f, .9f}, std::pair{1.f, 1.f}})
                for (float cancelDistance : {.6f, 1.2f})
                    for (float cancelTime : {0.f, .9f, 2.5f, 4.4f})
                    {
                        PairSession pair(lib);
                        pair.Start(cancelKind, cancelScaleA, cancelScaleB, cancelDistance);
                        pair.Seek(cancelTime);
                        const Pose cancelledLeft = pair.Left(), cancelledRight = pair.Right();
                        const Vec3 previous = pair.Left().world[0].position;
                        pair.Cancel();
                        pair.Update(0);
                        Require(Length(pair.Left().world[0].position - previous) < 1e-5f, "Cancel teleported root");
                        for (size_t joint = 0; joint < lib.Names().size(); ++joint)
                        {
                            Require(Length(pair.Left().world[joint].position - cancelledLeft.world[joint].position) <
                                        .00001f,
                                    "Cancel snapped left actor pose");
                            Require(Length(pair.Right().world[joint].position - cancelledRight.world[joint].position) <
                                        .00001f,
                                    "Cancel snapped right actor pose");
                        }
                        for (int i = 0; i < 30; ++i)
                        {
                            pair.Update(1 / 60.f);
                            for (const Pose *pose : {&pair.Left(), &pair.Right()})
                            {
                                const float actorScale = pose == &pair.Left() ? cancelScaleA : cancelScaleB;
                                BoneLengths(lib, *pose, actorScale);
                                for (const char *footName : {"foot_l", "foot_r"})
                                {
                                    const int foot = lib.Joint(footName);
                                    Require(FootBottom(lib, *pose, foot, actorScale).y > -.0001f,
                                            "Cancelled pair foot penetrates floor");
                                    const Pose &captured = pose == &pair.Left() ? cancelledLeft : cancelledRight;
                                    const Vec3 sole = FootBottom(lib, *pose, foot, actorScale),
                                               anchor = FootBottom(lib, captured, foot, actorScale);
                                    Require(std::hypot(sole.x - anchor.x, sole.z - anchor.z) < .0001f,
                                            "Cancelled support foot slides instead of settling in place");
                                    Require(Length(pose->world[foot].rotation.Rotate({0, 1, 0}) - Vec3{0, 1, 0}) <
                                                .0001f,
                                            "Cancelled pair foot loses its flat support orientation");
                                }
                            }
                        }
                        Require(!pair.Active(), "Cancel did not release ownership");
                        Require(Length(LocalNeckDirection(lib, pair.Left(), true) - Vec3{0, 1, 0}) < .0001f,
                                "Cancel did not return head to neutral");
                    }
        Pose untouchedIdle;
        lib.Sample(lib.FindClip("idle"), 0, untouchedIdle);
        for (size_t joint = 0; joint < lib.Names().size(); ++joint)
            Require(Length(untouchedIdle.world[joint].position - originalIdle.world[joint].position) < .000001f,
                    "Pair correction modified the source single-player clip");
        for (PairKind kind : {PairKind::Handshake, PairKind::Hug})
        {
            PairSession stepped(lib), scrubbed(lib);
            stepped.Start(kind, .9f, 1.1f);
            scrubbed.Start(kind, .9f, 1.1f);
            for (int frame = 0; frame < 300; ++frame)
            {
                stepped.Update(1 / 60.f);
                scrubbed.Seek(stepped.Time());
                Require(Length(LocalNeckDirection(lib, stepped.Left(), true) -
                               LocalNeckDirection(lib, scrubbed.Left(), true)) < .00001f,
                        "Left head gesture depends on playback history instead of shared time");
                Require(Length(LocalNeckDirection(lib, stepped.Right(), true) -
                               LocalNeckDirection(lib, scrubbed.Right(), true)) < .00001f,
                        "Right head gesture differs when scrubbing the pair timeline");
                for (const auto &[first, second] :
                     {std::pair{&stepped.Left(), &scrubbed.Left()}, std::pair{&stepped.Right(), &scrubbed.Right()}})
                    for (const char *name : {"shin_l", "shin_r", "foot_l", "foot_r"})
                        Require(Length(first->world[lib.Joint(name)].position -
                                       second->world[lib.Joint(name)].position) < .00001f,
                                "Pair lower-body contact depends on playback history");
            }
        }
        PairSession invalid(lib);
        Require(!invalid.Start(PairKind::Hug, .5f, 1), "Unsupported scale accepted");
        Require(!invalid.Start(PairKind::Hug, 1, 1, 3), "Unsupported distance accepted");
        bool rejected = false;
        try
        {
            lib.Sample(-1, 0, start);
        }
        catch (...)
        {
            rejected = true;
        }
        Require(rejected, "Invalid clip index accepted");
        std::cout << "PASS " << checks << " checks; visual naturalness is NOT automatically certified.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
}
