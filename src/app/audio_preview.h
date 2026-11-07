// Monitoring audio over the PulseAudio protocol, which PipeWire also serves.
// Not QtMultimedia: its first device query loads its FFmpeg plugin, which probes
// hardware encoders and pulls Vulkan and the GPU driver into the process, costing
// ~430 ms on the first Play. Connecting here takes ~15 ms.
#pragma once

#include <QString>
#include <atomic>
#include <thread>
#include <vector>

#include "core/timeline.h"

class AudioPreview {
public:
    AudioPreview() = default;
    ~AudioPreview();
    AudioPreview(const AudioPreview&) = delete;
    AudioPreview& operator=(const AudioPreview&) = delete;

    // Starts playing `segments` of `path` in order, replacing whatever was
    // playing. With no audio server this does nothing: the picture still plays.
    void play(const QString& path, const std::vector<rc::Span>& segments);
    // Returns within one write chunk, so it is safe on the UI thread.
    void stop();

private:
    std::thread thread_;
    std::atomic<bool> cancel_{false};
};
