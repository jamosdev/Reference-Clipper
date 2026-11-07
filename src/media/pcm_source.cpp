#include "media/pcm_source.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rc {

PcmSource::PcmSource(const std::string& path, std::vector<Span> segments)
    : reader_(path, AVMEDIA_TYPE_AUDIO), frame_(makeFrame()), segments_(std::move(segments))
{
    startSegment();
}

void PcmSource::startSegment()
{
    if (segment_ >= segments_.size()) return;
    const Span& s = segments_[segment_];
    segmentFrames_ = std::llround(s.length() * kRate);
    remaining_ = segmentFrames_;
    skip_ = 0;
    silence_ = 0;
    sourceEnded_ = false;
    resampler_.reset();
    buffer_.clear();
    bufferPos_ = 0;
    reader_.seekBefore(s.start);
}

bool PcmSource::decodeMore()
{
    const Span& s = segments_[segment_];
    while (true) {
        if (sourceEnded_ || !reader_.next(frame_.get())) {
            sourceEnded_ = true;
            return false;
        }
        const double end = reader_.timeOf(frame_.get()) + double(frame_->nb_samples) / frame_->sample_rate;
        if (end > s.start) break;
    }

    if (!resampler_) {
        SwrContext* raw = nullptr;
        const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        AVChannelLayout in{};
        if (frame_->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&in, frame_->ch_layout.nb_channels);
        else
            av_channel_layout_copy(&in, &frame_->ch_layout);
        const int ret = swr_alloc_set_opts2(&raw, &stereo, AV_SAMPLE_FMT_S16, kRate, &in,
                                            AVSampleFormat(frame_->format), frame_->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&in);
        check(ret, "configure resampler");
        resampler_.reset(raw);
        check(swr_init(resampler_.get()), "open resampler");
    }

    // Timestamps place the audio: a seek can land either side of the segment
    // start, and sources can have holes. Differences smaller than the
    // resampler's own delay are noise and left alone.
    const int64_t emitted = segmentFrames_ - remaining_;
    const int64_t position = std::llround((reader_.timeOf(frame_.get()) - s.start) * kRate);
    const int64_t offset = position - emitted;
    constexpr int64_t kTolerance = 64;
    if (offset > kTolerance) silence_ = offset;
    else if (offset < -kTolerance) skip_ = -offset;

    const int capacity = swr_get_out_samples(resampler_.get(), frame_->nb_samples);
    buffer_.resize(size_t(capacity) * kChannels);
    uint8_t* out = reinterpret_cast<uint8_t*>(buffer_.data());
    const int produced = swr_convert(resampler_.get(), &out, capacity,
                                     const_cast<const uint8_t**>(frame_->extended_data), frame_->nb_samples);
    check(produced, "resample");
    buffer_.resize(size_t(produced) * kChannels);
    bufferPos_ = 0;
    return true;
}

size_t PcmSource::read(int16_t* out, size_t frames)
{
    const auto writeSilence = [&](size_t n) {
        std::memset(out, 0, n * kChannels * sizeof(int16_t));
        out += n * kChannels;
        remaining_ -= int64_t(n);
        return n;
    };

    size_t written = 0;
    while (written < frames && segment_ < segments_.size()) {
        if (remaining_ <= 0) {
            ++segment_;
            startSegment();
            continue;
        }
        if (silence_ > 0) {
            const size_t n = std::min({frames - written, size_t(remaining_), size_t(silence_)});
            written += writeSilence(n);
            silence_ -= int64_t(n);
            continue;
        }
        const size_t available = buffer_.size() / kChannels - bufferPos_;
        if (available == 0) {
            if (!decodeMore())  // source ran out inside the segment: keep the timing
                written += writeSilence(std::min<size_t>(frames - written, size_t(remaining_)));
            continue;
        }
        if (skip_ > 0) {
            const size_t drop = std::min<size_t>(available, size_t(skip_));
            bufferPos_ += drop;
            skip_ -= int64_t(drop);
            continue;
        }
        const size_t n = std::min({available, frames - written, size_t(remaining_)});
        std::memcpy(out, buffer_.data() + bufferPos_ * kChannels, n * kChannels * sizeof(int16_t));
        out += n * kChannels;
        written += n;
        bufferPos_ += n;
        remaining_ -= int64_t(n);
    }
    return written;
}

}  // namespace rc
