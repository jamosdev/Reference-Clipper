// The encode pipeline: spans in source order, one AV1 encoder and one AAC
// encoder across all of them, muxed to MP4.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "core/quality.h"
#include "core/timeline.h"
#include "media/drift.h"

namespace rc {

struct EncodeRequest {
    std::string source;
    std::vector<Span> spans;
    Dims size;
    int crf = 25;
    int audioKbps = 243;
    std::string output;
};

struct EncodeProgress {
    long long framesDone = 0;
    long long framesTotal = 0;
    double fps = 0;
};

struct EncodeResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    DriftReport drift;
    long long sizeBytes = 0;
    // Each span after snapping to real frames: the source-to-output mapping.
    std::vector<Span> segments;
};

// Blocking; run it off the UI thread. Writes `<output>.part` and renames on
// success, so a failed or cancelled job leaves nothing at the output path.
EncodeResult encodeClip(const EncodeRequest& request,
                        const std::function<void(const EncodeProgress&)>& onProgress,
                        const std::atomic<bool>& cancel);

}  // namespace rc
