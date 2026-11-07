// A/V sync is by construction. Video is the master timeline: each span snaps to
// the real frames inside it, and its output length is a whole number of source
// ticks. Audio is then cut to exactly that length, sample-accurately, against
// the same source time, so neither stream can accumulate error across joins.
// Two demuxers, one per stream, let the streams advance in lockstep so the
// muxer is fed interleaved even though spans seek independently.
#include "media/encoder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>

#include "media/ffmpeg.h"
#include "media/frame_image.h"
#include "media/probe.h"
#include "media/stream_reader.h"

namespace rc {
namespace {

constexpr double kTimeEpsilon = 1e-4;
constexpr int kAudioRate = 48000;

class Muxer {
public:
    explicit Muxer(const std::string& path) : path_(path)
    {
        check(avformat_alloc_output_context2(&context_, nullptr, "mp4", path.c_str()), "create output");
    }

    ~Muxer()
    {
        if (!context_) return;
        if (context_->pb) avio_closep(&context_->pb);
        avformat_free_context(context_);
    }

    AVStream* addStream(const AVCodecContext* encoder)
    {
        AVStream* stream = avformat_new_stream(context_, nullptr);
        if (!stream) throw MediaError("out of memory");
        check(avcodec_parameters_from_context(stream->codecpar, encoder), "copy encoder parameters");
        stream->time_base = encoder->time_base;
        return stream;
    }

    void writeHeader()
    {
        check(avio_open(&context_->pb, path_.c_str(), AVIO_FLAG_WRITE), "open output");
        AVDictionary* options = nullptr;
        av_dict_set(&options, "movflags", "+faststart", 0);
        const int ret = avformat_write_header(context_, &options);
        av_dict_free(&options);
        check(ret, "write header");
    }

    void write(AVPacket* packet) { check(av_interleaved_write_frame(context_, packet), "write packet"); }
    void finish() { check(av_write_trailer(context_), "write trailer"); }

    bool wantsGlobalHeader() const { return context_->oformat->flags & AVFMT_GLOBALHEADER; }

private:
    std::string path_;
    AVFormatContext* context_ = nullptr;
};

// libsvtav1 emits packets with no duration. MP4 derives every sample's length
// from the next dts except the last, whose length is the packet's: left at 0,
// the edit list ends before the final frame and players never show it.
void drainEncoder(AVCodecContext* encoder, AVStream* stream, Muxer& muxer, int64_t fallbackDuration = 0)
{
    PacketPtr packet = makePacket();
    while (true) {
        const int ret = avcodec_receive_packet(encoder, packet.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return;
        check(ret, "encode");
        if (packet->duration <= 0) packet->duration = fallbackDuration;
        av_packet_rescale_ts(packet.get(), encoder->time_base, stream->time_base);
        packet->stream_index = stream->index;
        muxer.write(packet.get());
    }
}

// Source frames go in with output timestamps already assigned.
class VideoEncoder {
public:
    VideoEncoder(StreamReader& reader, const ClipInfo& clip, const EncodeRequest& request, Muxer& muxer)
        : reader_(reader), clip_(clip), request_(request), muxer_(muxer), filtered_(makeFrame())
    {
        const AVCodec* codec = avcodec_find_encoder_by_name("libsvtav1");
        if (!codec) throw MediaError("this FFmpeg has no libsvtav1 encoder");
        encoder_.reset(avcodec_alloc_context3(codec));
        if (!encoder_) throw MediaError("out of memory");
    }

    // The encoder needs the source's colour tags, known once a frame is decoded.
    void open(const AVFrame* first)
    {
        buildGraph(first);

        AVCodecContext* e = encoder_.get();
        e->width = request_.size.w;
        e->height = request_.size.h;
        e->sample_aspect_ratio = {1, 1};
        e->pix_fmt = AV_PIX_FMT_YUV420P10LE;
        e->time_base = reader_.stream()->time_base;
        const AVRational rate = reader_.stream()->avg_frame_rate;
        e->framerate = rate.num > 0 ? rate : av_d2q(clip_.fps, 100000);
        e->color_primaries = first->color_primaries;
        e->color_trc = first->color_trc;
        e->colorspace = first->colorspace;
        e->color_range = first->color_range;
        if (muxer_.wantsGlobalHeader()) e->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        check(av_opt_set_int(e->priv_data, "preset", kEncoderPreset, 0), "set preset");
        check(av_opt_set_int(e->priv_data, "crf", request_.crf, 0), "set crf");
        check(avcodec_open2(e, nullptr, nullptr), "open libsvtav1");
        stream_ = muxer_.addStream(e);
    }

    void push(AVFrame* frame, int64_t duration)
    {
        lastDuration_ = duration;
        if (frame->width != inW_ || frame->height != inH_ || frame->format != inFormat_) rebuild(frame);
        check(av_buffersrc_add_frame_flags(source_, frame, AV_BUFFERSRC_FLAG_KEEP_REF), "filter video");
        pull();
    }

    void finish()
    {
        check(av_buffersrc_add_frame_flags(source_, nullptr, 0), "flush video filter");
        pull();
        check(avcodec_send_frame(encoder_.get(), nullptr), "flush video encoder");
        drainEncoder(encoder_.get(), stream_, muxer_, lastDuration_);
    }

private:
    void buildGraph(const AVFrame* frame)
    {
        graph_.reset(avfilter_graph_alloc());
        if (!graph_) throw MediaError("out of memory");
        inW_ = frame->width;
        inH_ = frame->height;
        inFormat_ = frame->format;

        const AVRational tb = reader_.stream()->time_base;
        const AVRational sar = frame->sample_aspect_ratio.num > 0 ? frame->sample_aspect_ratio : AVRational{1, 1};
        const std::string args = "video_size=" + std::to_string(frame->width) + "x" + std::to_string(frame->height)
                                 + ":pix_fmt=" + std::to_string(frame->format)
                                 + ":time_base=" + std::to_string(tb.num) + "/" + std::to_string(tb.den)
                                 + ":pixel_aspect=" + std::to_string(sar.num) + "/" + std::to_string(sar.den);
        check(avfilter_graph_create_filter(&source_, avfilter_get_by_name("buffer"), "in", args.c_str(),
                                           nullptr, graph_.get()), "create video source");
        check(avfilter_graph_create_filter(&sink_, avfilter_get_by_name("buffersink"), "out", nullptr,
                                           nullptr, graph_.get()), "create video sink");

        std::string chain;
        if (clip_.rotation == 90) chain = "transpose=clock,";
        else if (clip_.rotation == 180) chain = "hflip,vflip,";
        else if (clip_.rotation == 270) chain = "transpose=cclock,";
        if (request_.crop) {
            const bool swap = clip_.rotation % 180 != 0;
            const Crop c = cropInPixels(*request_.crop, swap ? frame->height : frame->width,
                                        swap ? frame->width : frame->height, clip_.display);
            chain += "crop=" + std::to_string(c.w) + ":" + std::to_string(c.h) + ":" + std::to_string(c.x) + ":"
                     + std::to_string(c.y) + ",";
        }
        chain += "scale=" + std::to_string(request_.size.w) + ":" + std::to_string(request_.size.h)
                 + ":flags=lanczos,setsar=1,format=yuv420p10le";

        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();
        outputs->name = av_strdup("in");
        outputs->filter_ctx = source_;
        inputs->name = av_strdup("out");
        inputs->filter_ctx = sink_;
        const int parsed = avfilter_graph_parse_ptr(graph_.get(), chain.c_str(), &inputs, &outputs, nullptr);
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        check(parsed, "parse video filter");
        check(avfilter_graph_config(graph_.get(), nullptr), "configure video filter");
    }

    // Some sources change resolution mid-stream; the output size must not.
    void rebuild(const AVFrame* frame)
    {
        check(av_buffersrc_add_frame_flags(source_, nullptr, 0), "flush video filter");
        pull();
        buildGraph(frame);
    }

    void pull()
    {
        while (true) {
            const int ret = av_buffersink_get_frame(sink_, filtered_.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return;
            check(ret, "filter video");
            filtered_->pict_type = AV_PICTURE_TYPE_NONE;
            const int sent = avcodec_send_frame(encoder_.get(), filtered_.get());
            av_frame_unref(filtered_.get());
            check(sent, "encode video");
            drainEncoder(encoder_.get(), stream_, muxer_, lastDuration_);
        }
    }

    StreamReader& reader_;
    const ClipInfo& clip_;
    const EncodeRequest& request_;
    Muxer& muxer_;
    CodecPtr encoder_;
    AVStream* stream_ = nullptr;
    GraphPtr graph_;
    AVFilterContext* source_ = nullptr;
    AVFilterContext* sink_ = nullptr;
    FramePtr filtered_;
    int inW_ = 0, inH_ = 0, inFormat_ = -1;
    int64_t lastDuration_ = 0;
};

// Output position is counted in source-rate samples, so the cut points are
// exact before any resampling. Holes in the source are filled with silence.
class AudioEncoder {
public:
    AudioEncoder(const std::string& path, int kbps, Muxer& muxer)
        : reader_(path, AVMEDIA_TYPE_AUDIO), muxer_(muxer), frame_(makeFrame()), slice_(makeFrame()),
          filtered_(makeFrame())
    {
        const AVCodec* codec = avcodec_find_encoder_by_name("aac");
        if (!codec) throw MediaError("this FFmpeg has no aac encoder");
        encoder_.reset(avcodec_alloc_context3(codec));
        if (!encoder_) throw MediaError("out of memory");
        AVCodecContext* e = encoder_.get();
        const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        check(av_channel_layout_copy(&e->ch_layout, &stereo), "set channel layout");
        e->sample_rate = kAudioRate;
        e->sample_fmt = AV_SAMPLE_FMT_FLTP;
        e->bit_rate = kbps * 1000;
        e->time_base = {1, kAudioRate};
        if (muxer.wantsGlobalHeader()) e->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        check(avcodec_open2(e, codec, nullptr), "open aac");
        stream_ = muxer.addStream(e);
        rate_ = reader_.codec()->sample_rate;
        if (rate_ <= 0) throw MediaError("audio stream has no sample rate");
        buildGraph();
    }

    void beginSegment(double sourceStart, double outputStart)
    {
        segmentSource_ = std::llround(sourceStart * rate_);
        segmentOutput_ = std::llround(outputStart * rate_);
        reader_.seekBefore(sourceStart);
        hasFrame_ = false;
        ended_ = false;
    }

    // Encodes audio up to `outputEnd`. With `complete`, a source that runs short
    // is padded so the next segment still starts on time.
    void pumpTo(double outputEnd, bool complete)
    {
        const int64_t target = std::llround(outputEnd * rate_);
        while (written_ < target) {
            if (!hasFrame_) {
                if (ended_ || !reader_.next(frame_.get())) {
                    ended_ = true;
                    break;
                }
                hasFrame_ = true;
                frameOffset_ = 0;
            }
            const int64_t frameStart = std::llround(reader_.timeOf(frame_.get()) * rate_) + frameOffset_;
            const int64_t position = frameStart - segmentSource_ + segmentOutput_;
            const int64_t available = frame_->nb_samples - frameOffset_;

            if (position + available <= written_) {  // before the cut, or re-covered
                hasFrame_ = false;
                continue;
            }
            if (position < written_) {
                frameOffset_ += int(written_ - position);
                continue;
            }
            if (position > written_) {  // hole in the source
                silence(std::min(position, target) - written_);
                continue;
            }
            const int count = int(std::min(available, target - written_));
            feed(count);
            frameOffset_ += count;
            if (frameOffset_ >= frame_->nb_samples) hasFrame_ = false;
        }
        if (complete && written_ < target) silence(target - written_);
    }

    void finish()
    {
        check(av_buffersrc_add_frame_flags(source_, nullptr, 0), "flush audio filter");
        pull();
        check(avcodec_send_frame(encoder_.get(), nullptr), "flush audio encoder");
        drainEncoder(encoder_.get(), stream_, muxer_);
    }

private:
    void buildGraph()
    {
        graph_.reset(avfilter_graph_alloc());
        if (!graph_) throw MediaError("out of memory");
        AVCodecContext* d = reader_.codec();
        AVChannelLayout layout{};
        if (d->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&layout, d->ch_layout.nb_channels);
        else
            check(av_channel_layout_copy(&layout, &d->ch_layout), "copy channel layout");
        char description[128] = {};
        av_channel_layout_describe(&layout, description, sizeof description);
        av_channel_layout_uninit(&layout);

        const std::string args = "time_base=1/" + std::to_string(rate_) + ":sample_rate=" + std::to_string(rate_)
                                 + ":sample_fmt=" + av_get_sample_fmt_name(d->sample_fmt)
                                 + ":channel_layout=" + description;
        check(avfilter_graph_create_filter(&source_, avfilter_get_by_name("abuffer"), "in", args.c_str(),
                                           nullptr, graph_.get()), "create audio source");
        check(avfilter_graph_create_filter(&sink_, avfilter_get_by_name("abuffersink"), "out", nullptr,
                                           nullptr, graph_.get()), "create audio sink");
        const std::string chain = "aresample=" + std::to_string(kAudioRate)
                                  + ":async=1:first_pts=0,aformat=sample_fmts=fltp:sample_rates="
                                  + std::to_string(kAudioRate) + ":channel_layouts=stereo";
        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();
        outputs->name = av_strdup("in");
        outputs->filter_ctx = source_;
        inputs->name = av_strdup("out");
        inputs->filter_ctx = sink_;
        const int parsed = avfilter_graph_parse_ptr(graph_.get(), chain.c_str(), &inputs, &outputs, nullptr);
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        check(parsed, "parse audio filter");
        check(avfilter_graph_config(graph_.get(), nullptr), "configure audio filter");
        av_buffersink_set_frame_size(sink_, encoder_->frame_size);
    }

    void allocateSlice(int count)
    {
        AVFrame* in = slice_.get();
        av_frame_unref(in);
        in->nb_samples = count;
        in->format = reader_.codec()->sample_fmt;
        in->sample_rate = rate_;
        check(av_channel_layout_copy(&in->ch_layout, &reader_.codec()->ch_layout), "copy channel layout");
        check(av_frame_get_buffer(in, 0), "allocate audio frame");
        in->pts = written_;
    }

    void feed(int count)
    {
        allocateSlice(count);
        const int channels = slice_->ch_layout.nb_channels;
        check(av_samples_copy(slice_->extended_data, frame_->extended_data, 0, frameOffset_, count, channels,
                              AVSampleFormat(slice_->format)), "copy samples");
        push(count);
    }

    void silence(int64_t count)
    {
        while (count > 0) {
            const int n = int(std::min<int64_t>(count, 4096));
            allocateSlice(n);
            check(av_samples_set_silence(slice_->extended_data, 0, n, slice_->ch_layout.nb_channels,
                                         AVSampleFormat(slice_->format)), "write silence");
            push(n);
            count -= n;
        }
    }

    void push(int count)
    {
        check(av_buffersrc_add_frame_flags(source_, slice_.get(), 0), "filter audio");
        written_ += count;
        pull();
    }

    void pull()
    {
        while (true) {
            const int ret = av_buffersink_get_frame(sink_, filtered_.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return;
            check(ret, "filter audio");
            const int sent = avcodec_send_frame(encoder_.get(), filtered_.get());
            av_frame_unref(filtered_.get());
            check(sent, "encode audio");
            drainEncoder(encoder_.get(), stream_, muxer_);
        }
    }

    StreamReader reader_;
    Muxer& muxer_;
    CodecPtr encoder_;
    AVStream* stream_ = nullptr;
    GraphPtr graph_;
    AVFilterContext* source_ = nullptr;
    AVFilterContext* sink_ = nullptr;
    FramePtr frame_, slice_, filtered_;
    int rate_ = 0;
    bool hasFrame_ = false;
    bool ended_ = false;
    int frameOffset_ = 0;
    int64_t written_ = 0;
    int64_t segmentSource_ = 0;
    int64_t segmentOutput_ = 0;
};

class Cancelled {};

class Job {
public:
    Job(const EncodeRequest& request, const ClipInfo& clip, const std::string& partPath,
        const std::function<void(const EncodeProgress&)>& onProgress, const std::atomic<bool>& cancel)
        : request_(request), clip_(clip), onProgress_(onProgress), cancel_(cancel), muxer_(partPath),
          reader_(request.source, AVMEDIA_TYPE_VIDEO), video_(reader_, clip, request, muxer_)
    {
        if (clip.hasAudio) audio_.emplace(request.source, request.audioKbps, muxer_);
        progress_.framesTotal = std::max<long long>(1, std::llround(totalLength(request.spans) * clip.fps));
        started_ = std::chrono::steady_clock::now();
    }

    std::vector<Span> run()
    {
        for (const Span& span : sortedByStart(request_.spans)) encodeSpan(span);
        if (!headerWritten_) throw MediaError("no video frames inside the spans");
        video_.finish();
        if (audio_) audio_->finish();
        muxer_.finish();
        return segments_;
    }

private:
    void encodeSpan(const Span& span)
    {
        const AVRational tb = reader_.stream()->time_base;
        const double seconds = av_q2d(tb);
        FramePtr frame = makeFrame();
        reader_.seekBefore(span.start);

        std::optional<int64_t> firstPts;
        int64_t endPts = 0;
        while (true) {
            if (cancel_.load(std::memory_order_relaxed)) throw Cancelled{};
            if (!reader_.next(frame.get())) break;
            const double t = reader_.timeOf(frame.get());
            const int64_t pts = reader_.ptsOf(frame.get());
            const int64_t duration = std::max<int64_t>(1, std::llround(reader_.durationOf(frame.get()) / seconds));
            if (t < span.start - kTimeEpsilon) continue;
            if (t >= span.end - kTimeEpsilon && firstPts) {
                endPts = pts;  // the next span frame fixes this segment's length exactly
                break;
            }
            const bool lastFrame = t >= span.end - kTimeEpsilon;  // span narrower than a VFR gap
            if (!firstPts) {
                firstPts = pts;
                beginSegment(frame.get(), t);
            }
            frame->pts = pts - *firstPts + outputPts_;
            video_.push(frame.get(), duration);
            endPts = pts + duration;
            ++progress_.framesDone;
            report();
            if (audio_) audio_->pumpTo((frame->pts + duration) * seconds, false);
            if (lastFrame) break;
        }
        if (!firstPts) return;  // span lies past the last frame

        const double start = *firstPts * seconds - reader_.origin();
        const double length = (endPts - *firstPts) * seconds;
        segments_.push_back({span.id, start, start + length});
        outputPts_ += endPts - *firstPts;
        if (audio_) audio_->pumpTo(outputPts_ * seconds, true);
    }

    void beginSegment(const AVFrame* frame, double sourceTime)
    {
        if (!headerWritten_) {
            video_.open(frame);
            muxer_.writeHeader();
            headerWritten_ = true;
        }
        if (audio_) audio_->beginSegment(sourceTime, outputPts_ * av_q2d(reader_.stream()->time_base));
    }

    void report()
    {
        if (!onProgress_) return;
        const auto now = std::chrono::steady_clock::now();
        if (now - lastReport_ < std::chrono::milliseconds(200)) return;
        lastReport_ = now;
        const double elapsed = std::chrono::duration<double>(now - started_).count();
        progress_.fps = elapsed > 0 ? progress_.framesDone / elapsed : 0;
        onProgress_(progress_);
    }

    const EncodeRequest& request_;
    const ClipInfo& clip_;
    const std::function<void(const EncodeProgress&)>& onProgress_;
    const std::atomic<bool>& cancel_;
    Muxer muxer_;
    StreamReader reader_;
    VideoEncoder video_;
    std::optional<AudioEncoder> audio_;
    EncodeProgress progress_;
    std::vector<Span> segments_;
    int64_t outputPts_ = 0;
    bool headerWritten_ = false;
    std::chrono::steady_clock::time_point started_, lastReport_;
};

}  // namespace

EncodeResult encodeClip(const EncodeRequest& request,
                        const std::function<void(const EncodeProgress&)>& onProgress,
                        const std::atomic<bool>& cancel)
{
    namespace fs = std::filesystem;
    EncodeResult result;
    const std::string part = request.output + ".part";
    std::error_code ec;
    try {
        if (request.spans.empty()) throw MediaError("no spans to encode");
        const fs::path folder = fsPath(request.output).parent_path();
        if (!folder.empty()) fs::create_directories(folder);
        const ClipInfo clip = probeClip(request.source);
        {
            Job job(request, clip, part, onProgress, cancel);
            result.segments = job.run();
        }
        result.drift = probeDrift(part);
        if (!result.drift.withinTolerance) {
            result.error = "A/V drift " + std::to_string(int(std::lround(result.drift.driftSeconds * 1000)))
                           + " ms exceeds one frame";
        } else {
            fs::rename(fsPath(part), fsPath(request.output));
            result.sizeBytes = (long long)fs::file_size(fsPath(request.output));
            result.ok = true;
        }
    } catch (const Cancelled&) {
        result.cancelled = true;
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    if (!result.ok) fs::remove(fsPath(part), ec);
    return result;
}

}  // namespace rc
