// AVFrame to 32-bit image, the layout of QImage::Format_RGB32.
#pragma once

#include <cstdint>
#include <vector>

#include "core/quality.h"
#include "media/ffmpeg.h"
#include "media/probe.h"
#include "media/stream_reader.h"

namespace rc {

struct RgbImage {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;  // tightly packed

    bool empty() const { return pixels.empty(); }
};

enum class ScaleQuality { Fast, Lanczos };

class FrameImager {
public:
    FrameImager() = default;
    ~FrameImager();
    FrameImager(const FrameImager&) = delete;
    FrameImager& operator=(const FrameImager&) = delete;

    // `size` is the displayed size, after `rotation` (clockwise degrees).
    RgbImage convert(const AVFrame* frame, Dims size, int rotation, ScaleQuality quality);

private:
    SwsContext* context_ = nullptr;
};

// A frame of `info`-rotated `path` near t, `width` pixels wide. With `fast` it is
// the nearest keyframe, which is what strips and gallery cards need.
RgbImage extractFrame(const std::string& path, double t, int width, bool fast);

}  // namespace rc
