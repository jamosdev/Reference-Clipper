#include "core/quality.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rc {

int crfForQuality(int q)
{
    // round(63 - q*0.55) in integers: q=70 lands exactly on 24.5, which the
    // spec reads as 25, and in floating point that tie is not reliable.
    return (6350 - 55 * q) / 100;
}

int audioKbpsForQuality(int q)
{
    return 64 + (256 * q + 50) / 100;
}

const char* qualityHint(int q)
{
    if (q < 35) return "low bitrate";
    if (q < 60) return "good for review";
    if (q < 85) return "visually transparent";
    return "near-lossless";
}

Dims outputDims(Dims source, int presetHeight)
{
    const auto even = [](int v) { return std::max(2, v & ~1); };
    if (presetHeight <= 0 || presetHeight >= source.h) return {even(source.w), even(source.h)};
    const int width = int(std::lround(double(source.w) * presetHeight / source.h / 2)) * 2;
    return {std::max(2, width), presetHeight};
}

double estimatedMegabytes(Dims output, int videoQuality, int audioQuality, double seconds)
{
    const double scale = output.h / 1080.0;
    const double mbps = scale * scale * (0.8 + videoQuality / 100.0 * 7)
                        + (64 + audioQuality / 100.0 * 256) / 1000;
    return mbps * seconds / 8;
}

std::string formatEstimatedSize(double mb)
{
    char text[32];
    if (mb > 1024) std::snprintf(text, sizeof text, "%.2f GB", mb / 1024);
    else std::snprintf(text, sizeof text, "%d MB", int(std::lround(mb)));
    return text;
}

std::string formatFileSize(long long bytes)
{
    const double mb = bytes / 1048576.0;
    char text[32];
    if (mb >= 1024) std::snprintf(text, sizeof text, "%.1f GB", mb / 1024);
    else if (mb >= 1) std::snprintf(text, sizeof text, "%d MB", int(std::lround(mb)));
    else std::snprintf(text, sizeof text, "%d KB", int(std::lround(bytes / 1024.0)));
    return text;
}

}  // namespace rc
