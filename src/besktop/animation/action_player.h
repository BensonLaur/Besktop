#pragma once

#include "besktop/animation/action_clip.h"

#include <cstdint>

namespace besktop {

struct ActorActionState {
    ActionId actionId = ActionId::None;
    ActionPhase phase = ActionPhase::Complete;
    double localTimeSeconds = 0.0;
    double playbackRate = 1.0;
    double direction = 1.0;
    std::uint32_t emittedEventMask = 0;
    std::uint32_t pendingEventMask = 0;
};

class ActionPlayer {
public:
    void Start(ActionId actionId, double direction = 1.0, double playbackRate = 1.0);
    void Stop();
    void Update(double deltaSeconds);

    bool IsPlaying() const;
    bool IsComplete() const;
    ActionSample Sample() const;
    std::uint32_t ConsumeEvents();
    const ActorActionState& State() const;

private:
    ActorActionState state_;
};

} // namespace besktop
