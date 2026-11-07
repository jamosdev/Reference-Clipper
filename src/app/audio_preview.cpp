#include "app/audio_preview.h"

#include <pulse/error.h>
#include <pulse/simple.h>

#include <QDebug>
#include <cstdint>
#include <memory>

#include "media/pcm_source.h"

namespace {

constexpr int kBytesPerFrame = rc::PcmSource::kChannels * sizeof(int16_t);
constexpr int kBufferMs = 80;
constexpr size_t kChunkFrames = rc::PcmSource::kRate / 50;  // 20 ms: how long a stop can take

struct SimpleDeleter {
    void operator()(pa_simple* stream) const { pa_simple_free(stream); }
};

void playSegments(const std::string& path, const std::vector<rc::Span>& segments, const std::atomic<bool>& cancel)
{
    rc::PcmSource source(path, segments);

    const pa_sample_spec spec{PA_SAMPLE_S16LE, rc::PcmSource::kRate, rc::PcmSource::kChannels};
    pa_buffer_attr buffer;
    buffer.maxlength = uint32_t(-1);
    buffer.tlength = uint32_t(rc::PcmSource::kRate * kBytesPerFrame * kBufferMs / 1000);
    buffer.prebuf = uint32_t(-1);
    buffer.minreq = uint32_t(-1);
    buffer.fragsize = uint32_t(-1);

    int error = 0;
    std::unique_ptr<pa_simple, SimpleDeleter> stream(
        pa_simple_new(nullptr, "Reference Clipper", PA_STREAM_PLAYBACK, nullptr, "Preview", &spec, nullptr, &buffer, &error));
    if (!stream) {
        qInfo() << "audio preview unavailable:" << pa_strerror(error);
        return;
    }

    std::vector<int16_t> chunk(kChunkFrames * rc::PcmSource::kChannels);
    while (!cancel.load(std::memory_order_relaxed)) {
        const size_t frames = source.read(chunk.data(), kChunkFrames);
        if (frames == 0) {
            pa_simple_drain(stream.get(), &error);
            return;
        }
        if (pa_simple_write(stream.get(), chunk.data(), frames * kBytesPerFrame, &error) < 0) {
            qWarning() << "audio preview:" << pa_strerror(error);
            return;
        }
    }
    pa_simple_flush(stream.get(), &error);  // drop what's queued, or a pause trails sound
}

}  // namespace

AudioPreview::~AudioPreview()
{
    stop();
}

void AudioPreview::play(const QString& path, const std::vector<rc::Span>& segments)
{
    stop();
    cancel_ = false;
    thread_ = std::thread([this, path = path.toStdString(), segments] {
        try {
            playSegments(path, segments, cancel_);
        } catch (const std::exception& e) {
            qWarning() << "audio preview:" << e.what();
        }
    });
}

void AudioPreview::stop()
{
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}
