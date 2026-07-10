#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "besktop/desktop/desktop_snapshot.h"

namespace besktop {

struct IconImage {
    std::wstring sourcePath;
    std::wstring extractionMethod;
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    UINT width = 0;
    UINT height = 0;

    bool IsValid() const
    {
        return bitmap != nullptr && width > 0 && height > 0;
    }
};

class IconImageCache {
public:
    IconImageCache();
    ~IconImageCache();

    IconImageCache(const IconImageCache&) = delete;
    IconImageCache& operator=(const IconImageCache&) = delete;

    void Clear();
    const IconImage* Load(const DesktopIconSnapshot& icon);

    size_t ExtractedCount() const;
    size_t FailedCount() const;

private:
    ULONG_PTR gdiplusToken_ = 0;
    bool gdiplusReady_ = false;
    size_t failedCount_ = 0;
    std::vector<std::unique_ptr<IconImage>> images_;
    std::unordered_map<std::wstring, IconImage*> imageByPath_;
};

} // namespace besktop
