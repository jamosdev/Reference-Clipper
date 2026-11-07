#include "media/probe.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "media/stream_reader.h"

namespace rc {
namespace {

int rotationOf(const AVStream* stream)
{
    const AVPacketSideData* sd = av_packet_side_data_get(stream->codecpar->coded_side_data,
                                                         stream->codecpar->nb_coded_side_data,
                                                         AV_PKT_DATA_DISPLAYMATRIX);
    if (!sd || sd->size < 9 * int(sizeof(int32_t))) return 0;
    const double degrees = -av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
    if (std::isnan(degrees)) return 0;
    const int snapped = int(std::lround(degrees / 90)) * 90;
    return ((snapped % 360) + 360) % 360;
}

// The clip is as long as its video. Containers disagree on what their duration
// field means (Matroska's is the end timestamp, so it includes any start
// offset) and audio can outlast the picture, so read the last packets instead.
double videoEnd(const StreamReader& reader)
{
    AVFormatContext* format = reader.format();
    AVStream* st = reader.stream();
    const double tb = av_q2d(st->time_base);
    const double span = format->duration == AV_NOPTS_VALUE ? 0 : format->duration / double(AV_TIME_BASE);
    const auto past = int64_t(std::llround((reader.origin() + span) / tb)) + 1;
    if (av_seek_frame(format, st->index, past, AVSEEK_FLAG_BACKWARD) < 0) return 0;

    double end = 0;
    PacketPtr packet = makePacket();
    while (av_read_frame(format, packet.get()) >= 0) {
        if (packet->stream_index == st->index && packet->pts != AV_NOPTS_VALUE)
            end = std::max(end, (packet->pts + std::max<int64_t>(packet->duration, 0)) * tb);
        av_packet_unref(packet.get());
    }
    return end > 0 ? end - reader.origin() : 0;
}

}  // namespace

ClipInfo probeClip(const std::string& path)
{
    const StreamReader reader(path, AVMEDIA_TYPE_VIDEO);
    const AVStream* st = reader.stream();
    const AVCodecParameters* par = st->codecpar;
    const AVFormatContext* format = reader.format();

    ClipInfo info;
    info.path = path;
    info.name = std::filesystem::path(path).filename().string();
    info.rotation = rotationOf(st);

    AVRational sar = st->sample_aspect_ratio.num > 0 ? st->sample_aspect_ratio : par->sample_aspect_ratio;
    if (sar.num <= 0 || sar.den <= 0) sar = {1, 1};
    const int width = int(std::lround(par->width * av_q2d(sar)));
    info.display = info.rotation % 180 ? Dims{par->height, width} : Dims{width, par->height};

    AVRational rate = st->avg_frame_rate.num > 0 ? st->avg_frame_rate : st->r_frame_rate;
    info.fps = rate.num > 0 && rate.den > 0 ? av_q2d(rate) : 30;

    info.duration = videoEnd(reader);
    if (info.duration <= 0 && format->duration != AV_NOPTS_VALUE)
        info.duration = format->duration / double(AV_TIME_BASE);
    if (info.duration <= 0 && st->nb_frames > 0) info.duration = st->nb_frames / info.fps;
    if (info.duration <= 0) throw MediaError("could not determine duration of " + path);

    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    info.sizeBytes = ec ? 0 : (long long)size;

    AVFormatContext* fmt = reader.format();
    info.hasAudio = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) >= 0;
    return info;
}

LibraryVersions libraryVersions()
{
    LibraryVersions v;
    std::string version = av_version_info();
    // Distro builds report "n9.0.2"; the status bar shows "ffmpeg 9.0.2".
    if (!version.empty() && version[0] == 'n') version.erase(0, 1);
    v.ffmpeg = version;
#ifdef RC_SVTAV1_VERSION
    v.svtav1 = RC_SVTAV1_VERSION;
#endif
    v.canEncode = avcodec_find_encoder_by_name("libsvtav1") && avcodec_find_encoder_by_name("aac");
    return v;
}

}  // namespace rc
