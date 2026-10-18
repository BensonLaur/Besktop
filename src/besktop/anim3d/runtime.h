#pragma once

#include "besktop/anim3d/math.h"
#include <filesystem>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace besktop::anim3d
{

struct Contact
{
    int joint = -1;
    float start = 0, end = 0;
};
struct ClipInfo
{
    std::string name, origin, next;
    float duration = 0, blendSeconds = .3f;
    bool loop = false;
    std::vector<Contact> contacts;
};
struct Pose
{
    std::vector<Transform> local, world;
};
struct BodyPresentation
{
    float headRadius = .108f, jointRadius = .036f, limbRadius = .029f, torsoRadius = .063f;
    float chestHalfWidth = .135f, chestHalfDepth = .08f, waistHalfWidth = .08f, waistHalfDepth = .065f;
    float pelvisHalfWidth = .125f, pelvisHalfHeight = .09f, pelvisHalfDepth = .075f, pelvisCenterY = -.035f;
    float footHalfWidth = .045f, footHalfHeight = .035f, footHalfLength = .12f;
    float footCenterY = -.025f, footCenterZ = .045f;
    float palmHalfLength = .07f, palmHalfWidth = .032f, palmHalfThickness = .014f, palmCenterOffset = .08f;
};
struct BodyEllipsoid
{
    Transform transform;
    Vec3 radii;
};

class Library
{
  public:
    Library();
    ~Library();
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;
    void Load(const std::filesystem::path &directory);
    // Embedded assets use the same validated loader, without writing temporary files.
    void LoadEmbedded();
    const std::vector<ClipInfo> &Clips() const;
    int FindClip(const std::string &name) const;
    int Joint(const std::string &name) const;
    const std::vector<std::string> &Names() const;
    const std::vector<int> &Parents() const;
    const Pose &Rest() const;
    const BodyPresentation &Presentation() const;
    void Sample(int clip, float time, Pose &output) const;
    void Forward(Pose &pose) const;
    void Blend(const Pose &a, const Pose &b, float weight, Pose &output) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Shared visible geometry is also the basis of sole grounding and contact validation.
BodyEllipsoid FootShape(const Library &library, const Pose &pose, int foot, float scale = 1);
BodyEllipsoid PalmShape(const Library &library, const Pose &pose, int hand, float scale = 1);
std::array<BodyEllipsoid, 3> TorsoShapes(const Library &library, const Pose &pose, float scale = 1);
Vec3 FootBottom(const Library &library, const Pose &pose, int foot, float scale = 1);

// Fixed-length analytic IK. Returns endpoint residual; unreachable targets are clamped.
float SolveLimb(const Library &library, Pose &pose, int upper, int lower, int tip, Vec3 target, Vec3 pole,
                float weight = 1);

class Player
{
  public:
    explicit Player(const Library &library);
    void Select(int clip, bool blend = true);
    void Seek(float time);
    void Update(float dt);
    void SetPlacement(Transform transform);
    void TurnBy(float radians);
    void SetCorrection(bool enabled)
    {
        correction_ = enabled;
        ResetLocks();
    }
    void SetAutoNext(bool value)
    {
        autoNext_ = value;
    }
    bool Correction() const
    {
        return correction_;
    }
    int Clip() const
    {
        return clip_;
    }
    float Time() const
    {
        return time_;
    }
    const Pose &Current() const
    {
        return pose_;
    }
    const Pose &Raw() const
    {
        return raw_;
    }
    const std::vector<Vec3> &ContactTargets() const
    {
        return contactTargets_;
    }

  private:
    void Evaluate();
    void ResetLocks();
    const Library &library_;
    int clip_ = 0;
    float time_ = 0, blendTime_ = 1;
    Transform anchor_;
    Pose pose_, raw_, from_;
    bool correction_ = true, autoNext_ = true;
    struct Lock
    {
        int interval = -1;
        Vec3 target;
    };
    Lock locks_[2];
    std::vector<Vec3> contactTargets_;
};

enum class PairKind
{
    Handshake,
    Hug
};
class PairSession
{
  public:
    explicit PairSession(const Library &library) : library_(library)
    {
    }
    bool Start(PairKind kind, float leftScale = 1, float rightScale = 1, float distance = .9f);
    void Update(float dt);
    void Seek(float seconds);
    void Cancel();
    bool Active() const
    {
        return active_;
    }
    bool Holding() const
    {
        return active_ && !cancelling_ && time_ >= 1.6f && time_ < 3.4f;
    }
    float Time() const
    {
        return time_;
    }
    const Pose &Left() const
    {
        return a_;
    }
    const Pose &Right() const
    {
        return b_;
    }
    float Residual() const
    {
        return residual_;
    }
    std::string Phase() const;

  private:
    void Evaluate();
    const Library &library_;
    Pose a_, b_, cancelA_, cancelB_;
    PairKind kind_ = PairKind::Handshake;
    float time_ = 0, cancelTime_ = 0, scaleA_ = 1, scaleB_ = 1, distance_ = .9f, residual_ = 0;
    bool active_ = false, cancelling_ = false;
};
} // namespace besktop::anim3d
