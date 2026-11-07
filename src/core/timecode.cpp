#include "core/timecode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rc {

std::string timecode(double seconds, double fps)
{
    seconds = std::max(0.0, seconds);
    const double whole = std::floor(seconds + 1e-6);
    const int frames = std::max(0, int(std::floor((seconds - whole) * fps + 1e-6)));
    char text[32];
    std::snprintf(text, sizeof text, "%02d:%02d.%02d", int(whole) / 60, int(whole) % 60, frames);
    return text;
}

std::string shortTimecode(double seconds)
{
    const int whole = int(std::floor(std::max(0.0, seconds) + 1e-6));
    char text[24];
    std::snprintf(text, sizeof text, "%02d:%02d", whole / 60, whole % 60);
    return text;
}

}  // namespace rc
