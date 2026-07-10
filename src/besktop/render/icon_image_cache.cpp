#include "besktop/render/icon_image_cache.h"

#include "besktop/logging/logger.h"

#include <CommCtrl.h>
#include <commoncontrols.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

std::wstring FormatWin32Error(DWORD error)
{
    return L"win32=" + std::to_wstring(error);
}

std::wstring FormatHResult(HRESULT result)
{
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(result));
    return buffer;
}

std::wstring ToLower(std::wstring value)
{
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return value;
}

bool IsShortcutLikePath(const std::wstring& sourcePath)
{
    const std::wstring lower = ToLower(sourcePath);
    return lower.ends_with(L".lnk") || lower.ends_with(L".url");
}

class UniqueIcon {
public:
    UniqueIcon() = default;

    explicit UniqueIcon(HICON icon)
        : icon_(icon)
    {
    }

    UniqueIcon(const UniqueIcon&) = delete;
    UniqueIcon& operator=(const UniqueIcon&) = delete;

    ~UniqueIcon()
    {
        Reset();
    }

    HICON* Put()
    {
        Reset();
        return &icon_;
    }

    HICON Get() const
    {
        return icon_;
    }

    bool IsValid() const
    {
        return icon_ != nullptr;
    }

    void Reset(HICON icon = nullptr)
    {
        if (icon_ != nullptr) {
            DestroyIcon(icon_);
        }
        icon_ = icon;
    }

private:
    HICON icon_ = nullptr;
};

class UniqueBitmapHandle {
public:
    UniqueBitmapHandle() = default;

    explicit UniqueBitmapHandle(HBITMAP bitmap)
        : bitmap_(bitmap)
    {
    }

    UniqueBitmapHandle(const UniqueBitmapHandle&) = delete;
    UniqueBitmapHandle& operator=(const UniqueBitmapHandle&) = delete;

    ~UniqueBitmapHandle()
    {
        Reset();
    }

    HBITMAP* Put()
    {
        Reset();
        return &bitmap_;
    }

    HBITMAP Get() const
    {
        return bitmap_;
    }

    bool IsValid() const
    {
        return bitmap_ != nullptr;
    }

    void Reset(HBITMAP bitmap = nullptr)
    {
        if (bitmap_ != nullptr) {
            DeleteObject(bitmap_);
        }
        bitmap_ = bitmap;
    }

private:
    HBITMAP bitmap_ = nullptr;
};

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

std::wstring ShellImageListName(int imageListId)
{
    switch (imageListId) {
    case SHIL_JUMBO:
        return L"SHIL_JUMBO";
    case SHIL_EXTRALARGE:
        return L"SHIL_EXTRALARGE";
    default:
        return L"SHIL_" + std::to_wstring(imageListId);
    }
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

std::wstring ResolveShortcutTargetPath(const std::wstring& shortcutPath)
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

std::wstring FindMatchingIconInFolder(const std::wstring& folderPath, const std::wstring& label)
{
    std::error_code error;
    const std::filesystem::path root(folderPath);
    if (label.empty() ||
        !std::filesystem::exists(root, error) ||
        !std::filesystem::is_directory(root, error)) {
        return {};
    }

    const std::wstring normalizedLabel = ToLower(label);
    const std::filesystem::path commonCandidates[] = {
        root / L"Code" / label / L"uires" / L"image" / (label + L".ico"),
        root / L"Code" / label / L"uires" / L"image" / (L"icon-" + label + L".ico"),
        root / L"Release" / L"images" / (L"icon-" + label + L".ico"),
        root / L"VersionData" / (L"icon-" + label + L".ico"),
    };
    for (const std::filesystem::path& candidate : commonCandidates) {
        error.clear();
        if (std::filesystem::exists(candidate, error) && std::filesystem::is_regular_file(candidate, error)) {
            besktop::LogInfo(L"shortcut folder icon matched by common path: " + candidate.wstring());
            return candidate.wstring();
        }
    }

    std::wstring containsMatch;
    int visited = 0;
    constexpr int kMaxVisitedFiles = 20000;
    std::filesystem::recursive_directory_iterator iterator(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        error.clear();
    }
    while (iterator != end && visited < kMaxVisitedFiles) {
        const std::filesystem::directory_entry entry = *iterator;
        iterator.increment(error);
        if (error) {
            error.clear();
        }
        ++visited;
        std::error_code entryError;
        if (!entry.is_regular_file(entryError)) {
            continue;
        }

        const std::filesystem::path path = entry.path();
        if (ToLower(path.extension().wstring()) != L".ico") {
            continue;
        }

        const std::wstring stem = ToLower(path.stem().wstring());
        if (stem == normalizedLabel || stem == L"icon-" + normalizedLabel) {
            besktop::LogInfo(L"shortcut folder icon matched by recursive scan: " + path.wstring());
            return path.wstring();
        }
        if (containsMatch.empty() && stem.find(normalizedLabel) != std::wstring::npos) {
            containsMatch = path.wstring();
        }
    }

    if (!containsMatch.empty()) {
        besktop::LogInfo(L"shortcut folder icon matched by contains scan: " + containsMatch);
    }
    return containsMatch;
}

std::wstring FindMatchingExecutableInFolder(const std::wstring& folderPath, const std::wstring& label)
{
    std::error_code error;
    const std::filesystem::path root(folderPath);
    if (label.empty() ||
        !std::filesystem::exists(root, error) ||
        !std::filesystem::is_directory(root, error)) {
        return {};
    }

    const std::filesystem::path executableName = label + L".exe";
    const std::filesystem::path commonCandidates[] = {
        root / executableName,
        root / L"Release" / executableName,
        root / L"x64" / L"Release" / executableName,
        root / L"Debug" / executableName,
        root / L"x64" / L"Debug" / executableName,
    };
    for (const std::filesystem::path& candidate : commonCandidates) {
        error.clear();
        if (std::filesystem::exists(candidate, error) && std::filesystem::is_regular_file(candidate, error)) {
            besktop::LogInfo(L"shortcut folder executable matched by common path: " + candidate.wstring());
            return candidate.wstring();
        }
    }

    const std::wstring normalizedLabel = ToLower(label);
    int visited = 0;
    constexpr int kMaxVisitedFiles = 20000;
    std::filesystem::recursive_directory_iterator iterator(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        error.clear();
    }
    while (iterator != end && visited < kMaxVisitedFiles) {
        const std::filesystem::directory_entry entry = *iterator;
        iterator.increment(error);
        if (error) {
            error.clear();
        }
        ++visited;

        std::error_code entryError;
        if (!entry.is_regular_file(entryError)) {
            continue;
        }

        const std::filesystem::path path = entry.path();
        if (ToLower(path.extension().wstring()) != L".exe" ||
            ToLower(path.stem().wstring()) != normalizedLabel) {
            continue;
        }

        besktop::LogInfo(L"shortcut folder executable matched by recursive scan: " + path.wstring());
        return path.wstring();
    }

    return {};
}

std::unique_ptr<Gdiplus::Bitmap> BitmapFromHBitmap(HBITMAP bitmap, std::wstring& failureReason)
{
    BITMAP bitmapInfo{};
    if (GetObjectW(bitmap, sizeof(bitmapInfo), &bitmapInfo) == 0 ||
        bitmapInfo.bmWidth <= 0 ||
        bitmapInfo.bmHeight <= 0) {
        failureReason = L"GetObjectW(HBITMAP) failed";
        return nullptr;
    }

    const int width = bitmapInfo.bmWidth;
    const int height = bitmapInfo.bmHeight;
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

    BITMAPINFO dibInfo{};
    dibInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dibInfo.bmiHeader.biWidth = width;
    dibInfo.bmiHeader.biHeight = -height;
    dibInfo.bmiHeader.biPlanes = 1;
    dibInfo.bmiHeader.biBitCount = 32;
    dibInfo.bmiHeader.biCompression = BI_RGB;

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) {
        failureReason = L"GetDC(nullptr) failed";
        return nullptr;
    }
    const int scanLines = GetDIBits(
        screenDc,
        bitmap,
        0,
        static_cast<UINT>(height),
        pixels.data(),
        &dibInfo,
        DIB_RGB_COLORS);
    ReleaseDC(nullptr, screenDc);
    if (scanLines != height) {
        failureReason = L"GetDIBits failed";
        return nullptr;
    }

    bool hasAlpha = false;
    for (size_t index = 3; index < pixels.size(); index += 4) {
        if (pixels[index] != 0) {
            hasAlpha = true;
            break;
        }
    }
    if (!hasAlpha) {
        for (size_t index = 0; index + 3 < pixels.size(); index += 4) {
            const bool hasColor = pixels[index] != 0 || pixels[index + 1] != 0 || pixels[index + 2] != 0;
            pixels[index + 3] = hasColor ? 255 : 0;
        }
    }

    auto image = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppARGB);
    if (image == nullptr || image->GetLastStatus() != Gdiplus::Ok) {
        failureReason = L"GDI+ ARGB bitmap allocation failed";
        return nullptr;
    }

    Gdiplus::BitmapData locked{};
    Gdiplus::Rect lockRect(0, 0, width, height);
    if (image->LockBits(&lockRect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &locked) != Gdiplus::Ok) {
        failureReason = L"GDI+ bitmap LockBits failed";
        return nullptr;
    }

    const size_t sourceStride = static_cast<size_t>(width) * 4;
    auto* destination = static_cast<unsigned char*>(locked.Scan0);
    for (int y = 0; y < height; ++y) {
        std::memcpy(
            destination + (static_cast<size_t>(y) * static_cast<size_t>(locked.Stride)),
            pixels.data() + (static_cast<size_t>(y) * sourceStride),
            sourceStride);
    }
    image->UnlockBits(&locked);

    if (image->GetLastStatus() != Gdiplus::Ok) {
        failureReason = L"GDI+ bitmap creation from shell item image failed";
        return nullptr;
    }

    return image;
}

bool TryExtractShellItemBitmap(
    const std::wstring& path,
    std::unique_ptr<Gdiplus::Bitmap>& bitmap,
    std::wstring& method,
    std::wstring& failureReason)
{
    if (!PathExists(path)) {
        failureReason = L"path does not exist";
        return false;
    }

    ScopedComInitializer com;
    if (!com.IsUsable()) {
        failureReason = L"COM initialization failed (" + FormatHResult(com.Result()) + L")";
        return false;
    }

    ComPtr<IShellItemImageFactory> imageFactory;
    const HRESULT itemResult = SHCreateItemFromParsingName(
        path.c_str(),
        nullptr,
        IID_PPV_ARGS(imageFactory.GetAddressOf()));
    if (FAILED(itemResult) || imageFactory == nullptr) {
        failureReason = L"SHCreateItemFromParsingName failed (" + FormatHResult(itemResult) + L")";
        return false;
    }

    UniqueBitmapHandle shellBitmap;
    SIZE size{256, 256};
    const SIIGBF flags = static_cast<SIIGBF>(SIIGBF_BIGGERSIZEOK | SIIGBF_SCALEUP);
    const HRESULT imageResult = imageFactory->GetImage(size, flags, shellBitmap.Put());
    if (FAILED(imageResult) || !shellBitmap.IsValid()) {
        failureReason = L"IShellItemImageFactory::GetImage failed (" + FormatHResult(imageResult) + L")";
        return false;
    }

    bitmap = BitmapFromHBitmap(shellBitmap.Get(), failureReason);
    if (bitmap == nullptr) {
        return false;
    }

    method = L"IShellItemImageFactory";
    return true;
}

bool TryGetShellIconIndex(
    const std::wstring& sourcePath,
    int& iconIndex,
    int& overlayIndex,
    std::wstring& failureReason)
{
    SHFILEINFOW fileInfo{};
    UINT flags = SHGFI_SYSICONINDEX | SHGFI_ADDOVERLAYS | SHGFI_OVERLAYINDEX;
    if (IsShortcutLikePath(sourcePath)) {
        flags |= SHGFI_LINKOVERLAY;
    }
    if (SHGetFileInfoW(
            sourcePath.c_str(),
            0,
            &fileInfo,
            sizeof(fileInfo),
            flags) == 0) {
        failureReason = L"SHGetFileInfoW(SHGFI_SYSICONINDEX) failed (" + FormatWin32Error(GetLastError()) + L")";
        return false;
    }

    iconIndex = fileInfo.iIcon & 0x00FFFFFF;
    overlayIndex = (fileInfo.iIcon >> 24) & 0xFF;
    return true;
}

bool TryExtractShellImageListIcon(
    const std::wstring& sourcePath,
    int imageListId,
    UniqueIcon& icon,
    std::wstring& method,
    std::wstring& failureReason)
{
    int iconIndex = 0;
    int overlayIndex = 0;
    if (!TryGetShellIconIndex(sourcePath, iconIndex, overlayIndex, failureReason)) {
        return false;
    }

    ComPtr<IImageList> imageList;
    const HRESULT listResult = SHGetImageList(imageListId, IID_PPV_ARGS(imageList.GetAddressOf()));
    if (FAILED(listResult) || imageList == nullptr) {
        failureReason =
            L"SHGetImageList(" + ShellImageListName(imageListId) + L") failed (" + FormatHResult(listResult) + L")";
        return false;
    }

    HICON extractedIcon = nullptr;
    UINT drawFlags = ILD_TRANSPARENT;
    if (overlayIndex > 0) {
        drawFlags |= INDEXTOOVERLAYMASK(overlayIndex);
    }
    const HRESULT iconResult = imageList->GetIcon(iconIndex, drawFlags, &extractedIcon);
    if (FAILED(iconResult) || extractedIcon == nullptr) {
        failureReason =
            L"IImageList::GetIcon(" + ShellImageListName(imageListId) + L") failed (" + FormatHResult(iconResult) + L")";
        return false;
    }

    icon.Reset(extractedIcon);
    method = ShellImageListName(imageListId);
    if (overlayIndex > 0) {
        method += L"+overlay" + std::to_wstring(overlayIndex);
        besktop::LogInfo(
            L"shortcut/system overlay applied: " +
            sourcePath +
            L" (overlay " +
            std::to_wstring(overlayIndex) +
            L")");
    } else if (IsShortcutLikePath(sourcePath)) {
        besktop::LogInfo(
            L"shortcut overlay unavailable from system image list: " +
            sourcePath +
            L"; Explorer shortcut arrow may differ");
    }
    return true;
}

bool TryExtractHighResolutionShellIcon(
    const std::wstring& sourcePath,
    UniqueIcon& icon,
    std::wstring& method,
    std::wstring& failureReason)
{
    constexpr int kPreferredImageLists[] = {
        SHIL_JUMBO,
        SHIL_EXTRALARGE,
    };

    std::wstring lastFailure;
    for (const int imageListId : kPreferredImageLists) {
        if (TryExtractShellImageListIcon(sourcePath, imageListId, icon, method, lastFailure)) {
            return true;
        }
        besktop::LogInfo(
            L"high-resolution icon extraction attempt failed: " +
            sourcePath +
            L" (" + lastFailure + L")");
    }

    failureReason = lastFailure.empty() ? L"no preferred shell image list succeeded" : lastFailure;
    return false;
}

bool TryExtractFileInfoIcon(
    const std::wstring& sourcePath,
    UniqueIcon& icon,
    std::wstring& method,
    std::wstring& failureReason)
{
    SHFILEINFOW fileInfo{};
    if (SHGetFileInfoW(
            sourcePath.c_str(),
            0,
            &fileInfo,
            sizeof(fileInfo),
            SHGFI_ICON | SHGFI_LARGEICON) == 0 ||
        fileInfo.hIcon == nullptr) {
        failureReason = L"SHGetFileInfoW(SHGFI_ICON) failed (" + FormatWin32Error(GetLastError()) + L")";
        return false;
    }

    icon.Reset(fileInfo.hIcon);
    method = L"SHGetFileInfoW(SHGFI_LARGEICON)";
    return true;
}

std::unique_ptr<Gdiplus::Bitmap> BitmapFromIcon(HICON icon, std::wstring& failureReason)
{
    std::unique_ptr<Gdiplus::Bitmap> bitmap(Gdiplus::Bitmap::FromHICON(icon));
    if (bitmap == nullptr) {
        failureReason = L"Gdiplus::Bitmap::FromHICON returned null";
        return nullptr;
    }

    if (bitmap->GetLastStatus() != Gdiplus::Ok || bitmap->GetWidth() == 0 || bitmap->GetHeight() == 0) {
        failureReason = L"Gdiplus bitmap creation failed";
        return nullptr;
    }

    return bitmap;
}

} // namespace

namespace besktop {

IconImageCache::IconImageCache()
{
    Gdiplus::GdiplusStartupInput startupInput;
    gdiplusReady_ = Gdiplus::GdiplusStartup(&gdiplusToken_, &startupInput, nullptr) == Gdiplus::Ok;
    if (!gdiplusReady_) {
        LogWarning(L"icon image cache GDI+ startup failed");
    }
}

IconImageCache::~IconImageCache()
{
    Clear();
    if (gdiplusReady_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
        gdiplusReady_ = false;
    }
}

void IconImageCache::Clear()
{
    images_.clear();
    imageByPath_.clear();
    failedCount_ = 0;
}

const IconImage* IconImageCache::Load(const DesktopIconImageSnapshot& snapshot, const std::wstring& label)
{
    if (snapshot.usedFallback || snapshot.sourcePath.empty()) {
        ++failedCount_;
        const std::wstring reason =
            snapshot.warning.empty() ? L"no icon source path" : snapshot.warning;
        LogInfo(L"icon image fallback: " + label + L" (" + reason + L")");
        return nullptr;
    }

    if (!gdiplusReady_) {
        ++failedCount_;
        LogInfo(L"icon image fallback: " + label + L" (GDI+ is not ready)");
        return nullptr;
    }

    const auto cached = imageByPath_.find(snapshot.sourcePath);
    if (cached != imageByPath_.end()) {
        return cached->second;
    }

    std::vector<std::wstring> candidatePaths;
    auto addCandidate = [&](const std::wstring& path) {
        if (path.empty()) {
            return;
        }
        if (std::find(candidatePaths.begin(), candidatePaths.end(), path) == candidatePaths.end()) {
            candidatePaths.push_back(path);
        }
    };

    const std::wstring shortcutTarget = ResolveShortcutTargetPath(snapshot.sourcePath);
    if (!shortcutTarget.empty()) {
        std::error_code error;
        const std::filesystem::path targetPath(shortcutTarget);
        if (std::filesystem::is_directory(targetPath, error)) {
            addCandidate(FindMatchingExecutableInFolder(shortcutTarget, label));
            addCandidate(FindMatchingIconInFolder(shortcutTarget, label));
            addCandidate(shortcutTarget);
        } else {
            addCandidate(shortcutTarget);
        }
    }
    addCandidate(snapshot.sourcePath);

    std::wstring failureReason;
    std::wstring extractionMethod;
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    std::wstring bitmapSourcePath;
    for (const std::wstring& candidatePath : candidatePaths) {
        std::wstring candidateFailure;
        std::wstring candidateMethod;
        if (TryExtractShellItemBitmap(candidatePath, bitmap, candidateMethod, candidateFailure)) {
            extractionMethod = candidateMethod;
            bitmapSourcePath = candidatePath;
            break;
        }
        LogInfo(
            L"shell item image extraction failed: " +
            label +
            L" -> " +
            candidatePath +
            L" (" + candidateFailure + L")");
    }

    if (bitmap == nullptr) {
        UniqueIcon shellIcon;
        for (const std::wstring& candidatePath : candidatePaths) {
            std::wstring candidateFailure;
            std::wstring candidateMethod;
            if (TryExtractHighResolutionShellIcon(candidatePath, shellIcon, candidateMethod, candidateFailure)) {
                extractionMethod = candidateMethod;
                bitmapSourcePath = candidatePath;
                break;
            }
            LogInfo(
                L"high-resolution icon extraction attempt failed: " +
                label +
                L" -> " +
                candidatePath +
                L" (" + candidateFailure + L")");
        }

        if (!shellIcon.IsValid()) {
            for (const std::wstring& candidatePath : candidatePaths) {
                std::wstring candidateFailure;
                std::wstring candidateMethod;
                if (TryExtractFileInfoIcon(candidatePath, shellIcon, candidateMethod, candidateFailure)) {
                    extractionMethod = candidateMethod;
                    bitmapSourcePath = candidatePath;
                    break;
                }
                failureReason = candidateFailure;
            }
        }

        if (!shellIcon.IsValid()) {
            ++failedCount_;
            LogInfo(
                L"icon image extraction failed: " + label +
                L" -> " + snapshot.sourcePath +
                L" (" + failureReason + L")");
            return nullptr;
        }

        bitmap = BitmapFromIcon(shellIcon.Get(), failureReason);
        if (bitmap == nullptr) {
            ++failedCount_;
            LogInfo(
                L"icon image bitmap conversion failed: " + label +
                L" -> " + bitmapSourcePath +
                L" (" + failureReason + L")");
            return nullptr;
        }
    }

    auto iconImage = std::make_unique<IconImage>();
    iconImage->sourcePath = snapshot.sourcePath;
    iconImage->extractionMethod = extractionMethod;
    if (!bitmapSourcePath.empty() && bitmapSourcePath != snapshot.sourcePath) {
        iconImage->extractionMethod += L" via " + bitmapSourcePath;
    }
    iconImage->width = bitmap->GetWidth();
    iconImage->height = bitmap->GetHeight();
    iconImage->bitmap = std::move(bitmap);

    IconImage* imagePtr = iconImage.get();
    images_.push_back(std::move(iconImage));
    imageByPath_.emplace(imagePtr->sourcePath, imagePtr);

    LogInfo(
            L"icon image extracted: " + label +
        L" -> " + imagePtr->sourcePath +
        L" (" + std::to_wstring(imagePtr->width) +
        L" x " + std::to_wstring(imagePtr->height) +
        L", " + imagePtr->extractionMethod + L")");
    return imagePtr;
}

size_t IconImageCache::ExtractedCount() const
{
    return images_.size();
}

size_t IconImageCache::FailedCount() const
{
    return failedCount_;
}

} // namespace besktop
