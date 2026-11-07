// All application state and every user action. Widgets read from here and call
// actions; they never talk to each other or to the media layer directly.
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <optional>
#include <vector>

#include "app/assets.h"
#include "app/audio_preview.h"
#include "app/history.h"
#include "app/job_queue.h"
#include "app/scene_search.h"
#include "core/timeline.h"
#include "media/probe.h"

struct ResolutionPreset {
    const char* label;
    int height;
};
inline constexpr ResolutionPreset kResolutions[] = {{"Source", 0}, {"1080p", 1080}, {"720p", 720}, {"480p", 480}};

enum class AudioSource { Input, Output };

struct Job {
    enum class Status { Queued, Encoding, Done, Failed };

    int id = 0;
    QString jobId;  // stable id for history and the thumbnail file
    QString name;
    QString output;
    QString source;
    std::vector<rc::Span> spans;
    JobSettings settings;
    QString estimatedSize;
    double duration = 0;
    Status status = Status::Queued;
    double progress = 0;  // 0..1
    double fps = 0;
    bool cancelling = false;
    QImage thumb;
};

// A finished encode of the current clip. It stands for the output preview only
// while the spans and crop are the ones it was made from: once they are edited
// the file no longer shows what the editor says.
struct FinishedOutput {
    QString path;
    QString name;
    std::vector<rc::Span> spans;
    std::optional<rc::Crop> crop;
    rc::OutputMap map;
    rc::Dims size;
};

class Session : public QObject {
    Q_OBJECT
public:
    explicit Session(QObject* parent = nullptr);
    ~Session() override;

    // Clip
    bool hasClip() const { return clip_.has_value(); }
    const rc::ClipInfo& clip() const { return *clip_; }
    bool openFile(const QString& path);
    void openHistoryEntry(int index);
    void closeClip();
    QStringList recentFiles() const { return recent_; }

    // Playhead and playback
    double playhead() const { return playhead_; }
    bool playing() const { return playing_; }
    int playingSpan() const { return playback_.span; }
    void seek(double seconds);
    void stepFrames(int count);
    void goToFirstSpan();
    void togglePlay();
    void pause();

    // Spans
    const std::vector<rc::Span>& spans() const { return spans_; }
    std::vector<rc::Span> sortedSpans() const { return rc::sortedByStart(spans_); }
    int64_t selectedSpan() const { return selected_; }
    void selectSpan(int64_t id);
    void addSpan();
    // A new span over exactly this range, in either order.
    void addSpanBetween(double a, double b);
    // A new span around t that grows out to the scene's cuts, stopping at
    // neighbouring spans and the clip's ends. Inside a span, selects that span.
    void addSceneSpan(double t);
    bool findingScene() const { return sceneSpan_ != 0; }
    void setInPoint();
    void setOutPoint();
    void removeSelectedSpan();
    void editSpan(int64_t id, double start, double end, bool movePlayheadToStart, bool movePlayheadToEnd);
    double outputLength() const { return rc::totalLength(spans_); }

    // Crop, in displayed source pixels. Everything after it sees the cropped size.
    const std::optional<rc::Crop>& crop() const { return crop_; }
    void setCrop(const rc::Crop& crop);
    void clearCrop();

    // Output settings
    AudioSource audioSource() const { return audioSource_; }
    void setAudioSource(AudioSource source);
    int resolutionIndex() const { return resolution_; }
    void setResolutionIndex(int index);
    rc::Dims outputSize() const;
    int videoQuality() const { return videoQuality_; }
    int audioQuality() const { return audioQuality_; }
    void setVideoQuality(int quality);
    void setAudioQuality(int quality);
    QString outputName() const { return outputName_; }
    QString outputFolder() const { return outputFolder_; }
    void setOutputName(const QString& name);
    void setOutputFolder(const QString& folder);
    QString outputPath() const;
    QString estimatedSize() const;
    const FinishedOutput* finishedOutput() const;

    // Jobs
    const QVector<Job>& queue() const { return queue_; }
    const QVector<HistoryEntry>& history() const { return history_; }
    bool canEncode() const;
    bool outputConflicts() const;
    int activeJobs() const;
    bool encoding() const { return jobs_.busy(); }
    void encode();
    void cancelJob(int id);
    void playJobOutput(int id);

    // Everything else that is remembered
    QString layoutName() const { return QString::fromStdString(config_.layout); }
    void setLayoutName(const QString& name);
    void setThemeName(const QString& name);
    QString themeName() const { return QString::fromStdString(config_.theme); }

    QString libraryStatus() const;
    QString stateText(bool editingSpan) const;
    QString timecode(double seconds) const;

    ClipAssets* assets() { return &assets_; }

signals:
    void clipChanged();
    void spansChanged();
    void selectionChanged();
    void cropChanged();
    void playheadChanged();
    void playingChanged();
    void outputSettingsChanged();
    void audioSourceChanged();
    void queueChanged();
    void historyChanged();
    void recentChanged();
    void toast(const QString& text, int milliseconds);

private:
    void tick();
    void restartAudio();
    void refreshFinishedOutput();
    void saveConfigFile() const;
    void onJobFinished(int id, const rc::EncodeResult& result);
    Job* findJob(int id);
    double frame() const { return clip_ ? clip_->frame() : 1.0 / 30; }
    double snapped(double seconds) const;
    bool loadClip(const QString& path);
    void pushNewSpan(const rc::Span& span);
    void startNextSceneSearch();
    void applySceneRange(int64_t id, double start, double end);
    void stopSceneSearches();
    void abandonSceneSearch(int64_t id);

    std::optional<rc::ClipInfo> clip_;
    std::vector<rc::Span> spans_;
    int64_t selected_ = 0;
    int64_t nextSpanId_ = 1;
    std::optional<rc::Crop> crop_;
    double playhead_ = 0;
    bool playing_ = false;
    rc::Playback playback_;
    QTimer ticker_;
    QElapsedTimer tickClock_;

    AudioSource audioSource_ = AudioSource::Input;
    int resolution_ = 1;
    int videoQuality_ = 70;
    int audioQuality_ = 70;
    QString outputName_;
    QString outputFolder_;
    std::optional<FinishedOutput> lastOutput_;

    QVector<Job> queue_;
    QVector<HistoryEntry> history_;
    int nextJobId_ = 1;
    struct PendingScene {
        int64_t span;
        double time;
    };
    QVector<PendingScene> scenesWaiting_;
    int64_t sceneSpan_ = 0;  // the span being searched for, or 0
    double sceneTime_ = 0;
    SceneSearch sceneSearch_;

    JobQueue jobs_;
    ClipAssets assets_;
    AudioPreview audio_;

    rc::Config config_;
    QStringList recent_;
};
