#pragma once
#include "besktop/anim3d/runtime.h"
#include <windows.h>
#include <filesystem>
#include <memory>

namespace besktop::anim3d
{
struct Camera
{
    float yaw = .6f, pitch = .15f, distance = 3.5f;
    Vec3 target{0, .88f, 0};
    bool orthographic = false;
    float orthoHeight = 2.4f;
};
struct RenderActor
{
    const Pose *pose = nullptr;
    Vec3 color{.10f, .55f, .86f};
    int icon = -1;
    float bodyScale = 1;
    float iconWidth = .34f;
};
struct RenderOptions
{
    Camera camera;
    bool skeleton = false, grid = true, contacts = true;
    std::vector<Vec3> contactPoints, rootTrail;
};
class Renderer
{
  public:
    Renderer();
    ~Renderer();
    void Initialize(HWND window, int width, int height, bool warp = false);
    void Resize(int width, int height);
    int AddTexture(int width, int height, const unsigned char *bgra);
    void SetBackground(int texture)
    {
        background_ = texture;
    }
    void SetOverlay(int texture);
    void Draw(const Library &library, const std::vector<RenderActor> &actors, const RenderOptions &options);
    void Present(bool vsync = true);
    void WaitForGpu();
    void SavePng(const std::filesystem::path &path);
    const std::string &Adapter() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int background_ = -1, overlay_ = -1;
};
} // namespace besktop::anim3d
