#include "media/frame_image.h"

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
