// Sequential 48 kHz stereo s16 PCM over a list of segments, for monitoring and
// for the waveform. Gaps in the source decode as silence.
#pragma once

#include <cstdint>
#include <vector>

#include "core/timeline.h"
#include "media/stream_reader.h"

namespace rc {

class PcmSource {
public:
    static constexpr int kRate = 48000;
    static constexpr int kChannels = 2;

    PcmSource(const std::string& path, std::vector<Span> segments);

    // Fills up to `frames` stereo frames; 0 once every segment is exhausted.
    size_t read(int16_t* out, size_t frames);

private:
    void startSegment();
    bool decodeMore();

    StreamReader reader_;
    SwrPtr resampler_;
    FramePtr frame_;
    std::vector<Span> segments_;
    size_t segment_ = 0;
    int64_t segmentFrames_ = 0;
    int64_t remaining_ = 0;  // frames left in this segment
    int64_t skip_ = 0;       // converted frames to drop before the segment start
    int64_t silence_ = 0;    // frames of silence owed for a hole in the source
    bool sourceEnded_ = false;
    std::vector<int16_t> buffer_;
    size_t bufferPos_ = 0;   // in frames
};

}  // namespace rc
