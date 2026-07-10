#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "besktop/desktop/desktop_snapshot.h"
#include "besktop/render/icon_image_cache.h"

namespace besktop {

class IconFightScene {
public:
    void Reset(const DesktopSnapshot& snapshot, const RECT& clientRect);
    void Update(double elapsedSeconds);
    void Render(HDC hdc, const RECT& clientRect) const;

    // Public for the small free functions in the implementation file; this is
    // still an internal scene type, not a plugin-facing API.
    enum class ScenePhase {
        Calm,
        TextShaking,
        Awakening,
        Fighting,
    };

    struct IconActor {
        std::wstring label;
        double baseX = 0.0;
        double baseY = 0.0;
        double battleX = 0.0;
        double battleY = 0.0;
        double planeWidth = 48.0;
        double planeHeight = 48.0;
        bool usedPlaneFallback = true;
        std::wstring planeSizeSource;
        int role = 0;
        unsigned char red = 80;
        unsigned char green = 140;
        unsigned char blue = 240;
        const IconImage* iconImage = nullptr;
        bool usedIconImageFallback = true;
    };

private:
    ScenePhase DeterminePhase(double elapsedSeconds) const;
    void LogPhase(ScenePhase phase);

    std::vector<IconActor> actors_;
    IconImageCache iconImageCache_;
    RECT monitorBounds_{};
    double elapsedSeconds_ = 0.0;
    ScenePhase phase_ = ScenePhase::Calm;
};

} // namespace besktop
