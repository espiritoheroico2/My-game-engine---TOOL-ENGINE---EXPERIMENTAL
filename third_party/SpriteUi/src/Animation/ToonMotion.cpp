#include "SpriteUi/Animation/ToonMotion.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ToolEngine::SpriteUi::Animation {

float EvaluateEasing(Easing easing, float t) noexcept {
    t = Clamp01(t);
    switch (easing) {
    case Easing::Linear:
        return t;
    case Easing::EaseOutQuad:
        return t * (2.0f - t);
    case Easing::EaseInOutCubic:
        return t < 0.5f ? 4.0f * t * t * t
                        : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
    case Easing::SmoothStep:
        return t * t * (3.0f - 2.0f * t);
    }
    return t;
}

Vec2 EvaluateShake(float elapsed, float duration, Vec2 amplitude, float frequency) noexcept {
    if (!IsFinite(elapsed) || !IsFinite(duration) || !IsFinite(amplitude) ||
        !IsFinite(frequency) || duration <= 0.0f || frequency <= 0.0f ||
        elapsed >= duration) {
        return {};
    }

    const float t = Clamp01(elapsed / duration);
    const float envelope = (1.0f - t) * (1.0f - t);
    constexpr float phase = 1.61803398875f;
    return {
        amplitude.x * envelope * std::sin(elapsed * frequency),
        amplitude.y * envelope * std::sin(elapsed * frequency * phase)
    };
}

bool ToonMotionSystem::HasTrack(UIHandle target, TweenProperty property) const noexcept {
    return std::any_of(tracks_.begin(), tracks_.end(), [&](const TweenTrack& track) {
        return track.target == target && track.property == property;
    });
}

bool ToonMotionSystem::Add(TweenTrack track, TweenConflict conflict) {
    if (!track.target.IsValid() || !IsFinite(track.from) || !IsFinite(track.to) ||
        !IsFinite(track.duration) || track.duration < 0.0f ||
        static_cast<std::uint8_t>(track.property) >
            static_cast<std::uint8_t>(TweenProperty::VisualOffset) ||
        static_cast<std::uint8_t>(track.easing) >
            static_cast<std::uint8_t>(Easing::SmoothStep)) {
        return false;
    }
    if (track.id == std::numeric_limits<std::uint64_t>::max()) return false;
    if (track.id != 0 && std::any_of(tracks_.begin(), tracks_.end(),
        [&](const TweenTrack& existing) { return existing.id == track.id; })) {
        return false;
    }

    const bool exists = HasTrack(track.target, track.property);
    if (exists && conflict == TweenConflict::Ignore) return false;
    if (exists && conflict == TweenConflict::Override) {
        tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
            [&](const TweenTrack& existing) {
                return existing.target == track.target &&
                       existing.property == track.property;
            }), tracks_.end());
    }

    if (track.id == 0) {
        while (nextId_ != 0 && nextId_ != std::numeric_limits<std::uint64_t>::max() &&
               std::any_of(tracks_.begin(), tracks_.end(),
                   [&](const TweenTrack& existing) { return existing.id == nextId_; })) {
            ++nextId_;
        }
        if (nextId_ == 0 || nextId_ == std::numeric_limits<std::uint64_t>::max()) return false;
        track.id = nextId_++;
    } else if (track.id >= nextId_) {
        nextId_ = track.id + 1;
    }
    track.elapsed = 0.0f;
    track.queued = exists && conflict == TweenConflict::Queue;
    track.state = TweenState::Playing;
    tracks_.push_back(track);
    return true;
}

TweenUpdate ToonMotionSystem::Update(float deltaTime) {
    TweenUpdate result;
    if (!IsFinite(deltaTime) || deltaTime < 0.0f) return result;

    // Queued tracks become active when no earlier track for the same property remains.
    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        TweenTrack& track = tracks_[i];
        if (!track.queued || track.state == TweenState::Paused) continue;

        bool earlierActive = false;
        for (std::size_t j = 0; j < i; ++j) {
            if (tracks_[j].target == track.target &&
                tracks_[j].property == track.property) {
                earlierActive = true;
                break;
            }
        }
        if (!earlierActive) track.queued = false;
    }

    for (TweenTrack& track : tracks_) {
        if (track.queued || track.state == TweenState::Paused) continue;

        track.elapsed += deltaTime;
        const float progress = track.duration <= 0.0f
            ? 1.0f
            : Clamp01(track.elapsed / track.duration);
        const float eased = EvaluateEasing(track.easing, progress);
        result.samples.push_back({
            track.id, track.target, track.property, Lerp(track.from, track.to, eased)
        });

        if (progress >= 1.0f) {
            result.completed.push_back({track.id, track.target, track.property});
        }
    }

    if (!result.completed.empty()) {
        tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
            [&](const TweenTrack& track) {
                return std::any_of(result.completed.begin(), result.completed.end(),
                    [&](const TweenCompleted& completed) {
                        return completed.trackId == track.id;
                    });
            }), tracks_.end());
    }

    return result;
}

bool ToonMotionSystem::SetPaused(std::uint64_t trackId, bool paused) noexcept {
    for (TweenTrack& track : tracks_) {
        if (track.id == trackId) {
            track.state = paused ? TweenState::Paused : TweenState::Playing;
            return true;
        }
    }
    return false;
}

void ToonMotionSystem::CancelTarget(UIHandle target) noexcept {
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
        [&](const TweenTrack& track) { return track.target == target; }), tracks_.end());
}

bool ToonTimeline::AddTrack(TimelineTrack track) {
    if (!track.target.IsValid() ||
        static_cast<std::uint8_t>(track.property) >
            static_cast<std::uint8_t>(TweenProperty::VisualOffset) ||
        track.keyframes.empty() ||
        std::any_of(tracks_.begin(), tracks_.end(), [&](const TimelineTrack& existing) {
            return existing.target == track.target && existing.property == track.property;
        })) {
        return false;
    }

    float previousTime = -1.0f;
    for (const TweenKeyframe& keyframe : track.keyframes) {
        if (!IsFinite(keyframe.time) || keyframe.time < 0.0f ||
            keyframe.time <= previousTime || !IsFinite(keyframe.value) ||
            static_cast<std::uint8_t>(keyframe.easingToNext) >
                static_cast<std::uint8_t>(Easing::SmoothStep)) {
            return false;
        }
        previousTime = keyframe.time;
    }

    const float trackDuration = track.keyframes.back().time;
    tracks_.push_back(std::move(track));
    duration_ = std::max(duration_, trackDuration);
    return true;
}

bool ToonTimeline::RemoveTrack(UIHandle target, TweenProperty property) noexcept {
    const auto it = std::find_if(tracks_.begin(), tracks_.end(),
        [&](const TimelineTrack& track) {
            return track.target == target && track.property == property;
        });
    if (it == tracks_.end()) return false;
    tracks_.erase(it);
    duration_ = 0.0f;
    for (const TimelineTrack& track : tracks_) {
        duration_ = std::max(duration_, track.keyframes.back().time);
    }
    return true;
}

std::vector<TimelineSample> ToonTimeline::Sample(float time) const {
    std::vector<TimelineSample> samples;
    if (!IsFinite(time)) return samples;
    samples.reserve(tracks_.size());

    for (const TimelineTrack& track : tracks_) {
        const auto upper = std::upper_bound(
            track.keyframes.begin(), track.keyframes.end(), time,
            [](float value, const TweenKeyframe& keyframe) {
                return value < keyframe.time;
            });
        if (upper == track.keyframes.begin()) {
            samples.push_back({track.target, track.property, upper->value});
            continue;
        }
        if (upper == track.keyframes.end()) {
            samples.push_back({track.target, track.property, track.keyframes.back().value});
            continue;
        }

        const TweenKeyframe& from = *(upper - 1);
        const TweenKeyframe& to = *upper;
        const float span = to.time - from.time;
        const float progress = span > 0.0f ? (time - from.time) / span : 1.0f;
        const float eased = EvaluateEasing(from.easingToNext, progress);
        samples.push_back({track.target, track.property, Lerp(from.value, to.value, eased)});
    }
    return samples;
}

bool ToonTimelinePlayer::Seek(float time, const ToonTimeline& timeline) noexcept {
    if (!IsFinite(time) || !IsFinite(timeline.Duration()) || timeline.Duration() < 0.0f) {
        return false;
    }

    const float duration = timeline.Duration();
    if (looping_ && duration > 0.0f && time > duration) {
        time_ = std::fmod(time, duration);
    } else {
        time_ = std::clamp(time, 0.0f, duration);
    }
    return true;
}

bool ToonTimelinePlayer::Update(
    float deltaTime,
    const ToonTimeline& timeline,
    std::vector<TimelineSample>& samples) {
    const float duration = timeline.Duration();
    if (!IsFinite(deltaTime) || deltaTime < 0.0f ||
        !IsFinite(duration) || duration < 0.0f) {
        return false;
    }

    float nextTime = time_;
    TimelinePlaybackState nextState = state_;
    if (state_ == TimelinePlaybackState::Playing) {
        if (duration == 0.0f) {
            nextTime = 0.0f;
            nextState = TimelinePlaybackState::Stopped;
        } else {
            const double advanced = static_cast<double>(time_) +
                                    static_cast<double>(deltaTime);
            if (looping_) {
                nextTime = static_cast<float>(advanced > duration
                    ? std::fmod(advanced, static_cast<double>(duration))
                    : advanced);
            } else if (advanced >= duration) {
                nextTime = duration;
                nextState = TimelinePlaybackState::Stopped;
            } else {
                nextTime = static_cast<float>(advanced);
            }
        }
    }

    std::vector<TimelineSample> nextSamples = timeline.Sample(nextTime);
    time_ = nextTime;
    state_ = nextState;
    samples = std::move(nextSamples);
    return true;
}

} // namespace ToolEngine::SpriteUi::Animation
