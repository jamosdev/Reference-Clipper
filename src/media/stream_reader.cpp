#include "media/stream_reader.h"

#include <algorithm>
#include <cmath>

namespace rc {
namespace {

constexpr double kTimeEpsilon = 1e-4;

InputPtr openInput(const std::string& path)
{
    AVFormatContext* raw = nullptr;
    check(avformat_open_input(&raw, path.c_str(), nullptr, nullptr), "open " + path);
    InputPtr format(raw);
    check(avformat_find_stream_info(format.get(), nullptr), "read stream info");
    return format;
}

double clipOrigin(AVFormatContext* format)
{
    const int video = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video >= 0) {
        const AVStream* st = format->streams[video];
        if (st->start_time != AV_NOPTS_VALUE) return st->start_time * av_q2d(st->time_base);
    }
    return format->start_time == AV_NOPTS_VALUE ? 0 : format->start_time / double(AV_TIME_BASE);
}

}  // namespace

StreamReader::StreamReader(const std::string& path, AVMediaType type)
    : format_(openInput(path)), packet_(makePacket()), pending_(makeFrame())
{
    origin_ = clipOrigin(format_.get());
    const AVCodec* decoder = nullptr;
    const int index = av_find_best_stream(format_.get(), type, -1, -1, &decoder, 0);
    check(index, std::string("find ") + av_get_media_type_string(type) + " stream");
    if (!decoder) throw MediaError("no decoder for stream");
    stream_ = format_->streams[index];
    for (unsigned i = 0; i < format_->nb_streams; ++i)
        if (int(i) != index) format_->streams[i]->discard = AVDISCARD_ALL;

    codec_.reset(avcodec_alloc_context3(decoder));
    if (!codec_) throw MediaError("out of memory");
    check(avcodec_parameters_to_context(codec_.get(), stream_->codecpar), "copy codec parameters");
    codec_->pkt_timebase = stream_->time_base;
    codec_->thread_count = 0;
    check(avcodec_open2(codec_.get(), decoder, nullptr), "open decoder");
}

bool StreamReader::decodeOne(AVFrame* out)
{
    while (true) {
        const int ret = avcodec_receive_frame(codec_.get(), out);
        if (ret == 0) return true;
        if (ret == AVERROR_EOF) return false;
        if (ret != AVERROR(EAGAIN)) throw MediaError("decode: " + avError(ret));
        if (flushing_) return false;

        const int read = av_read_frame(format_.get(), packet_.get());
        if (read == AVERROR_EOF) {
            flushing_ = true;
            check(avcodec_send_packet(codec_.get(), nullptr), "flush decoder");
            continue;
        }
        check(read, "read packet");
        if (packet_->stream_index == stream_->index) {
            const int sent = avcodec_send_packet(codec_.get(), packet_.get());
            // A damaged packet should cost a frame, not the whole clip.
            if (sent < 0 && sent != AVERROR_INVALIDDATA) {
                av_packet_unref(packet_.get());
                throw MediaError("decode: " + avError(sent));
            }
        }
        av_packet_unref(packet_.get());
    }
}

bool StreamReader::next(AVFrame* out)
{
    av_frame_unref(out);
    bool got;
    if (hasPending_) {
        av_frame_move_ref(out, pending_.get());
        hasPending_ = false;
        got = true;
    } else {
        got = decodeOne(out);
    }
    if (!got) return false;
    lastPts_ = ptsOf(out);
    lastDuration_ = out->duration;
    return true;
}

void StreamReader::seekBefore(double t)
{
    double margin = codec_->codec_type == AVMEDIA_TYPE_AUDIO ? 0.25 : 0.1;
    while (true) {
        // The target may be negative: AAC priming packets sit before 0, and
        // landing after them costs the first frame of real audio.
        const double target = t - margin;
        const auto ts = int64_t(std::llround((target + origin_) / av_q2d(stream_->time_base)));
        int ret = av_seek_frame(format_.get(), stream_->index, ts, AVSEEK_FLAG_BACKWARD);
        if (ret < 0)  // before the first index entry: take the first packet
            ret = av_seek_frame(format_.get(), stream_->index, ts, 0);

        avcodec_flush_buffers(codec_.get());
        flushing_ = false;
        hasPending_ = false;
        av_frame_unref(pending_.get());
        lastPts_ = AV_NOPTS_VALUE;

        if (ret >= 0 && decodeOne(pending_.get())) {
            hasPending_ = true;
            if (timeOf(pending_.get()) <= t + kTimeEpsilon) return;
        }
        if (target < -2) return;  // nothing earlier exists: the stream starts late
        margin *= 4;
    }
}

void StreamReader::setKeyframesOnly(bool on)
{
    codec_->skip_frame = on ? AVDISCARD_NONKEY : AVDISCARD_DEFAULT;
}

void StreamReader::setFastDecode(bool on)
{
    codec_->skip_loop_filter = on ? AVDISCARD_ALL : AVDISCARD_DEFAULT;
}

int64_t StreamReader::ptsOf(const AVFrame* frame) const
{
    if (frame->best_effort_timestamp != AV_NOPTS_VALUE) return frame->best_effort_timestamp;
    if (frame->pts != AV_NOPTS_VALUE) return frame->pts;
    return lastPts_ == AV_NOPTS_VALUE ? 0 : lastPts_ + lastDuration_;
}

double StreamReader::timeOf(const AVFrame* frame) const
{
    return ptsOf(frame) * av_q2d(stream_->time_base) - origin_;
}

double StreamReader::durationOf(const AVFrame* frame) const
{
    if (frame->duration > 0) return frame->duration * av_q2d(stream_->time_base);
    const AVRational rate = stream_->avg_frame_rate;
    return rate.num > 0 ? av_q2d(av_inv_q(rate)) : 1.0 / 30;
}

}  // namespace rc
