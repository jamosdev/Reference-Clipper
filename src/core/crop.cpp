#include "core/crop.h"

#include <algorithm>
#include <cmath>

namespace rc {
namespace {

int roundEven(int value)
{
    return int(std::lround(value / 2.0)) * 2;
}

struct Limits {
    int width, height, minWidth, minHeight;
};

Limits limitsFor(Dims display)
{
    const int width = display.w & ~1;
    const int height = display.h & ~1;
    return {width, height, std::min(kMinCropSize, width), std::min(kMinCropSize, height)};
}

}  // namespace

Crop cropBetween(int ax, int ay, int bx, int by, Dims display)
{
    const Limits l = limitsFor(display);
    const int left = std::clamp(roundEven(std::min(ax, bx)), 0, l.width - l.minWidth);
    const int top = std::clamp(roundEven(std::min(ay, by)), 0, l.height - l.minHeight);
    const int right = std::clamp(roundEven(std::max(ax, bx)), left + l.minWidth, l.width);
    const int bottom = std::clamp(roundEven(std::max(ay, by)), top + l.minHeight, l.height);
    return {left, top, right - left, bottom - top};
}

Crop resizeCrop(const Crop& crop, int edges, int px, int py, Dims display)
{
    const Limits l = limitsFor(display);
    int left = crop.x, top = crop.y, right = crop.right(), bottom = crop.bottom();
    if (edges & EdgeLeft) left = std::clamp(roundEven(px), 0, right - l.minWidth);
    if (edges & EdgeRight) right = std::clamp(roundEven(px), left + l.minWidth, l.width);
    if (edges & EdgeTop) top = std::clamp(roundEven(py), 0, bottom - l.minHeight);
    if (edges & EdgeBottom) bottom = std::clamp(roundEven(py), top + l.minHeight, l.height);
    return {left, top, right - left, bottom - top};
}

Crop moveCrop(const Crop& crop, int dx, int dy, Dims display)
{
    const Limits l = limitsFor(display);
    Crop moved = crop;
    moved.x = std::clamp(roundEven(crop.x + dx), 0, l.width - crop.w);
    moved.y = std::clamp(roundEven(crop.y + dy), 0, l.height - crop.h);
    return moved;
}

}  // namespace rc
