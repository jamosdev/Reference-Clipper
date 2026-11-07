#include "app/session.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <algorithm>
#include <cmath>

#include "core/quality.h"
#include "core/timecode.h"

namespace {

constexpr int kRecentFiles = 3;
constexpr int kTickMs = 33;

bool sameExtents(std::vector<rc::Span> a, std::vector<rc::Span> b)
{
    if (a.size() != b.size()) return false;
    a = rc::sortedByStart(std::move(a));
    b = rc::sortedByStart(std::move(b));
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].start != b[i].start || a[i].end != b[i].end) return false;
    return true;
}

}  // namespace

Session::Session(QObject* parent) : QObject(parent)
{
    config_ = loadConfig();
    outputFolder_ = config_.outDir.empty() ? paths::defaultOutputDir() : QString::fromStdString(config_.outDir);
    for (const std::string& path : config_.recent) recent_.push_back(QString::fromStdString(path));
    history_ = loadHistory();

    ticker_.setInterval(kTickMs);
    connect(&ticker_, &QTimer::timeout, this, &Session::tick);

    connect(&jobs_, &JobQueue::started, this, [this](int id) {
        if (Job* job = findJob(id)) job->status = Job::Status::Encoding;
        emit queueChanged();
    });
    connect(&jobs_, &JobQueue::progress, this, [this](int id, const rc::EncodeProgress& p) {
        if (Job* job = findJob(id)) {
            job->progress = std::min(1.0, double(p.framesDone) / p.framesTotal);
            job->fps = p.fps;
        }
        emit queueChanged();
    });
    connect(&jobs_, &JobQueue::thumbnail, this, [this](int id, const QImage& image) {
        if (Job* job = findJob(id)) job->thumb = image;
        emit queueChanged();
    });
    connect(&jobs_, &JobQueue::finished, this, &Session::onJobFinished);
}

Session::~Session() = default;

QString Session::timecode(double seconds) const
{
    return QString::fromStdString(rc::timecode(seconds, clip_ ? clip_->fps : 30));
}

double Session::snapped(double seconds) const
{
    return clip_ ? rc::snapToFrame(seconds, clip_->fps) : seconds;
}

bool Session::loadClip(const QString& path)
{
    rc::ClipInfo info;
    try {
        info = rc::probeClip(path.toStdString());
    } catch (const std::exception& e) {
        emit toast(QString("Cannot open %1: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what())), 3500);
        return false;
    }
    pause();
    clip_ = info;
    spans_.clear();
    selected_ = 0;
    playhead_ = 0;
    audioSource_ = AudioSource::Input;
    lastOutput_.reset();
    outputName_ = QFileInfo(path).completeBaseName() + "_clip.mp4";

    recent_.removeAll(path);
    recent_.prepend(path);
    while (recent_.size() > kRecentFiles) recent_.removeLast();
    saveConfigFile();
    emit recentChanged();
    assets_.load(info);
    return true;
}

bool Session::openFile(const QString& path)
{
    if (!loadClip(path)) return false;
    emit clipChanged();
    emit spansChanged();
    emit selectionChanged();
    emit audioSourceChanged();
    emit outputSettingsChanged();
    emit playheadChanged();
    return true;
}

void Session::openHistoryEntry(int index)
{
    if (index < 0 || index >= history_.size()) return;
    const HistoryEntry entry = history_[index];
    if (!QFile::exists(entry.source)) {
        emit toast(QString("Source no longer exists: %1").arg(entry.source), 3500);
        return;
    }
    if (!loadClip(entry.source)) return;

    spans_ = entry.spans;
    for (rc::Span& span : spans_) span.id = nextSpanId_++;
    selected_ = spans_.empty() ? 0 : spans_.front().id;
    playhead_ = spans_.empty() ? 0 : sortedSpans().front().start;
    videoQuality_ = entry.settings.videoQuality;
    audioQuality_ = entry.settings.audioQuality;
    for (size_t i = 0; i < std::size(kResolutions); ++i)
        if (entry.settings.resolution == kResolutions[i].label) resolution_ = int(i);
    outputName_ = entry.name;

    if (QFile::exists(entry.output))
        lastOutput_ = FinishedOutput{entry.output, entry.name, spans_, rc::OutputMap(entry.segments), entry.settings.size};

    emit clipChanged();
    emit spansChanged();
    emit selectionChanged();
    emit audioSourceChanged();
    emit outputSettingsChanged();
    emit playheadChanged();
}

void Session::closeClip()
{
    pause();
    assets_.clear();
    clip_.reset();
    spans_.clear();
    selected_ = 0;
    playhead_ = 0;
    lastOutput_.reset();
    emit clipChanged();
    emit spansChanged();
    emit selectionChanged();
    emit playheadChanged();
    emit outputSettingsChanged();
}

void Session::seek(double seconds)
{
    if (!clip_) return;
    playhead_ = snapped(std::clamp(seconds, 0.0, clip_->duration));
    if (playing_) {
        const auto sorted = sortedSpans();
        playback_ = rc::beginPlayback(sorted, playhead_);
        playhead_ = playback_.playhead;
        restartAudio();
    }
    emit playheadChanged();
}

void Session::stepFrames(int count)
{
    if (!clip_) return;
    // Frame arithmetic rather than time: repeated += 1/fps drifts off the grid.
    seek((std::floor(playhead_ * clip_->fps + 1e-6) + count) / clip_->fps);
}

void Session::goToFirstSpan()
{
    const auto sorted = sortedSpans();
    seek(sorted.empty() ? 0 : sorted.front().start);
}

void Session::togglePlay()
{
    if (playing_) {
        pause();
        return;
    }
    const auto sorted = sortedSpans();
    if (!clip_ || sorted.empty()) return;
    playback_ = rc::beginPlayback(sorted, playhead_);
    playhead_ = playback_.playhead;
    playing_ = true;
    tickClock_.start();
    ticker_.start();
    restartAudio();
    emit playingChanged();
    emit playheadChanged();
}

void Session::pause()
{
    if (!playing_) return;
    playing_ = false;
    ticker_.stop();
    audio_.stop();
    emit playingChanged();
    emit playheadChanged();
}

void Session::tick()
{
    const double dt = tickClock_.restart() / 1000.0;
    const auto sorted = sortedSpans();
    playback_ = rc::advancePlayback(sorted, playback_, std::min(dt, 0.5));
    playhead_ = playback_.playhead;
    if (!playback_.playing) {
        playing_ = false;
        ticker_.stop();
        audio_.stop();
        emit playingChanged();
    }
    emit playheadChanged();
}

void Session::restartAudio()
{
    audio_.stop();
    if (!playing_ || !clip_) return;
    const auto sorted = sortedSpans();
    if (playback_.span < 0) return;

    if (audioSource_ == AudioSource::Output) {
        if (const FinishedOutput* output = finishedOutput()) {
            const auto at = output->map.toOutput(playhead_, frame(), playback_.span);
            audio_.play(output->path, {{0, at.value_or(0), output->map.duration()}});
            return;
        }
    }
    if (!clip_->hasAudio) return;
    std::vector<rc::Span> segments;
    for (size_t i = size_t(playback_.span); i < sorted.size(); ++i) {
        rc::Span segment = sorted[i];
        if (int(i) == playback_.span) segment.start = playhead_;
        segments.push_back(segment);
    }
    audio_.play(QString::fromStdString(clip_->path), segments);
}

void Session::selectSpan(int64_t id)
{
    if (id == selected_) return;
    selected_ = id;
    emit selectionChanged();
}

void Session::addSpan()
{
    if (!clip_) return;
    const rc::Span span = rc::spanAt(nextSpanId_++, playhead_, clip_->duration, frame());
    spans_.push_back(span);
    selected_ = span.id;
    emit spansChanged();
    emit selectionChanged();
    emit outputSettingsChanged();
    refreshFinishedOutput();
}

void Session::setInPoint()
{
    auto it = std::find_if(spans_.begin(), spans_.end(), [this](const rc::Span& s) { return s.id == selected_; });
    if (it == spans_.end()) return;
    rc::setIn(*it, playhead_, frame());
    emit spansChanged();
    emit outputSettingsChanged();
    refreshFinishedOutput();
}

void Session::setOutPoint()
{
    auto it = std::find_if(spans_.begin(), spans_.end(), [this](const rc::Span& s) { return s.id == selected_; });
    if (it == spans_.end()) return;
    rc::setOut(*it, playhead_, frame(), clip_->duration);
    emit spansChanged();
    emit outputSettingsChanged();
    refreshFinishedOutput();
}

void Session::removeSelectedSpan()
{
    const auto removed = std::remove_if(spans_.begin(), spans_.end(),
                                        [this](const rc::Span& s) { return s.id == selected_; });
    if (removed == spans_.end()) return;
    spans_.erase(removed, spans_.end());
    selected_ = spans_.empty() ? 0 : sortedSpans().front().id;
    if (playing_ && spans_.empty()) pause();
    emit spansChanged();
    emit selectionChanged();
    emit outputSettingsChanged();
    refreshFinishedOutput();
}

void Session::editSpan(int64_t id, double start, double end, bool movePlayheadToStart, bool movePlayheadToEnd)
{
    auto it = std::find_if(spans_.begin(), spans_.end(), [id](const rc::Span& s) { return s.id == id; });
    if (it == spans_.end() || !clip_) return;
    it->start = snapped(start);
    if (movePlayheadToStart || movePlayheadToEnd) {  // a trim: both edges on the frame grid
        it->end = std::min(clip_->duration, std::max(snapped(end), it->start + frame()));
        playhead_ = movePlayheadToStart ? it->start : it->end;
    } else {  // a move keeps its exact length
        it->end = std::min(clip_->duration, it->start + std::max(end - start, frame()));
    }
    emit spansChanged();
    emit outputSettingsChanged();
    refreshFinishedOutput();
}

void Session::refreshFinishedOutput()
{
    if (playing_) restartAudio();
    emit playheadChanged();
}

const FinishedOutput* Session::finishedOutput() const
{
    if (!lastOutput_ || !sameExtents(lastOutput_->spans, spans_)) return nullptr;
    return &*lastOutput_;
}

void Session::setAudioSource(AudioSource source)
{
    if (source == audioSource_) return;
    audioSource_ = source;
    restartAudio();
    emit audioSourceChanged();
    emit playheadChanged();
}

void Session::setResolutionIndex(int index)
{
    resolution_ = std::clamp(index, 0, int(std::size(kResolutions)) - 1);
    emit outputSettingsChanged();
}

rc::Dims Session::outputSize() const
{
    return clip_ ? rc::outputDims(clip_->display, kResolutions[resolution_].height) : rc::Dims{};
}

void Session::setVideoQuality(int quality)
{
    videoQuality_ = quality;
    emit outputSettingsChanged();
}

void Session::setAudioQuality(int quality)
{
    audioQuality_ = quality;
    emit outputSettingsChanged();
}

void Session::setOutputName(const QString& name)
{
    outputName_ = name;
    emit outputSettingsChanged();
}

void Session::setOutputFolder(const QString& folder)
{
    outputFolder_ = folder;
    saveConfigFile();
    emit outputSettingsChanged();
    emit toast("Output folder saved to config", 2000);
}

QString Session::outputPath() const
{
    QString name = outputName_.trimmed();
    if (!name.endsWith(".mp4", Qt::CaseInsensitive)) name += ".mp4";
    return QDir(outputFolder_).filePath(name);
}

QString Session::estimatedSize() const
{
    const double mb = rc::estimatedMegabytes(outputSize(), videoQuality_, audioQuality_, outputLength());
    return QString::fromStdString(rc::formatEstimatedSize(mb));
}

bool Session::canEncode() const
{
    return clip_ && !spans_.empty() && !outputName_.trimmed().isEmpty() && rc::libraryVersions().canEncode;
}

bool Session::outputConflicts() const
{
    const QString path = outputPath();
    if (QFile::exists(path)) return true;
    return std::any_of(queue_.begin(), queue_.end(), [&](const Job& job) {
        return job.output == path && (job.status == Job::Status::Queued || job.status == Job::Status::Encoding);
    });
}

int Session::activeJobs() const
{
    return int(std::count_if(queue_.begin(), queue_.end(), [](const Job& job) {
        return job.status == Job::Status::Queued || job.status == Job::Status::Encoding;
    }));
}

void Session::encode()
{
    if (!canEncode()) return;
    Job job;
    job.id = nextJobId_++;
    job.jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.output = outputPath();
    job.name = QFileInfo(job.output).fileName();
    job.source = QString::fromStdString(clip_->path);
    job.spans = sortedSpans();
    job.settings.resolution = kResolutions[resolution_].label;
    job.settings.presetHeight = kResolutions[resolution_].height;
    job.settings.size = outputSize();
    job.settings.videoQuality = videoQuality_;
    job.settings.audioQuality = audioQuality_;
    job.estimatedSize = estimatedSize();
    job.duration = outputLength();

    rc::EncodeRequest request;
    request.source = clip_->path;
    request.spans = job.spans;
    request.size = job.settings.size;
    request.crf = rc::crfForQuality(videoQuality_);
    request.audioKbps = rc::audioKbpsForQuality(audioQuality_);
    request.output = job.output.toStdString();

    queue_.push_back(job);
    emit queueChanged();
    jobs_.enqueue(job.id, request, paths::thumbFile(job.jobId));
    emit toast("Job added to queue", 2000);
}

void Session::cancelJob(int id)
{
    Job* job = findJob(id);
    if (!job) return;
    if (job->status == Job::Status::Encoding) {
        job->cancelling = true;
        jobs_.cancel(id);
        emit queueChanged();
        return;
    }
    jobs_.cancel(id);
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [id](const Job& j) { return j.id == id; }), queue_.end());
    emit queueChanged();
}

void Session::playJobOutput(int id)
{
    Job* job = findJob(id);
    if (!job) return;
    const QString output = job->output;
    for (int i = 0; i < history_.size(); ++i) {
        if (history_[i].output != output) continue;
        const bool loaded = clip_ && QString::fromStdString(clip_->path) == history_[i].source && finishedOutput()
                            && finishedOutput()->path == output;
        if (!loaded) openHistoryEntry(i);
        setAudioSource(AudioSource::Output);
        goToFirstSpan();
        if (!playing_) togglePlay();
        return;
    }
}

Job* Session::findJob(int id)
{
    auto it = std::find_if(queue_.begin(), queue_.end(), [id](const Job& job) { return job.id == id; });
    return it == queue_.end() ? nullptr : &*it;
}

void Session::onJobFinished(int id, const rc::EncodeResult& result)
{
    Job* job = findJob(id);
    if (!job) return;

    if (result.cancelled) {
        QFile::remove(paths::thumbFile(job->jobId));
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [id](const Job& j) { return j.id == id; }), queue_.end());
        emit queueChanged();
        return;
    }
    if (!result.ok) {
        job->status = Job::Status::Failed;
        QFile::remove(paths::thumbFile(job->jobId));
        emit queueChanged();
        emit toast(QString("%1 failed: %2").arg(job->name, QString::fromStdString(result.error)), 3500);
        return;
    }

    job->status = Job::Status::Done;
    job->progress = 1;

    HistoryEntry entry;
    entry.id = job->jobId;
    entry.source = job->source;
    entry.output = job->output;
    entry.name = job->name;
    entry.date = QDateTime::currentDateTime();
    entry.spans = job->spans;
    entry.segments = result.segments;
    entry.settings = job->settings;
    entry.sizeBytes = result.sizeBytes;
    entry.duration = rc::OutputMap(result.segments).duration();
    entry.driftMs = result.drift.driftSeconds * 1000;
    history_.prepend(entry);
    saveHistory(history_);

    const QString name = job->name;
    const bool currentClip = clip_ && QString::fromStdString(clip_->path) == job->source;
    if (currentClip) {
        lastOutput_ = FinishedOutput{job->output, job->name, job->spans, rc::OutputMap(result.segments), job->settings.size};
        // A job only takes the preview if the timeline still shows what it encoded.
        if (finishedOutput()) {
            pause();
            audioSource_ = AudioSource::Output;
            emit audioSourceChanged();
            goToFirstSpan();
        }
    }
    emit outputSettingsChanged();
    emit playheadChanged();
    emit queueChanged();
    emit historyChanged();
    emit toast(QString("%1 written · A/V drift %2 ms").arg(name).arg(qRound(entry.driftMs)), 3500);
}

void Session::setLayoutName(const QString& name)
{
    config_.layout = name.toStdString();
    saveConfigFile();
}

void Session::setThemeName(const QString& name)
{
    config_.theme = name.toStdString();
    saveConfigFile();
}

void Session::saveConfigFile() const
{
    rc::Config config = config_;
    config.outDir = outputFolder_.toStdString();
    config.recent.clear();
    for (const QString& path : recent_) config.recent.push_back(path.toStdString());
    saveConfig(config);
}

QString Session::libraryStatus() const
{
    const rc::LibraryVersions v = rc::libraryVersions();
    QString text = "ffmpeg " + QString::fromStdString(v.ffmpeg);
    if (!v.canEncode) return text + " · encoders missing";
    text += " · libsvtav1";
    if (!v.svtav1.empty()) text += " " + QString::fromStdString(v.svtav1);
    return text + " · aac";
}

QString Session::stateText(bool editingSpan) const
{
    if (!clip_) return QString("%1 jobs in history").arg(history_.size());
    const rc::Dims size = outputSize();
    return QString("%1 · %2 span%3 · %4×%5")
        .arg(playing_ ? "playing" : editingSpan ? "editing span" : "ready")
        .arg(spans_.size())
        .arg(spans_.size() == 1 ? "" : "s")
        .arg(size.w)
        .arg(size.h);
}
