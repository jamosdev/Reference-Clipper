// The Output panel's formulas, from the design handoff.
#pragma once

#include <string>

namespace rc {

struct Dims {
    int w = 0;
    int h = 0;
    bool operator==(const Dims&) const = default;
};

constexpr int kEncoderPreset = 6;

int crfForQuality(int quality);
int audioKbpsForQuality(int quality);
const char* qualityHint(int quality);

// presetHeight 0 means Source; presets taller than the source show the source
// size. 4:2:0 needs even dimensions, so odd sources round down.
Dims outputDims(Dims source, int presetHeight);

double estimatedMegabytes(Dims output, int videoQuality, int audioQuality, double seconds);
std::string formatEstimatedSize(double megabytes);
std::string formatFileSize(long long bytes);

}  // namespace rc
