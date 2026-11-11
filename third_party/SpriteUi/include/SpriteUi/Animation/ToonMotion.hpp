#pragma once

#include "SpriteUi/Core/Handle.hpp"
#include "SpriteUi/Core/Math.hpp"

#include <cstdint>
#include <vector>

namespace ToolEngine::SpriteUi::Animation {

enum class TweenProperty : std::uint8_t { Position, Scale, Rotation, Opacity, VisualOffset };
enum class TweenConflict : std::uint8_t { Override, Queue, Ignore };
enum class Easing : std::uint8_t { Linear, EaseOutQuad, EaseInOutCubic, SmoothStep };
enum class TweenState : std::uint8_t { Playing, Paused };

struct TweenTrack {
    std::uint64_t id = 0;
    UIHandle target{};
    TweenProperty property = TweenProperty::Position;
    Vec4 from{};
    Vec4 to{};
    float duration = 0.25f;
    float elapsed = 0.0f;
    Easing easing = Easing::EaseOutQuad;
    TweenState state = TweenState::Playing;
    bool queued = false;
};

struct TweenSample {
    std::uint64_t trackId = 0;
    UIHandle target{};
    TweenProperty property = TweenProperty::Position;
    Vec4 value{};
};

struct TweenCompleted {
    std::uint64_t trackId = 0;
    UIHandle target{};
    TweenProperty property = TweenProperty::Position;
};

struct TweenUpdate {
    std::vector<TweenSample> samples;
    std::vector<TweenCompleted> completed;
};

struct TweenKeyframe {
    float time = 0.0f;
    Vec4 value{};
    Easing easingToNext = Easing::Linear;
};

struct TimelineTrack {
    UIHandle target{};
    TweenProperty property = TweenProperty::Position;
    std::vector<TweenKeyframe> keyframes;
};

struct TimelineSample {
    UIHandle target{};
    TweenProperty property = TweenProperty::Position;
    Vec4 value{};
};

enum class TimelinePlaybackState : std::uint8_t { Stopped, Playing, Paused };

// Aplica opacidade a uma cor ja pre-multiplicada (V4.2 sec. 5): RGB e A escalam juntos,
// preservando as proporcoes RGB/A sem degradar cores emissivas.
[[nodiscard]] inline Vec4 ApplyOpacityPremultiplied(Vec4 premultipliedTint, float opacity) noexcept {
    if (!IsFinite(premultipliedTint) || !IsFinite(opacity)) return {};
    return premultipliedTint * Clamp01(opacity);
}

[[nodiscard]] float EvaluateEasing(Easing easing, float t) noexcept;
[[nodiscard]] Vec2 EvaluateShake(
    float elapsed, float duration, Vec2 amplitude, float frequency = 38.0f) noexcept;

class ToonMotionSystem {
public:
    [[nodiscard]] bool Add(TweenTrack track, TweenConflict conflict = TweenConflict::Override);
    [[nodiscard]] TweenUpdate Update(float deltaTime);
    [[nodiscard]] bool SetPaused(std::uint64_t trackId, bool paused) noexcept;
    void CancelTarget(UIHandle target) noexcept;
    void Clear() noexcept { tracks_.clear(); }
    [[nodiscard]] std::size_t Size() const noexcept { return tracks_.size(); }

private:
    [[nodiscard]] bool HasTrack(UIHandle target, TweenProperty property) const noexcept;
    std::vector<TweenTrack> tracks_;
    std::uint64_t nextId_ = 1;
};

// Stateless keyframe sampling; playback clocks and ECS property writes stay host-owned.
class ToonTimeline {
public:
    [[nodiscard]] bool AddTrack(TimelineTrack track);
    [[nodiscard]] bool RemoveTrack(UIHandle target, TweenProperty property) noexcept;
    [[nodiscard]] std::vector<TimelineSample> Sample(float time) const;
    [[nodiscard]] float Duration() const noexcept { return duration_; }
    [[nodiscard]] std::size_t TrackCount() const noexcept { return tracks_.size(); }
    void Clear() noexcept {
        tracks_.clear();
        duration_ = 0.0f;
    }

private:
    std::vector<TimelineTrack> tracks_;
    float duration_ = 0.0f;
};

// Playback control stays independent of clocks, ECS writes, and rendering.
class ToonTimelinePlayer {
public:
    void Play() noexcept { state_ = TimelinePlaybackState::Playing; }
    void Pause() noexcept {
        if (state_ == TimelinePlaybackState::Playing) state_ = TimelinePlaybackState::Paused;
    }
    void Stop() noexcept {
        time_ = 0.0f;
        state_ = TimelinePlaybackState::Stopped;
    }
    void SetLooping(bool looping) noexcept { looping_ = looping; }
    [[nodiscard]] bool IsLooping() const noexcept { return looping_; }
    [[nodiscard]] TimelinePlaybackState State() const noexcept { return state_; }
    [[nodiscard]] float Time() const noexcept { return time_; }
    [[nodiscard]] bool Seek(float time, const ToonTimeline& timeline) noexcept;
    [[nodiscard]] bool Update(
        float deltaTime,
        const ToonTimeline& timeline,
        std::vector<TimelineSample>& samples);

private:
    float time_ = 0.0f;
    TimelinePlaybackState state_ = TimelinePlaybackState::Stopped;
    bool looping_ = false;
};

} // namespace ToolEngine::SpriteUi::Animation
