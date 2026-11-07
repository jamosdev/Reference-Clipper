#include "media/waveform.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "media/stream_reader.h"

namespace rc {
namespace {

float sampleAt(const AVFrame* frame, int channel, int index)
{
    const auto format = AVSampleFormat(frame->format);
    const bool planar = av_sample_fmt_is_planar(format);
    const int channels = frame->ch_layout.nb_channels;
    const uint8_t* base = frame->extended_data[planar ? channel : 0];
    const int i = planar ? index : index * channels + channel;
    switch (av_get_packed_sample_fmt(format)) {
    case AV_SAMPLE_FMT_U8: return (base[i] - 128) / 128.0f;
    case AV_SAMPLE_FMT_S16: return reinterpret_cast<const int16_t*>(base)[i] / 32768.0f;
    case AV_SAMPLE_FMT_S32: return reinterpret_cast<const int32_t*>(base)[i] / 2147483648.0f;
    case AV_SAMPLE_FMT_S64: return float(reinterpret_cast<const int64_t*>(base)[i] / 9223372036854775808.0);
    case AV_SAMPLE_FMT_FLT: return reinterpret_cast<const float*>(base)[i];
    case AV_SAMPLE_FMT_DBL: return float(reinterpret_cast<const double*>(base)[i]);
    default: return 0;
    }
}

}  // namespace

std::vector<float> computeWaveform(const std::string& path, double duration, int bins,
                                   const std::atomic<bool>& cancel,
                                   const std::function<void(const std::vector<float>&)>& onUpdate)
{
    std::vector<float> peaks(size_t(bins), 0.0f);
    if (duration <= 0 || bins <= 0) return peaks;
    StreamReader reader(path, AVMEDIA_TYPE_AUDIO);
    FramePtr frame = makeFrame();

    using Clock = std::chrono::steady_clock;
    auto lastUpdate = Clock::now();
    while (!cancel.load(std::memory_order_relaxed) && reader.next(frame.get())) {
        const double start = reader.timeOf(frame.get());
        const double perBin = duration / bins;
        const int channels = frame->ch_layout.nb_channels;
        // A few hundred probes per frame hold the peaks well at this resolution.
        const int stride = std::max(1, frame->nb_samples / 256);
        for (int i = 0; i < frame->nb_samples; i += stride) {
            const double t = start + double(i) / frame->sample_rate;
            const int bin = int(t / perBin);
            if (bin < 0 || bin >= bins) continue;
            float& peak = peaks[size_t(bin)];
            for (int c = 0; c < channels; ++c) peak = std::max(peak, std::abs(sampleAt(frame.get(), c, i)));
        }
        if (onUpdate && Clock::now() - lastUpdate > std::chrono::milliseconds(150)) {
            lastUpdate = Clock::now();
            onUpdate(peaks);
        }
    }
    return peaks;
}

}  // namespace rc
