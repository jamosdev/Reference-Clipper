// Demux + decode of one stream of a file, with a clip timeline shared by the
// audio and video readers of that file.
#pragma once

#include "media/ffmpeg.h"

namespace rc {

class StreamReader {
public:
    // Throws MediaError if the file cannot be opened or has no such stream.
    StreamReader(const std::string& path, AVMediaType type);

    // Decodes the next frame into `out`. False at end of stream.
    bool next(AVFrame* out);

    // Positions the reader so next() yields a frame at or before t, unless the
    // stream starts later or ends earlier. Plain keyframe seeks can land after
    // t in files whose index is in dts, so this widens the seek until it holds.
    void seekBefore(double t);

    // Decode keyframes only, for fast thumbnails.
    void setKeyframesOnly(bool on);

    double timeOf(const AVFrame* frame) const;
    double durationOf(const AVFrame* frame) const;
    int64_t ptsOf(const AVFrame* frame) const;

    // Clip time 0 is the first video frame, so spans mean the same thing to the
    // audio reader, including when audio starts before or after the video.
    double origin() const { return origin_; }

    AVStream* stream() const { return stream_; }
    AVCodecContext* codec() const { return codec_.get(); }
    AVFormatContext* format() const { return format_.get(); }

private:
    bool decodeOne(AVFrame* out);

    InputPtr format_;
    CodecPtr codec_;
    AVStream* stream_ = nullptr;
    PacketPtr packet_;
    FramePtr pending_;
    bool hasPending_ = false;
    bool flushing_ = false;
    bool ended_ = false;
    double origin_ = 0;
    int64_t lastPts_ = AV_NOPTS_VALUE;
    int64_t lastDuration_ = 0;
};

}  // namespace rc
