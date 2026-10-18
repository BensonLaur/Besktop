#include "besktop/anim3d/runtime.h"
#include "besktop/core/json_value.h"

#include <windows.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/maths/simd_math.h>
#include <array>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>

namespace besktop::anim3d
{
namespace
{
using Bytes = std::vector<unsigned char>;
using Read = std::function<Bytes(const std::string &)>;
const JsonValue &Field(const JsonValue &object, const char *name)
{
    const auto *value = object.Find(name);
    if (!value)
        throw std::runtime_error(std::string("Missing animation field: ") + name);
    return *value;
}
std::string Text(const JsonValue &object, const char *name)
{
    const auto &v = Field(object, name);
    if (!v.IsString())
        throw std::runtime_error("Expected animation string");
    return v.stringValue;
}
float Number(const JsonValue &object, const char *name)
{
    const auto &v = Field(object, name);
    if (!v.IsNumber() || !std::isfinite(v.numberValue) || !std::isfinite(static_cast<float>(v.numberValue)))
        throw std::runtime_error("Expected finite animation number");
    return static_cast<float>(v.numberValue);
}
JsonValue Json(const Bytes &bytes)
{
    return ParseJson(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}
template <class T> void Archive(const Bytes &bytes, T &output)
{
    if (bytes.empty() || bytes.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid animation archive size");
    ozz::io::MemoryStream stream;
    stream.Write(bytes.data(), bytes.size());
    stream.Seek(0, ozz::io::Stream::kSet);
    ozz::io::IArchive archive(&stream);
    if (!archive.TestTag<T>())
        throw std::runtime_error("Incompatible ozz archive");
    archive >> output;
}
void Unpack(const ozz::span<const ozz::math::SoaTransform> &transforms, size_t joints, Pose &out)
{
    out.local.resize(joints);
    out.world.resize(joints);
    for (size_t group = 0; group < transforms.size(); ++group)
    {
        const auto &s = transforms[group];
        float p[3][4], q[4][4];
        ozz::math::StorePtrU(s.translation.x, p[0]);
        ozz::math::StorePtrU(s.translation.y, p[1]);
        ozz::math::StorePtrU(s.translation.z, p[2]);
        ozz::math::StorePtrU(s.rotation.x, q[0]);
        ozz::math::StorePtrU(s.rotation.y, q[1]);
        ozz::math::StorePtrU(s.rotation.z, q[2]);
        ozz::math::StorePtrU(s.rotation.w, q[3]);
        for (size_t lane = 0; lane < 4 && group * 4 + lane < joints; ++lane)
            out.local[group * 4 + lane] = {{p[0][lane], p[1][lane], p[2][lane]},
                                           Normalize({q[0][lane], q[1][lane], q[2][lane], q[3][lane]})};
    }
}
Bytes File(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        throw std::runtime_error("Cannot open animation asset: " + path.string());
    const auto size = in.tellg();
    if (size < 0 || size > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid asset size");
    Bytes bytes(static_cast<size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char *>(bytes.data()), size);
    if (!in)
        throw std::runtime_error("Truncated animation asset");
    return bytes;
}
Transform Placement(float z, float yaw)
{
    return {{0, 0, z}, AxisAngle({0, 1, 0}, yaw)};
}
} // namespace

struct Library::Impl
{
    ozz::animation::Skeleton skeleton;
    std::vector<std::unique_ptr<ozz::animation::Animation>> animations;
    std::vector<ClipInfo> clips;
    std::vector<std::string> names;
    std::vector<int> parents;
    Pose rest;
    BodyPresentation presentation;
    // Library sampling is deliberately single-threaded; each render thread owns its library.
    mutable ozz::animation::SamplingJob::Context context;
    mutable std::vector<ozz::math::SoaTransform> scratch;
    void Load(const Read &read)
    {
        const auto manifest = Json(read("manifest.json"));
        if (Text(manifest, "skeleton") != "besktop.humanoid.v1")
            throw std::runtime_error("Unsupported skeleton id");
        Archive(read("skeleton.ozz"), skeleton);
        if (skeleton.num_joints() != 24)
            throw std::runtime_error("Expected humanoid v1: 24 joints");
        const auto spec = Json(read("skeleton.json"));
        if (Text(spec, "id") != "besktop.humanoid.v1" || Number(spec, "version") != 1 ||
            Text(spec, "units") != "meters" || Text(spec, "handedness") != "right" || Text(spec, "up") != "+Y" ||
            Text(spec, "forward") != "+Z" || Text(spec, "quaternion") != "xyzw")
            throw std::runtime_error("Skeleton metadata mismatch");
        const auto &appearance = Field(spec, "presentation");
        presentation = {Number(appearance, "head_radius"), Number(appearance, "joint_radius"),
                        Number(appearance, "limb_radius"), Number(appearance, "torso_radius")};
        const std::pair<const char *, float *> dimensions[] = {{"chest_half_width", &presentation.chestHalfWidth},
                                                               {"chest_half_depth", &presentation.chestHalfDepth},
                                                               {"waist_half_width", &presentation.waistHalfWidth},
                                                               {"waist_half_depth", &presentation.waistHalfDepth},
                                                               {"pelvis_half_width", &presentation.pelvisHalfWidth},
                                                               {"pelvis_half_height", &presentation.pelvisHalfHeight},
                                                               {"pelvis_half_depth", &presentation.pelvisHalfDepth},
                                                               {"foot_half_width", &presentation.footHalfWidth},
                                                               {"foot_half_height", &presentation.footHalfHeight},
                                                               {"foot_half_length", &presentation.footHalfLength},
                                                               {"palm_half_length", &presentation.palmHalfLength},
                                                               {"palm_half_width", &presentation.palmHalfWidth},
                                                               {"palm_half_thickness", &presentation.palmHalfThickness},
                                                               {"palm_center_offset", &presentation.palmCenterOffset}};
        for (const auto &[name, dimension] : dimensions)
        {
            *dimension = Number(appearance, name);
            if (*dimension <= 0 || *dimension > .3f)
                throw std::runtime_error("Invalid body dimension");
        }
        presentation.pelvisCenterY = Number(appearance, "pelvis_center_y");
        presentation.footCenterY = Number(appearance, "foot_center_y");
        presentation.footCenterZ = Number(appearance, "foot_center_z");
        if (std::abs(presentation.pelvisCenterY) > .2f || std::abs(presentation.footCenterY) > .2f ||
            std::abs(presentation.footCenterZ) > .2f)
            throw std::runtime_error("Invalid body shape center");
        for (float radius :
             {presentation.headRadius, presentation.jointRadius, presentation.limbRadius, presentation.torsoRadius})
            if (radius <= 0 || radius > .3f)
                throw std::runtime_error("Invalid body presentation radius");
        constexpr const char *expectedNames[] = {
            "root",       "pelvis",     "spine_low", "spine",  "chest",      "neck_base",  "neck",      "head",
            "clavicle_l", "upperarm_l", "forearm_l", "hand_l", "clavicle_r", "upperarm_r", "forearm_r", "hand_r",
            "thigh_l",    "shin_l",     "foot_l",    "toe_l",  "thigh_r",    "shin_r",     "foot_r",    "toe_r"};
        constexpr int expectedParents[] = {-1, 0,  1,  2,  3, 4,  5,  6,  4, 8,  9,  10,
                                           4,  12, 13, 14, 1, 16, 17, 18, 1, 20, 21, 22};
        const auto &joints = Field(spec, "joints").arrayValue;
        if (joints.size() != 24)
            throw std::runtime_error("Skeleton joint metadata mismatch");
        names.clear();
        parents.clear();
        clips.clear();
        animations.clear();
        for (int i = 0; i < skeleton.num_joints(); ++i)
        {
            names.emplace_back(skeleton.joint_names()[i]);
            parents.push_back(skeleton.joint_parents()[i]);
            if (names.back() != expectedNames[i] || parents.back() != expectedParents[i] ||
                names.back() != Text(joints[i], "name") || Number(joints[i], "parent") != parents.back())
                throw std::runtime_error("Skeleton order/hierarchy mismatch");
        }
        Unpack(skeleton.joint_rest_poses(), names.size(), rest);
        context.Resize(skeleton.num_joints());
        scratch.resize(skeleton.num_soa_joints());
        const auto &entries = Field(manifest, "clips");
        if (!entries.IsArray() || entries.arrayValue.empty() || entries.arrayValue.size() > 128)
            throw std::runtime_error("Invalid clip manifest");
        for (const auto &item : entries.arrayValue)
        {
            ClipInfo clip;
            clip.name = Text(item, "name");
            clip.origin = Text(item, "origin");
            clip.next = Text(item, "next");
            clip.duration = Number(item, "duration");
            clip.blendSeconds = Number(item, "blend_seconds");
            const auto &loop = Field(item, "loop");
            if (!loop.IsBoolean())
                throw std::runtime_error("Invalid loop policy");
            clip.loop = loop.boolValue;
            if (clip.duration <= 0 || clip.duration > 300 || clip.blendSeconds < 0 || clip.blendSeconds > 5)
                throw std::runtime_error("Invalid clip duration");
            const std::string filename = Text(item, "file");
            if (filename != clip.name + ".ozz" || filename.find_first_of("/\\:") != std::string::npos)
                throw std::runtime_error("Unsafe asset filename");
            for (const auto &existing : clips)
                if (existing.name == clip.name)
                    throw std::runtime_error("Duplicate animation clip");
            auto animation = std::make_unique<ozz::animation::Animation>();
            Archive(read(filename), *animation);
            if (animation->num_tracks() != 24 || std::abs(animation->duration() - clip.duration) > .002f ||
                clip.name != animation->name())
                throw std::runtime_error("Incompatible clip tracks/duration/name");
            const auto &contactEntries = Field(item, "contacts");
            if (!contactEntries.IsArray())
                throw std::runtime_error("Contact markers must be an array");
            for (const auto &c : contactEntries.arrayValue)
            {
                Contact contact;
                contact.start = Number(c, "start");
                contact.end = Number(c, "end");
                const std::string joint = Text(c, "joint");
                const auto found = std::find(names.begin(), names.end(), joint);
                if (found == names.end() || (joint != "foot_l" && joint != "foot_r") || contact.start < 0 ||
                    contact.end <= contact.start || contact.end > clip.duration + .001f)
                    throw std::runtime_error("Invalid foot contact interval");
                contact.joint = static_cast<int>(found - names.begin());
                clip.contacts.push_back(contact);
            }
            clips.push_back(std::move(clip));
            animations.push_back(std::move(animation));
        }
        context.Invalidate();
    }
};
Library::Library() : impl_(std::make_unique<Impl>())
{
}
Library::~Library() = default;
void Library::Load(const std::filesystem::path &path)
{
    auto candidate = std::make_unique<Impl>();
    candidate->Load([&](const std::string &name) { return File(path / name); });
    impl_ = std::move(candidate);
    Forward(impl_->rest);
}
void Library::LoadEmbedded()
{
    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(5100), RT_RCDATA);
    if (!resource)
        throw std::runtime_error("Animation resources are not embedded in this build");
    const auto *bytes = static_cast<const unsigned char *>(LockResource(LoadResource(module, resource)));
    const size_t size = SizeofResource(module, resource);
    size_t pos = 0;
    auto uint32 = [&]() {
        if (pos + 4 > size)
            throw std::runtime_error("Truncated resource index");
        uint32_t n;
        std::memcpy(&n, bytes + pos, 4);
        pos += 4;
        return n;
    };
    if (uint32() != 0x31464142)
        throw std::runtime_error("Bad animation resource signature");
    const uint32_t count = uint32();
    if (count > 132)
        throw std::runtime_error("Too many assets");
    std::map<std::string, Bytes> files;
    for (uint32_t i = 0; i < count; ++i)
    {
        const size_t nameSize = uint32(), dataSize = uint32();
        if (nameSize > 128 || dataSize > 16 * 1024 * 1024 || pos + nameSize + dataSize > size)
            throw std::runtime_error("Invalid resource bounds");
        const std::string name(reinterpret_cast<const char *>(bytes + pos), nameSize);
        pos += nameSize;
        if (!files.emplace(name, Bytes(bytes + pos, bytes + pos + dataSize)).second)
            throw std::runtime_error("Duplicate embedded asset");
        pos += dataSize;
    }
    if (pos != size)
        throw std::runtime_error("Unexpected animation resource tail");
    auto candidate = std::make_unique<Impl>();
    candidate->Load([&](const std::string &name) { return files.at(name); });
    impl_ = std::move(candidate);
    Forward(impl_->rest);
}
const std::vector<ClipInfo> &Library::Clips() const
{
    return impl_->clips;
}
int Library::FindClip(const std::string &name) const
{
    for (size_t i = 0; i < impl_->clips.size(); ++i)
        if (impl_->clips[i].name == name)
            return static_cast<int>(i);
    return -1;
}
int Library::Joint(const std::string &name) const
{
    const auto i = std::find(impl_->names.begin(), impl_->names.end(), name);
    return i == impl_->names.end() ? -1 : static_cast<int>(i - impl_->names.begin());
}
const std::vector<std::string> &Library::Names() const
{
    return impl_->names;
}
const std::vector<int> &Library::Parents() const
{
    return impl_->parents;
}
const Pose &Library::Rest() const
{
    return impl_->rest;
}
const BodyPresentation &Library::Presentation() const
{
    return impl_->presentation;
}
BodyEllipsoid FootShape(const Library &library, const Pose &pose, int foot, float scale)
{
    const auto &body = library.Presentation();
    const auto &joint = pose.world[foot];
    return {{joint.position + joint.rotation.Rotate({0, body.footCenterY * scale, body.footCenterZ * scale}),
             joint.rotation},
            Vec3{body.footHalfWidth, body.footHalfHeight, body.footHalfLength} * scale};
}
BodyEllipsoid PalmShape(const Library &library, const Pose &pose, int hand, float scale)
{
    const auto &body = library.Presentation();
    const auto &joint = pose.world[hand];
    const float sign = hand == library.Joint("hand_l") ? 1.f : -1.f;
    return {{joint.position + joint.rotation.Rotate({sign * body.palmCenterOffset * scale, 0, 0}), joint.rotation},
            Vec3{body.palmHalfLength, body.palmHalfWidth, body.palmHalfThickness} * scale};
}
std::array<BodyEllipsoid, 3> TorsoShapes(const Library &library, const Pose &pose, float scale)
{
    const auto &body = library.Presentation();
    const int pelvis = library.Joint("pelvis"), spine = library.Joint("spine"), chest = library.Joint("chest");
    auto section = [&](int lower, int upper, float width, float depth) {
        const Vec3 a = pose.world[lower].position, b = pose.world[upper].position;
        const Quat rotation =
            Normalize(FromTo(pose.world[lower].rotation.Rotate({0, 1, 0}), b - a) * pose.world[lower].rotation);
        return BodyEllipsoid{{(a + b) * .5f, rotation}, {width * scale, Length(b - a) * .65f, depth * scale}};
    };
    const auto &hip = pose.world[pelvis];
    return {section(spine, chest, body.chestHalfWidth, body.chestHalfDepth),
            section(pelvis, spine, body.waistHalfWidth, body.waistHalfDepth),
            BodyEllipsoid{{hip.position + hip.rotation.Rotate({0, body.pelvisCenterY * scale, 0}), hip.rotation},
                          Vec3{body.pelvisHalfWidth, body.pelvisHalfHeight, body.pelvisHalfDepth} * scale}};
}
Vec3 FootBottom(const Library &library, const Pose &pose, int foot, float scale)
{
    const auto shape = FootShape(library, pose, foot, scale);
    const Vec3 down = shape.transform.rotation.Inverse().Rotate({0, -1, 0}), r = shape.radii;
    const float divisor =
        std::sqrt(r.x * r.x * down.x * down.x + r.y * r.y * down.y * down.y + r.z * r.z * down.z * down.z);
    const Vec3 local{r.x * r.x * down.x / divisor, r.y * r.y * down.y / divisor, r.z * r.z * down.z / divisor};
    return shape.transform.position + shape.transform.rotation.Rotate(local);
}
void Library::Sample(int clip, float time, Pose &output) const
{
    if (clip < 0 || clip >= static_cast<int>(impl_->clips.size()) || !std::isfinite(time))
        throw std::runtime_error("Invalid animation sample");
    ozz::animation::SamplingJob job;
    job.animation = impl_->animations[clip].get();
    job.context = &impl_->context;
    job.ratio = std::clamp(time / impl_->clips[clip].duration, 0.f, 1.f);
    job.output = ozz::make_span(impl_->scratch);
    if (!job.Run())
        throw std::runtime_error("ozz sampling failed");
    Unpack(ozz::make_span(impl_->scratch), impl_->names.size(), output);
    // ozz quantizes translation tracks. Reapply the fixed skeleton offsets so the
    // first blend frame does not snap from compressed offsets back to rest lengths.
    for (size_t j = 2; j < output.local.size(); ++j)
        output.local[j].position = impl_->rest.local[j].position;
    Forward(output);
}
void Library::Forward(Pose &pose) const
{
    if (pose.local.size() != impl_->parents.size())
        throw std::runtime_error("Pose joint count mismatch");
    pose.world.resize(pose.local.size());
    for (size_t i = 0; i < pose.local.size(); ++i)
        pose.world[i] = impl_->parents[i] < 0 ? pose.local[i] : pose.world[impl_->parents[i]] * pose.local[i];
}
void Library::Blend(const Pose &a, const Pose &b, float weight, Pose &output) const
{
    if (a.local.size() != impl_->parents.size() || b.local.size() != a.local.size())
        throw std::runtime_error("Blend pose mismatch");
    weight = std::clamp(weight, 0.f, 1.f);
    output.local.resize(a.local.size());
    for (size_t i = 0; i < a.local.size(); ++i)
    {
        output.local[i].rotation = Slerp(a.local[i].rotation, b.local[i].rotation, weight);
        // Only root and pelvis can translate. Skeleton proportions are immutable.
        output.local[i].position =
            i < 2 ? Lerp(a.local[i].position, b.local[i].position, weight) : impl_->rest.local[i].position;
    }
    Forward(output);
}

float SolveLimb(const Library &library, Pose &pose, int upper, int lower, int tip, Vec3 target, Vec3 pole, float weight)
{
    if (upper < 0 || lower < 0 || tip < 0 || static_cast<size_t>(std::max({upper, lower, tip})) >= pose.world.size() ||
        !Finite(target) || !Finite(pole) || !std::isfinite(weight))
        throw std::runtime_error("Invalid IK target");
    weight = std::clamp(weight, 0.f, 1.f);
    const auto original = pose.local;
    const Vec3 a = pose.world[upper].position, b = pose.world[lower].position, c = pose.world[tip].position;
    const float l1 = Length(b - a), l2 = Length(c - b);
    if (l1 < 1e-5f || l2 < 1e-5f)
        return Length(target - c);
    const Vec3 direction = Normal(target - a, Normal(c - a));
    const float d = std::clamp(Length(target - a), std::abs(l1 - l2) + .0001f, l1 + l2 - .0001f);
    Vec3 bend = pole - a - direction * Dot(pole - a, direction);
    if (Length(bend) < 1e-4f)
        bend = b - a - direction * Dot(b - a, direction);
    if (Length(bend) < 1e-4f)
        bend = Cross(direction, {1, 0, 0});
    bend = Normal(bend);
    const float along = (l1 * l1 + d * d - l2 * l2) / (2 * d),
                height = std::sqrt(std::max(0.f, l1 * l1 - along * along));
    const Vec3 elbow = a + direction * along + bend * height, endpoint = a + direction * d;
    const Quat tipOrientation = pose.world[tip].rotation;
    const int parent = library.Parents()[upper];
    const Quat parentQ = parent < 0 ? Quat{} : pose.world[parent].rotation;
    pose.local[upper].rotation = Normalize(parentQ.Inverse() * FromTo(b - a, elbow - a) * pose.world[upper].rotation);
    library.Forward(pose);
    pose.local[lower].rotation = Normalize(
        pose.world[upper].rotation.Inverse() *
        FromTo(pose.world[tip].position - pose.world[lower].position, endpoint - elbow) * pose.world[lower].rotation);
    library.Forward(pose);
    pose.local[tip].rotation = Normalize(pose.world[lower].rotation.Inverse() * tipOrientation);
    for (int i : {upper, lower, tip})
        pose.local[i].rotation = Slerp(original[i].rotation, pose.local[i].rotation, weight);
    library.Forward(pose);
    return Length(pose.world[tip].position - target);
}

namespace
{
void GroundFeet(const Library &library, Pose &pose, const std::array<Vec3, 2> &soleTargets,
                const std::array<float, 2> &weights, float scale = 1)
{
    std::array<Vec3, 2> ankleTargets;
    float pelvisShift = 100, strongest = 0;
    for (int side = 0; side < 2; ++side)
    {
        const int foot = library.Joint(side == 0 ? "foot_l" : "foot_r");
        const Vec3 offset = FootBottom(library, pose, foot, scale) - pose.world[foot].position;
        ankleTargets[side] = soleTargets[side] - offset;
        if (weights[side] > 0)
        {
            pelvisShift = std::min(pelvisShift, ankleTargets[side].y - pose.world[foot].position.y);
            const int hip = library.Joint(side == 0 ? "thigh_l" : "thigh_r");
            const int knee = library.Joint(side == 0 ? "shin_l" : "shin_r");
            const float reach = Length(pose.world[knee].position - pose.world[hip].position) +
                                Length(pose.world[foot].position - pose.world[knee].position) - .001f * scale;
            const Vec3 delta = ankleTargets[side] - pose.world[hip].position;
            const float verticalReach = std::sqrt(std::max(0.f, reach * reach - delta.x * delta.x - delta.z * delta.z));
            pelvisShift = std::min(pelvisShift, ankleTargets[side].y + verticalReach - pose.world[hip].position.y);
            strongest = std::max(strongest, weights[side]);
        }
    }
    if (strongest <= 0)
        return;
    // Move the pelvis before extending a leg toward the floor; do not stretch bones or move RootMotion twice.
    pose.local[1].position.y += std::clamp(pelvisShift, -.16f * scale, .12f * scale) * strongest;
    library.Forward(pose);
    for (int side = 0; side < 2; ++side)
    {
        if (weights[side] <= 0)
            continue;
        const std::string suffix = side == 0 ? "_l" : "_r";
        const int foot = library.Joint("foot" + suffix), shin = library.Joint("shin" + suffix);
        const Vec3 pole = pose.world[shin].position + pose.world[0].rotation.Rotate({0, 0, .2f * scale});
        SolveLimb(library, pose, library.Joint("thigh" + suffix), shin, foot, ankleTargets[side], pole, weights[side]);
    }
    // Pelvis compensation can lower the swinging foot. Resolve floor collision without
    // acquiring a planted lock or discarding its captured heel/toe rotation.
    for (const char *side : {"_l", "_r"})
    {
        const int foot = library.Joint(std::string("foot") + side), shin = library.Joint(std::string("shin") + side);
        const float height = FootBottom(library, pose, foot, scale).y;
        if (height < 0)
            SolveLimb(library, pose, library.Joint(std::string("thigh") + side), shin, foot,
                      pose.world[foot].position + Vec3{0, -height, 0},
                      pose.world[shin].position + pose.world[0].rotation.Rotate({0, 0, .2f * scale}));
    }
}
// Rotate a parent frame, not a joint offset: all neck segment lengths stay immutable.
void PointBone(const Library &library, Pose &pose, int parent, int child, Vec3 direction)
{
    const Quat world = Normalize(FromTo(pose.world[child].position - pose.world[parent].position, direction) *
                                 pose.world[parent].rotation);
    const int grandparent = library.Parents()[parent];
    pose.local[parent].rotation = Normalize(pose.world[grandparent].rotation.Inverse() * world);
    library.Forward(pose);
}

Pose InteractionBase(const Library &library, float time)
{
    Pose pose;
    const int idle = library.FindClip("idle");
    if (idle < 0)
        throw std::runtime_error("Pair demo requires idle clip");
    library.Sample(idle, std::fmod(time, library.Clips()[idle].duration), pose);
    // Retain upper-limb relaxation only. Mixing a neutral pelvis with the captured
    // lower-body rotations/height makes the feet fight the authored contact plan.
    for (const char *name :
         {"pelvis", "spine_low", "spine", "chest", "neck_base", "neck", "head", "clavicle_l", "clavicle_r", "thigh_l",
          "shin_l", "foot_l", "toe_l", "thigh_r", "shin_r", "foot_r", "toe_r"})
    {
        const int joint = library.Joint(name);
        pose.local[joint].rotation = library.Rest().local[joint].rotation;
    }
    const int pelvis = library.Joint("pelvis");
    pose.local[pelvis].position = library.Rest().local[pelvis].position;
    pose.local[pelvis].position.y -= .025f; // Comfortable knee bend, not a near-straight IK singularity.
    library.Forward(pose);
    const Vec3 up = pose.world[0].rotation.Rotate({0, 1, 0});
    PointBone(library, pose, library.Joint("neck_base"), library.Joint("neck"), up);
    PointBone(library, pose, library.Joint("neck"), library.Joint("head"), up);
    const int head = library.Joint("head");
    pose.local[head].rotation =
        Normalize(pose.world[library.Parents()[head]].rotation.Inverse() * pose.world[0].rotation);
    library.Forward(pose);
    return pose;
}

Quat InteractionFootRotation(const Pose &pose, int side)
{
    // A small symmetric toe-out is intentional; both sole normals remain vertical.
    return Normalize(pose.world[0].rotation * AxisAngle({0, 1, 0}, side == 0 ? .06f : -.06f));
}

std::array<Vec3, 2> InteractionStance(const Library &library, const Pose &pose, float scale)
{
    std::array<Vec3, 2> targets;
    for (int side = 0; side < 2; ++side)
    {
        const int foot = library.Joint(side == 0 ? "foot_l" : "foot_r");
        Vec3 ankle = library.Rest().world[foot].position;
        ankle.y = 0;
        ankle.z -= .02f; // Keep the toes clear of the partner during a close hug.
        targets[side] = pose.world[0].position + pose.world[0].rotation.Rotate(ankle * scale) +
                        InteractionFootRotation(pose, side).Rotate({0, 0, library.Presentation().footCenterZ * scale});
        targets[side].y = 0;
    }
    return targets;
}

void InteractionFeet(const Library &library, Pose &pose, const std::array<Vec3, 2> &soles, float scale)
{
    std::array<Vec3, 2> ankles;
    float pelvisShift = 0;
    const auto &body = library.Presentation();
    for (int side = 0; side < 2; ++side)
    {
        const std::string suffix = side == 0 ? "_l" : "_r";
        const int hip = library.Joint("thigh" + suffix), knee = library.Joint("shin" + suffix),
                  foot = library.Joint("foot" + suffix);
        const Quat rotation = InteractionFootRotation(pose, side);
        // Full foot pose, not just one low point. This prototype lifts a flat foot;
        // heel/toe roll can later be authored explicitly instead of inherited from idle.
        ankles[side] = soles[side] -
                       rotation.Rotate({0, (body.footCenterY - body.footHalfHeight) * scale, body.footCenterZ * scale});
        const float reach = (Length(pose.local[knee].position) + Length(pose.local[foot].position)) * .985f;
        const Vec3 delta = ankles[side] - pose.world[hip].position;
        const float vertical = std::sqrt(std::max(0.f, reach * reach - delta.x * delta.x - delta.z * delta.z));
        pelvisShift = std::min(pelvisShift, ankles[side].y + vertical - pose.world[hip].position.y);
        pose.local[foot].rotation = Normalize(pose.world[knee].rotation.Inverse() * rotation);
    }
    pose.local[library.Joint("pelvis")].position.y += pelvisShift;
    library.Forward(pose);
    for (int side = 0; side < 2; ++side)
    {
        const std::string suffix = side == 0 ? "_l" : "_r";
        const int hip = library.Joint("thigh" + suffix), knee = library.Joint("shin" + suffix),
                  foot = library.Joint("foot" + suffix);
        // The bend plane belongs to the actor, not to last frame's sampled knee.
        const Vec3 pole = pose.world[hip].position + pose.world[0].rotation.Rotate({0, -.2f * scale, .4f * scale});
        SolveLimb(library, pose, hip, knee, foot, ankles[side], pole);
        pose.local[foot].rotation =
            Normalize(pose.world[knee].rotation.Inverse() * InteractionFootRotation(pose, side));
        library.Forward(pose);
    }
}

float ContactProgress(float value)
{
    const float t = std::clamp(value, 0.f, 1.f);
    return t * t * t * (t * (t * 6 - 15) + 10); // Zero velocity and acceleration at both ends.
}

void InteractionHead(const Library &library, Pose &pose, Vec3 partnerHead, float attention, float roll)
{
    const int base = library.Joint("neck_base"), neck = library.Joint("neck"), head = library.Joint("head");
    const Quat root = pose.world[0].rotation;
    const Vec3 target = root.Inverse().Rotate(partnerHead - pose.world[head].position);
    const float pitch = std::clamp(std::atan2(target.y, std::hypot(target.x, target.z)), -.12f, .12f) * attention;
    const float yaw = std::clamp(std::atan2(target.x, target.z), -.35f, .35f) * attention;
    const auto tilt = [&](float weight) {
        return AxisAngle({1, 0, 0}, -pitch * weight) * AxisAngle({0, 0, 1}, roll * weight);
    };
    // Distribute the bend over both neck segments; yaw affects head orientation, not
    // an artificial sideways displacement of its center. Limits are prototype choices.
    PointBone(library, pose, base, neck, root.Rotate(tilt(.35f).Rotate({0, 1, 0})));
    PointBone(library, pose, neck, head, root.Rotate(tilt(1).Rotate({0, 1, 0})));
    const Quat orientation = root * AxisAngle({0, 1, 0}, yaw) * tilt(1);
    pose.local[head].rotation = Normalize(pose.world[neck].rotation.Inverse() * orientation);
    library.Forward(pose);
}
} // namespace

Player::Player(const Library &library) : library_(library)
{
    clip_ = std::max(0, library_.FindClip("idle"));
    library_.Sample(clip_, 0, pose_);
    raw_ = pose_;
    from_ = pose_;
}
void Player::ResetLocks()
{
    locks_[0] = {};
    locks_[1] = {};
    contactTargets_.clear();
}
void Player::SetPlacement(Transform transform)
{
    anchor_ = transform;
    ResetLocks();
    blendTime_ = 1;
    Evaluate();
}
void Player::TurnBy(float radians)
{
    if (!std::isfinite(radians))
        throw std::runtime_error("Invalid steering angle");
    const Vec3 pivot = raw_.world[0].position;
    anchor_ = Transform{pivot, AxisAngle({0, 1, 0}, radians)} * Transform{-pivot, {}} * anchor_;
    // Retain planted world-space feet while the body turns around its current root.
    Evaluate();
}
void Player::Select(int clip, bool blend)
{
    if (clip < 0 || clip >= static_cast<int>(library_.Clips().size()))
        return;
    from_ = pose_;
    const Transform oldRoot = raw_.world.empty() ? Transform{} : raw_.world[0];
    clip_ = clip;
    time_ = 0;
    blendTime_ = blend ? 0 : 10;
    Pose first;
    library_.Sample(clip_, 0, first);
    anchor_ = oldRoot * first.local[0].Inverse();
    ResetLocks();
    Evaluate();
}
void Player::Seek(float time)
{
    if (!std::isfinite(time))
        throw std::runtime_error("Invalid seek time");
    time_ = std::clamp(time, 0.f, library_.Clips()[clip_].duration);
    blendTime_ = 10;
    ResetLocks();
    Evaluate();
}
void Player::Update(float dt)
{
    if (!std::isfinite(dt) || dt < 0)
        throw std::runtime_error("Invalid animation delta time");
    // Caller can subdivide a long simulation; interactive stalls must not teleport actors.
    dt = std::min(dt, .1f);
    time_ += dt;
    blendTime_ += dt;
    const auto &clip = library_.Clips()[clip_];
    if (time_ > clip.duration)
    {
        if (clip.loop)
        {
            Pose first, last;
            library_.Sample(clip_, 0, first);
            library_.Sample(clip_, clip.duration, last);
            while (time_ > clip.duration)
            {
                anchor_ = anchor_ * last.local[0] * first.local[0].Inverse();
                time_ -= clip.duration;
            }
            ResetLocks();
        }
        else
        {
            time_ = clip.duration;
            Evaluate();
            const int next = autoNext_ ? library_.FindClip(clip.next) : -1;
            if (next >= 0 && next != clip_)
            {
                Select(next);
                return;
            }
        }
    }
    Evaluate();
}
void Player::Evaluate()
{
    library_.Sample(clip_, time_, raw_);
    raw_.local[0] = anchor_ * raw_.local[0];
    library_.Forward(raw_);
    const auto &clip = library_.Clips()[clip_];
    const float blend = clip.blendSeconds > 0 ? Smooth(blendTime_ / clip.blendSeconds) : 1;
    if (blend < 1)
        library_.Blend(from_, raw_, blend, pose_);
    else
        pose_ = raw_;
    contactTargets_.clear();
    if (!correction_)
        return;
    std::array<Vec3, 2> soleTargets{};
    std::array<float, 2> weights{};
    for (int side = 0; side < 2; ++side)
    {
        const char *suffix = side == 0 ? "_l" : "_r";
        const int ankle = library_.Joint(std::string("foot") + suffix);
        int interval = -1;
        for (size_t i = 0; i < clip.contacts.size(); ++i)
            if (clip.contacts[i].joint == ankle && time_ >= clip.contacts[i].start && time_ < clip.contacts[i].end)
                interval = static_cast<int>(i);
        if (interval < 0)
        {
            locks_[side].interval = -1;
            continue;
        }
        const auto &c = clip.contacts[interval];
        if (locks_[side].interval != interval)
        {
            Pose start;
            library_.Sample(clip_, c.start, start);
            start.local[0] = anchor_ * start.local[0];
            library_.Forward(start);
            Vec3 sole = FootBottom(library_, start, ankle);
            sole.y = 0;
            locks_[side] = {interval, sole};
        }
        const float weight = Smooth((time_ - c.start) / .05f) * Smooth((c.end - time_) / .05f) * blend;
        soleTargets[side] = locks_[side].target;
        weights[side] = weight;
        contactTargets_.push_back(locks_[side].target);
    }
    GroundFeet(library_, pose_, soleTargets, weights);
}

bool PairSession::Start(PairKind kind, float leftScale, float rightScale, float distance)
{
    if (active_ || !std::isfinite(leftScale) || !std::isfinite(rightScale) || !std::isfinite(distance) ||
        leftScale < .9f || leftScale > 1.1f || rightScale < .9f || rightScale > 1.1f || distance < .6f ||
        distance > 1.2f)
        return false;
    kind_ = kind;
    scaleA_ = leftScale;
    scaleB_ = rightScale;
    distance_ = distance;
    time_ = 0;
    cancelling_ = false;
    active_ = true;
    Evaluate();
    return true;
}
void PairSession::Cancel()
{
    if (active_ && !cancelling_)
    {
        cancelling_ = true;
        cancelTime_ = 0;
        cancelA_ = a_;
        cancelB_ = b_;
    }
}
void PairSession::Update(float dt)
{
    if (!active_)
        return;
    if (!std::isfinite(dt) || dt < 0)
        throw std::runtime_error("Invalid pair delta");
    if (cancelling_)
        cancelTime_ += std::min(dt, .1f);
    else
        time_ += std::min(dt, .1f);
    Evaluate();
    if (time_ >= 5.2f || (cancelling_ && cancelTime_ >= .4f))
        active_ = false;
}
void PairSession::Seek(float seconds)
{
    if (!std::isfinite(seconds))
        throw std::runtime_error("Invalid pair seek time");
    if (!active_ || cancelling_)
        return;
    time_ = std::clamp(seconds, 0.f, 5.2f);
    Evaluate();
}
std::string PairSession::Phase() const
{
    if (!active_)
        return "released";
    if (cancelling_)
        return "cancel / recover";
    if (time_ < .8f)
        return "approach";
    if (time_ < 1.6f)
        return "prepare";
    if (time_ < 3.4f)
        return "contact / hold";
    if (time_ < 4.3f)
        return "release";
    return "recover";
}
void PairSession::Evaluate()
{
    const float enter = Smooth(time_ / 1.2f), exit = Smooth((time_ - 4.3f) / .9f);
    const float targetDistance = (kind_ == PairKind::Hug ? .32f : .58f) * (scaleA_ + scaleB_) * .5f;
    const float distance = distance_ + (targetDistance - distance_) * enter * (1 - exit);
    // Independent idle phases affect only upper-limb relaxation, not the foot plan.
    Pose idleA = InteractionBase(library_, time_), idleB = InteractionBase(library_, time_ + .83f);
    auto place = [&](Pose &p, float scale, float z, float yaw) {
        for (size_t j = 1; j < p.local.size(); ++j)
            p.local[j].position = p.local[j].position * scale;
        p.local[0] = Placement(z, yaw);
        library_.Forward(p);
    };
    place(idleA, scaleA_, -distance * .5f, 0);
    place(idleB, scaleB_, distance * .5f, Pi);
    a_ = idleA;
    b_ = idleB;
    residual_ = 0;
    if (cancelling_)
    {
        // Use each actor's captured root during cancellation; release cannot teleport the pair.
        idleA.local[0] = cancelA_.local[0];
        idleB.local[0] = cancelB_.local[0];
        const float w = Smooth(cancelTime_ / .4f);
        for (size_t i = 0; i < a_.local.size(); ++i)
        {
            a_.local[i] = {Lerp(cancelA_.local[i].position, idleA.local[i].position, w),
                           Slerp(cancelA_.local[i].rotation, idleA.local[i].rotation, w)};
            b_.local[i] = {Lerp(cancelB_.local[i].position, idleB.local[i].position, w),
                           Slerp(cancelB_.local[i].rotation, idleB.local[i].rotation, w)};
        }
        library_.Forward(a_);
        library_.Forward(b_);
        auto settle = [&](Pose &pose, const Pose &captured, float scale) {
            std::array<Vec3, 2> soles;
            for (int side = 0; side < 2; ++side)
            {
                soles[side] = FootBottom(library_, captured, library_.Joint(side == 0 ? "foot_l" : "foot_r"), scale);
                soles[side].y *= 1 - w;
            }
            // Keep planted feet fixed; a cancelled airborne foot settles in place.
            InteractionFeet(library_, pose, soles, scale);
        };
        settle(a_, cancelA_, scaleA_);
        settle(b_, cancelB_, scaleB_);
        return;
    }
    // Short contact approach uses explicit alternating foot placements, not sliding a standing pose.
    // This is interaction-specific choreography; normal clips remain entirely data-driven.
    Pose referenceA = InteractionBase(library_, 0), referenceB = InteractionBase(library_, .83f);
    place(referenceA, scaleA_, -distance_ * .5f, 0);
    place(referenceB, scaleB_, distance_ * .5f, Pi);
    const auto stanceA = InteractionStance(library_, referenceA, scaleA_);
    const auto stanceB = InteractionStance(library_, referenceB, scaleB_);
    InteractionFeet(library_, referenceA, stanceA, scaleA_);
    InteractionFeet(library_, referenceB, stanceB, scaleB_);
    auto feet = [&](Pose &pose, const std::array<Vec3, 2> &stance, float direction, float scale) {
        const float step = (distance_ - targetDistance) * .5f;
        const float lift = std::min(.045f * scale, std::abs(step) * .35f);
        const float crouch = .025f * std::sin(Pi * enter) * (1 - exit) + .025f * std::sin(Pi * exit);
        pose.local[1].position.y -= std::max(0.f, crouch);
        library_.Forward(pose);
        std::array<Vec3, 2> targets;
        for (int side = 0; side < 2; ++side)
        {
            const float advance = ContactProgress((time_ - (side == 1 ? .05f : .55f)) / .65f);
            const float retreat = ContactProgress((time_ - 4.3f - (side == 1 ? 0.f : .25f)) / .65f);
            Vec3 target = stance[side];
            target.z += direction * step * (advance - retreat);
            target.y = lift * (std::sin(Pi * advance) + std::sin(Pi * retreat));
            targets[side] = target;
        }
        InteractionFeet(library_, pose, targets, scale);
    };
    feet(a_, stanceA, 1, scaleA_);
    feet(b_, stanceB, -1, scaleB_);
    const float attention = Smooth(time_ / .65f) * (1 - exit);
    // Both actors lean to the same local side during the hug, which places their heads
    // on opposite world-space sides. Only the contact envelope enables this gesture.
    const float headRoll =
        kind_ == PairKind::Hug ? .14f * Smooth((time_ - .9f) / .7f) * (1 - Smooth((time_ - 3.4f) / .8f)) : 0;
    const Vec3 headA = a_.world[library_.Joint("head")].position;
    const Vec3 headB = b_.world[library_.Joint("head")].position;
    // Use the settled reference eye-height difference, not per-frame pelvis IK
    // compensation. The latter can otherwise turn small floor corrections into head snaps.
    const float eyeHeightDifference =
        referenceB.world[library_.Joint("head")].position.y - referenceA.world[library_.Joint("head")].position.y;
    Vec3 targetA = headB, targetB = headA;
    targetA.y = headA.y + eyeHeightDifference;
    targetB.y = headB.y - eyeHeightDifference;
    InteractionHead(library_, a_, targetA, attention, headRoll);
    InteractionHead(library_, b_, targetB, attention, headRoll);
    const float reach = Smooth((time_ - .55f) / 1.05f) * (1 - Smooth((time_ - 3.4f) / .9f));
    auto arm = [&](Pose &pose, bool left, Vec3 contact, Vec3 longAxis, Vec3 normal, Vec3 pole, float scale) {
        const std::string suffix = left ? "_l" : "_r";
        longAxis = Normal(longAxis - normal * Dot(longAxis, normal));
        normal = Normal(normal);
        Quat rotation = FromTo({left ? 1.f : -1.f, 0, 0}, longAxis);
        const Vec3 currentNormal = rotation.Rotate({0, 0, 1});
        const float twist = std::atan2(Dot(longAxis, Cross(currentNormal, normal)), Dot(currentNormal, normal));
        rotation = Normalize(AxisAngle(longAxis, twist) * rotation);
        const auto &body = library_.Presentation();
        const Vec3 target =
            contact - normal * (body.palmHalfThickness * scale) - longAxis * (body.palmCenterOffset * scale);
        const int hand = library_.Joint("hand" + suffix);
        SolveLimb(library_, pose, library_.Joint("upperarm" + suffix), library_.Joint("forearm" + suffix), hand, target,
                  pole, reach);
        pose.local[hand].rotation =
            Slerp(pose.local[hand].rotation,
                  Normalize(pose.world[library_.Parents()[hand]].rotation.Inverse() * rotation), reach);
        library_.Forward(pose);
        if (reach > .999f)
        {
            const auto palm = PalmShape(library_, pose, hand, scale);
            const Vec3 surface = palm.transform.position + palm.transform.rotation.Rotate({0, 0, palm.radii.z});
            residual_ = std::max(residual_, Length(surface - contact));
        }
    };
    if (kind_ == PairKind::Handshake)
    {
        const float shoulderHeight =
            (a_.world[library_.Joint("upperarm_r")].position.y + b_.world[library_.Joint("upperarm_r")].position.y) *
            .5f;
        const float height = shoulderHeight - .23f + .012f * std::sin(std::max(0.f, time_ - 1.6f) * 9) * reach;
        const Vec3 meet{0, height, 0};
        arm(a_, false, meet, {0, 1, 0}, {0, 0, 1}, {-.55f, .85f, -distance * .2f}, scaleA_);
        arm(b_, false, meet, {0, 1, 0}, {0, 0, -1}, {.55f, .85f, distance * .2f}, scaleB_);
    }
    else
    {
        auto embrace = [&](Pose &actor, const Pose &partner, float actorScale, float partnerScale, float direction) {
            const auto chest = TorsoShapes(library_, partner, partnerScale)[0];
            for (bool left : {true, false})
            {
                const float sign = left ? 1.f : -1.f;
                const float x = -sign * .075f * partnerScale, y = sign * .035f * partnerScale;
                const float z = -chest.radii.z * std::sqrt(std::max(0.f, 1 - x * x / (chest.radii.x * chest.radii.x) -
                                                                             y * y / (chest.radii.y * chest.radii.y)));
                const Vec3 contact = chest.transform.position + chest.transform.rotation.Rotate({x, y, z});
                const Vec3 normal = -Normal(chest.transform.rotation.Rotate({x / (chest.radii.x * chest.radii.x),
                                                                             y / (chest.radii.y * chest.radii.y),
                                                                             z / (chest.radii.z * chest.radii.z)}));
                const Vec3 alongBack = chest.transform.rotation.Rotate({sign, 0, 0});
                const Vec3 pole{sign * direction * .56f, contact.y + .04f,
                                chest.transform.position.z + direction * .04f};
                arm(actor, left, contact, alongBack, normal, pole, actorScale);
            }
        };
        embrace(a_, b_, scaleA_, scaleB_, 1);
        embrace(b_, a_, scaleB_, scaleA_, -1);
    }
}
} // namespace besktop::anim3d
