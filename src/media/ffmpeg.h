// libav* includes and RAII. Only .cpp files and media headers include this.
#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace rc {

class MediaError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Paths travel as UTF-8 std::string, which is what FFmpeg takes on every
// platform. std::filesystem::path(std::string) instead reads the ANSI code page
// on Windows, so go through these for anything that touches the filesystem.
inline std::filesystem::path fsPath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

inline std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

inline std::string avError(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof text);
    return text;
}

inline void check(int ret, const std::string& what)
{
    if (ret < 0) throw MediaError(what + ": " + avError(ret));
}

struct FrameDeleter {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketDeleter {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct CodecDeleter {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct InputDeleter {
    void operator()(AVFormatContext* c) const { avformat_close_input(&c); }
};
struct GraphDeleter {
    void operator()(AVFilterGraph* g) const { avfilter_graph_free(&g); }
};
struct SwrDeleter {
    void operator()(SwrContext* s) const { swr_free(&s); }
};

using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecDeleter>;
using InputPtr = std::unique_ptr<AVFormatContext, InputDeleter>;
using GraphPtr = std::unique_ptr<AVFilterGraph, GraphDeleter>;
using SwrPtr = std::unique_ptr<SwrContext, SwrDeleter>;

inline FramePtr makeFrame()
{
    FramePtr f(av_frame_alloc());
    if (!f) throw MediaError("out of memory");
    return f;
}

inline PacketPtr makePacket()
{
    PacketPtr p(av_packet_alloc());
    if (!p) throw MediaError("out of memory");
    return p;
}

}  // namespace rc
