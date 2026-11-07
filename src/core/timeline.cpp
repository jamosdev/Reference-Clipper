#include "core/timeline.h"

#include <algorithm>
#include <cmath>

namespace rc {

std::vector<Span> sortedByStart(std::vector<Span> spans)
{
    std::stable_sort(spans.begin(), spans.end(),
                     [](const Span& a, const Span& b) { return a.start < b.start; });
    return spans;
}

double totalLength(const std::vector<Span>& spans)
{
    double total = 0;
    for (const Span& s : spans) total += s.length();
    return total;
}

bool anyContains(const std::vector<Span>& spans, double t)
{
    return std::any_of(spans.begin(), spans.end(), [t](const Span& s) { return s.contains(t); });
}

double snapToFrame(double t, double fps)
{
    if (fps <= 0) return t;
    return std::floor(t * fps + 1e-6) / fps;
}

Span spanAt(int64_t id, double playhead, double clipDuration, double frame)
{
    const double len = std::max(frame, std::min(10.0, clipDuration * 0.05));
    double start = std::clamp(playhead, 0.0, clipDuration);
    // A span dropped at the very end of the clip still has to be one frame long.
    start = std::max(0.0, std::min(start, clipDuration - frame));
    return {id, start, std::min(clipDuration, start + len)};
}

void setIn(Span& span, double t, double frame)
{
    span.start = std::max(0.0, std::min(t, span.end - frame));
}

void setOut(Span& span, double t, double frame, double clipDuration)
{
    span.end = std::min(clipDuration, std::max(t, span.start + frame));
}

void moveTo(Span& span, double start, double clipDuration)
{
    const double len = span.length();
    span.start = std::clamp(start, 0.0, std::max(0.0, clipDuration - len));
    span.end = span.start + len;
}

FreeRange freeRangeAround(const std::vector<Span>& spans, double t, double clipDuration, int64_t exclude)
{
    FreeRange range{0, clipDuration};
    for (const Span& s : spans) {
        if (s.id == exclude) continue;
        if (s.end <= t) range.lower = std::max(range.lower, s.end);
        if (s.start > t) range.upper = std::min(range.upper, s.start);
    }
    return range;
}

bool isClickGesture(int pixels, long long milliseconds)
{
    constexpr int kStillPixels = 5;
    constexpr int kSlipPixels = 30;
    constexpr long long kQuickMs = 160;
    return pixels <= kStillPixels || (pixels <= kSlipPixels && milliseconds <= kQuickMs);
}

Playback beginPlayback(const std::vector<Span>& sorted, double playhead)
{
    if (sorted.empty()) return {playhead, false, -1};
    for (size_t i = 0; i < sorted.size(); ++i)
        if (sorted[i].contains(playhead)) return {playhead, true, int(i)};
    for (size_t i = 0; i < sorted.size(); ++i)
        if (sorted[i].start > playhead) return {sorted[i].start, true, int(i)};
    return {sorted.front().start, true, 0};
}

Playback advancePlayback(const std::vector<Span>& sorted, Playback state, double dt)
{
    if (!state.playing || state.span < 0 || state.span >= int(sorted.size()))
        return {state.playhead, false, -1};
    const Span& current = sorted[state.span];
    const double t = state.playhead + dt;
    if (t < current.end) return {t, true, state.span};
    const int next = state.span + 1;
    if (next < int(sorted.size())) return {sorted[next].start, true, next};
    return {current.end, false, -1};
}

OutputMap::OutputMap(std::vector<Span> segments) : segments_(std::move(segments))
{
    offsets_.reserve(segments_.size());
    for (const Span& s : segments_) {
        offsets_.push_back(duration_);
        duration_ += s.length();
    }
}

std::optional<double> OutputMap::toOutput(double t, double tolerance, int preferred) const
{
    const auto mapped = [&](size_t i) -> std::optional<double> {
        const Span& s = segments_[i];
        if (t < s.start - tolerance || t >= s.end + tolerance) return std::nullopt;
        return offsets_[i] + std::clamp(t, s.start, s.end) - s.start;
    };
    if (preferred >= 0 && preferred < int(segments_.size()))
        if (auto hit = mapped(size_t(preferred))) return hit;
    for (size_t i = 0; i < segments_.size(); ++i)
        if (auto hit = mapped(i)) return hit;
    return std::nullopt;
}

}  // namespace rc
