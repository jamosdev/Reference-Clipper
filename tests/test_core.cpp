#include <QtTest>

#include "core/config.h"
#include "core/quality.h"
#include "core/timecode.h"
#include "core/timeline.h"

using namespace rc;

class TestCore : public QObject {
    Q_OBJECT
private slots:
    void crfMatchesHandoff()
    {
        QCOMPARE(crfForQuality(70), 25);  // the README's "70 · crf 25"
        QCOMPARE(crfForQuality(90), 14);  // 13.5, which binary floats round down
        QCOMPARE(crfForQuality(0), 63);
        QCOMPARE(crfForQuality(100), 8);
    }
    void audioBitrateMatchesHandoff()
    {
        QCOMPARE(audioKbpsForQuality(70), 243);
        QCOMPARE(audioKbpsForQuality(0), 64);
        QCOMPARE(audioKbpsForQuality(100), 320);
    }
    void qualityHintBoundaries()
    {
        QCOMPARE(qualityHint(34), "low bitrate");
        QCOMPARE(qualityHint(35), "good for review");
        QCOMPARE(qualityHint(59), "good for review");
        QCOMPARE(qualityHint(60), "visually transparent");
        QCOMPARE(qualityHint(84), "visually transparent");
        QCOMPARE(qualityHint(85), "near-lossless");
    }
    void outputDimensions()
    {
        QVERIFY((outputDims({3840, 2160}, 1080) == Dims{1920, 1080}));
        QVERIFY((outputDims({3840, 2160}, 0) == Dims{3840, 2160}));
        QVERIFY((outputDims({1280, 720}, 1080) == Dims{1280, 720}));  // taller preset: source size
        QVERIFY((outputDims({1279, 719}, 0) == Dims{1278, 718}));     // odd source
        QVERIFY((outputDims({854, 480}, 480) == Dims{854, 480}));
        QVERIFY((outputDims({1080, 1920}, 720) == Dims{406, 720}));   // portrait
    }
    void estimatedSizeFormula()
    {
        const double mb = estimatedMegabytes({1920, 1080}, 70, 70, 60);
        QVERIFY(qAbs(mb - (0.8 + 4.9 + 0.2432) * 60 / 8) < 1e-9);
        QCOMPARE(formatEstimatedSize(45.4), std::string("45 MB"));
        QCOMPARE(formatEstimatedSize(2048), std::string("2.00 GB"));
    }
    void timecodes()
    {
        QCOMPARE(timecode(187.4, 30), std::string("03:07.12"));
        QCOMPARE(timecode(0.7, 30), std::string("00:00.21"));  // 0.7*30 is 20.999… in binary
        QCOMPARE(timecode(-1, 30), std::string("00:00.00"));
        QCOMPARE(timecode(6000, 30), std::string("100:00.00"));
        QCOMPARE(shortTimecode(6000), std::string("100:00"));
    }
    void spanEditsClamp()
    {
        const double frame = 1.0 / 30;
        Span s{1, 10, 20};
        setIn(s, 25, frame);
        QVERIFY(qAbs(s.end - s.start - frame) < 1e-9);
        s = {1, 10, 20};
        setOut(s, 5, frame, 100);
        QVERIFY(qAbs(s.end - s.start - frame) < 1e-9);
        s = {1, 10, 20};
        setOut(s, 500, frame, 100);
        QCOMPARE(s.end, 100.0);
        s = {1, 10, 20};
        moveTo(s, 95, 100);
        QCOMPARE(s.start, 90.0);
        QCOMPARE(s.end, 100.0);
    }
    void newSpanLength()
    {
        const double frame = 1.0 / 30;
        QCOMPARE(spanAt(1, 0, 400, frame).end, 10.0);  // 10 s cap
        QCOMPARE(spanAt(1, 0, 100, frame).end, 5.0);   // 5 % of clip
        const Span atEnd = spanAt(1, 100, 100, frame);  // at the end, still one frame long
        QCOMPARE(atEnd.end, 100.0);
        QVERIFY(atEnd.length() >= frame - 1e-9);
    }
    void playbackSkipsGapsAndStops()
    {
        const std::vector<Span> spans{{1, 2, 3}, {2, 5, 6}};
        Playback p = beginPlayback(spans, 0);
        QCOMPARE(p.playhead, 2.0);
        p = advancePlayback(spans, p, 0.5);
        QCOMPARE(p.playhead, 2.5);
        p = advancePlayback(spans, p, 0.6);
        QCOMPARE(p.playhead, 5.0);  // jumped the gap
        QCOMPARE(p.span, 1);
        p = advancePlayback(spans, p, 2);
        QVERIFY(!p.playing);
        QCOMPARE(p.playhead, 6.0);
        QCOMPARE(beginPlayback(spans, 9).playhead, 2.0);  // past the last span: wrap
        QVERIFY(!beginPlayback({}, 1).playing);
    }
    void playbackRepeatsOverlapLikeTheEncoder()
    {
        const std::vector<Span> spans{{1, 2, 4}, {2, 3, 5}};
        Playback p = beginPlayback(spans, 2);
        p = advancePlayback(spans, p, 2.1);
        QCOMPARE(p.span, 1);
        QCOMPARE(p.playhead, 3.0);  // back to the start of the second span
    }
    void outputMapping()
    {
        const OutputMap map({{1, 2, 3}, {2, 5, 7}});
        QCOMPARE(map.duration(), 3.0);
        QCOMPARE(*map.toOutput(2.5), 0.5);
        QCOMPARE(*map.toOutput(6), 2.0);
        QVERIFY(!map.toOutput(4));
        QCOMPARE(*map.toOutput(4.99, 0.02), 1.0);  // tolerance clamps to the segment

        const OutputMap overlap({{1, 2, 4}, {2, 3, 5}});
        QCOMPARE(*overlap.toOutput(3.5), 1.5);
        QCOMPARE(*overlap.toOutput(3.5, 0, 1), 2.5);
    }
    void configRoundTrip()
    {
        Config in;
        in.outDir = "/home/me/Clips \"quoted\"\\x";
        in.layout = "stacked";
        in.theme = "dark";
        in.recent = {"/a b/c.mp4", "/d, e.mkv"};
        const Config out = parseConfig(serializeConfig(in));
        QCOMPARE(out.outDir, in.outDir);
        QCOMPARE(out.layout, in.layout);
        QCOMPARE(out.theme, in.theme);
        QVERIFY(out.recent == in.recent);
    }
    void configToleratesGarbage()
    {
        const Config c = parseConfig("# note\n\nnonsense\nlayout = 3\ntheme = \"light\"\nrecent = [\"x\",\n");
        QCOMPARE(c.layout, std::string("classic"));
        QCOMPARE(c.theme, std::string("light"));
    }
};

QTEST_APPLESS_MAIN(TestCore)
#include "test_core.moc"
