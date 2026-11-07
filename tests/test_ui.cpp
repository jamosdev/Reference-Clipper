// Drives the real MainWindow on the offscreen platform. HOME and the XDG paths
// point into a temp dir before Qt starts, so this never sees the user's config,
// history, ~/Videos or sound server. Set RC_SCREENSHOT_DIR to save a PNG of each key state.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QSlider>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "app/history.h"
#include "app/main_window.h"
#include "app/preview_panel.h"
#include "app/source_view.h"
#include "app/theme.h"
#include "app/timeline_widget.h"
#include "fixtures.h"
#include "media/probe.h"

class TestUi : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        Theme::instance().install();
        window_ = new MainWindow;
        window_->resize(1440, 900);
        window_->show();
        QVERIFY(QTest::qWaitForWindowExposed(window_));
        window_->activateWindow();
        source_ = fixtures::flashBeep();
        session()->setOutputFolder(QDir::homePath() + "/Clips");
    }
    void cleanupTestCase() { delete window_; }

    void emptyStateShowsNoClip()
    {
        QVERIFY(!session()->hasClip());
        QVERIFY(!window_->encodeButton()->isVisible());
        shot("empty");
    }

    void openingAClipResetsTheEditor()
    {
        QVERIFY(session()->openFile(source_));
        QCOMPARE(session()->outputName(), QString("flash_beep_clip.mp4"));
        QVERIFY(session()->spans().empty());
        QVERIFY(!window_->encodeButton()->isEnabled());  // nothing to encode yet
        QTRY_VERIFY(window_->previewPanel()->inputView()->hasImage());
        QCOMPARE(session()->recentFiles().first(), source_);
    }

    void unreadableFileIsRefusedWithoutLeavingTheClip()
    {
        const QString junk = fixtures::dir() + "/junk.mp4";
        QFile file(junk);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("this is not a video");
        file.close();
        QVERIFY(!session()->openFile(junk));
        QVERIFY(session()->hasClip());
        QCOMPARE(QString::fromStdString(session()->clip().path), source_);
    }

    void keysMarkSpans()
    {
        session()->seek(1);
        session()->addSpan();
        QCOMPARE(session()->spans().size(), size_t(1));
        QVERIFY(window_->encodeButton()->isEnabled());

        session()->seek(1.5);
        key(Qt::Key_I);
        session()->seek(3.5);
        key(Qt::Key_O);
        const rc::Span span = session()->spans().front();
        QVERIFY(qAbs(span.start - 1.5) < 1e-6);
        QVERIFY(qAbs(span.end - 3.5) < 1e-6);

        session()->seek(5);
        session()->addSpan();
        session()->seek(5.5);
        key(Qt::Key_O);
        QCOMPARE(session()->spans().size(), size_t(2));
    }

    void arrowsStepExactFrames()
    {
        session()->seek(2);
        for (int i = 0; i < 3; ++i) key(Qt::Key_Right);
        QVERIFY(qAbs(session()->playhead() - (2 + 3.0 / 30)) < 1e-6);
        for (int i = 0; i < 3; ++i) key(Qt::Key_Left);
        QVERIFY(qAbs(session()->playhead() - 2) < 1e-6);
    }

    void keysAreIgnoredWhileTyping()
    {
        auto* name = window_->findChildren<QLineEdit*>().first();
        name->setFocus();
        QTRY_VERIFY(name->hasFocus());
        const double before = session()->playhead();
        QTest::keyClick(name, Qt::Key_Right);
        QTest::keyClick(name, Qt::Key_Space);
        QCOMPARE(session()->playhead(), before);
        QVERIFY(!session()->playing());
        name->undo();
        session()->setOutputName("flash_beep_clip.mp4");
        window_->setFocus();
    }

    void spaceDoesNotClickAFocusedButton()
    {
        window_->encodeButton()->setFocus();
        key(Qt::Key_Space);
        QVERIFY(session()->playing());
        QCOMPARE(session()->queue().size(), 0);
        key(Qt::Key_Space);
        QVERIFY(!session()->playing());
        window_->setFocus();
    }

    void outputPaneFollowsTheSpans()
    {
        auto* out = window_->previewPanel()->outputView();
        session()->seek(4);
        QTRY_COMPARE(out->chip(), QString("black"));
        QVERIFY(!out->hasImage());
        session()->setAudioSource(AudioSource::Output);
        QTRY_COMPARE(out->chip(), QString("black · silence"));
        session()->setAudioSource(AudioSource::Input);
        session()->seek(2);
        QTRY_VERIFY(out->chip().startsWith("seek preview @"));
        QTRY_VERIFY(out->hasImage());
        shot("editor_light");
    }

    void playbackVisitsOnlyTheSpans()
    {
        session()->seek(0);
        session()->togglePlay();
        QVERIFY(qAbs(session()->playhead() - 1.5) < 1e-6);  // jumped to the first span
        bool visitedGap = false;
        QTRY_VERIFY_WITH_TIMEOUT((visitedGap |= session()->playhead() > 3.5 && session()->playhead() < 5, !session()->playing()), 6000);
        QVERIFY(!visitedGap);
        QVERIFY(qAbs(session()->playhead() - 5.5) < 1e-6);
    }

    void dragTrimsASpan()
    {
        auto* timeline = window_->timeline();
        const rc::Span span = session()->sortedSpans().front();
        const int y = bandY;
        const QPoint from(timeline->xForTime(span.end), y);
        const QPoint to(timeline->xForTime(span.end + 1), y);
        QTest::mousePress(timeline, Qt::LeftButton, Qt::NoModifier, from);
        QVERIFY(timeline->editingSpan());
        QTest::mouseMove(timeline, to);
        QTest::mouseRelease(timeline, Qt::LeftButton, Qt::NoModifier, to);
        QVERIFY(!timeline->editingSpan());
        const rc::Span edited = session()->sortedSpans().front();
        QCOMPARE(edited.start, span.start);
        QVERIFY(qAbs(edited.end - (span.end + 1)) < 0.05);
        QVERIFY(qAbs(session()->playhead() - edited.end) < 1e-9);
        session()->editSpan(edited.id, edited.start, span.end, false, true);  // put it back
    }

    void encodeRunsToADecodedPreview()
    {
        session()->setResolutionIndex(3);  // 480p; the fixture is 360p, so source size
        session()->setVideoQuality(20);    // fast enough for a test
        window_->encodeButton()->click();
        QCOMPARE(session()->queue().size(), 1);
        shot("encoding");
        QTRY_VERIFY_WITH_TIMEOUT(session()->queue().first().status == Job::Status::Done, 60000);

        const QString output = QDir::homePath() + "/Clips/flash_beep_clip.mp4";
        QVERIFY(QFile::exists(output));
        QVERIFY(!QFile::exists(output + ".part"));
        QVERIFY(session()->finishedOutput());
        QCOMPARE(session()->audioSource(), AudioSource::Output);
        QVERIFY(qAbs(session()->playhead() - 1.5) < 1e-6);

        auto* out = window_->previewPanel()->outputView();
        QTRY_VERIFY(out->chip().startsWith("decoded @"));
        QTRY_VERIFY(out->hasImage());

        session()->seek(2);  // a flash frame, through the decoded file
        QTest::qWait(300);
        shot("decoded");
    }

    void jobIsRecordedInHistory()
    {
        QCOMPARE(session()->history().size(), 1);
        const HistoryEntry entry = session()->history().first();
        QVERIFY(QFile::exists(paths::historyFile()));
        QVERIFY(QFile::exists(paths::thumbFile(entry.id)));
        QCOMPARE(entry.spans.size(), size_t(2));
        QCOMPARE(entry.segments.size(), size_t(2));
        QCOMPARE(entry.source, source_);

        QFile file(paths::historyFile());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
        QCOMPARE(array.size(), 1);
        QCOMPARE(array.first()["name"].toString(), QString("flash_beep_clip.mp4"));
    }

    void outputReallyHasTheRequestedSpans()
    {
        const rc::ClipInfo out = rc::probeClip((QDir::homePath() + "/Clips/flash_beep_clip.mp4").toStdString());
        QVERIFY((out.display == rc::Dims{640, 360}));
        QVERIFY(qAbs(out.duration - 2.5) < 0.05);
        QVERIFY(out.hasAudio);
    }

    void outputStopsBeingCurrentWhenSpansChange()
    {
        auto* out = window_->previewPanel()->outputView();
        session()->seek(2);
        QTRY_VERIFY(out->chip().startsWith("decoded @"));
        session()->seek(5.2);
        session()->setInPoint();
        QVERIFY(!session()->finishedOutput());
        QTRY_VERIFY(out->chip().startsWith("seek preview @") || out->chip().startsWith("black"));
    }

    void replacingAnExistingFileIsAConflict()
    {
        QVERIFY(session()->outputConflicts());
        session()->setOutputName("different.mp4");
        QVERIFY(!session()->outputConflicts());
    }

    void historyRowRestoresTheJob()
    {
        session()->closeClip();
        QVERIFY(!session()->hasClip());
        session()->openHistoryEntry(0);
        QVERIFY(session()->hasClip());
        QCOMPARE(session()->spans().size(), size_t(2));
        QCOMPARE(session()->videoQuality(), 20);
        QVERIFY(session()->finishedOutput());  // the output on disk still matches
        session()->setAudioSource(AudioSource::Output);
        session()->seek(2);
        QTRY_VERIFY(window_->previewPanel()->outputView()->chip().startsWith("decoded @"));
    }

    void removingASpanSelectsAnother()
    {
        const size_t before = session()->spans().size();
        key(Qt::Key_Delete);
        QCOMPARE(session()->spans().size(), before - 1);
        QVERIFY(session()->selectedSpan() != 0);
    }

    void cancellingAnEncodeRemovesTheJob()
    {
        session()->setVideoQuality(100);
        session()->setResolutionIndex(0);
        session()->setOutputName("cancel_me.mp4");
        const int before = session()->queue().size();
        session()->encode();
        const int id = session()->queue().last().id;
        session()->cancelJob(id);
        QTRY_COMPARE_WITH_TIMEOUT(session()->queue().size(), before, 30000);
        QVERIFY(!QFile::exists(QDir::homePath() + "/Clips/cancel_me.mp4"));
        QVERIFY(!QFile::exists(QDir::homePath() + "/Clips/cancel_me.mp4.part"));
    }

    void noAudioClipWorksEndToEnd()
    {
        const QString silent = fixtures::make("nosound.mp4", {"-f", "lavfi", "-i", "testsrc2=s=320x180:r=25:d=3",
                                                              "-c:v", "libx264", "-preset", "veryfast"});
        QVERIFY(session()->openFile(silent));
        session()->addSpan();
        session()->togglePlay();
        QTest::qWait(200);
        session()->pause();
        session()->setVideoQuality(20);
        session()->encode();
        QTRY_VERIFY_WITH_TIMEOUT(session()->queue().last().status == Job::Status::Done, 60000);
        QVERIFY(session()->finishedOutput());
    }

    void layoutsAndThemes()
    {
        QVERIFY(session()->openFile(source_));
        session()->seek(1.5);
        session()->addSpan();
        for (const char* name : {"threeColumn", "stacked", "classic"}) {
            window_->applyLayout(name);
            QTest::qWait(150);
            shot(QString("layout_") + name);
        }
        Theme::instance().setMode(Theme::Mode::Dark);
        QTest::qWait(150);
        shot("editor_dark");
        session()->closeClip();
        QTest::qWait(150);
        shot("empty_dark");
        Theme::instance().setMode(Theme::Mode::System);
    }

    void configIsRemembered()
    {
        const rc::Config config = loadConfig();
        QCOMPARE(QString::fromStdString(config.outDir), QDir::homePath() + "/Clips");
        QVERIFY(!config.recent.empty());
    }

    void rightDragAddsExactlyTheDraggedRange()
    {
        QVERIFY(session()->openFile(fixtures::threeScenes()));
        QTRY_VERIFY(window_->timeline()->width() > 200);
        auto* timeline = window_->timeline();
        const QPoint from(timeline->xForTime(1), bandY);
        const QPoint to(timeline->xForTime(2.5), bandY);
        QTest::mousePress(timeline, Qt::RightButton, Qt::NoModifier, from);
        QTest::mouseMove(timeline, to);
        QTest::qWait(200);  // slower than a click, and far enough to be a drag
        QTest::mouseRelease(timeline, Qt::RightButton, Qt::NoModifier, to);
        QCOMPARE(session()->spans().size(), size_t(1));
        const rc::Span span = session()->spans().front();
        QVERIFY(qAbs(span.start - 1) < 0.05);
        QVERIFY(qAbs(span.end - 2.5) < 0.05);
        QCOMPARE(session()->selectedSpan(), span.id);
        QVERIFY(!session()->findingScene());
        session()->removeSelectedSpan();
    }

    void rightDragWorksInEitherDirection()
    {
        auto* timeline = window_->timeline();
        const QPoint from(timeline->xForTime(2.5), bandY);
        const QPoint to(timeline->xForTime(1), bandY);
        QTest::mousePress(timeline, Qt::RightButton, Qt::NoModifier, from);
        QTest::mouseMove(timeline, to);
        QTest::qWait(200);
        QTest::mouseRelease(timeline, Qt::RightButton, Qt::NoModifier, to);
        const rc::Span span = session()->spans().front();
        QVERIFY(qAbs(span.start - 1) < 0.05);
        QVERIFY(qAbs(span.end - 2.5) < 0.05);
        session()->removeSelectedSpan();
    }

    void rightClickGrowsToTheScene()
    {
        auto* timeline = window_->timeline();
        const QPoint at(timeline->xForTime(4.5), bandY);
        QTest::mouseClick(timeline, Qt::RightButton, Qt::NoModifier, at);
        QCOMPARE(session()->spans().size(), size_t(1));  // there at once
        QTRY_VERIFY_WITH_TIMEOUT(!session()->findingScene(), 20000);
        const rc::Span span = session()->spans().front();
        QVERIFY2(qAbs(span.start - 3) < 0.02 && qAbs(span.end - 6) < 0.02,
                 qPrintable(QString("%1..%2").arg(span.start).arg(span.end)));
    }

    void aSloppyRightClickStillCountsAsAClick()
    {
        auto* timeline = window_->timeline();
        const QPoint from(timeline->xForTime(1), bandY);
        QTest::mousePress(timeline, Qt::RightButton, Qt::NoModifier, from);
        QTest::mouseMove(timeline, from + QPoint(3, 0));
        QTest::mouseRelease(timeline, Qt::RightButton, Qt::NoModifier, from + QPoint(3, 0));
        QTRY_VERIFY_WITH_TIMEOUT(!session()->findingScene(), 20000);
        QCOMPARE(session()->spans().size(), size_t(2));
        const rc::Span first = session()->sortedSpans().front();
        QCOMPARE(first.start, 0.0);                // saturates at the clip start
        QVERIFY(qAbs(first.end - 3) < 0.02);       // and meets the cut, not the drag
    }

    void rightClickStopsAtAdjacentSpans()
    {
        // Open up a gap inside the third scene's neighbour and fill it.
        session()->selectSpan(session()->sortedSpans().back().id);
        session()->removeSelectedSpan();
        session()->addSpanBetween(7, 7.5);
        session()->addSpanBetween(3.5, 4);
        session()->addSceneSpan(5);
        QTRY_VERIFY_WITH_TIMEOUT(!session()->findingScene(), 20000);
        const auto spans = session()->sortedSpans();
        QCOMPARE(spans.size(), size_t(4));
        const rc::Span middle = spans[2];
        QVERIFY(qAbs(middle.start - 4) < 1e-3);    // the span before it
        QVERIFY(qAbs(middle.end - 6) < 0.02);      // the cut comes before the span after
    }

    void rightClickInsideASpanSelectsIt()
    {
        const auto before = session()->sortedSpans();
        session()->selectSpan(before.back().id);
        auto* timeline = window_->timeline();
        QTest::mouseClick(timeline, Qt::RightButton, Qt::NoModifier, QPoint(timeline->xForTime(3.75), bandY));
        QCOMPARE(session()->spans().size(), before.size());
        QCOMPARE(session()->selectedSpan(), before[1].id);
        QVERIFY(!session()->findingScene());
    }

    void removingASpanStopsItsSearch()
    {
        while (!session()->spans().empty()) session()->removeSelectedSpan();
        session()->addSceneSpan(4.5);
        session()->removeSelectedSpan();
        QTRY_VERIFY_WITH_TIMEOUT(!session()->findingScene(), 20000);
        QVERIFY(session()->spans().empty());
    }

    void closingTheClipDuringASearchIsSafe()
    {
        session()->addSceneSpan(4.5);
        session()->closeClip();
        QVERIFY(!session()->findingScene());
        QVERIFY(session()->openFile(fixtures::threeScenes()));
        QVERIFY(session()->spans().empty());
    }

    void draggingOnTheInputDrawsACrop()
    {
        QVERIFY(session()->openFile(fixtures::shortWithInset()));
        QTest::qWait(100);
        auto* view = window_->previewPanel()->inputView();
        QVERIFY(!session()->crop());
        const QPoint from = view->widgetPoint(0, 186);
        const QPoint to = view->widgetPoint(360, 454);
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(view, from + QPoint(10, 10));
        QTest::mouseMove(view, to);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to);
        QVERIFY(session()->crop());
        const rc::Crop crop = *session()->crop();
        QVERIFY2(qAbs(crop.y - 186) <= 8 && qAbs(crop.bottom() - 454) <= 8 && crop.w >= 350,
                 qPrintable(QString("%1,%2 %3x%4").arg(crop.x).arg(crop.y).arg(crop.w).arg(crop.h)));
        QCOMPARE(crop.x % 2, 0);
        QCOMPARE(crop.h % 2, 0);
        shot("crop");
    }

    void outputFollowsTheCrop()
    {
        const rc::Crop crop = *session()->crop();
        session()->setResolutionIndex(0);
        QVERIFY((session()->outputSize() == crop.size()));
        session()->addSpanBetween(1, 2);
        session()->seek(1.5);
        auto* out = window_->previewPanel()->outputView();
        QTRY_VERIFY(out->chip().startsWith("seek preview"));
        QTRY_VERIFY(out->hasImage());
        QTest::qWait(250);
        shot("crop_preview");
    }

    void cropEdgesDragIndependently()
    {
        auto* view = window_->previewPanel()->inputView();
        const rc::Crop before = *session()->crop();
        const QPoint from = view->widgetPoint(before.x + before.w / 2, before.bottom());
        const QPoint to = view->widgetPoint(before.x + before.w / 2, 560);
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(view, to);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to);
        const rc::Crop after = *session()->crop();
        QCOMPARE(after.y, before.y);
        QCOMPARE(after.x, before.x);
        QVERIFY(qAbs(after.bottom() - 560) <= 8);
    }

    void cropCanBeDraggedAround()
    {
        session()->setCrop({100, 100, 160, 160});
        auto* view = window_->previewPanel()->inputView();
        const QPoint from = view->widgetPoint(180, 180);
        const QPoint to = view->widgetPoint(180, 280);
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(view, to);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, to);
        const rc::Crop moved = *session()->crop();
        QCOMPARE(moved.w, 160);
        QCOMPARE(moved.h, 160);
        QCOMPARE(moved.x, 100);
        QVERIFY(qAbs(moved.y - 200) <= 8);
    }

    void clickingOutsideDismissesTheCrop()
    {
        auto* view = window_->previewPanel()->inputView();
        const rc::Crop crop = *session()->crop();
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, view->widgetPoint(180, crop.y + crop.h / 2));
        QVERIFY(session()->crop());  // inside: still there
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, view->widgetPoint(20, 600));
        QVERIFY(!session()->crop());
    }

    void clickingTheLetterboxBarsDismissesToo()
    {
        session()->setCrop({100, 100, 160, 160});
        auto* view = window_->previewPanel()->inputView();
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, QPoint(5, view->height() / 2));  // portrait clip: a bar
        QVERIFY(!session()->crop());
    }

    void croppedEncodeRoundTrips()
    {
        session()->setCrop({0, 186, 360, 268});
        session()->setVideoQuality(20);
        session()->setResolutionIndex(0);
        session()->setOutputName("inset.mp4");
        session()->seek(1.5);
        session()->encode();
        const int id = session()->queue().last().id;
        QTRY_VERIFY_WITH_TIMEOUT(session()->queue().last().status != Job::Status::Encoding
                                     && session()->queue().last().status != Job::Status::Queued, 60000);
        QCOMPARE(session()->queue().last().id, id);
        QVERIFY(session()->queue().last().status == Job::Status::Done);

        const rc::ClipInfo out = rc::probeClip((QDir::homePath() + "/Clips/inset.mp4").toStdString());
        QVERIFY((out.display == rc::Dims{360, 268}));
        QVERIFY(session()->finishedOutput());
        QTest::qWait(250);
        shot("crop_decoded");

        // Moving the crop means the file no longer shows what the editor says.
        session()->setCrop({0, 180, 360, 268});
        QVERIFY(!session()->finishedOutput());
        session()->setCrop({0, 186, 360, 268});
        QVERIFY(session()->finishedOutput());
    }

    void historyRestoresTheCrop()
    {
        session()->closeClip();
        session()->openHistoryEntry(0);
        QVERIFY(session()->crop());
        QVERIFY((*session()->crop() == rc::Crop{0, 186, 360, 268}));
        QVERIFY(session()->finishedOutput());
        const QVector<HistoryEntry> reloaded = loadHistory();
        QVERIFY(reloaded.first().settings.crop.has_value());
    }

    void aCropLimitsWhatCountsAsACut()
    {
        session()->openFile(fixtures::shortWithInset());
        session()->setCrop({0, 186, 360, 268});
        session()->addSceneSpan(4.5);
        QTRY_VERIFY_WITH_TIMEOUT(!session()->findingScene(), 20000);
        const rc::Span span = session()->spans().front();
        QVERIFY2(qAbs(span.start - 3) < 0.02 && qAbs(span.end - 6) < 0.02,
                 qPrintable(QString("%1..%2").arg(span.start).arg(span.end)));
    }

    void openingAnotherClipClearsTheCrop()
    {
        QVERIFY(session()->crop());
        QVERIFY(session()->openFile(fixtures::threeScenes()));
        QVERIFY(!session()->crop());
    }

private:
    static constexpr int bandY = 10 + 30 + 38;  // inside the timeline's span band

    Session* session() { return window_->session(); }

    void key(Qt::Key code)
    {
        // Through the application, as a real key press arrives.
        QWidget* target = QApplication::focusWidget() ? QApplication::focusWidget() : window_;
        QTest::keyClick(target, code);
    }

    void shot(const QString& name)
    {
        const QString dir = qEnvironmentVariable("RC_SCREENSHOT_DIR");
        if (dir.isEmpty()) return;
        QTest::qWait(250);
        window_->grab().save(dir + "/" + name + ".png");
    }

    MainWindow* window_ = nullptr;
    QString source_;
};

int main(int argc, char** argv)
{
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());  // Windows' idea of home
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/.local/share").toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + "/.cache").toUtf8());
    qputenv("XDG_VIDEOS_DIR", (home.path() + "/Videos").toUtf8());
    qputenv("PULSE_SERVER", "unix:/nonexistent");  // no beeps out of the speakers
    QApplication app(argc, argv);
    // Windows ignores the XDG variables, so keep config and history apart from
    // the real ones and start from nothing on every run.
    QStandardPaths::setTestModeEnabled(true);
    QDir(paths::dataDir()).removeRecursively();
    QFile::remove(paths::configFile());
    TestUi test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_ui.moc"
