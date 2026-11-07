#include "media/drift.h"

#include <algorithm>
#include <cmath>

#include "media/stream_reader.h"

namespace rc {

DriftReport probeDrift(const std::string& path)
{
    AVFormatContext* raw = nullptr;
    check(avformat_open_input(&raw, path.c_str(), nullptr, nullptr), "open " + path);
    InputPtr format(raw);
    check(avformat_find_stream_info(format.get(), nullptr), "read stream info");

    DriftReport report;
    const int video = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int audio = av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (video >= 0) {
        const AVRational rate = format->streams[video]->avg_frame_rate;
        report.frameDuration = rate.num > 0 ? av_q2d(av_inv_q(rate)) : 1.0 / 30;
    }

    PacketPtr packet = makePacket();
    while (av_read_frame(format.get(), packet.get()) >= 0) {
        const int index = packet->stream_index;
        if ((index == video || index == audio) && packet->pts != AV_NOPTS_VALUE) {
            const double tb = av_q2d(format->streams[index]->time_base);
            // AAC priming sits at negative pts; the edit list hides it from playback.
            const double start = std::max(0.0, packet->pts * tb);
            const double end = (packet->pts + packet->duration) * tb;
            StreamExtent& extent = index == video ? report.video : report.audio;
            extent.start = extent.present ? std::min(extent.start, start) : start;
            extent.end = extent.present ? std::max(extent.end, end) : end;
            extent.present = true;
        }
        av_packet_unref(packet.get());
    }

    if (report.video.present && report.audio.present) {
        report.driftSeconds = std::max(std::abs(report.video.start - report.audio.start),
                                       std::abs(report.video.end - report.audio.end));
        report.withinTolerance = report.driftSeconds <= report.frameDuration + 1e-6;
    }
    return report;
}

}  // namespace rc
