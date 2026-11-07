#include "app/audio_preview.h"

#include <QDebug>
#include <cstdint>
#include <memory>

#include "media/pcm_source.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <pulse/error.h>
#include <pulse/simple.h>
#endif

namespace {

constexpr int kBytesPerFrame = rc::PcmSource::kChannels * sizeof(int16_t);
constexpr int kBufferMs = 80;
constexpr size_t kChunkFrames = rc::PcmSource::kRate / 50;  // 20 ms: how long a stop can take

#ifdef _WIN32

// waveOut, which every Windows has: a ring of 20 ms buffers, refilled as the
// device hands them back. The event wakes on every buffer completion.
void playSegments(const std::string& path, const std::vector<rc::Span>& segments, const std::atomic<bool>& cancel)
{
    rc::PcmSource source(path, segments);
    constexpr int kBuffers = kBufferMs / 20;

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = rc::PcmSource::kChannels;
    format.nSamplesPerSec = rc::PcmSource::kRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = kBytesPerFrame;
    format.nAvgBytesPerSec = rc::PcmSource::kRate * kBytesPerFrame;

    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HWAVEOUT device = nullptr;
    if (waveOutOpen(&device, WAVE_MAPPER, &format, DWORD_PTR(done), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        qInfo() << "audio preview unavailable: no wave output device";
        CloseHandle(done);
        return;
    }

    std::vector<std::vector<int16_t>> buffers(kBuffers, std::vector<int16_t>(kChunkFrames * rc::PcmSource::kChannels));
    std::vector<WAVEHDR> headers(kBuffers);
    const auto busy = [&](const WAVEHDR& h) { return (h.dwFlags & WHDR_PREPARED) && !(h.dwFlags & WHDR_DONE); };

    bool finished = false;
    for (int next = 0; !finished && !cancel.load(std::memory_order_relaxed); next = (next + 1) % kBuffers) {
        WAVEHDR& header = headers[next];
        while (busy(header) && !cancel.load(std::memory_order_relaxed)) WaitForSingleObject(done, 20);
        if (cancel.load(std::memory_order_relaxed)) break;
        if (header.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(device, &header, sizeof header);

        const size_t frames = source.read(buffers[next].data(), kChunkFrames);
        if (frames == 0) {
            finished = true;
            break;
        }
        header = {};
        header.lpData = reinterpret_cast<LPSTR>(buffers[next].data());
        header.dwBufferLength = DWORD(frames * kBytesPerFrame);
        waveOutPrepareHeader(device, &header, sizeof header);
        waveOutWrite(device, &header, sizeof header);
    }
    if (finished) {  // let the tail play out
        for (const WAVEHDR& header : headers)
            while (busy(header) && !cancel.load(std::memory_order_relaxed)) WaitForSingleObject(done, 20);
    }
    waveOutReset(device);  // drops anything still queued
    for (WAVEHDR& header : headers)
        if (header.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(device, &header, sizeof header);
    waveOutClose(device);
    CloseHandle(done);
}

#else

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

#endif

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
