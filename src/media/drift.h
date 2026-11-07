#pragma once

#include <string>

namespace rc {

struct StreamExtent {
    double start = 0;
    double end = 0;
    bool present = false;
};

struct DriftReport {
    StreamExtent video;
    StreamExtent audio;
    double driftSeconds = 0;  // larger of start and end disagreement
    double frameDuration = 0;
    bool withinTolerance = true;
};

// Reads every packet of a muxed file and compares the extents of its streams.
DriftReport probeDrift(const std::string& path);

}  // namespace rc
