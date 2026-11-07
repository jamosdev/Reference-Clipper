// Runs the real encoder over synthetic media and measures the decoded result.
#include <QFile>
#include <QtTest>
#include <cmath>
#include <filesystem>

#include "fixtures.h"
#include "media/drift.h"
#include "media/encoder.h"
#include "media/pcm_source.h"
#include "media/probe.h"
#include "media/stream_reader.h"
#include "media/waveform.h"

using namespace rc;

namespace {

constexpr double kFrame = 1.0 / 30;

EncodeResult encode(const QString& source, std::vector<Span> spans, Dims size, const QString& name)
{
    EncodeRequest request;
    request.source = source.toStdString();
    request.spans = std::move(spans);
    request.size = size;
    request.crf = 45;
    request.audioKbps = 128;
    request.output = (fixtures::dir() + "/out/" + name).toStdString();
    std::atomic<bool> cancel{false};
    return encodeClip(request, {}, cancel);
}

QString outPath(const QString& name) { return fixtures::dir() + "/out/" + name; }

std::vector<double> flashTimes(const QString& path, int* frameCount = nullptr)
{
    StreamReader reader(path.toStdString(), AVMEDIA_TYPE_VIDEO);
    FramePtr frame = makeFrame();
    std::vector<double> flashes;
    int frames = 0;
    while (reader.next(frame.get())) {
        ++frames;
        const auto* row = frame->data[0] + frame->linesize[0] * (frame->height / 2);
        const int luma = frame->format == AV_PIX_FMT_YUV420P10LE
                             ? reinterpret_cast<const uint16_t*>(row)[frame->width / 2] >> 2
                             : row[frame->width / 2];
        if (luma > 128) flashes.push_back(reader.timeOf(frame.get()));
    }
    if (frameCount) *frameCount = frames;
    return flashes;
}

std::vector<double> beepTimes(const QString& path, double duration)
{
    PcmSource pcm(path.toStdString(), {{0, 0, duration}});
    std::vector<int16_t> samples(size_t(PcmSource::kRate * duration) * 2);
    const size_t got = pcm.read(samples.data(), samples.size() / 2);
    std::vector<double> beeps;
    double lastLoud = -1;
    for (size_t i = 0; i < got; ++i) {
        if (std::abs(int(samples[i * 2])) < 8000) continue;
        const double t = double(i) / PcmSource::kRate;
        // The 30 ms beeps can land 100 ms apart where a span join brings them together.
        if (t - lastLoud > 0.05) beeps.push_back(t);
        lastLoud = t;
    }
    return beeps;
}

}  // namespace

class TestEncode : public QObject {
    Q_OBJECT
private slots:
    void cutsAreFrameAccurateAcrossSpans()
    {
        const QString source = fixtures::flashBeep();
        const std::vector<Span> spans{{1, 1.5, 3.5}, {2, 5.25, 7.1}, {3, 9.0, 10.0}};
        const EncodeResult result = encode(source, spans, {320, 180}, "accurate.mp4");
        QVERIFY2(result.ok, result.error.c_str());
        QCOMPARE(result.segments.size(), size_t(3));

        int frames = 0;
        const auto flashes = flashTimes(outPath("accurate.mp4"), &frames);
        // 60 frames from 1.5-3.5, ceil(5.25*30)=158 up to ceil(7.1*30)=213, 30 from 9-10.
        QCOMPARE(frames, 60 + 55 + 30);
        const std::vector<double> expected{15 * kFrame, 45 * kFrame, (60 + 22) * kFrame,
                                           (60 + 52) * kFrame, (60 + 55) * kFrame};
        QCOMPARE(flashes.size(), expected.size());
        for (size_t i = 0; i < expected.size(); ++i)
            QVERIFY2(std::abs(flashes[i] - expected[i]) < kFrame / 2,
                     qPrintable(QString("flash %1 at %2, expected %3").arg(i).arg(flashes[i]).arg(expected[i])));
    }

    void audioStaysLockedToVideoAcrossJoins()
    {
        const QString source = fixtures::flashBeep();
        const std::vector<Span> spans{{1, 1.5, 3.5}, {2, 5.25, 7.1}, {3, 9.0, 10.0}};
        const EncodeResult result = encode(source, spans, {320, 180}, "sync.mp4");
        QVERIFY2(result.ok, result.error.c_str());
        const QString out = outPath("sync.mp4");
        const auto flashes = flashTimes(out);
        const auto beeps = beepTimes(out, OutputMap(result.segments).duration());
        QCOMPARE(beeps.size(), flashes.size());
        for (size_t i = 0; i < flashes.size(); ++i)
            QVERIFY2(std::abs(flashes[i] - beeps[i]) < 0.008,
                     qPrintable(QString("offset %1 ms at flash %2").arg((beeps[i] - flashes[i]) * 1000).arg(i)));
    }

    void containerStartOffsetIsTheClipOrigin()
    {
        const QString source = fixtures::flashBeep("offset.mkv", {"-output_ts_offset", "7.5"});
        const ClipInfo clip = probeClip(source.toStdString());
        QVERIFY(std::abs(clip.duration - 12) < 0.1);
        const EncodeResult result = encode(source, {{1, 1.5, 3.5}}, {320, 180}, "offset.mp4");
        QVERIFY2(result.ok, result.error.c_str());
        const auto flashes = flashTimes(outPath("offset.mp4"));
        QCOMPARE(flashes.size(), size_t(2));
        QVERIFY(std::abs(flashes[0] - 15 * kFrame) < kFrame / 2);
        const auto beeps = beepTimes(outPath("offset.mp4"), 2);
        QCOMPARE(beeps.size(), size_t(2));
        QVERIFY(std::abs(beeps[0] - flashes[0]) < 0.008);
    }

    void spanFromTheVeryStartKeepsItsFirstAudio()
    {
        const QString source = fixtures::make(
            "tone.mp4", {"-f", "lavfi", "-i", "testsrc2=s=320x180:r=30:d=3", "-f", "lavfi", "-i",
                         "sine=f=1000:r=48000:d=3", "-c:v", "libx264", "-preset", "veryfast", "-c:a", "aac"});
        const EncodeResult result = encode(source, {{1, 0, 1}}, {320, 180}, "from_start.mp4");
        QVERIFY2(result.ok, result.error.c_str());
        PcmSource pcm(outPath("from_start.mp4").toStdString(), {{0, 0, 0.1}});
        std::vector<int16_t> samples(4800 * 2);
        QCOMPARE(pcm.read(samples.data(), 4800), size_t(4800));
        int early = 0;
        for (size_t i = 2400; i < 2600; ++i) early = std::max(early, std::abs(int(samples[i * 2])));
        QVERIFY(early > 1000);
        // Past the fade-in that any AAC stream starts with, the tone must be there.
        int start = 0;
        for (size_t i = 1100; i < 1300; ++i) start = std::max(start, std::abs(int(samples[i * 2])));
        QVERIFY2(start > 1000, qPrintable(QString("peak %1 at 25 ms").arg(start)));
    }

    void pcmSourcePadsHolesAndKeepsTime()
    {
        // Audio that starts 1 s after the video: the first second must be silence.
        const QString source = fixtures::make(
            "late_audio.mkv", {"-f", "lavfi", "-i", "testsrc2=s=320x180:r=30:d=4", "-f", "lavfi", "-i",
                               "sine=f=1000:r=48000:d=2", "-filter_complex", "[1:a]adelay=1000:all=1[a]",
                               "-map", "0:v", "-map", "[a]", "-c:v", "libx264", "-preset", "veryfast", "-c:a", "aac"});
        PcmSource pcm(source.toStdString(), {{1, 0.5, 1.5}});
        std::vector<int16_t> samples(48000 * 2);
        QCOMPARE(pcm.read(samples.data(), 48000), size_t(48000));
        QVERIFY(std::abs(int(samples[12000 * 2])) < 100);
        int loudest = 0;
        for (size_t i = 36000; i < 37000; ++i) loudest = std::max(loudest, std::abs(int(samples[i * 2])));
        QVERIFY(loudest > 1000);
    }

    void overlappingSpansRepeatTheOverlap()
    {
        const QString source = fixtures::flashBeep();
        const EncodeResult result = encode(source, {{1, 1.5, 2.5}, {2, 1.75, 2.75}}, {320, 180}, "overlap.mp4");
        QVERIFY2(result.ok, result.error.c_str());
        int frames = 0;
        const auto flashes = flashTimes(outPath("overlap.mp4"), &frames);
        QCOMPARE(frames, 60);
        QCOMPARE(flashes.size(), size_t(2));  // the flash at 2 s, once from each span
    }

    void noAudioStream()
    {
        const QString source = fixtures::make("silent.mp4", {"-f", "lavfi", "-i", "testsrc2=s=640x360:r=25:d=4",
                                                             "-c:v", "libx264", "-preset", "veryfast"});
        QVERIFY(!probeClip(source.toStdString()).hasAudio);
        const EncodeResult result = encode(source, {{1, 1, 2}}, {640, 360}, "silent_out.mp4");
        QVERIFY2(result.ok, result.error.c_str());
        QVERIFY(!result.drift.audio.present);
        QVERIFY(result.drift.withinTolerance);
    }

    void audioShorterThanVideoIsPadded()
    {
        const QString source = fixtures::make(
            "short_audio.mp4", {"-f", "lavfi", "-i", "testsrc2=s=320x180:r=30:d=6", "-f", "lavfi", "-i",
                                "sine=f=440:r=48000:d=2", "-c:v", "libx264", "-preset", "veryfast", "-c:a", "aac"});
        const EncodeResult result = encode(source, {{1, 1, 4}}, {320, 180}, "short_audio_out.mp4");
        QVERIFY2(result.ok, result.error.c_str());
        QVERIFY(std::abs(result.drift.audio.end - result.drift.video.end) < 0.03);
    }

    void rotatedSourceIsUprightInOutput()
    {
        const QString plain = fixtures::make("landscape.mp4", {"-f", "lavfi", "-i", "testsrc2=s=640x360:r=30:d=3",
                                                               "-c:v", "libx264", "-preset", "veryfast"});
        const QString source = fixtures::make("rotated.mp4", {"-display_rotation", "90", "-i", plain, "-c", "copy"});
        const ClipInfo clip = probeClip(source.toStdString());
        QVERIFY(clip.rotation == 90 || clip.rotation == 270);
        QVERIFY((clip.display == Dims{360, 640}));
        const EncodeResult result = encode(source, {{1, 0, 1}}, outputDims(clip.display, 0), "rotated_out.mp4");
        QVERIFY2(result.ok, result.error.c_str());
        const ClipInfo out = probeClip(outPath("rotated_out.mp4").toStdString());
        QVERIFY((out.display == Dims{360, 640}));
        QCOMPARE(out.rotation, 0);
    }

    void sliderExtremesAreValidCrfs()
    {
        for (const int quality : {0, 100}) {
            EncodeRequest request;
            request.source = fixtures::flashBeep().toStdString();
            request.spans = {{1, 1, 1.5}};
            request.size = {320, 180};
            request.crf = crfForQuality(quality);
            request.audioKbps = audioKbpsForQuality(quality);
            request.output = outPath(QString("crf_%1.mp4").arg(quality)).toStdString();
            std::atomic<bool> cancel{false};
            const EncodeResult result = encodeClip(request, {}, cancel);
            QVERIFY2(result.ok, result.error.c_str());
        }
    }

    void oddSourceDimensionsEncode()
    {
        const QString source = fixtures::make("odd.mkv", {"-f", "lavfi", "-i", "testsrc2=s=641x361:r=30:d=2",
                                                          "-pix_fmt", "yuv444p", "-c:v", "libx264", "-preset", "veryfast"});
        const ClipInfo clip = probeClip(source.toStdString());
        const Dims size = outputDims(clip.display, 0);
        QVERIFY((size == Dims{640, 360}));
        const EncodeResult result = encode(source, {{1, 0, 1}}, size, "odd_out.mp4");
        QVERIFY2(result.ok, result.error.c_str());
        QVERIFY((probeClip(outPath("odd_out.mp4").toStdString()).display == Dims{640, 360}));
    }

    void spanPastTheEndIsSkipped()
    {
        const QString source = fixtures::flashBeep();
        const EncodeResult result = encode(source, {{1, 1, 2}, {2, 50, 60}}, {320, 180}, "past_end.mp4");
        QVERIFY2(result.ok, result.error.c_str());
        QCOMPARE(result.segments.size(), size_t(1));
    }

    void onlySpanPastTheEndFails()
    {
        const QString source = fixtures::flashBeep();
        const EncodeResult result = encode(source, {{1, 50, 60}}, {320, 180}, "none.mp4");
        QVERIFY(!result.ok);
        QVERIFY(!QFile::exists(outPath("none.mp4")));
        QVERIFY(!QFile::exists(outPath("none.mp4.part")));
    }

    void cancelLeavesNothingBehind()
    {
        EncodeRequest request;
        request.source = fixtures::flashBeep().toStdString();
        request.spans = {{1, 0, 12}};
        request.size = {320, 180};
        request.crf = 45;
        request.output = outPath("cancelled.mp4").toStdString();
        std::atomic<bool> cancel{true};
        const EncodeResult result = encodeClip(request, {}, cancel);
        QVERIFY(result.cancelled);
        QVERIFY(!QFile::exists(outPath("cancelled.mp4")));
        QVERIFY(!QFile::exists(outPath("cancelled.mp4.part")));
    }

    void waveformFindsTheBeeps()
    {
        std::atomic<bool> cancel{false};
        const auto peaks = computeWaveform(fixtures::flashBeep().toStdString(), 12, 240, cancel, {});
        QCOMPARE(peaks.size(), size_t(240));
        QVERIFY(peaks[20] > 0.5f);   // the beep at 1 s
        QVERIFY(peaks[30] < 0.05f);  // silence at 1.5 s
    }

    void pcmSourceHonoursSegments()
    {
        PcmSource pcm(fixtures::flashBeep().toStdString(), {{1, 0.5, 0.75}, {2, 3.0, 3.25}});
        std::vector<int16_t> samples(48000 * 2);
        const size_t got = pcm.read(samples.data(), 48000);
        QCOMPARE(got, size_t(24000));
        // The second segment starts on a beep, the first is silent.
        QVERIFY(std::abs(int(samples[6000 * 2])) < 500);
        int loudest = 0;
        for (size_t i = 12000; i < 13000; ++i) loudest = std::max(loudest, std::abs(int(samples[i * 2])));
        QVERIFY(loudest > 8000);
    }
};

QTEST_GUILESS_MAIN(TestEncode)
#include "test_encode.moc"
