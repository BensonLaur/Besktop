#include "besktop/render/stage_guide_renderer.h"
#include "besktop/render/stage_guide_support_images.h"
#include "besktop/version.h"

#include <filesystem>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace besktop;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::unique_ptr<Gdiplus::Bitmap> Render(int width, int height,
    const StageGuideNpcState& state, const StageGuideLayout& layout,
    const StageGuideSupportImages& images, StageGuideCardCache* cache)
{
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(bitmap.get());
    graphics.Clear(Gdiplus::Color(255, 222, 231, 238));
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
    DrawStageGuideNpc(graphics, state, layout, &images, cache);
    Require(graphics.GetLastStatus() == Gdiplus::Ok, "render status");
    Require(graphics.GetInterpolationMode() == Gdiplus::InterpolationModeBilinear,
        "cache must restore caller interpolation");
    return bitmap;
}

void Compare(Gdiplus::Bitmap& direct, Gdiplus::Bitmap& cached,
    const StageGuideRect* exactRegion = nullptr, bool compareQrMask = false)
{
    const int width = direct.GetWidth(), height = direct.GetHeight();
    Gdiplus::Rect bounds(0, 0, width, height);
    Gdiplus::BitmapData a{}, b{};
    Require(direct.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &a) == Gdiplus::Ok,
        "lock direct pixels");
    const auto locked = cached.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &b);
    if (locked != Gdiplus::Ok) { direct.UnlockBits(&a); throw std::runtime_error("lock cached pixels"); }
    std::size_t error = 0, majorPixels = 0, exactMismatches = 0;
    int exactMax = 0;
    for (int y = 0; y < height; ++y) {
        const auto* rowA = static_cast<const BYTE*>(a.Scan0) + y * a.Stride;
        const auto* rowB = static_cast<const BYTE*>(b.Scan0) + y * b.Stride;
        for (int x = 0; x < width; ++x) {
            int largest = 0;
            for (int channel = 0; channel < 4; ++channel) {
                const int delta = std::abs(rowA[x * 4 + channel] - rowB[x * 4 + channel]);
                error += delta;
                largest = std::max(largest, delta);
            }
            if (largest > 16) ++majorPixels;
            if (exactRegion && StageGuidePointInRect({static_cast<double>(x), static_cast<double>(y)},
                    *exactRegion) && largest != 0) {
                // Alipay's supplied RGB poster has near-black/white compression
                // noise. GDI+ x86 resampling can choose an adjacent source row at
                // exact scale boundaries: require the QR mask, not that noise,
                // to match bit-for-bit. Character pixels are compared literally.
                const auto dark = [x](const BYTE* row) {
                    return row[x * 4] * 114 + row[x * 4 + 1] * 587 + row[x * 4 + 2] * 299 < 128000;
                };
                if (compareQrMask && dark(rowA) == dark(rowB)) continue;
                ++exactMismatches;
                exactMax = std::max(exactMax, largest);
            }
        }
    }
    direct.UnlockBits(&a);
    cached.UnlockBits(&b);
    // Transparent intermediate compositing can round antialiased edge channels.
    // QR black/white pixels must remain identical. Poster text/photo resampling may differ
    // very slightly due to GDI+'s inverse-transform rounding at integer offsets.
    const double mean = static_cast<double>(error) / (width * height * 4.0);
    if (mean >= 0.5 || majorPixels > width * height * 0.002 || exactMismatches != 0) {
        std::cerr << "pixel comparison: mean=" << mean << " major=" << majorPixels
            << " exact-region mismatches=" << exactMismatches << " max=" << exactMax << '\n';
        throw std::runtime_error("cached card differs from direct rendering");
    }
}

void CacheLifecycle(StageGuideSupportImages& images)
{
    StageGuideCardCache cache;
    StageGuideNpcState state;
    state.phase = StageGuideNpcPhase::ShowingSupport;
    state.position = {1000, 450};
    StageGuideLayoutInput input;
    input.workArea = {0, 0, 1280, 900};
    input.bodyCenter = state.position;
    input.showSupport = true;
    auto layout = ComputeStageGuideLayout(input);
    auto verify = [&] {
        auto direct = Render(1280, 900, state, layout, images, nullptr);
        auto cached = Render(1280, 900, state, layout, images, &cache);
        const StageGuideRect characterCenter{state.position.x - 25, state.position.y - 16,
            state.position.x + 25, state.position.y + 16};
        try {
            Compare(*direct, *cached, &characterCenter);
        } catch (...) {
            std::cerr << "lifecycle builds=" << cache.BuildCount() << " motion=" << state.motionPhase << '\n';
            throw;
        }
    };
    verify();
    for (int i = 0; i < 12; ++i) {
        state.moving = true;
        state.motionPhase = i * 0.073;
        state.pointerPresent = true;
        state.pointerPosition = {50.0 + i * 30, 150};
        verify();
    }
    Require(cache.BuildCount() == 1, "animated character must not rebuild the card");
    state.supportProvider = StageGuideSupportProvider::Alipay;
    verify();
    Require(cache.BuildCount() == 2, "provider must invalidate");
    state.supportProvider = StageGuideSupportProvider::WeChat;
    verify();
    Require(cache.BuildCount() == 3, "switching back must not retain Alipay");
    Require(!images.Load(GetModuleHandleW(L"kernel32.dll")), "failed resource reload");
    verify();
    Require(cache.BuildCount() == 4, "failed reload must discard old payment image");
    Require(images.Load(GetModuleHandleW(nullptr)), "resource reload");
    verify();
    Require(cache.BuildCount() == 5, "resource reload must invalidate");
    input.dpiScale = 1.5;
    layout = ComputeStageGuideLayout(input);
    verify();
    Require(cache.BuildCount() == 6, "DPI must invalidate");
    input.workArea = {20, 10, 1240, 850};
    layout = ComputeStageGuideLayout(input);
    verify();
    Require(cache.BuildCount() == 7, "work-area layout must invalidate");
    input.showSupport = false;
    input.showAbout = true;
    state.phase = StageGuideNpcPhase::ShowingAbout;
    layout = ComputeStageGuideLayout(input);
    verify();
    Require(cache.BuildCount() == 8, "card type must invalidate");
    input.bodyCenter = state.position = {800, 760};
    layout = ComputeStageGuideLayout(input);
    verify();
    Require(cache.BuildCount() == 9, "card and speech-tail position must invalidate");
    cache.Clear();
    verify();
    Require(cache.BuildCount() == 10, "explicit reset must rebuild");
    input.showAbout = false;
    state.phase = StageGuideNpcPhase::Roaming;
    layout = ComputeStageGuideLayout(input);
    verify();
    Require(cache.BuildCount() == 10, "closed card must not be drawn or rebuilt");
    input.showExternalConfirmation = true;
    input.confirmingEntry = StageGuideMenuEntry::Feedback;
    layout = ComputeStageGuideLayout(input);
    verify();
    Require(cache.BuildCount() == 11, "confirmation card must invalidate");
    layout.confirmingEntry = StageGuideMenuEntry::Support;
    verify();
    Require(cache.BuildCount() == 12, "confirmation content must invalidate");
    auto oversized = layout;
    oversized.cardRect = {0, 0, 100000, 100000};
    Gdiplus::Bitmap target(32, 32, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&target);
    Require(!cache.Draw(graphics, state, oversized, &images), "oversized cache must fall back");
    verify();
    Require(cache.BuildCount() == 13, "failed build must release previous cached pixels");
}

template<class Draw> void Measure(const char* name, Draw draw)
{
    for (int i = 0; i < 8; ++i) { draw(); GdiFlush(); }
    std::vector<double> samples;
    for (int i = 0; i < 80; ++i) {
        const auto start = std::chrono::steady_clock::now();
        draw();
        GdiFlush();
        samples.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
    }
    double sum = 0;
    for (double value : samples) sum += value;
    std::sort(samples.begin(), samples.end());
    std::cout << name << ", mean=" << sum / samples.size() << ", p95=" << samples[76] << " ms\n";
}

int Benchmark(StageGuideSupportImages& images)
{
    std::cout << std::fixed << std::setprecision(3);
    for (const double dpi : {1.0, 1.5}) {
        const int width = dpi == 1 ? 1600 : 2753, height = dpi == 1 ? 952 : 1906;
        const HDC screen = GetDC(nullptr);
        const HDC memory = CreateCompatibleDC(screen);
        const HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
        ReleaseDC(nullptr, screen);
        if (!memory || !bitmap) {
            if (memory) DeleteDC(memory);
            if (bitmap) DeleteObject(bitmap);
            return 8;
        }
        const auto previous = SelectObject(memory, bitmap);
        PatBlt(memory, 0, 0, width, height, BLACKNESS);
        std::cout << "fixture=" << width << 'x' << height << ", dpi=" << dpi << '\n';
        StageGuideNpcState state;
        state.phase = StageGuideNpcPhase::ShowingSupport;
        StageGuideLayoutInput input;
        input.workArea = {0, 0, static_cast<double>(width), static_cast<double>(height)};
        input.bodyCenter = state.position = {width * 0.82, height * 0.55};
        input.dpiScale = dpi;
        for (const char* card : {"wechat", "alipay", "about"}) {
            input.showSupport = std::string(card) != "about";
            input.showAbout = !input.showSupport;
            state.phase = input.showAbout ? StageGuideNpcPhase::ShowingAbout : StageGuideNpcPhase::ShowingSupport;
            state.supportProvider = std::string(card) == "alipay" ?
                StageGuideSupportProvider::Alipay : StageGuideSupportProvider::WeChat;
            const auto layout = ComputeStageGuideLayout(input);
            StageGuideCardCache cache;
            for (bool useCache : {false, true}) {
                const auto label = std::string(card) + (useCache ? "-cached-with-character" : "-direct-with-character");
                Measure(label.c_str(), [&] {
                    Gdiplus::Graphics graphics(memory);
                    DrawStageGuideNpc(graphics, state, layout, &images, useCache ? &cache : nullptr);
                    graphics.Flush(Gdiplus::FlushIntentionSync);
                });
            }
            Require(cache.BuildCount() == 1, "steady-state benchmark rebuilt cache");
        }
        SelectObject(memory, previous);
        DeleteObject(bitmap);
        DeleteDC(memory);
    }
    return 0;
}

bool PngEncoder(CLSID& id)
{
    UINT count = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &size) != Gdiplus::Ok || size == 0) return false;
    std::vector<BYTE> bytes(size);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(bytes.data());
    if (Gdiplus::GetImageEncoders(count, size, encoders) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < count; ++i) {
        if (std::wcscmp(encoders[i].MimeType, L"image/png") == 0) { id = encoders[i].Clsid; return true; }
    }
    return false;
}
}

// Off-screen rendering exercises the same resources/renderer without reading
// the user's desktop, opening payment links or starting a full-screen stage.
int RunTests(int argc, wchar_t** argv)
{
    const std::string version = BESKTOP_VERSION_STRING;
    const std::wstring expectedVersion(version.begin(), version.end());
    const std::wstring label = besktop::kBuildLabel;
    Require(label.starts_with(L"v" + expectedVersion + L" \u00B7 "), "label must use configured version");
#if defined(_M_X64) || defined(_M_IX86)
    Require(label.ends_with(sizeof(void*) == 8 ? L"x64" : L"x86"),
        "label must describe executable architecture, not host OS");
#endif
    besktop::StageGuideSupportImages images;
    if (images.IsReady() || !images.Load(GetModuleHandleW(nullptr)) || !images.IsReady()) return 1;
    if (images.Load(GetModuleHandleW(L"kernel32.dll")) || images.IsReady()) return 2;
    if (!images.Load(GetModuleHandleW(nullptr))) return 3;
    if (argc > 1 && std::wstring(argv[1]) == L"--benchmark") return Benchmark(images);
    CacheLifecycle(images);
    CLSID png{};
    if (argc > 1 && !PngEncoder(png)) return 4;
    if (argc > 1) std::filesystem::create_directories(argv[1]);
    struct Fixture { int width; int height; double dpi; const wchar_t* name; };
    for (const auto& fixture : {
            Fixture{1600, 952, 1.0, L"desktop-100"},
            Fixture{2753, 1906, 1.5, L"large-150"},
            Fixture{1280, 720, 1.5, L"laptop-150"},
            Fixture{800, 560, 2.0, L"compact-200"}}) {
        for (int card = 0; card < 5; ++card) {
            const auto provider = card == 1 ? StageGuideSupportProvider::Alipay : StageGuideSupportProvider::WeChat;
            besktop::StageGuideNpcState state;
            state.phase = card < 2 ? StageGuideNpcPhase::ShowingSupport : StageGuideNpcPhase::ShowingAbout;
            state.position = card < 2 ? StageGuidePoint{fixture.width * 0.82, fixture.height * 0.55} :
                StageGuidePoint{fixture.width * (card == 2 ? 0.03 : 0.92), fixture.height * (card == 2 ? 0.08 : 0.88)};
            state.supportProvider = provider;
            besktop::StageGuideLayoutInput input;
            input.workArea = {0, 0, static_cast<double>(fixture.width), static_cast<double>(fixture.height)};
            input.bodyCenter = state.position;
            input.showSupport = card < 2;
            input.showAbout = card == 2 || card == 3;
            input.showExternalConfirmation = card == 4;
            input.confirmingEntry = StageGuideMenuEntry::Feedback;
            input.dpiScale = fixture.dpi;
            const auto layout = besktop::ComputeStageGuideLayout(input);
            StageGuideCardCache cache;
            auto direct = Render(fixture.width, fixture.height, state, layout, images, nullptr);
            auto bitmap = Render(fixture.width, fixture.height, state, layout, images, &cache);
            StageGuideRect payment{};
            if (card < 2) {
                const double w = card == 0 ? 1266 : 1080, h = card == 0 ? 1749 : 1680;
                const auto& bounds = layout.supportImageRect;
                const double scale = std::min((bounds.right - bounds.left) / w, (bounds.bottom - bounds.top) / h);
                payment.left = std::round((bounds.left + bounds.right - w * scale) * 0.5);
                payment.top = std::round((bounds.top + bounds.bottom - h * scale) * 0.5);
                // Source-space QR bounds in the two unchanged supplied posters.
                const StageGuideRect qr = card == 0 ? StageGuideRect{321, 462, 945, 1086} :
                    StageGuideRect{292, 470, 787, 966};
                const double sx = std::round(w * scale) / w, sy = std::round(h * scale) / h;
                payment.right = std::floor(payment.left + qr.right * sx) - 1;
                payment.bottom = std::floor(payment.top + qr.bottom * sy) - 1;
                payment.left = std::ceil(payment.left + qr.left * sx);
                payment.top = std::ceil(payment.top + qr.top * sy);
            }
            Compare(*direct, *bitmap, card < 2 ? &payment : nullptr, card < 2);
            Require(cache.BuildCount() == 1, "fixture cache did not build");
            if (argc > 1) {
                const auto name = std::wstring(fixture.name) +
                    std::array{L"-wechat.png", L"-alipay.png", L"-about-top.png", L"-about-bottom.png", L"-feedback.png"}[card];
                const auto path = std::filesystem::path(argv[1]) / name;
                if (bitmap->Save(path.c_str(), &png) != Gdiplus::Ok) return 7;
            }
        }
    }
    std::cout << "support resources, cache lifecycle/pixel equivalence and 20 render fixtures passed\n";
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    try {
        return RunTests(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 10;
    }
}
