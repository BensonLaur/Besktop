#include "besktop/animation/action_player.h"

#include <algorithm>
#include <cmath>

namespace besktop {

void ActionPlayer::Start(ActionId actionId, double direction, double playbackRate)
{
    const ActionClip& clip = GetActionClip(actionId);
    state_ = {};
    state_.actionId = clip.id;
    state_.phase = clip.id == ActionId::None ? ActionPhase::Complete : ActionPhase::Prepare;
    state_.playbackRate = std::isfinite(playbackRate) ? std::clamp(playbackRate, 0.01, 32.0) : 1.0;
    state_.direction = direction < 0.0 ? -1.0 : 1.0;
}

void ActionPlayer::Stop()
{
    state_ = {};
}

void ActionPlayer::Update(double deltaSeconds)
{
    const ActionClip& clip = GetActionClip(state_.actionId);
    if (clip.id == ActionId::None || clip.duration <= 0.0 || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0) {
        return;
    }

    const double previousTime = state_.localTimeSeconds;
    const double nextTime = std::min(clip.duration, previousTime + deltaSeconds * state_.playbackRate);
    for (std::size_t index = 0; index < clip.eventCount; ++index) {
        const ActionEvent& event = clip.events[index];
        if (event.timeSeconds > previousTime && event.timeSeconds <= nextTime &&
            (state_.emittedEventMask & event.mask) == 0) {
            state_.emittedEventMask |= event.mask;
            state_.pendingEventMask |= event.mask;
        }
    }
    state_.localTimeSeconds = nextTime;
    state_.phase = ActionPhaseAt(clip, nextTime);
}

bool ActionPlayer::IsPlaying() const
{
    return state_.actionId != ActionId::None && !IsComplete();
}

bool ActionPlayer::IsComplete() const
{
    const ActionClip& clip = GetActionClip(state_.actionId);
    return state_.actionId == ActionId::None || state_.localTimeSeconds >= clip.duration;
}

ActionSample ActionPlayer::Sample() const
{
    return SampleAction(GetActionClip(state_.actionId), state_.localTimeSeconds, state_.direction);
}

std::uint32_t ActionPlayer::ConsumeEvents()
{
    const std::uint32_t events = state_.pendingEventMask;
    state_.pendingEventMask = 0;
    return events;
}

const ActorActionState& ActionPlayer::State() const
{
    return state_;
}

} // namespace besktop
