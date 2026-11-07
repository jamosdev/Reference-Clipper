// Span and playback logic. All times are seconds on the source timeline, where
// 0 is the first video frame (see StreamReader::origin).
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace rc {

struct Span {
    int64_t id = 0;
    double start = 0;
    double end = 0;

    double length() const { return end - start; }
    // Half-open, matching the encoder: a frame at exactly `end` is not output.
    bool contains(double t) const { return t >= start && t < end; }
    bool operator==(const Span&) const = default;
};

// Spans encode and play in source order. Overlaps are allowed and the shared
// region appears twice, once per span, both in the output and in playback.
std::vector<Span> sortedByStart(std::vector<Span> spans);
double totalLength(const std::vector<Span>& spans);
bool anyContains(const std::vector<Span>& spans, double t);

// Floors to the frame showing at t, so an in-point set on a displayed frame
// keeps that frame: the encoder drops pts < start.
double snapToFrame(double t, double fps);

// Span edits. `frame` is one frame duration, the minimum span length.
Span spanAt(int64_t id, double playhead, double clipDuration, double frame);
void setIn(Span& span, double t, double frame);
void setOut(Span& span, double t, double frame, double clipDuration);
void moveTo(Span& span, double start, double clipDuration);

struct Playback {
    double playhead = 0;
    bool playing = false;
    int span = -1;  // index into the sorted spans
};

// Start at the span under the playhead, else the next one, else wrap to first.
Playback beginPlayback(const std::vector<Span>& sorted, double playhead);
// Advance by a wall-clock delta, jumping gaps; stops after the last span.
Playback advancePlayback(const std::vector<Span>& sorted, Playback state, double dt);

// Maps source time to output time through the segments of a finished encode.
class OutputMap {
public:
    OutputMap() = default;
    explicit OutputMap(std::vector<Span> segments);

    bool empty() const { return segments_.empty(); }
    double duration() const { return duration_; }
    const std::vector<Span>& segments() const { return segments_; }

    // `tolerance` admits the sub-frame slack between a nominal span and the
    // frame-snapped segment. `preferred` picks a segment where spans overlap.
    std::optional<double> toOutput(double t, double tolerance = 0, int preferred = -1) const;

private:
    std::vector<Span> segments_;
    std::vector<double> offsets_;
    double duration_ = 0;
};

}  // namespace rc
