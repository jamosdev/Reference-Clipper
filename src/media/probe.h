#pragma once

#include <string>

#include "core/quality.h"

namespace rc {

struct ClipInfo {
    std::string path;
    std::string name;
    Dims display;       // after rotation and pixel aspect
    int rotation = 0;   // clockwise: 0, 90, 180 or 270
    double fps = 30;
    double duration = 0;
    long long sizeBytes = 0;
    bool hasAudio = false;

    double frame() const { return 1.0 / fps; }
};

// Throws MediaError if the file is unreadable or has no video stream.
ClipInfo probeClip(const std::string& path);

struct LibraryVersions {
    std::string ffmpeg;
    std::string svtav1;  // empty if unknown at build time
    bool canEncode = false;
};
LibraryVersions libraryVersions();

}  // namespace rc
