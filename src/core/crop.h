// A static crop, in displayed source pixels: after rotation, with square pixels.
// Values are even, because 4:2:0 chroma cannot start or end on an odd pixel.
#pragma once

#include "core/quality.h"

namespace rc {

struct Crop {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    Dims size() const { return {w, h}; }
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    bool operator==(const Crop&) const = default;
};

// SVT-AV1 refuses very small pictures.
constexpr int kMinCropSize = 64;

enum CropEdge { EdgeLeft = 1, EdgeRight = 2, EdgeTop = 4, EdgeBottom = 8 };

// The crop spanned by a drag from (ax, ay) to (bx, by), in either direction.
Crop cropBetween(int ax, int ay, int bx, int by, Dims display);
// Moves the given edges (a CropEdge mask) to (px, py), keeping the minimum size.
Crop resizeCrop(const Crop& crop, int edges, int px, int py, Dims display);
Crop moveCrop(const Crop& crop, int dx, int dy, Dims display);

}  // namespace rc
