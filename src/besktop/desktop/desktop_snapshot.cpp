#include "besktop/desktop/desktop_snapshot.h"

#include "besktop/logging/logger.h"

#include <commctrl.h>
#include <shellscalingapi.h>
#include <filesystem>
#include <iterator>
#include <objbase.h>
#include <exdisp.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <unordered_map>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

std::wstring FormatHResult(HRESULT result)
{
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(result));
    return buffer;
}

std::wstring FormatRect(const RECT& rect)
{
    return L"(" +
        std::to_wstring(rect.left) +
        L"," +
        std::to_wstring(rect.top) +
        L")-(" +
        std::to_wstring(rect.right) +
        L"," +
        std::to_wstring(rect.bottom) +
        L") " +
        std::to_wstring(rect.right - rect.left) +
        L" x " +
        std::to_wstring(rect.bottom - rect.top);
}

UINT GetDpiForSystemCompat()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        using GetDpiForSystemFn = UINT(WINAPI*)();
        auto* getDpiForSystem = reinterpret_cast<GetDpiForSystemFn>(
            GetProcAddress(user32, "GetDpiForSystem"));
        if (getDpiForSystem != nullptr) {
            return getDpiForSystem();
        }
    }
    HDC screenDc = GetDC(nullptr);
    const UINT dpi = screenDc != nullptr ? static_cast<UINT>(GetDeviceCaps(screenDc, LOGPIXELSX)) : 96;
    if (screenDc != nullptr) {
        ReleaseDC(nullptr, screenDc);
    }
    return dpi > 0 ? dpi : 96;
}

UINT GetDpiForWindowCompat(HWND hwnd)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        auto* getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (getDpiForWindow != nullptr) {
            const UINT dpi = getDpiForWindow(hwnd);
            if (dpi > 0) {
                return dpi;
            }
        }
    }
    return GetDpiForSystemCompat();
}

besktop::WallpaperLayout MapWallpaperLayout(DESKTOP_WALLPAPER_POSITION position)
{
    switch (position) {
    case DWPOS_CENTER:
        return besktop::WallpaperLayout::Center;
    case DWPOS_TILE:
        return besktop::WallpaperLayout::Tile;
    case DWPOS_STRETCH:
        return besktop::WallpaperLayout::Stretch;
    case DWPOS_FIT:
        return besktop::WallpaperLayout::Fit;
    case DWPOS_FILL:
        return besktop::WallpaperLayout::Fill;
    case DWPOS_SPAN:
        return besktop::WallpaperLayout::Span;
    default:
        return besktop::WallpaperLayout::Unknown;
    }
}

class ScopedComInitializer {
public:
    ScopedComInitializer()
    {
        result_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        shouldUninitialize_ = SUCCEEDED(result_);
    }

    ScopedComInitializer(const ScopedComInitializer&) = delete;
    ScopedComInitializer& operator=(const ScopedComInitializer&) = delete;

    ~ScopedComInitializer()
    {
        if (shouldUninitialize_) {
            CoUninitialize();
        }
    }

    bool IsUsable() const
    {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

    HRESULT Result() const
    {
        return result_;
    }

private:
    HRESULT result_ = E_FAIL;
    bool shouldUninitialize_ = false;
};

class ScopedCoTaskMemString {
public:
    explicit ScopedCoTaskMemString(PWSTR value)
        : value_(value)
    {
    }

    ScopedCoTaskMemString(const ScopedCoTaskMemString&) = delete;
    ScopedCoTaskMemString& operator=(const ScopedCoTaskMemString&) = delete;

    ~ScopedCoTaskMemString()
    {
        if (value_ != nullptr) {
            CoTaskMemFree(value_);
        }
    }

    const wchar_t* Get() const
    {
        return value_;
    }

private:
    PWSTR value_ = nullptr;
};

class ScopedPidl {
public:
    explicit ScopedPidl(PITEMID_CHILD value)
        : value_(value)
    {
    }

    ScopedPidl(const ScopedPidl&) = delete;
    ScopedPidl& operator=(const ScopedPidl&) = delete;

    ~ScopedPidl()
    {
        if (value_ != nullptr) {
            CoTaskMemFree(value_);
        }
    }

    PITEMID_CHILD Get() const
    {
        return value_;
    }

private:
    PITEMID_CHILD value_ = nullptr;
};

void CapturePrimaryMonitorBounds(besktop::DesktopSnapshot& snapshot)
{
    const POINT primaryPoint{0, 0};
    HMONITOR primaryMonitor = MonitorFromPoint(primaryPoint, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (primaryMonitor != nullptr && GetMonitorInfoW(primaryMonitor, &monitorInfo)) {
        snapshot.monitorBounds = monitorInfo.rcMonitor;
        snapshot.workArea = monitorInfo.rcWork;
        snapshot.usedWorkAreaFallback = false;
        besktop::LogInfo(
            L"primary monitor captured: " +
            std::to_wstring(monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left) +
            L" x " +
            std::to_wstring(monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top));
        return;
    }

    snapshot.monitorBounds = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    snapshot.workArea = snapshot.monitorBounds;
    snapshot.warnings.push_back(L"Primary monitor rcMonitor unavailable; using screen metrics fallback.");
    besktop::LogWarning(L"primary monitor fallback used");
}

bool IsValidTaskbarRect(const RECT& rect, const RECT& monitor)
{
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int monitorWidth = monitor.right - monitor.left;
    const int monitorHeight = monitor.bottom - monitor.top;
    if (width <= 0 || height <= 0 || monitorWidth <= 0 || monitorHeight <= 0) {
        return false;
    }
    const bool touchesEdge = rect.left == monitor.left || rect.top == monitor.top ||
        rect.right == monitor.right || rect.bottom == monitor.bottom;
    return touchesEdge &&
        static_cast<long long>(width) * height <=
            static_cast<long long>(monitorWidth) * monitorHeight * 2 / 5;
}

bool CaptureScreenPixels(const RECT& bounds, besktop::TaskbarSnapshot& taskbar)
{
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    HDC screenDc = GetDC(nullptr);
    HDC memoryDc = screenDc != nullptr ? CreateCompatibleDC(screenDc) : nullptr;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = screenDc != nullptr ?
        CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
    bool succeeded = false;
    HGDIOBJ previous = nullptr;
    if (memoryDc != nullptr && bitmap != nullptr && pixels != nullptr) {
        previous = SelectObject(memoryDc, bitmap);
        if (previous != nullptr && previous != HGDI_ERROR) {
            succeeded = BitBlt(
                memoryDc, 0, 0, width, height, screenDc, bounds.left, bounds.top,
                SRCCOPY | CAPTUREBLT) != FALSE;
        }
        if (succeeded) {
            const auto* begin = static_cast<const unsigned char*>(pixels);
            taskbar.bgraPixels.assign(begin, begin + static_cast<size_t>(width) * height * 4);
            taskbar.pixelWidth = width;
            taskbar.pixelHeight = height;
        }
    }
    if (previous != nullptr && previous != HGDI_ERROR) {
        SelectObject(memoryDc, previous);
    }
    if (bitmap != nullptr) {
        DeleteObject(bitmap);
    }
    if (memoryDc != nullptr) {
        DeleteDC(memoryDc);
    }
    if (screenDc != nullptr) {
        ReleaseDC(nullptr, screenDc);
    }
    return succeeded;
}

void CaptureTaskbarSnapshot(besktop::DesktopSnapshot& snapshot)
{
    HWND taskbarWindow = FindWindowW(L"Shell_TrayWnd", nullptr);
    RECT candidate{};
    bool hasCandidate = taskbarWindow != nullptr && IsWindowVisible(taskbarWindow) &&
        GetWindowRect(taskbarWindow, &candidate) != FALSE;

    if (!hasCandidate) {
        APPBARDATA appbar{};
        appbar.cbSize = sizeof(appbar);
        if (SHAppBarMessage(ABM_GETTASKBARPOS, &appbar) != 0) {
            candidate = appbar.rc;
            hasCandidate = true;
        }
    }

    RECT clipped{};
    if (!hasCandidate || !IntersectRect(&clipped, &candidate, &snapshot.monitorBounds) ||
        !IsValidTaskbarRect(clipped, snapshot.monitorBounds)) {
        snapshot.warnings.push_back(L"Visible primary taskbar was not detected; using the monitor work area only.");
        besktop::LogWarning(L"visible primary taskbar not detected; static taskbar rendering disabled");
        return;
    }

    snapshot.taskbar.visible = true;
    snapshot.taskbar.screenBounds = clipped;
    snapshot.taskbar.captureSucceeded = CaptureScreenPixels(clipped, snapshot.taskbar);
    if (!snapshot.taskbar.captureSucceeded) {
        snapshot.taskbar.bgraPixels.clear();
        snapshot.taskbar.pixelWidth = 0;
        snapshot.taskbar.pixelHeight = 0;
        snapshot.warnings.push_back(L"Static taskbar capture failed; the stage will continue without taskbar pixels.");
        besktop::LogWarning(L"static taskbar capture failed; rendering disabled");
    } else {
        besktop::LogInfo(L"static primary taskbar captured: " + FormatRect(clipped));
    }
}

void CaptureWallpaperSnapshot(besktop::DesktopSnapshot& snapshot)
{
    ScopedComInitializer com;
    if (!com.IsUsable()) {
        snapshot.wallpaper.usedFallback = true;
        snapshot.warnings.push_back(
            L"Wallpaper COM initialization failed (" + FormatHResult(com.Result()) + L").");
        besktop::LogWarning(L"wallpaper COM initialization failed: " + FormatHResult(com.Result()));
        return;
    }
    if (com.Result() == RPC_E_CHANGED_MODE) {
        besktop::LogWarning(L"COM apartment already initialized with a different mode");
    }

    ComPtr<IDesktopWallpaper> desktopWallpaper;
    HRESULT createResult = CoCreateInstance(
        CLSID_DesktopWallpaper,
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(desktopWallpaper.GetAddressOf()));

    if (FAILED(createResult) || desktopWallpaper == nullptr) {
        snapshot.wallpaper.usedFallback = true;
        snapshot.warnings.push_back(
            L"IDesktopWallpaper creation failed (" + FormatHResult(createResult) + L").");
        besktop::LogWarning(L"IDesktopWallpaper creation failed: " + FormatHResult(createResult));
        return;
    }

    LPWSTR wallpaperPath = nullptr;
    HRESULT wallpaperResult = desktopWallpaper->GetWallpaper(nullptr, &wallpaperPath);
    if (SUCCEEDED(wallpaperResult) && wallpaperPath != nullptr && wallpaperPath[0] != L'\0') {
        snapshot.wallpaper.path = wallpaperPath;
        besktop::LogInfo(L"wallpaper path captured: " + snapshot.wallpaper.path);
    } else {
        snapshot.wallpaper.usedFallback = true;
        snapshot.warnings.push_back(
            L"Wallpaper path capture failed (" + FormatHResult(wallpaperResult) + L").");
        besktop::LogWarning(L"wallpaper path capture failed: " + FormatHResult(wallpaperResult));
    }
    if (wallpaperPath != nullptr) {
        CoTaskMemFree(wallpaperPath);
    }

    DESKTOP_WALLPAPER_POSITION wallpaperPosition = DWPOS_CENTER;
    HRESULT positionResult = desktopWallpaper->GetPosition(&wallpaperPosition);
    if (SUCCEEDED(positionResult)) {
        snapshot.wallpaper.layout = MapWallpaperLayout(wallpaperPosition);
        besktop::LogInfo(L"wallpaper layout captured: " + besktop::ToDisplayString(snapshot.wallpaper.layout));
    } else {
        snapshot.wallpaper.usedFallback = true;
        snapshot.warnings.push_back(
            L"Wallpaper layout capture failed (" + FormatHResult(positionResult) + L").");
        besktop::LogWarning(L"wallpaper layout capture failed: " + FormatHResult(positionResult));
    }

}

std::wstring ToLower(std::wstring value)
{
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return value;
}

std::wstring Trim(std::wstring value)
{
    while (!value.empty() && iswspace(value.front())) {
        value.erase(value.begin());
    }
    while (!value.empty() && iswspace(value.back())) {
        value.pop_back();
    }
    return value;
}

std::wstring NormalizeIconKey(const std::wstring& value)
{
    return ToLower(Trim(value));
}

std::wstring NormalizePathKey(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    return NormalizeIconKey(error ? path.wstring() : canonical.wstring());
}

bool IsShortcutLikePath(const std::filesystem::path& path)
{
    const std::wstring extension = ToLower(path.extension().wstring());
    return extension == L".lnk" || extension == L".url";
}

std::wstring ExpandEnvironmentStringsIfNeeded(const std::wstring& value)
{
    if (value.find(L'%') == std::wstring::npos) {
        return value;
    }

    const DWORD required = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (required == 0) {
        return value;
    }

    std::wstring expanded(required, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), required);
    if (written == 0 || written > required) {
        return value;
    }
    if (!expanded.empty() && expanded.back() == L'\0') {
        expanded.pop_back();
    }
    return expanded;
}

bool PathExists(const std::wstring& path)
{
    std::error_code error;
    return !path.empty() && std::filesystem::exists(std::filesystem::path(path), error);
}

std::wstring ResolveShortcutTargetPath(const std::filesystem::path& shortcutPath)
{
    if (!IsShortcutLikePath(shortcutPath)) {
        return {};
    }

    ScopedComInitializer com;
    if (!com.IsUsable()) {
        return {};
    }

    ComPtr<IShellLinkW> shellLink;
    HRESULT result = CoCreateInstance(
        CLSID_ShellLink,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(shellLink.GetAddressOf()));
    if (FAILED(result) || shellLink == nullptr) {
        return {};
    }

    ComPtr<IPersistFile> persistFile;
    result = shellLink.As(&persistFile);
    if (FAILED(result) || persistFile == nullptr ||
        FAILED(persistFile->Load(shortcutPath.c_str(), STGM_READ))) {
        return {};
    }

    wchar_t targetPath[MAX_PATH]{};
    WIN32_FIND_DATAW findData{};
    result = shellLink->GetPath(targetPath, static_cast<int>(std::size(targetPath)), &findData, SLGP_UNCPRIORITY);
    if (FAILED(result) || targetPath[0] == L'\0') {
        return {};
    }

    std::wstring expanded = ExpandEnvironmentStringsIfNeeded(targetPath);
    return PathExists(expanded) ? expanded : std::wstring{};
}

int ScoreDesktopIconSource(const std::filesystem::path& sourcePath)
{
    const std::wstring extension = ToLower(sourcePath.extension().wstring());
    if (extension == L".lnk") {
        const std::wstring targetPath = ResolveShortcutTargetPath(sourcePath);
        const std::filesystem::path target(targetPath);
        const std::wstring targetExtension = ToLower(target.extension().wstring());
        std::error_code error;
        if (targetExtension == L".exe") {
            return 500;
        }
        if (std::filesystem::is_regular_file(target, error)) {
            return 420;
        }
        if (std::filesystem::is_directory(target, error)) {
            return 260;
        }
        return 220;
    }
    if (extension == L".exe") {
        return 480;
    }
    if (extension == L".ico") {
        return 440;
    }
    if (extension == L".url") {
        return 210;
    }
    return 300;
}

std::wstring ShellDisplayNameForPath(const std::filesystem::path& path)
{
    SHFILEINFOW fileInfo{};
    if (SHGetFileInfoW(
            path.c_str(),
            0,
            &fileInfo,
            sizeof(fileInfo),
            SHGFI_DISPLAYNAME) != 0 &&
        fileInfo.szDisplayName[0] != L'\0') {
        return fileInfo.szDisplayName;
    }
    return {};
}

struct DesktopIconSourceCandidate {
    std::wstring path;
    int score = 0;
};

struct DesktopIconSourceIndex {
    std::unordered_map<std::wstring, DesktopIconSourceCandidate> pathByName;
    std::vector<std::wstring> warnings;
};

void AddDesktopFolder(std::vector<std::filesystem::path>& folders, const std::filesystem::path& folder)
{
    if (folder.empty()) {
        return;
    }

    std::error_code error;
    if (!std::filesystem::exists(folder, error) || !std::filesystem::is_directory(folder, error)) {
        return;
    }

    const std::wstring key = NormalizePathKey(folder);
    for (const std::filesystem::path& existing : folders) {
        if (NormalizePathKey(existing) == key) {
            return;
        }
    }
    folders.push_back(folder);
}

void AddKnownDesktopFolder(std::vector<std::filesystem::path>& folders, REFKNOWNFOLDERID folderId)
{
    PWSTR folderPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &folderPath)) && folderPath != nullptr) {
        ScopedCoTaskMemString scopedPath(folderPath);
        AddDesktopFolder(folders, std::filesystem::path(scopedPath.Get()));
    }
}

void AddOneDriveDesktopFolders(std::vector<std::filesystem::path>& folders)
{
    constexpr const wchar_t* kOneDriveVariables[] = {
        L"OneDrive",
        L"OneDriveConsumer",
        L"OneDriveCommercial",
    };

    for (const wchar_t* variable : kOneDriveVariables) {
        wchar_t buffer[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableW(variable, buffer, static_cast<DWORD>(std::size(buffer)));
        if (length > 0 && length < std::size(buffer)) {
            AddDesktopFolder(folders, std::filesystem::path(buffer) / L"Desktop");
        }
    }
}

void AddSourceKey(
    std::unordered_map<std::wstring, DesktopIconSourceCandidate>& pathByName,
    const std::wstring& key,
    const std::filesystem::path& sourcePath,
    int score)
{
    const std::wstring normalized = NormalizeIconKey(key);
    if (normalized.empty()) {
        return;
    }

    auto [it, inserted] = pathByName.try_emplace(
        normalized,
        DesktopIconSourceCandidate{sourcePath.wstring(), score});
    if (!inserted && score > it->second.score) {
        besktop::LogInfo(
            L"desktop icon source duplicate replaced: " +
            key +
            L" -> " +
            sourcePath.wstring() +
            L" (score " +
            std::to_wstring(score) +
            L" > " +
            std::to_wstring(it->second.score) +
            L")");
        it->second = DesktopIconSourceCandidate{sourcePath.wstring(), score};
    }
}

void AddDesktopFileToIndex(
    DesktopIconSourceIndex& index,
    const std::filesystem::directory_entry& entry)
{
    const std::filesystem::path sourcePath = entry.path();
    const std::wstring fileName = sourcePath.filename().wstring();
    const std::wstring stem = sourcePath.stem().wstring();
    const std::wstring extension = ToLower(sourcePath.extension().wstring());
    const std::wstring shellDisplayName = ShellDisplayNameForPath(sourcePath);
    const int sourceScore = ScoreDesktopIconSource(sourcePath);

    AddSourceKey(index.pathByName, fileName, sourcePath, sourceScore);
    AddSourceKey(index.pathByName, stem, sourcePath, sourceScore);
    AddSourceKey(index.pathByName, shellDisplayName, sourcePath, sourceScore);

    if (extension == L".lnk" || extension == L".url") {
        AddSourceKey(index.pathByName, stem, sourcePath, sourceScore);
    }
}

DesktopIconSourceIndex BuildDesktopIconSourceIndex()
{
    DesktopIconSourceIndex index;

    std::vector<std::filesystem::path> folders;
    AddKnownDesktopFolder(folders, FOLDERID_Desktop);
    AddKnownDesktopFolder(folders, FOLDERID_PublicDesktop);
    AddOneDriveDesktopFolders(folders);

    for (const std::filesystem::path& folder : folders) {
        std::error_code error;
        std::filesystem::directory_iterator iterator(folder, error);
        if (error) {
            index.warnings.push_back(L"Desktop folder enumeration failed: " + folder.wstring());
            besktop::LogInfo(L"desktop icon source folder enumeration failed: " + folder.wstring());
            continue;
        }

        int fileCount = 0;
        for (const std::filesystem::directory_entry& entry : iterator) {
            AddDesktopFileToIndex(index, entry);
            ++fileCount;
        }
        besktop::LogInfo(
            L"desktop icon source folder indexed: " + folder.wstring() +
            L" (" + std::to_wstring(fileCount) + L" entries)");
    }

    besktop::LogInfo(L"desktop icon source name keys: " + std::to_wstring(index.pathByName.size()));
    return index;
}

besktop::DesktopIconImageSnapshot ResolveDesktopIconImageSource(
    const DesktopIconSourceIndex& index,
    const std::wstring& displayName)
{
    constexpr const wchar_t* kProbeExtensions[] = {
        L"",
        L".lnk",
        L".url",
        L".exe",
    };

    for (const wchar_t* extension : kProbeExtensions) {
        const std::wstring probeName = displayName + extension;
        const auto found = index.pathByName.find(NormalizeIconKey(probeName));
        if (found != index.pathByName.end()) {
            besktop::DesktopIconImageSnapshot image;
            image.sourceKind = besktop::DesktopIconImageSourceKind::FileSystemPath;
            image.sourceIdentifier = found->second.path;
            image.usedFallback = false;
            besktop::LogInfo(L"desktop icon source matched: " + displayName + L" -> " + image.sourceIdentifier);
            return image;
        }
    }

    besktop::DesktopIconImageSnapshot image;
    image.usedFallback = true;
    image.warning = L"No matching Desktop/Public Desktop file was found.";
    besktop::LogInfo(L"desktop icon source fallback: " + displayName + L" (" + image.warning + L")");
    return image;
}

std::wstring ShellItemDisplayName(IShellItem* item, SIGDN sigdn)
{
    if (item == nullptr) {
        return {};
    }

    PWSTR rawName = nullptr;
    if (FAILED(item->GetDisplayName(sigdn, &rawName)) || rawName == nullptr || rawName[0] == L'\0') {
        if (rawName != nullptr) {
            CoTaskMemFree(rawName);
        }
        return {};
    }

    ScopedCoTaskMemString name(rawName);
    return name.Get();
}

struct DesktopShellItemSource {
    besktop::DesktopIconImageSnapshot image;
    std::wstring displayName;
    POINT viewPosition{};
    bool hasViewPosition = false;
};

std::vector<DesktopShellItemSource> CaptureDesktopShellItemSources(int expectedItemCount)
{
    std::vector<DesktopShellItemSource> sources(static_cast<size_t>(std::max(expectedItemCount, 0)));
    if (expectedItemCount <= 0) {
        return sources;
    }

    ScopedComInitializer com;
    if (!com.IsUsable()) {
        besktop::LogWarning(L"desktop shell view COM initialization failed: " + FormatHResult(com.Result()));
        return sources;
    }

    ComPtr<IShellWindows> shellWindows;
    HRESULT result = CoCreateInstance(
        CLSID_ShellWindows,
        nullptr,
        CLSCTX_ALL,
        IID_IShellWindows,
        reinterpret_cast<void**>(shellWindows.GetAddressOf()));
    if (FAILED(result) || shellWindows == nullptr) {
        besktop::LogWarning(L"desktop ShellWindows creation failed: " + FormatHResult(result));
        return sources;
    }

    VARIANT desktopLocation{};
    VARIANT emptyVariant{};
    desktopLocation.vt = VT_I4;
    desktopLocation.lVal = CSIDL_DESKTOP;
    long desktopHwnd = 0;
    ComPtr<IDispatch> dispatch;
    result = shellWindows->FindWindowSW(
        &desktopLocation,
        &emptyVariant,
        SWC_DESKTOP,
        &desktopHwnd,
        SWFO_NEEDDISPATCH,
        dispatch.GetAddressOf());
    if (FAILED(result) || dispatch == nullptr) {
        besktop::LogWarning(L"desktop ShellWindows::FindWindowSW failed: " + FormatHResult(result));
        return sources;
    }

    ComPtr<IServiceProvider> serviceProvider;
    result = dispatch.As(&serviceProvider);
    if (FAILED(result) || serviceProvider == nullptr) {
        besktop::LogWarning(L"desktop shell dispatch IServiceProvider query failed: " + FormatHResult(result));
        return sources;
    }

    ComPtr<IShellBrowser> shellBrowser;
    result = serviceProvider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(shellBrowser.GetAddressOf()));
    if (FAILED(result) || shellBrowser == nullptr) {
        besktop::LogWarning(L"desktop shell browser query failed: " + FormatHResult(result));
        return sources;
    }

    ComPtr<IShellView> shellView;
    result = shellBrowser->QueryActiveShellView(shellView.GetAddressOf());
    if (FAILED(result) || shellView == nullptr) {
        besktop::LogWarning(L"desktop active shell view query failed: " + FormatHResult(result));
        return sources;
    }

    ComPtr<IFolderView> folderView;
    result = shellView.As(&folderView);
    if (FAILED(result) || folderView == nullptr) {
        besktop::LogWarning(L"desktop shell view IFolderView query failed: " + FormatHResult(result));
        return sources;
    }

    ComPtr<IShellFolder> shellFolder;
    result = folderView->GetFolder(IID_PPV_ARGS(shellFolder.GetAddressOf()));
    if (FAILED(result) || shellFolder == nullptr) {
        besktop::LogWarning(L"desktop folder view parent folder query failed: " + FormatHResult(result));
        return sources;
    }

    int shellItemCount = 0;
    result = folderView->ItemCount(SVGIO_ALLVIEW, &shellItemCount);
    if (FAILED(result) || shellItemCount <= 0) {
        besktop::LogWarning(L"desktop folder view item count failed: " + FormatHResult(result));
        return sources;
    }

    const int count = std::min(expectedItemCount, shellItemCount);
    int resolvedCount = 0;
    for (int index = 0; index < count; ++index) {
        PITEMID_CHILD rawChildPidl = nullptr;
        result = folderView->Item(index, &rawChildPidl);
        if (FAILED(result) || rawChildPidl == nullptr) {
            besktop::LogWarning(
                L"desktop folder view item PIDL failed: #" +
                std::to_wstring(index) +
                L" (" +
                FormatHResult(result) +
                L")");
            continue;
        }

        ScopedPidl childPidl(rawChildPidl);
        POINT viewPosition{};
        const bool hasViewPosition = SUCCEEDED(folderView->GetItemPosition(childPidl.Get(), &viewPosition));
        ComPtr<IShellItem> shellItem;
        result = SHCreateItemWithParent(
            nullptr,
            shellFolder.Get(),
            childPidl.Get(),
            IID_PPV_ARGS(shellItem.GetAddressOf()));
        if (FAILED(result) || shellItem == nullptr) {
            besktop::LogWarning(
                L"desktop shell item creation failed: #" +
                std::to_wstring(index) +
                L" (" +
                FormatHResult(result) +
                L")");
            continue;
        }

        DesktopShellItemSource source;
        const std::wstring fileSystemPath = ShellItemDisplayName(shellItem.Get(), SIGDN_FILESYSPATH);
        if (!fileSystemPath.empty() && PathExists(fileSystemPath)) {
            source.image.sourceKind = besktop::DesktopIconImageSourceKind::FileSystemPath;
            source.image.sourceIdentifier = fileSystemPath;
        } else {
            const std::wstring parsingName = ShellItemDisplayName(shellItem.Get(), SIGDN_DESKTOPABSOLUTEPARSING);
            if (!parsingName.empty()) {
                source.image.sourceKind = besktop::DesktopIconImageSourceKind::ShellParsingName;
                source.image.sourceIdentifier = parsingName;
            }
        }
        if (source.image.sourceKind == besktop::DesktopIconImageSourceKind::None) {
            besktop::LogInfo(
                L"desktop shell item source unavailable: #" + std::to_wstring(index + 1));
            continue;
        }

        source.image.usedFallback = false;
        source.displayName = ShellItemDisplayName(shellItem.Get(), SIGDN_NORMALDISPLAY);
        source.viewPosition = viewPosition;
        source.hasViewPosition = hasViewPosition;
        sources[static_cast<size_t>(index)] = std::move(source);
        ++resolvedCount;
    }

    besktop::LogInfo(
        L"desktop shell item sources resolved: " +
        std::to_wstring(resolvedCount) +
        L" / " +
        std::to_wstring(expectedItemCount));
    return sources;
}

bool IsLikelyOnPrimaryMonitor(const RECT& monitorBounds, const POINT& point)
{
    constexpr int kMargin = 128;
    return point.x >= monitorBounds.left - kMargin &&
        point.x <= monitorBounds.right + kMargin &&
        point.y >= monitorBounds.top - kMargin &&
        point.y <= monitorBounds.bottom + kMargin;
}

HWND FindDesktopListViewInParent(HWND parent)
{
    HWND shellView = FindWindowExW(parent, nullptr, L"SHELLDLL_DefView", nullptr);
    if (shellView == nullptr) {
        return nullptr;
    }
    return FindWindowExW(shellView, nullptr, L"SysListView32", nullptr);
}

HWND FindDesktopListView()
{
    if (HWND progman = FindWindowW(L"Progman", nullptr)) {
        if (HWND listView = FindDesktopListViewInParent(progman)) {
            return listView;
        }
    }

    struct EnumState {
        HWND listView = nullptr;
    } state{};

    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* enumState = reinterpret_cast<EnumState*>(lParam);
            wchar_t className[64]{};
            if (GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) == 0 ||
                wcscmp(className, L"WorkerW") != 0) {
                return TRUE;
            }

            HWND listView = FindDesktopListViewInParent(hwnd);
            if (listView != nullptr) {
                enumState->listView = listView;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&state));

    return state.listView;
}

class RemoteAllocation {
public:
    RemoteAllocation(HANDLE process, SIZE_T size)
        : process_(process)
    {
        address_ = VirtualAllocEx(process_, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }

    RemoteAllocation(const RemoteAllocation&) = delete;
    RemoteAllocation& operator=(const RemoteAllocation&) = delete;

    ~RemoteAllocation()
    {
        if (address_ != nullptr) {
            VirtualFreeEx(process_, address_, 0, MEM_RELEASE);
        }
    }

    bool IsValid() const
    {
        return address_ != nullptr;
    }

    void* Address() const
    {
        return address_;
    }

private:
    HANDLE process_ = nullptr;
    void* address_ = nullptr;
};

std::wstring ReadDesktopListViewText(HWND listView, HANDLE explorerProcess, int index)
{
    constexpr int kMaxTextLength = 260;
    constexpr SIZE_T kTextBytes = sizeof(wchar_t) * kMaxTextLength;
    constexpr SIZE_T kRemoteSize = sizeof(LVITEMW) + kTextBytes;

    RemoteAllocation remote(explorerProcess, kRemoteSize);
    if (!remote.IsValid()) {
        return L"Icon " + std::to_wstring(index + 1);
    }

    auto* remoteItem = static_cast<LVITEMW*>(remote.Address());
    auto* remoteText = reinterpret_cast<wchar_t*>(
        static_cast<unsigned char*>(remote.Address()) + sizeof(LVITEMW));

    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    item.iSubItem = 0;
    item.pszText = remoteText;
    item.cchTextMax = kMaxTextLength;

    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(explorerProcess, remoteItem, &item, sizeof(item), &bytesWritten)) {
        return L"Icon " + std::to_wstring(index + 1);
    }

    SendMessageW(listView, LVM_GETITEMTEXTW, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(remoteItem));

    wchar_t buffer[kMaxTextLength]{};
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(explorerProcess, remoteText, buffer, kTextBytes, &bytesRead) || buffer[0] == L'\0') {
        return L"Icon " + std::to_wstring(index + 1);
    }

    return buffer;
}

bool AreProcessesSameBitness(HANDLE otherProcess)
{
    BOOL currentIsWow64 = FALSE;
    BOOL otherIsWow64 = FALSE;
    if (!IsWow64Process(GetCurrentProcess(), &currentIsWow64) ||
        !IsWow64Process(otherProcess, &otherIsWow64)) {
        return false;
    }
    return currentIsWow64 == otherIsWow64;
}

size_t FindUniqueShellSourceByPosition(
    const std::vector<DesktopShellItemSource>& sources,
    const std::vector<bool>& used,
    const POINT& position,
    int tolerance)
{
    size_t match = sources.size();
    for (size_t index = 0; index < sources.size(); ++index) {
        const DesktopShellItemSource& source = sources[index];
        if (used[index] || source.image.usedFallback || !source.hasViewPosition ||
            std::abs(source.viewPosition.x - position.x) > tolerance ||
            std::abs(source.viewPosition.y - position.y) > tolerance) {
            continue;
        }
        if (match != sources.size()) {
            return sources.size();
        }
        match = index;
    }
    return match;
}

size_t FindUniqueShellSourceByDisplayName(
    const std::vector<DesktopShellItemSource>& sources,
    const std::vector<bool>& used,
    const std::wstring& displayName)
{
    const std::wstring normalizedName = NormalizeIconKey(displayName);
    if (normalizedName.empty()) {
        return sources.size();
    }

    size_t match = sources.size();
    for (size_t index = 0; index < sources.size(); ++index) {
        const DesktopShellItemSource& source = sources[index];
        if (used[index] || source.image.usedFallback ||
            NormalizeIconKey(source.displayName) != normalizedName) {
            continue;
        }
        if (match != sources.size()) {
            return sources.size();
        }
        match = index;
    }
    return match;
}

bool TryReadDesktopListViewPosition(HWND listView, HANDLE explorerProcess, int index, POINT& position)
{
    RemoteAllocation remote(explorerProcess, sizeof(POINT));
    if (!remote.IsValid()) {
        return false;
    }

    if (!SendMessageW(listView, LVM_GETITEMPOSITION, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(remote.Address()))) {
        return false;
    }

    SIZE_T bytesRead = 0;
    return ReadProcessMemory(explorerProcess, remote.Address(), &position, sizeof(position), &bytesRead) &&
        bytesRead == sizeof(position);
}

bool IsUsableRect(const RECT& rect)
{
    return rect.right > rect.left && rect.bottom > rect.top;
}

bool TryReadDesktopListViewItemRect(HWND listView, HANDLE explorerProcess, int index, int rectKind, RECT& bounds)
{
    RemoteAllocation remote(explorerProcess, sizeof(RECT));
    if (!remote.IsValid()) {
        return false;
    }

    RECT request{};
    request.left = rectKind;
    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(explorerProcess, remote.Address(), &request, sizeof(request), &bytesWritten) ||
        bytesWritten != sizeof(request)) {
        return false;
    }

    if (!SendMessageW(listView, LVM_GETITEMRECT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(remote.Address()))) {
        return false;
    }

    SIZE_T bytesRead = 0;
    RECT result{};
    if (!ReadProcessMemory(explorerProcess, remote.Address(), &result, sizeof(result), &bytesRead) ||
        bytesRead != sizeof(result) ||
        !IsUsableRect(result)) {
        return false;
    }

    bounds = result;
    return true;
}

bool TryReadDesktopListViewIconRect(HWND listView, HANDLE explorerProcess, int index, RECT& iconBounds)
{
    return TryReadDesktopListViewItemRect(listView, explorerProcess, index, LVIR_ICON, iconBounds);
}

bool TryReadDesktopListViewLabelRect(HWND listView, HANDLE explorerProcess, int index, RECT& labelBounds)
{
    return TryReadDesktopListViewItemRect(listView, explorerProcess, index, LVIR_LABEL, labelBounds);
}

bool TryImageListGetIconSize(HIMAGELIST imageList, int* width, int* height)
{
#if defined(_MSC_VER)
    __try {
        return ImageList_GetIconSize(imageList, width, height) != FALSE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    return ImageList_GetIconSize(imageList, width, height) != FALSE;
#endif
}

void CaptureDesktopIconDisplayMetrics(besktop::DesktopSnapshot& snapshot, HWND listView)
{
    HIMAGELIST imageList = reinterpret_cast<HIMAGELIST>(
        SendMessageW(listView, LVM_GETIMAGELIST, LVSIL_NORMAL, 0));
    int imageListWidth = 0;
    int imageListHeight = 0;
    if (imageList != nullptr &&
        TryImageListGetIconSize(imageList, &imageListWidth, &imageListHeight) &&
        imageListWidth > 0 &&
        imageListHeight > 0) {
        snapshot.iconDisplay.imageListIconSize = SIZE{imageListWidth, imageListHeight};
        snapshot.iconDisplay.usedFallback = false;
        snapshot.iconDisplay.source = L"LVM_GETIMAGELIST(LVSIL_NORMAL)/ImageList_GetIconSize";
        besktop::LogInfo(
            L"desktop image list icon size: " +
            std::to_wstring(imageListWidth) +
            L" x " +
            std::to_wstring(imageListHeight) +
            L" (" +
            snapshot.iconDisplay.source +
            L")");
        return;
    }

    if (imageList != nullptr) {
        besktop::LogWarning(
            L"desktop image list icon size read failed; ListView image list handle may be cross-process");
    }
    const UINT dpi = GetDpiForWindowCompat(listView);
    const int fallbackSize = std::max(32, MulDiv(48, static_cast<int>(dpi), 96));
    snapshot.iconDisplay.imageListIconSize = SIZE{fallbackSize, fallbackSize};
    snapshot.iconDisplay.usedFallback = true;
    snapshot.iconDisplay.source = L"DPI-aware fallback 48px @ " + std::to_wstring(dpi) + L"dpi";
    snapshot.warnings.push_back(L"Desktop ListView image list icon size unavailable; using DPI fallback.");
    besktop::LogWarning(
        L"desktop image list icon size fallback: " +
        std::to_wstring(fallbackSize) +
        L" x " +
        std::to_wstring(fallbackSize) +
        L" (" +
        snapshot.iconDisplay.source +
        L")");
}

RECT BuildFallbackIconBounds(const RECT& itemBounds)
{
    constexpr int kFallbackIconSize = 48;
    const int itemWidth = itemBounds.right - itemBounds.left;
    const int left = itemBounds.left + std::max(0, (itemWidth - kFallbackIconSize) / 2);
    const int top = itemBounds.top;
    return RECT{left, top, left + kFallbackIconSize, top + kFallbackIconSize};
}

RECT BuildFallbackLabelBounds(const RECT& itemBounds, const RECT& iconBounds)
{
    constexpr int kFallbackLabelGap = 4;
    constexpr int kFallbackLabelHeight = 40;
    const int itemWidth = std::max(72, static_cast<int>(itemBounds.right - itemBounds.left));
    const int iconCenter = (iconBounds.left + iconBounds.right) / 2;
    const int left = iconCenter - (itemWidth / 2);
    const int top = iconBounds.bottom + kFallbackLabelGap;
    return RECT{left, top, left + itemWidth, top + kFallbackLabelHeight};
}

bool CaptureDesktopIconsFromListView(besktop::DesktopSnapshot& snapshot)
{
    HWND listView = FindDesktopListView();
    if (listView == nullptr) {
        snapshot.warnings.push_back(L"Desktop SysListView32 was not found.");
        besktop::LogWarning(L"desktop SysListView32 was not found");
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(listView, &processId);
    if (processId == 0) {
        snapshot.warnings.push_back(L"Desktop SysListView32 process id was not found.");
        besktop::LogWarning(L"desktop SysListView32 process id was not found");
        return false;
    }

    HANDLE explorerProcess = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
        FALSE,
        processId);
    if (explorerProcess == nullptr) {
        const DWORD error = GetLastError();
        snapshot.warnings.push_back(
            L"Desktop SysListView32 process open failed (" + std::to_wstring(error) + L").");
        besktop::LogWarning(L"desktop SysListView32 process open failed: " + std::to_wstring(error));
        return false;
    }

    const LRESULT itemCountResult = SendMessageW(listView, LVM_GETITEMCOUNT, 0, 0);
    if (itemCountResult <= 0) {
        CloseHandle(explorerProcess);
        snapshot.warnings.push_back(L"Desktop SysListView32 returned no icons.");
        besktop::LogWarning(L"desktop SysListView32 returned no icons");
        return false;
    }

    const int itemCount = static_cast<int>(itemCountResult);
    CaptureDesktopIconDisplayMetrics(snapshot, listView);
    const std::vector<DesktopShellItemSource> shellItemSources = CaptureDesktopShellItemSources(itemCount);
    std::vector<bool> shellItemSourceUsed(shellItemSources.size(), false);
    const bool canReadListViewText = AreProcessesSameBitness(explorerProcess);
    const DesktopIconSourceIndex sourceIndex = BuildDesktopIconSourceIndex();
    int capturedCount = 0;
    int imageSourceCount = 0;
    int iconBoundsCount = 0;
    int labelBoundsCount = 0;
    for (int index = 0; index < itemCount; ++index) {
        POINT position{};
        if (!TryReadDesktopListViewPosition(listView, explorerProcess, index, position) ||
            !IsLikelyOnPrimaryMonitor(snapshot.monitorBounds, position)) {
            continue;
        }

        constexpr int kIconWidth = 84;
        constexpr int kIconHeight = 92;

        besktop::DesktopIconSnapshot icon;
        icon.id = L"desktop-listview-icon-" + std::to_wstring(index + 1);
        const std::wstring listViewDisplayName = canReadListViewText
            ? ReadDesktopListViewText(listView, explorerProcess, index)
            : std::wstring{};

        size_t matchedShellIndex = FindUniqueShellSourceByPosition(
            shellItemSources,
            shellItemSourceUsed,
            position,
            0);
        if (matchedShellIndex == shellItemSources.size()) {
            constexpr int kViewPositionTolerance = 2;
            matchedShellIndex = FindUniqueShellSourceByPosition(
                shellItemSources,
                shellItemSourceUsed,
                position,
                kViewPositionTolerance);
        }
        if (matchedShellIndex == shellItemSources.size() && canReadListViewText) {
            matchedShellIndex = FindUniqueShellSourceByDisplayName(
                shellItemSources,
                shellItemSourceUsed,
                listViewDisplayName);
        }
        if (matchedShellIndex != shellItemSources.size()) {
            const DesktopShellItemSource& matchedSource = shellItemSources[matchedShellIndex];
            icon.displayName = matchedSource.displayName;
            icon.image = matchedSource.image;
            shellItemSourceUsed[matchedShellIndex] = true;
            besktop::LogInfo(L"desktop icon shell source matched: " + icon.displayName + L" -> " + icon.image.sourceIdentifier);
        } else {
            icon.displayName = listViewDisplayName.empty()
                ? L"Icon " + std::to_wstring(index + 1)
                : listViewDisplayName;
            icon.image = ResolveDesktopIconImageSource(sourceIndex, icon.displayName);
        }
        icon.listViewPosition = position;
        icon.bounds = RECT{
            position.x,
            position.y,
            position.x + kIconWidth,
            position.y + kIconHeight,
        };
        if (TryReadDesktopListViewIconRect(listView, explorerProcess, index, icon.iconBounds)) {
            icon.usedIconBoundsFallback = false;
            ++iconBoundsCount;
        } else {
            icon.iconBounds = BuildFallbackIconBounds(icon.bounds);
            icon.usedIconBoundsFallback = true;
            besktop::LogInfo(L"desktop icon LVIR_ICON fallback: " + icon.displayName);
        }
        if (TryReadDesktopListViewLabelRect(listView, explorerProcess, index, icon.labelBounds)) {
            icon.usedLabelBoundsFallback = false;
            ++labelBoundsCount;
        } else {
            icon.labelBounds = BuildFallbackLabelBounds(icon.bounds, icon.iconBounds);
            icon.usedLabelBoundsFallback = true;
            besktop::LogInfo(L"desktop icon LVIR_LABEL fallback: " + icon.displayName);
        }
        icon.usedFallback = false;
        snapshot.icons.push_back(icon);
        if (!icon.image.usedFallback) {
            ++imageSourceCount;
        }
        ++capturedCount;
    }

    CloseHandle(explorerProcess);

    if (capturedCount <= 0) {
        snapshot.warnings.push_back(L"Desktop SysListView32 returned no icons on the primary monitor.");
        besktop::LogWarning(L"desktop SysListView32 returned no icons on the primary monitor");
        return false;
    }

    besktop::LogInfo(L"desktop icons captured from SysListView32: " + std::to_wstring(capturedCount));
    besktop::LogInfo(
        L"desktop icon image sources resolved: " +
        std::to_wstring(imageSourceCount) +
        L" / " +
        std::to_wstring(capturedCount));
    besktop::LogInfo(
        L"desktop icon bounds captured: " +
        std::to_wstring(iconBoundsCount) +
        L" / " +
        std::to_wstring(capturedCount));
    besktop::LogInfo(
        L"desktop icon label bounds captured: " +
        std::to_wstring(labelBoundsCount) +
        L" / " +
        std::to_wstring(capturedCount));
    for (size_t index = 0; index < snapshot.icons.size() && index < 5; ++index) {
        const besktop::DesktopIconSnapshot& icon = snapshot.icons[index];
        besktop::LogInfo(
            L"desktop icon geometry sample: " +
            icon.displayName +
            L"; listview position: " +
            std::to_wstring(icon.listViewPosition.x) +
            L"," +
            std::to_wstring(icon.listViewPosition.y) +
            L"; LVIR_ICON: " +
            FormatRect(icon.iconBounds) +
            L"; LVIR_LABEL: " +
            FormatRect(icon.labelBounds));
    }
    if (imageSourceCount < capturedCount) {
        snapshot.warnings.push_back(
            L"Some desktop icon image sources were not resolved; unresolved icons will use fallback bodies.");
    }
    if (iconBoundsCount < capturedCount) {
        snapshot.warnings.push_back(
            L"Some desktop icon bounds were not resolved; fallback icon plane sizes will be used.");
    }
    if (labelBoundsCount < capturedCount) {
        snapshot.warnings.push_back(
            L"Some desktop icon label bounds were not resolved; fallback label rectangles will be used.");
    }
    for (const std::wstring& warning : sourceIndex.warnings) {
        snapshot.warnings.push_back(warning);
    }
    return true;
}

void AddDemoIconFallbacks(besktop::DesktopSnapshot& snapshot)
{
    constexpr const wchar_t* kDemoIconNames[] = {
        L"Chat",
        L"Music",
        L"Game",
    };

    const RECT bounds = snapshot.monitorBounds;
    const int monitorWidth = bounds.right - bounds.left;
    const int monitorHeight = bounds.bottom - bounds.top;
    const int iconWidth = 84;
    const int iconHeight = 92;
    const int spacing = 116;
    const int startX = bounds.left + (monitorWidth / 2) - spacing;
    const int top = bounds.top + (monitorHeight / 2) + 112;

    for (int index = 0; index < 3; ++index) {
        const int left = startX + (spacing * index);

        besktop::DesktopIconSnapshot icon;
        icon.id = L"demo-icon-" + std::to_wstring(index + 1);
        icon.displayName = kDemoIconNames[index];
        icon.listViewPosition = POINT{left, top};
        icon.bounds = RECT{left, top, left + iconWidth, top + iconHeight};
        icon.iconBounds = BuildFallbackIconBounds(icon.bounds);
        icon.usedIconBoundsFallback = true;
        icon.labelBounds = BuildFallbackLabelBounds(icon.bounds, icon.iconBounds);
        icon.usedLabelBoundsFallback = true;
        icon.usedFallback = true;
        snapshot.icons.push_back(icon);
    }

    snapshot.warnings.push_back(L"Using demo icon fallback.");
    if (snapshot.iconDisplay.imageListIconSize.cx <= 0 || snapshot.iconDisplay.imageListIconSize.cy <= 0) {
        snapshot.iconDisplay.imageListIconSize = SIZE{48, 48};
        snapshot.iconDisplay.usedFallback = true;
        snapshot.iconDisplay.source = L"demo fallback";
    }
    besktop::LogWarning(L"using demo icon fallback");
}

} // namespace

namespace besktop {

DesktopSnapshot CaptureDesktopSnapshot()
{
    DesktopSnapshot snapshot;
    CapturePrimaryMonitorBounds(snapshot);
    CaptureTaskbarSnapshot(snapshot);
    CaptureWallpaperSnapshot(snapshot);
    if (!CaptureDesktopIconsFromListView(snapshot)) {
        AddDemoIconFallbacks(snapshot);
    }
    return snapshot;
}

std::wstring ToDisplayString(WallpaperLayout layout)
{
    switch (layout) {
    case WallpaperLayout::Center:
        return L"Center";
    case WallpaperLayout::Tile:
        return L"Tile";
    case WallpaperLayout::Stretch:
        return L"Stretch";
    case WallpaperLayout::Fit:
        return L"Fit";
    case WallpaperLayout::Fill:
        return L"Fill";
    case WallpaperLayout::Span:
        return L"Span";
    case WallpaperLayout::Unknown:
    default:
        return L"Unknown";
    }
}

} // namespace besktop
