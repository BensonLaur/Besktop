#include "besktop/render/icon_image_cache.h"

#include "besktop/logging/logger.h"

#include <CommCtrl.h>
#include <commoncontrols.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <cwchar>
#include <cwctype>
#include <iterator>

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
        besktop::LogWarning(
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
        besktop::LogWarning(
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
        LogWarning(L"icon image fallback: " + label + L" (" + reason + L")");
        return nullptr;
    }

    if (!gdiplusReady_) {
        ++failedCount_;
        LogWarning(L"icon image fallback: " + label + L" (GDI+ is not ready)");
        return nullptr;
    }

    const auto cached = imageByPath_.find(snapshot.sourcePath);
    if (cached != imageByPath_.end()) {
        return cached->second;
    }

    std::wstring failureReason;
    std::wstring extractionMethod;
    UniqueIcon shellIcon;
    if (!TryExtractHighResolutionShellIcon(snapshot.sourcePath, shellIcon, extractionMethod, failureReason)) {
        LogWarning(
            L"icon high-resolution extraction failed; falling back to large icon: " +
            label +
            L" -> " +
            snapshot.sourcePath +
            L" (" + failureReason + L")");
    }

    if (!shellIcon.IsValid() &&
        !TryExtractFileInfoIcon(snapshot.sourcePath, shellIcon, extractionMethod, failureReason)) {
        ++failedCount_;
        LogWarning(
            L"icon image extraction failed: " + label +
            L" -> " + snapshot.sourcePath +
            L" (" + failureReason + L")");
        return nullptr;
    }

    std::unique_ptr<Gdiplus::Bitmap> bitmap = BitmapFromIcon(shellIcon.Get(), failureReason);
    if (bitmap == nullptr) {
        ++failedCount_;
        LogWarning(
            L"icon image bitmap conversion failed: " + label +
            L" -> " + snapshot.sourcePath +
            L" (" + failureReason + L")");
        return nullptr;
    }

    auto iconImage = std::make_unique<IconImage>();
    iconImage->sourcePath = snapshot.sourcePath;
    iconImage->extractionMethod = extractionMethod;
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
