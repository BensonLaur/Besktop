#include "besktop/anim3d/app.h"
#include "besktop/anim3d/renderer.h"
#include "besktop/desktop/desktop_snapshot.h"
#include "besktop/render/icon_image_cache.h"
#include "besktop/render/wallpaper_renderer.h"
#include "besktop/render/taskbar_renderer.h"
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <psapi.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstring>

namespace besktop::anim3d
{
namespace
{
using Clock = std::chrono::steady_clock;
enum Control
{
    Clip = 101,
    Play,
    Slower,
    NormalSpeed,
    Previous,
    Next,
    Reset,
    Handshake,
    Hug,
    Cancel,
    Bones,
    Feet,
    Icons,
    Timeline,
    Status
};
std::wstring Wide(const std::string &text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}
struct Dib
{
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    Dib(int w, int h) : width(w), height(h)
    {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void **>(&pixels), nullptr, 0);
        if (!dc || !bitmap)
            throw std::runtime_error("Cannot create stage image");
        previous = SelectObject(dc, bitmap);
        std::memset(pixels, 0, static_cast<size_t>(w) * h * 4);
    }
    ~Dib()
    {
        if (dc && previous)
            SelectObject(dc, previous);
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
    }
};
struct StageActor
{
    std::unique_ptr<Player> player;
    Pose display;
    Vec3 origin;
    float scale = .5f;
    int icon = -1;
};
class App
{
  public:
    HINSTANCE instance;
    HWND window = nullptr, canvas = nullptr, slider = nullptr, status = nullptr, clipBox = nullptr;
    HFONT font = nullptr;
    Library library;
    std::unique_ptr<Player> player;
    std::unique_ptr<PairSession> pair;
    Renderer renderer;
    RenderOptions render;
    std::vector<StageActor> actors;
    std::vector<int> textures;
    bool stage = false, paused = false, showIcons = false, dragging = false, ready = false, offline = false,
         hotkey = false, sequences = false, pairMode = false;
    PairKind selectedPair = PairKind::Handshake;
    bool uiTest = false, syntheticStage = false;
    float speed = 1;
    int width = 1100, height = 760;
    POINT pointer{};
    Clock::time_point last = Clock::now(), lastStatus = Clock::now();
    float frameMs = 0;
    std::filesystem::path output;
    std::string failure;
    explicit App(HINSTANCE i, bool s) : instance(i), stage(s)
    {
    }
    ~App()
    {
        if (hotkey)
            UnregisterHotKey(window, 7);
        if (font)
            DeleteObject(font);
    }
    HWND Button(int id, const wchar_t *title, int x, int y, int w)
    {
        HWND control = CreateWindowW(L"BUTTON", title, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w, 28, window,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return control;
    }
    void Layout()
    {
        if (!window)
            return;
        RECT rect{};
        GetClientRect(window, &rect);
        width = rect.right;
        height = rect.bottom;
        if (stage)
            MoveWindow(canvas, 0, 0, width, height, TRUE);
        else
        {
            MoveWindow(canvas, 0, 78, width, std::max(1, height - 132), TRUE);
            MoveWindow(slider, 10, height - 49, width - 20, 23, TRUE);
            MoveWindow(status, 16, height - 25, width - 32, 24, TRUE);
        }
        if (ready)
            renderer.Resize(width, std::max(1, stage ? height : height - 132));
    }
    int SyntheticIcon(unsigned index)
    {
        constexpr int size = 96;
        Dib image(size, size);
        const COLORREF colors[] = {RGB(34, 158, 230), RGB(241, 157, 51), RGB(170, 99, 220)};
        HBRUSH brush = CreateSolidBrush(colors[index % 3]);
        HGDIOBJ old = SelectObject(image.dc, brush);
        HPEN pen = CreatePen(PS_SOLID, 1, colors[index % 3]);
        HGDIOBJ oldPen = SelectObject(image.dc, pen);
        RoundRect(image.dc, 4, 4, 92, 92, 23, 23);
        SetBkMode(image.dc, TRANSPARENT);
        SetTextColor(image.dc, RGB(255, 255, 255));
        HFONT large = CreateFontW(62, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY,
                                  0, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(image.dc, large);
        RECT r{0, 8, 96, 90};
        DrawTextW(image.dc, index % 2 ? L"+" : L"B", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(image.dc, oldFont);
        DeleteObject(large);
        SelectObject(image.dc, oldPen);
        DeleteObject(pen);
        SelectObject(image.dc, old);
        DeleteObject(brush);
        for (int i = 0; i < size * size; ++i)
            if (image.pixels[i * 4] || image.pixels[i * 4 + 1] || image.pixels[i * 4 + 2])
                image.pixels[i * 4 + 3] = 255;
        return renderer.AddTexture(size, size, image.pixels);
    }
    void Caption(const wchar_t *title, const wchar_t *subtitle)
    {
        const int h = std::max(1, stage ? height : height - 132);
        Dib image(width, h);
        SetBkMode(image.dc, TRANSPARENT);
        SetTextColor(image.dc, RGB(214, 231, 242));
        HGDIOBJ previous = SelectObject(image.dc, font);
        RECT top{22, 16, width - 22, 46};
        DrawTextW(image.dc, title, -1, &top, DT_SINGLELINE);
        SetTextColor(image.dc, RGB(145, 172, 193));
        RECT bottom{22, h - 37, width - 22, h - 12};
        DrawTextW(image.dc, subtitle, -1, &bottom, DT_SINGLELINE);
        SelectObject(image.dc, previous);
        for (int i = 0; i < width * h; ++i)
            if (image.pixels[i * 4] || image.pixels[i * 4 + 1] || image.pixels[i * 4 + 2])
                image.pixels[i * 4 + 3] = 255;
        renderer.SetOverlay(renderer.AddTexture(width, h, image.pixels));
    }
    void InitializeStage(const DesktopSnapshot &snapshot)
    {
        Dib background(width, height);
        RECT target{0, 0, width, height};
        WallpaperRenderer wallpaper;
        wallpaper.Draw(background.dc, target, snapshot.wallpaper);
        TaskbarRenderer taskbar;
        taskbar.Draw(background.dc, target, snapshot);
        for (int i = 0; i < width * height; ++i)
            background.pixels[i * 4 + 3] = 255;
        renderer.SetBackground(renderer.AddTexture(width, height, background.pixels));
        IconImageCache cache;
        constexpr float pixelsPerMeter = 140;
        const float pitch = .45f;
        render.camera = {0, pitch, 80, {0, 0, 0}, true, height / pixelsPerMeter};
        render.grid = false;
        render.contacts = false;
        for (size_t i = 0; i < snapshot.icons.size() && i < 100; ++i)
        {
            const auto &icon = snapshot.icons[i];
            const IconImage *image = cache.Load(icon.image, icon.displayName);
            int texture = textures[i % textures.size()];
            if (image && image->IsValid())
            {
                Dib iconPixels(static_cast<int>(image->width), static_cast<int>(image->height));
                Gdiplus::Graphics graphics(iconPixels.dc);
                graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
                graphics.DrawImage(image->bitmap.get(), 0, 0);
                texture = renderer.AddTexture(iconPixels.width, iconPixels.height, iconPixels.pixels);
            }
            const float px = (icon.iconBounds.left + icon.iconBounds.right) * .5f;
            const float py = (icon.iconBounds.top + icon.iconBounds.bottom) * .5f;
            const float iconWidth = static_cast<float>(std::max(32L, icon.iconBounds.right - icon.iconBounds.left));
            StageActor actor;
            actor.player = std::make_unique<Player>(library);
            actor.player->Select(library.FindClip("walk"), false);
            actor.player->SetPlacement({{}, AxisAngle({0, 1, 0}, static_cast<float>(i) * 2.39996f)});
            actor.player->Seek(static_cast<float>(i % 20) * .03f);
            actor.scale = std::clamp(iconWidth / (pixelsPerMeter * .58f), .35f, .8f);
            // Align the sampled head, not a guessed body height, with the original icon center.
            const Vec3 head = actor.player->Current().world[library.Joint("head")].position * actor.scale;
            actor.origin = {(px - width * .5f) / pixelsPerMeter - head.x, 0,
                            ((py - height * .5f) / pixelsPerMeter + head.y * std::cos(pitch)) / std::sin(pitch) -
                                head.z};
            actor.icon = texture;
            actors.push_back(std::move(actor));
        }
        if (actors.empty())
            for (int i = 0; i < 12; ++i)
            {
                StageActor actor;
                actor.player = std::make_unique<Player>(library);
                actor.player->Select(library.FindClip("walk"), false);
                actor.player->SetPlacement({{}, AxisAngle({0, 1, 0}, i * .7f)});
                actor.origin = {static_cast<float>(i % 4 - 2) * 2, 0, static_cast<float>(i / 4 - 1) * 3};
                actor.icon = textures[i % textures.size()];
                actors.push_back(std::move(actor));
            }
        showIcons = true;
    }
    void Create(int showCommand)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.lpszClassName = L"BesktopAnimationFoundation";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(12, 20, 31));
        RegisterClassW(&wc);
        wc.lpfnWndProc = CanvasProc;
        wc.lpszClassName = L"BesktopAnimationCanvas";
        wc.hbrBackground = nullptr;
        RegisterClassW(&wc);
        RECT bounds{0, 0, width, height};
        DWORD style = WS_OVERLAPPEDWINDOW;
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
        if (stage && !offline)
        {
            MONITORINFO info{sizeof(info)};
            GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
            bounds = info.rcMonitor;
            width = bounds.right - bounds.left;
            height = bounds.bottom - bounds.top;
            x = bounds.left;
            y = bounds.top;
            style = WS_POPUP;
        }
        else
            AdjustWindowRect(&bounds, style, FALSE);
        window = CreateWindowExW(0, L"BesktopAnimationFoundation", L"Besktop · 三维动作实验场 — 开发预览，不是正式版",
                                 style, x, y, bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr,
                                 instance, this);
        if (!window)
            throw std::runtime_error("Cannot create animation window");
        canvas = CreateWindowExW(0, L"BesktopAnimationCanvas", L"", WS_CHILD | WS_VISIBLE, 0, 78, width, height - 132,
                                 window, nullptr, instance, this);
        font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                           L"Microsoft YaHei UI");
        if (!stage)
        {
            clipBox = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 12, 9, 180, 240,
                                    window, reinterpret_cast<HMENU>(Clip), instance, nullptr);
            SendMessageW(clipBox, WM_SETFONT, reinterpret_cast<WPARAM>(font), 0);
            Button(Play, L"暂停 / 空格", 202, 8, 110);
            Button(Previous, L"前一帧", 320, 8, 80);
            Button(Next, L"后一帧", 408, 8, 80);
            Button(Slower, L"0.25×", 496, 8, 70);
            Button(NormalSpeed, L"1×", 574, 8, 50);
            Button(Reset, L"复位", 632, 8, 60);
            Button(Handshake, L"握手 H", 704, 8, 90);
            Button(Hug, L"拥抱 U", 802, 8, 90);
            Button(Cancel, L"取消 C", 900, 8, 90);
            Button(Bones, L"骨架 B", 12, 43, 95);
            Button(Feet, L"脚底修正 F", 115, 43, 120);
            Button(Icons, L"图标外观 I", 243, 43, 120);
            HWND hint = CreateWindowW(L"STATIC", L"拖动视角 · 滚轮缩放 · 1/2/3/4 正/侧/背/斜视 · Esc 退出",
                                      WS_CHILD | WS_VISIBLE, 380, 48, 660, 24, window, nullptr, instance, nullptr);
            SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(font), 0);
            slider = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ, 0, 0, 100, 20, window,
                                   reinterpret_cast<HMENU>(Timeline), instance, nullptr);
            SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 10000));
            status = CreateWindowW(L"STATIC", L"加载资源…", WS_CHILD | WS_VISIBLE, 0, 0, 100, 24, window,
                                   reinterpret_cast<HMENU>(Status), instance, nullptr);
            SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(font), 0);
        }
        library.LoadEmbedded();
        player = std::make_unique<Player>(library);
        player->SetAutoNext(false);
        player->Select(library.FindClip("walk"), false);
        pair = std::make_unique<PairSession>(library);
        if (clipBox)
        {
            for (const auto &clip : library.Clips())
                SendMessageW(clipBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Wide(clip.name).c_str()));
            SendMessageW(clipBox, CB_SETCURSEL, player->Clip(), 0);
        }
        Layout();
        renderer.Initialize(canvas, width, std::max(1, stage ? height : height - 132));
        ready = true;
        for (unsigned i = 0; i < 3; ++i)
            textures.push_back(SyntheticIcon(i));
        // Offline verification never reads the user's desktop, even through the stage EXE switch.
        if (stage && !offline)
            InitializeStage(CaptureDesktopSnapshot());
        if (syntheticStage)
        {
            DesktopSnapshot snapshot;
            snapshot.monitorBounds = {0, 0, width, height};
            snapshot.workArea = snapshot.monitorBounds;
            for (int i = 0; i < 48; ++i)
            {
                DesktopIconSnapshot icon;
                icon.displayName = L"B";
                const int x = 80 + (i % 12) * 80, y = 120 + (i / 12) * 140;
                icon.iconBounds = {x, y, x + 48, y + 48};
                snapshot.icons.push_back(icon);
            }
            InitializeStage(snapshot);
            for (size_t i = 0; i < actors.size(); ++i)
            {
                const Vec3 head = actors[i].player->Current().world[library.Joint("head")].position * actors[i].scale +
                                  actors[i].origin;
                const float px = width * .5f + head.x * 140,
                            py = height * .5f - (head.y * std::cos(.45f) - head.z * std::sin(.45f)) * 140;
                if (std::abs(px - (104 + (i % 12) * 80)) > .01f || std::abs(py - (144 + (i / 12) * 140)) > .01f)
                    throw std::runtime_error("Stage icon anchor projection mismatch");
            }
        }
        if (stage)
            Caption(
                L"Besktop 3D · 实验预览",
                L"Esc 退出   Ctrl+Shift+B 备用退出   P 暂停   I 切换图标/火柴人   B 骨架   本窗口不改变真实桌面文件");
        if (!offline)
            hotkey = RegisterHotKey(window, 7, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'B') != FALSE;
        if (!offline)
        {
            ShowWindow(window, showCommand);
            UpdateWindow(window);
            SetFocus(canvas);
            SetTimer(window, 1, 16, nullptr);
        }
        last = Clock::now();
    }
    void SeekPair(float time)
    {
        // Scrubbing is a deliberate preview restart, even after ownership was released or cancelled.
        pair = std::make_unique<PairSession>(library);
        pair->Start(selectedPair);
        pair->Seek(time);
    }
    void Command(int id)
    {
        if (!ready)
            return;
        switch (id)
        {
        case Play:
            paused = !paused;
            break;
        case Slower:
            speed = .25f;
            break;
        case NormalSpeed:
            speed = 1;
            break;
        case Previous:
        case Next:
            paused = true;
            if (pairMode)
                SeekPair(pair->Time() + (id == Next ? 1 : -1) / 60.f);
            else
                player->Seek(player->Time() + (id == Next ? 1 : -1) / 60.f);
            break;
        case Reset:
            pairMode = false;
            pair = std::make_unique<PairSession>(library);
            player->Seek(0);
            player->SetPlacement({});
            render.camera = Camera{};
            render.rootTrail.clear();
            break;
        case Handshake:
        case Hug:
            pairMode = true;
            selectedPair = id == Handshake ? PairKind::Handshake : PairKind::Hug;
            SeekPair(0);
            paused = false;
            render.camera = Camera{};
            render.rootTrail.clear();
            break;
        case Cancel:
            pair->Cancel();
            break;
        case Bones:
            render.skeleton = !render.skeleton;
            break;
        case Feet:
            player->SetCorrection(!player->Correction());
            for (auto &actor : actors)
                actor.player->SetCorrection(player->Correction());
            break;
        case Icons:
            showIcons = !showIcons;
            break;
        case Clip:
            if (!stage)
            {
                pairMode = false;
                pair = std::make_unique<PairSession>(library);
                player->Select(static_cast<int>(SendMessageW(clipBox, CB_GETCURSEL, 0, 0)));
                render.rootTrail.clear();
            }
            break;
        }
        if (!offline)
            Draw(0);
    }
    void Key(WPARAM key)
    {
        if (stage && key != VK_ESCAPE && key != VK_SPACE && key != 'P' && key != 'B' && key != 'F' && key != 'I')
            return;
        switch (key)
        {
        case VK_ESCAPE:
            DestroyWindow(window);
            return;
        case VK_SPACE:
        case 'P':
            Command(Play);
            return;
        case VK_LEFT:
            Command(Previous);
            return;
        case VK_RIGHT:
            Command(Next);
            return;
        case 'H':
            Command(Handshake);
            return;
        case 'U':
            Command(Hug);
            return;
        case 'C':
            Command(Cancel);
            return;
        case 'B':
            Command(Bones);
            return;
        case 'F':
            Command(Feet);
            return;
        case 'I':
            Command(Icons);
            return;
        case '1':
            render.camera.yaw = 0;
            render.camera.pitch = 0;
            break;
        case '2':
            render.camera.yaw = Pi / 2;
            render.camera.pitch = 0;
            break;
        case '3':
            render.camera.yaw = Pi;
            render.camera.pitch = 0;
            break;
        case '4':
            render.camera.yaw = .65f;
            render.camera.pitch = .2f;
            break;
        }
    }
    void Draw(float dt, bool present = true)
    {
        if (!ready || width < 1 || height < 1)
            return;
        const auto started = Clock::now();
        if (!paused)
        {
            if (pairMode)
                pair->Update(dt * speed);
            else
                player->Update(dt * speed);
        }
        std::vector<RenderActor> list;
        if (stage && !actors.empty() && !pair->Active())
        {
            for (size_t i = 0; i < actors.size(); ++i)
            {
                auto &actor = actors[i];
                if (!paused)
                    actor.player->Update(dt);
                actor.display = actor.player->Current();
                for (auto &joint : actor.display.world)
                    joint.position = joint.position * actor.scale + actor.origin;
                const Vec3 root = actor.display.world[0].position;
                const float xLimit = width / 280.f - .8f, zLimit = height / (280.f * std::sin(.45f)) + .5f;
                if (!paused && (std::abs(root.x) > xLimit || std::abs(root.z) > zLimit))
                {
                    const Vec3 forward = actor.player->Raw().world[0].rotation.Rotate({0, 0, 1});
                    float difference = std::atan2(-root.x, -root.z) - std::atan2(forward.x, forward.z);
                    difference = std::remainder(difference, 2 * Pi);
                    actor.player->TurnBy(std::clamp(difference, -dt * 2.5f, dt * 2.5f));
                }
                list.push_back({&actor.display, {.88f, .94f, .98f}, showIcons ? actor.icon : -1, actor.scale, .58f});
            }
        }
        else if (pairMode)
        {
            list = {{&pair->Left(), {.08f, .52f, .83f}, showIcons ? textures[0] : -1, 1},
                    {&pair->Right(), {.94f, .51f, .18f}, showIcons ? textures[1] : -1, 1}};
            render.camera.target = {0, .88f, 0};
            render.contactPoints.clear();
        }
        else
        {
            list = {{&player->Current(), {.08f, .52f, .83f}, showIcons ? textures[0] : -1, 1}};
            const auto root = player->Current().world[0].position;
            render.camera.target = {root.x, .88f, root.z};
            render.contactPoints = player->ContactTargets();
            if (dt > 0 && !paused)
            {
                render.rootTrail.push_back(root);
                if (render.rootTrail.size() > 500)
                    render.rootTrail.erase(render.rootTrail.begin());
            }
        }
        renderer.Draw(library, list, render);
        if (present)
            renderer.Present(!offline);
        frameMs = std::chrono::duration<float, std::milli>(Clock::now() - started).count();
        if (!stage && Clock::now() - lastStatus > std::chrono::milliseconds(200))
        {
            const float time = pairMode ? pair->Time() : player->Time(),
                        duration = pairMode ? 5.2f : library.Clips()[player->Clip()].duration;
            SendMessageW(slider, TBM_SETPOS, TRUE, static_cast<LPARAM>(10000 * time / duration));
            std::ostringstream info;
            info << std::fixed << std::setprecision(2) << time << " / " << duration << " s  |  " << speed << "x  |  "
                 << frameMs << " ms (includes present)  |  "
                 << (pairMode ? pair->Phase() : library.Clips()[player->Clip()].origin) << "  |  IK "
                 << (player->Correction() ? "ON" : "OFF");
            SetWindowTextW(status, Wide(info.str()).c_str());
            lastStatus = Clock::now();
        }
    }
    void OfflinePreview()
    {
        std::filesystem::create_directories(output);
        paused = true;
        Caption(L"G2 · 同一骨架 / 同一行走动作",
                L"引擎离线渲染 · 不含真实桌面 · 24 joints / meters / ozz-animation / Direct3D 11");
        for (int angle = 0; angle < 4; ++angle)
        {
            Key('1' + angle);
            player->Select(library.FindClip("walk"), false);
            player->SetPlacement({});
            player->SetCorrection(false);
            player->Seek(.65f);
            render.camera.orthographic = true;
            Draw(0, false);
            renderer.SavePng(output / ("G2-" + std::to_string(angle) + ".png"));
        }
        render.camera = Camera{};
        player->SetCorrection(true);
        player->Select(library.FindClip("walk"), false);
        player->SetPlacement({});
        player->Seek(.18f);
        Caption(L"Full body · 对称比例 / 胸腔与骨盆 / 手掌与脚掌",
                L"同一动作核心 · 可见脚底接触与滚动 · 全身正面、侧面、背面与斜视");
        for (int angle = 0; angle < 4; ++angle)
        {
            Key('1' + angle);
            render.camera.orthographic = true;
            render.camera.orthoHeight = 2.15f;
            Draw(0, false);
            renderer.SavePng(output / ("fullbody-" + std::to_string(angle) + ".png"));
        }
        Caption(L"Visible sole · 脚掌落地近景", L"脚底接触点来自可见脚掌，保留捕获的脚跟—脚尖滚动");
        for (int phase = 0; phase < 2; ++phase)
        {
            player->Seek(phase == 0 ? .18f : .65f);
            Key('2');
            render.camera.orthoHeight = .55f;
            const auto &pose = player->Current();
            render.camera.target =
                (pose.world[library.Joint("foot_l")].position + pose.world[library.Joint("foot_r")].position) * .5f +
                Vec3{0, .06f, 0};
            renderer.Draw(library, {{&pose, {.08f, .52f, .83f}}}, render);
            renderer.SavePng(output / ("foot-contact-" + std::to_string(phase) + ".png"));
        }
        render.camera = Camera{};
        for (const auto &clip : library.Clips())
        {
            Caption(Wide("G3 / " + clip.name).c_str(), Wide(clip.origin).c_str());
            player->Select(library.FindClip(clip.name), false);
            player->SetPlacement({});
            player->Seek(clip.duration * .48f);
            Draw(0, false);
            renderer.SavePng(output / ("clip-" + clip.name + ".png"));
        }
        // Fixed upper-body views make shoulder/chin spacing inspectable across asset revisions.
        for (const char *name : {"idle", "walk", "celebrate"})
        {
            const int clip = library.FindClip(name);
            player->Select(clip, false);
            player->SetPlacement({});
            player->Seek(library.Clips()[clip].duration * .48f);
            Caption(Wide(std::string("Shoulder calibration / ") + name).c_str(),
                    L"同一骨架与外观参数 · 站立、行走、举手 · 肩颈近景检查");
            for (int angle = 0; angle < 3; ++angle)
            {
                Key('1' + angle);
                render.camera.orthographic = true;
                render.camera.orthoHeight = 1.0f;
                render.camera.target = player->Current().world[library.Joint("chest")].position + Vec3{0, .12f, 0};
                // Bypass the full-body follow camera used by the interactive Draw path.
                renderer.Draw(library, {{&player->Current(), {.08f, .52f, .83f}}}, render);
                renderer.SavePng(output / (std::string("shoulder-") + name + "-" + std::to_string(angle) + ".png"));
            }
        }
        render.camera = Camera{};
        for (PairKind kind : {PairKind::Handshake, PairKind::Hug})
        {
            Caption(kind == PairKind::Hug ? L"G4 · 拥抱接触原型" : L"G4 · 握手接触原型",
                    L"共享时间线 / 固定骨长 / 双方接触点约束 · 专项编排，不是双人动捕成片");
            pairMode = true;
            pair = std::make_unique<PairSession>(library);
            pair->Start(kind);
            pair->Seek(2.4f);
            Draw(0, false);
            renderer.SavePng(output / (kind == PairKind::Hug ? "G4-hug.png" : "G4-handshake.png"));
            for (int angle = 0; angle < 4; ++angle)
            {
                Key('1' + angle);
                Draw(0, false);
                renderer.SavePng(output /
                                 ((kind == PairKind::Hug ? std::string("G4-hug-") : std::string("G4-handshake-")) +
                                  std::to_string(angle) + ".png"));
            }
            // Use a fixed upper-body camera across start/contact/recovery; full-body
            // contact screenshots alone hide regressions at the beginning of a pair.
            const float neckTimes[] = {0, 2.4f, 5.2f};
            const wchar_t *neckPhases[] = {L"起始中立姿态", L"接触阶段 · 有限头颈动作", L"释放后恢复中立"};
            for (int phase = 0; phase < 3; ++phase)
            {
                pair->Seek(neckTimes[phase]);
                Caption(kind == PairKind::Hug ? L"拥抱 · 头颈检查" : L"握手 · 头颈检查", neckPhases[phase]);
                render.camera.yaw = .95f;
                render.camera.pitch = .08f;
                render.camera.orthographic = true;
                render.camera.orthoHeight = .9f;
                render.camera.target = (pair->Left().world[library.Joint("chest")].position +
                                        pair->Right().world[library.Joint("chest")].position) *
                                           .5f +
                                       Vec3{0, .10f, 0};
                renderer.Draw(library, {{&pair->Left(), {.08f, .52f, .83f}}, {&pair->Right(), {.93f, .43f, .12f}}},
                              render);
                renderer.SavePng(output / (std::string("pair-neck-") + (kind == PairKind::Hug ? "hug-" : "handshake-") +
                                           std::to_string(phase) + ".png"));
            }
            const float footTimes[] = {0, .45f, 2.4f, 4.65f, 5.2f};
            for (int phase = 0; phase < 5; ++phase)
            {
                pair->Seek(footTimes[phase]);
                Caption(kind == PairKind::Hug ? L"拥抱 · 下肢检查" : L"握手 · 下肢检查",
                        L"中立站姿 / 抬脚接近 / 站定接触 / 退回 / 收尾 · 脚掌朝向与膝盖连续性");
                render.camera.yaw = 1.0f;
                render.camera.pitch = .12f;
                render.camera.orthographic = true;
                render.camera.orthoHeight = 1.25f;
                render.camera.target = {0, .48f, 0};
                renderer.Draw(library, {{&pair->Left(), {.08f, .52f, .83f}}, {&pair->Right(), {.93f, .43f, .12f}}},
                              render);
                renderer.SavePng(output / (std::string("pair-feet-") + (kind == PairKind::Hug ? "hug-" : "handshake-") +
                                           std::to_string(phase) + ".png"));
            }
            pair->Seek(2.4f);
            render.camera = Camera{};
        }
        Caption(L"G5 · 图标只是外观", L"图标角色与火柴人复用同一骨架、动作采样与接触求解");
        showIcons = true;
        Draw(0, false);
        renderer.SavePng(output / "G5-icon-hug.png");
        if (sequences)
        {
            std::filesystem::create_directories(output / "frames");
            int frameIndex = 0;
            for (int scene = 0; scene < 6; ++scene)
            {
                pairMode = scene >= 3;
                pair = std::make_unique<PairSession>(library);
                showIcons = scene == 5;
                render.rootTrail.clear();
                render.camera = Camera{};
                paused = false;
                const wchar_t *titles[] = {L"G2 / G3 · CMU 真实行走 · 脚底接触修正",
                                           L"G3 · 行走 → 减速停止 → 站立 → 转弯",
                                           L"G3 · 庆祝（制作端关键帧编排）",
                                           L"G4 · 握手：接近、接触、释放",
                                           L"G4 · 拥抱：双人共享时间线",
                                           L"G5 · 相同动作核心，换成图标外观"};
                Caption(titles[scene], L"开发预览 · 引擎离线渲染，非桌面录屏 · 自然度仍需人工观看判断");
                if (scene < 3)
                {
                    player->Select(library.FindClip(scene == 2 ? "celebrate" : "walk"), false);
                    player->SetPlacement({});
                    player->SetCorrection(true);
                }
                else
                    pair->Start(scene == 3 ? PairKind::Handshake : PairKind::Hug);
                const int count = scene == 1 ? 240 : (scene < 3 ? 120 : 160);
                for (int frame = 0; frame < count; ++frame)
                {
                    if (scene == 1 && frame == 40)
                        player->Select(library.FindClip("stop"));
                    if (scene == 1 && frame == 110)
                        player->Select(library.FindClip("idle"));
                    if (scene == 1 && frame == 130)
                        player->Select(library.FindClip("turn"));
                    Draw(1 / 30.f, false);
                    std::ostringstream filename;
                    filename << "frame-" << std::setw(5) << std::setfill('0') << frameIndex++ << ".png";
                    renderer.SavePng(output / "frames" / filename.str());
                }
            }
            paused = true;
        }
        // Engine-rendered synthetic frames only. Never capture real desktop pixels during verification.
        pairMode = false;
        pair = std::make_unique<PairSession>(library);
        showIcons = false;
        renderer.SetOverlay(-1);
        renderer.Resize(1920, 1080);
        std::ofstream benchmark(output / "render-benchmark.csv");
        benchmark
            << "actors,appearance,frames,width,height,mean_completed_frame_ms,working_set_mib,private_mib,adapter\n";
        for (bool icons : {false, true})
            for (int count : {1, 24, 64, 100})
            {
                std::vector<std::unique_ptr<Player>> crowd;
                std::vector<RenderActor> visible;
                for (int i = 0; i < count; ++i)
                {
                    auto p = std::make_unique<Player>(library);
                    p->Select(library.FindClip("walk"), false);
                    p->SetPlacement({{(i % 10 - 5) * 1.3f, 0, (i / 10) * 1.8f}, {}});
                    visible.push_back(
                        {&p->Current(), {.08f, .52f, .83f}, icons ? textures[i % textures.size()] : -1, 1});
                    crowd.push_back(std::move(p));
                }
                RenderOptions options;
                options.camera = {.3f, .65f, 28, {0, 0, 7}};
                for (int warmup = 0; warmup < 30; ++warmup)
                {
                    renderer.Draw(library, visible, options);
                    renderer.Present(false);
                    renderer.WaitForGpu();
                }
                const auto start = Clock::now();
                for (int frame = 0; frame < 120; ++frame)
                {
                    for (auto &p : crowd)
                        p->Update(1 / 60.f);
                    renderer.Draw(library, visible, options);
                    renderer.Present(false);
                    renderer.WaitForGpu();
                }
                const float milliseconds = std::chrono::duration<float, std::milli>(Clock::now() - start).count() / 120;
                PROCESS_MEMORY_COUNTERS_EX memory{};
                memory.cb = sizeof(memory);
                if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                                          sizeof(memory)))
                    throw std::runtime_error("Cannot read benchmark process memory");
                benchmark << count << "," << (icons ? "icons" : "stick") << ",120,1920,1080," << milliseconds << ","
                          << memory.WorkingSetSize / 1048576.0 << "," << memory.PrivateUsage / 1048576.0 << ",\""
                          << renderer.Adapter() << "\"\n";
                if (count == 64)
                    renderer.SavePng(output / "G5-crowd64.png");
            }
        std::ofstream success(output / "preview-complete.txt");
        success << "D3D11 rendered synthetic scene only\nAdapter: " << renderer.Adapter() << "\n";
    }
    void UiSmokeTest()
    {
        std::filesystem::create_directories(output);
        auto require = [&](bool condition, const char *message) {
            if (!condition)
                throw std::runtime_error(message);
            if (!failure.empty())
                throw std::runtime_error(failure);
        };
        require(!stage && !hotkey && actors.empty(),
                "Hidden UI test must not capture the desktop or register a global hotkey");
        SendMessageW(canvas, WM_KEYDOWN, VK_SPACE, 0);
        require(paused, "Pause keyboard route");
        SendMessageW(window, WM_COMMAND, Next, 0);
        require(player->Time() > 0, "Single frame step");
        SendMessageW(slider, TBM_SETPOS, TRUE, 5000);
        SendMessageW(window, WM_HSCROLL, 0, reinterpret_cast<LPARAM>(slider));
        require(std::abs(player->Time() - library.Clips()[player->Clip()].duration * .5f) < .001f,
                "Single timeline route");
        for (int id : {Handshake, Hug})
        {
            SendMessageW(window, WM_COMMAND, id, 0);
            for (const Pose *pose : {&pair->Left(), &pair->Right()})
            {
                const Vec3 neck =
                    Normal(pose->world[library.Joint("head")].position - pose->world[library.Joint("neck")].position);
                require(Dot(neck, {0, 1, 0}) > .9999f, "Pair command must begin with neutral neck");
            }
            pair->Seek(5.2f);
            pair->Update(0);
            require(!pair->Active() && pairMode, "Completed pair must retain its final preview");
            SendMessageW(window, WM_COMMAND, Previous, 0);
            require(pair->Active() && pair->Time() < 5.2f, "Completed pair frame step must replay");
            SendMessageW(slider, TBM_SETPOS, TRUE, 5000);
            SendMessageW(window, WM_HSCROLL, 0, reinterpret_cast<LPARAM>(slider));
            require(std::abs(pair->Time() - 2.6f) < .001f, "Pair timeline route");
            SendMessageW(canvas, WM_KEYDOWN, 'C', 0);
            for (int i = 0; i < 30; ++i)
                pair->Update(1 / 60.f);
            require(!pair->Active(), "Cancel route releases both actors");
            Draw(0, false);
        }
        SendMessageW(window, WM_COMMAND, Reset, 0);
        require(!pairMode, "Reset returns to single player");
        for (int i = 0; i < static_cast<int>(library.Clips().size()); ++i)
        {
            SendMessageW(clipBox, CB_SETCURSEL, i, 0);
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(Clip, CBN_SELCHANGE), reinterpret_cast<LPARAM>(clipBox));
            Draw(0, false);
            require(player->Clip() == i, "Clip selector route");
        }
        for (int key : {'1', '2', '3', '4', 'B', 'F', 'I'})
            SendMessageW(canvas, WM_KEYDOWN, key, 0);
        RECT bounds{};
        GetWindowRect(window, &bounds);
        SetWindowPos(window, nullptr, 0, 0, 1240, 840, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
        Draw(0, false);
        require(width > 1100 && height > 760, "Resize route");
        SendMessageW(canvas, WM_KEYDOWN, VK_ESCAPE, 0);
        require(!IsWindow(window), "Escape closes native window");
        std::ofstream(output / "ui-smoke-test.txt") << "PASS hidden native controls, keyboard, timeline, completion "
                                                       "replay, cancel, resize and Esc; no desktop capture\n";
    }
    void StageSmokeTest()
    {
        std::filesystem::create_directories(output);
        if (!offline || !syntheticStage || actors.size() != 48 || hotkey)
            throw std::runtime_error("Synthetic stage isolation failed");
        Caption(L"G5 · 合成桌面尺寸 / 48 个图标角色",
                L"自动验证画面，不是真实桌面 · 与独立实验场共用骨架、动作与渲染器");
        Draw(0, false);
        renderer.SavePng(output / "G5-stage-initial.png");
        for (int i = 0; i < 180; ++i)
            Draw(1 / 60.f, false);
        renderer.SavePng(output / "G5-stage-walking.png");
        SendMessageW(canvas, WM_KEYDOWN, 'P', 0);
        const float previous = actors[0].player->Time();
        Draw(.05f, false);
        if (actors[0].player->Time() != previous)
            throw std::runtime_error("Stage pause failed");
        SendMessageW(canvas, WM_KEYDOWN, 'H', 0);
        if (pairMode)
            throw std::runtime_error("Lab-only command leaked into stage");
        SendMessageW(canvas, WM_KEYDOWN, 'I', 0);
        SendMessageW(canvas, WM_KEYDOWN, 'B', 0);
        Draw(0, false);
        SendMessageW(window, WM_HOTKEY, 7, 0);
        if (IsWindow(window) || !failure.empty())
            throw std::runtime_error("Stage backup exit route failed");
        std::ofstream(output / "stage-smoke-test.txt")
            << "PASS 48 synthetic desktop icons, exact anchor projection, walking, pause, appearance, command "
               "isolation and backup exit handler; no real desktop read\n";
    }
    static LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        App *app = reinterpret_cast<App *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE)
        {
            app = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app)
            return DefWindowProcW(hwnd, msg, wp, lp);
        try
        {
            if (msg == WM_ERASEBKGND)
                return 1;
            if (msg == WM_PAINT)
            {
                PAINTSTRUCT ps{};
                BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
                return 0;
            }
            if (msg == WM_KEYDOWN)
            {
                app->Key(wp);
                return 0;
            }
            if (msg == WM_LBUTTONDOWN)
            {
                SetFocus(hwnd);
                app->dragging = true;
                app->pointer = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                SetCapture(hwnd);
                return 0;
            }
            if (msg == WM_LBUTTONUP)
            {
                app->dragging = false;
                ReleaseCapture();
                return 0;
            }
            if (msg == WM_CAPTURECHANGED)
                app->dragging = false;
            if (msg == WM_MOUSEMOVE && app->dragging && !app->stage)
            {
                const POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                app->render.camera.yaw -= (p.x - app->pointer.x) * .008f;
                app->render.camera.pitch =
                    std::clamp(app->render.camera.pitch + (p.y - app->pointer.y) * .006f, -.6f, 1.3f);
                app->pointer = p;
                return 0;
            }
            if (msg == WM_MOUSEWHEEL && !app->stage)
            {
                const float factor = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? .9f : 1.1f;
                app->render.camera.distance = std::clamp(app->render.camera.distance * factor, 1.7f, 20.f);
                return 0;
            }
        }
        catch (const std::exception &e)
        {
            app->failure = e.what();
            DestroyWindow(app->window);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        App *app = reinterpret_cast<App *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE)
        {
            app = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->window = hwnd;
        }
        if (!app)
            return DefWindowProcW(hwnd, msg, wp, lp);
        try
        {
            switch (msg)
            {
            case WM_COMMAND:
                if (LOWORD(wp) != Clip || HIWORD(wp) == CBN_SELCHANGE)
                    app->Command(LOWORD(wp));
                SetFocus(app->canvas);
                return 0;
            case WM_KEYDOWN:
                app->Key(wp);
                return 0;
            case WM_HOTKEY:
                DestroyWindow(hwnd);
                return 0;
            case WM_GETMINMAXINFO:
                if (!app->stage)
                {
                    auto *limits = reinterpret_cast<MINMAXINFO *>(lp);
                    limits->ptMinTrackSize = {1040, 540};
                }
                return 0;
            case WM_SIZE:
                if (wp != SIZE_MINIMIZED)
                    app->Layout();
                return 0;
            case WM_HSCROLL:
                if (app->ready)
                {
                    app->paused = true;
                    const float ratio = static_cast<float>(SendMessageW(app->slider, TBM_GETPOS, 0, 0)) / 10000;
                    if (app->pairMode)
                        app->SeekPair(ratio * 5.2f);
                    else
                        app->player->Seek(ratio * app->library.Clips()[app->player->Clip()].duration);
                    app->Draw(0);
                }
                return 0;
            case WM_TIMER: {
                const auto now = Clock::now();
                const float dt = std::chrono::duration<float>(now - app->last).count();
                app->last = now;
                if (!IsIconic(hwnd))
                    app->Draw(std::min(dt, .1f));
                return 0;
            }
            case WM_CTLCOLORSTATIC:
                SetTextColor(reinterpret_cast<HDC>(wp), RGB(200, 218, 231));
                SetBkColor(reinterpret_cast<HDC>(wp), RGB(12, 20, 31));
                return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
            case WM_DESTROY:
                KillTimer(hwnd, 1);
                if (app->hotkey)
                {
                    UnregisterHotKey(hwnd, 7);
                    app->hotkey = false;
                }
                PostQuitMessage(0);
                return 0;
            }
        }
        catch (const std::exception &e)
        {
            app->failure = e.what();
            DestroyWindow(hwnd);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
};
} // namespace
int RunAnimationApp(HINSTANCE instance, int showCommand, bool desktopStage)
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    SetProcessDPIAware();
    int result = 0;
    bool offline = false;
    std::filesystem::path reportDirectory;
    try
    {
        App app(instance, desktopStage);
        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; ++i)
        {
            if ((std::wstring(argv[i]) == L"--render-preview" || std::wstring(argv[i]) == L"--ui-self-test") &&
                i + 1 < argc)
            {
                app.uiTest = std::wstring(argv[i]) == L"--ui-self-test";
                app.offline = true;
                app.stage = false;
                app.output = argv[++i];
                offline = true;
                reportDirectory = app.output;
            }
            else if (std::wstring(argv[i]) == L"--synthetic-stage-test" && i + 1 < argc)
            {
                app.offline = true;
                app.syntheticStage = true;
                app.stage = true;
                app.output = argv[++i];
                offline = true;
                reportDirectory = app.output;
            }
            else if (std::wstring(argv[i]) == L"--sequences")
                app.sequences = true;
        }
        LocalFree(argv);
        const auto initializeStart = Clock::now();
        app.Create(showCommand);
        if (app.offline)
        {
            std::filesystem::create_directories(app.output);
            const float initializeMs = std::chrono::duration<float, std::milli>(Clock::now() - initializeStart).count();
            std::ofstream(app.output / (app.syntheticStage ? "stage-initialize.txt" : "lab-initialize.txt"))
                << "Hidden App::Create ready in " << initializeMs << " ms (not end-user cold-launch latency)\n";
            if (app.syntheticStage)
                app.StageSmokeTest();
            else if (app.uiTest)
                app.UiSmokeTest();
            else
                app.OfflinePreview();
            if (IsWindow(app.window))
                DestroyWindow(app.window);
        }
        else
        {
            MSG msg{};
            while (GetMessageW(&msg, nullptr, 0, 0) > 0)
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (!app.failure.empty())
            throw std::runtime_error(app.failure);
    }
    catch (const std::exception &e)
    {
        OutputDebugStringA(e.what());
        if (offline)
        {
            std::filesystem::create_directories(reportDirectory);
            std::ofstream(reportDirectory / "preview-error.txt") << e.what();
        }
        else
            MessageBoxW(nullptr, Wide(e.what()).c_str(), L"Besktop 3D — 启动或渲染失败", MB_OK | MB_ICONERROR);
        result = 1;
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return result;
}
} // namespace besktop::anim3d
