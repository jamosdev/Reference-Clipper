#include "media/frame_image.h"

#include <algorithm>
#include <utility>

namespace rc {
namespace {

RgbImage rotated(const RgbImage& in, int rotation)
{
    RgbImage out;
    const bool swap = rotation % 180 != 0;
    out.width = swap ? in.height : in.width;
    out.height = swap ? in.width : in.height;
    out.pixels.resize(in.pixels.size());
    for (int y = 0; y < in.height; ++y) {
        for (int x = 0; x < in.width; ++x) {
            int dx = x, dy = y;
            if (rotation == 90) { dx = in.height - 1 - y; dy = x; }
            else if (rotation == 180) { dx = in.width - 1 - x; dy = in.height - 1 - y; }
            else if (rotation == 270) { dx = y; dy = in.width - 1 - x; }
            out.pixels[size_t(dy) * out.width + dx] = in.pixels[size_t(y) * in.width + x];
        }
    }
    return out;
}

}  // namespace

FrameImager::~FrameImager()
{
    sws_freeContext(context_);
}

RgbImage FrameImager::convert(const AVFrame* frame, Dims size, int rotation, ScaleQuality quality)
{
    const bool swap = rotation % 180 != 0;
    const int width = std::max(2, swap ? size.h : size.w);
    const int height = std::max(2, swap ? size.w : size.h);
    const int flags = quality == ScaleQuality::Lanczos ? SWS_LANCZOS | SWS_ACCURATE_RND : SWS_BILINEAR;

    context_ = sws_getCachedContext(context_, frame->width, frame->height, AVPixelFormat(frame->format),
                                    width, height, AV_PIX_FMT_BGRA, flags, nullptr, nullptr, nullptr);
    if (!context_) throw MediaError("cannot create scaler");

    // Without this swscale assumes BT.601 and HD sources render with shifted colours.
    const int space = frame->colorspace == AVCOL_SPC_UNSPECIFIED
                          ? (frame->height >= 720 ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M)
                          : frame->colorspace;
    const int full = frame->color_range == AVCOL_RANGE_JPEG;
    sws_setColorspaceDetails(context_, sws_getCoefficients(space), full,
                             sws_getCoefficients(space), 1, 0, 1 << 16, 1 << 16);

    RgbImage image;
    image.width = width;
    image.height = height;
    image.pixels.resize(size_t(width) * height);
    uint8_t* planes[4] = {reinterpret_cast<uint8_t*>(image.pixels.data()), nullptr, nullptr, nullptr};
    const int strides[4] = {width * 4, 0, 0, 0};
    sws_scale(context_, frame->data, frame->linesize, 0, frame->height, planes, strides);
    return rotation ? rotated(image, rotation) : image;
}

Crop cropInPixels(const Crop& crop, int rotatedW, int rotatedH, Dims display)
{
    const double fx = double(rotatedW) / display.w;
    const double fy = double(rotatedH) / display.h;
    Crop out;
    out.x = int(crop.x * fx) & ~1;
    out.y = int(crop.y * fy) & ~1;
    out.w = std::min(int(crop.w * fx + 0.5) & ~1, rotatedW - out.x);
    out.h = std::min(int(crop.h * fy + 0.5) & ~1, rotatedH - out.y);
    return out;
}

FramePtr croppedFrame(const AVFrame* frame, const Crop& crop, int rotation, Dims display)
{
    const bool swap = rotation % 180 != 0;
    const int rotatedW = swap ? frame->height : frame->width;
    const int rotatedH = swap ? frame->width : frame->height;
    const Crop r = cropInPixels(crop, rotatedW, rotatedH, display);

    // Back from the displayed picture to stored pixels.
    int x = r.x, y = r.y, w = r.w, h = r.h;
    if (rotation == 90) {
        x = r.y;
        y = frame->height - r.x - r.w;
        w = r.h;
        h = r.w;
    } else if (rotation == 180) {
        x = frame->width - r.x - r.w;
        y = frame->height - r.y - r.h;
    } else if (rotation == 270) {
        x = frame->width - r.y - r.h;
        y = r.x;
        w = r.h;
        h = r.w;
    }

    FramePtr out(av_frame_clone(frame));
    if (!out) throw MediaError("out of memory");
    out->crop_left = size_t(x);
    out->crop_top = size_t(y);
    out->crop_right = size_t(frame->width - x - w);
    out->crop_bottom = size_t(frame->height - y - h);
    check(av_frame_apply_cropping(out.get(), AV_FRAME_CROP_UNALIGNED), "crop frame");
    return out;
}

RgbImage extractFrame(const std::string& path, double t, int width, bool fast)
{
    const ClipInfo clip = probeClip(path);
    StreamReader reader(path, AVMEDIA_TYPE_VIDEO);
    reader.setKeyframesOnly(fast);
    reader.seekBefore(t);

    FramePtr frame = makeFrame();
    if (!reader.next(frame.get())) return {};
    if (!fast) {
        FramePtr following = makeFrame();
        while (reader.next(following.get()) && reader.timeOf(following.get()) <= t + 1e-4) std::swap(frame, following);
    }
    const Dims size{width, std::max(2, int(double(width) * clip.display.h / clip.display.w))};
    FrameImager imager;
    return imager.convert(frame.get(), size, clip.rotation, ScaleQuality::Fast);
}

}  // namespace rc
