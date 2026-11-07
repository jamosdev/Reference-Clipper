#include "app/jobs_panel.h"

#include <QDateTime>
#include <QFile>
#include <QGridLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QVBoxLayout>

#include "app/session.h"
#include "app/theme.h"
#include "core/quality.h"
#include "core/timecode.h"

namespace {

QString dateText(const QDateTime& date)
{
    return date.date() == QDate::currentDate() ? "Today" : date.toString("MMM d");
}

class ThumbView : public QWidget {
public:
    ThumbView(QSize size, QWidget* parent = nullptr) : QWidget(parent) { setFixedSize(size); }
    void setImage(const QImage& image)
    {
        if (image.isNull()) return;
        image_ = image;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), Theme::tokens().surface);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (!image_.isNull()) p.drawImage(rect(), image_);
        p.setPen(Theme::tokens().divider);
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }

private:
    QImage image_;
};

class ProgressBar : public QWidget {
public:
    ProgressBar(QWidget* parent = nullptr) : QWidget(parent) { setFixedHeight(3); }
    void setValue(double value)
    {
        value_ = value;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), Theme::tokens().divider);
        p.fillRect(QRect(0, 0, int(width() * value_), height()), Theme::tokens().accent);
    }

private:
    double value_ = 0;
};

}  // namespace

class JobRow : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double pulse READ pulse WRITE setPulse)
public:
    JobRow(Session* session, int id, QWidget* parent = nullptr) : QWidget(parent), session_(session), id_(id)
    {
        setAttribute(Qt::WA_StyledBackground);
        setProperty("divider", "bottom");
        auto* grid = new QGridLayout(this);
        grid->setContentsMargins(12, 8, 12, 8);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(2);
        thumb_ = new ThumbView({48, 27}, this);
        name_ = new ElidedLabel(ui::monoFont(12));
        meta_ = new ElidedLabel(ui::bodyFont(11), "caption");
        bar_ = new ProgressBar(this);
        status_ = ui::label("", nullptr, ui::monoFont(11), this);
        status_->setAlignment(Qt::AlignRight);
        action_ = ui::button("Cancel", "ghost", this);
        action_->setFont(ui::bodyFont(12));

        grid->addWidget(thumb_, 0, 0, 3, 1, Qt::AlignVCenter);
        grid->addWidget(name_, 0, 1);
        grid->addWidget(meta_, 1, 1);
        grid->addWidget(bar_, 2, 1);
        grid->addWidget(status_, 0, 2, Qt::AlignRight);
        grid->addWidget(action_, 1, 2, 2, 1, Qt::AlignRight | Qt::AlignTop);
        grid->setColumnStretch(1, 1);

        auto* pulse = new QPropertyAnimation(this, "pulse", this);
        pulse->setDuration(1400);
        pulse->setStartValue(1.0);
        pulse->setKeyValueAt(0.5, 0.35);
        pulse->setEndValue(1.0);
        pulse->setLoopCount(-1);
        pulse_ = pulse;

        connect(action_, &QPushButton::clicked, this, [this] {
            const Job* job = find();
            if (!job) return;
            if (job->status == Job::Status::Done) session_->playJobOutput(id_);
            else session_->cancelJob(id_);
        });
    }

    void sync(const Job& job)
    {
        name_->setText(job.name);
        meta_->setText(QString("%1 span%2 · %3 · %4 · crf %5 · %6")
                           .arg(job.spans.size())
                           .arg(job.spans.size() == 1 ? "" : "s")
                           .arg(session_->timecode(job.duration), job.settings.resolution)
                           .arg(rc::crfForQuality(job.settings.videoQuality))
                           .arg(job.estimatedSize));
        bar_->setValue(job.progress);
        thumb_->setImage(job.thumb);

        switch (job.status) {
        case Job::Status::Queued:
            status_->setText("queued");
            action_->setText("Cancel");
            break;
        case Job::Status::Encoding:
            status_->setText(job.cancelling ? "cancelling"
                                            : QString("%1% · %2 fps").arg(qRound(job.progress * 100)).arg(qRound(job.fps)));
            action_->setText("Cancel");
            action_->setEnabled(!job.cancelling);
            break;
        case Job::Status::Done:
            status_->setText("done ✓");
            action_->setText("Play output");
            action_->setEnabled(true);
            break;
        case Job::Status::Failed:
            status_->setText("failed");
            action_->setText("Dismiss");
            action_->setEnabled(true);
            break;
        }
        if (job.status == Job::Status::Encoding && !job.cancelling) {
            if (pulse_->state() != QAbstractAnimation::Running) pulse_->start();
        } else {
            pulse_->stop();
            setPulse(1);
        }
    }

    double pulse() const { return pulseValue_; }
    void setPulse(double value)
    {
        pulseValue_ = value;
        status_->setStyleSheet(QString("color: rgba(%1,%2,%3,%4);")
                                   .arg(Theme::tokens().text.red())
                                   .arg(Theme::tokens().text.green())
                                   .arg(Theme::tokens().text.blue())
                                   .arg(value, 0, 'f', 2));
    }

private:
    const Job* find() const
    {
        for (const Job& job : session_->queue())
            if (job.id == id_) return &job;
        return nullptr;
    }

    Session* session_;
    int id_;
    ThumbView* thumb_;
    ElidedLabel* name_;
    ElidedLabel* meta_;
    ProgressBar* bar_;
    QLabel* status_;
    QPushButton* action_;
    QAbstractAnimation* pulse_;
    double pulseValue_ = 1;
};

HistoryRow::HistoryRow(const QString& jobId, const QString& name, const QString& meta, QSize thumbSize, QWidget* parent)
    : QWidget(parent), thumbSize_(thumbSize)
{
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(thumbSize.height() + 15);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setAttribute(Qt::WA_Hover);
    thumb_ = QPixmap(paths::thumbFile(jobId));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(12 + thumbSize.width() + 10, 4, 12, 4);
    column->setSpacing(0);
    auto* title = new ElidedLabel(ui::monoFont(12));
    title->setText(name);
    auto* detail = new ElidedLabel(ui::bodyFont(11), "caption");
    detail->setText(meta);
    column->addWidget(title);
    column->addWidget(detail);
}

void HistoryRow::mouseReleaseEvent(QMouseEvent* event)
{
    if (rect().contains(event->pos())) emit clicked();
}

void HistoryRow::enterEvent(QEnterEvent*)
{
    hovered_ = true;
    update();
}

void HistoryRow::leaveEvent(QEvent*)
{
    hovered_ = false;
    update();
}

void HistoryRow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const Tokens& t = Theme::tokens();
    if (hovered_) p.fillRect(rect(), t.accent100);
    p.setPen(t.divider);
    p.drawLine(rect().bottomLeft(), rect().bottomRight());
    QRect thumb(QPoint(12, (height() - thumbSize_.height()) / 2), thumbSize_);
    p.fillRect(thumb, t.surface);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!thumb_.isNull()) p.drawPixmap(thumb, thumb_);
    p.setPen(t.divider);
    p.drawRect(thumb.adjusted(0, 0, -1, -1));
}

QString historyMeta(const Session& session, int index)
{
    const HistoryEntry& e = session.history()[index];
    return QString("%1 · %2 span%3 · %4 · %5")
        .arg(dateText(e.date))
        .arg(e.spans.size())
        .arg(e.spans.size() == 1 ? "" : "s")
        .arg(session.timecode(e.duration), QString::fromStdString(rc::formatFileSize(e.sizeBytes)));
}

JobsPanel::JobsPanel(Session* session, QWidget* parent) : Panel(parent), session_(session)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    summary_ = ui::label("", "caption", ui::monoFont(11));
    root->addWidget(ui::headerRow({ui::kicker("Encode queue"), summary_, ui::spacer()}));

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    empty_ = ui::label("No jobs queued. Encode adds the current spans and settings as a job.", "muted", ui::bodyFont(12));
    empty_->setWordWrap(true);
    empty_->setContentsMargins(12, 14, 12, 14);
    column->addWidget(empty_);
    queueRows_ = new QVBoxLayout;
    queueRows_->setSpacing(0);
    column->addLayout(queueRows_);
    column->addSpacing(8);

    auto* head = ui::headerRow({ui::kicker("History"),
                                ui::label("~/.local/share/reference-clipper/", "faint", ui::monoFont(10)), ui::spacer()});
    head->setProperty("divider", "both");
    column->addWidget(head);
    historyRows_ = new QVBoxLayout;
    historyRows_->setSpacing(0);
    column->addLayout(historyRows_);
    column->addStretch(1);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    connect(session_, &Session::queueChanged, this, &JobsPanel::syncQueue);
    connect(session_, &Session::historyChanged, this, &JobsPanel::syncHistory);
    syncQueue();
    syncHistory();
}

void JobsPanel::syncQueue()
{
    const QVector<Job>& queue = session_->queue();
    QSet<int> live;
    for (const Job& job : queue) {
        live.insert(job.id);
        JobRow* row = rows_.value(job.id);
        if (!row) {
            row = new JobRow(session_, job.id);
            rows_.insert(job.id, row);
            queueRows_->addWidget(row);
        }
        row->sync(job);
    }
    for (auto it = rows_.begin(); it != rows_.end();) {
        if (live.contains(it.key())) {
            ++it;
        } else {
            it.value()->deleteLater();
            it = rows_.erase(it);
        }
    }
    empty_->setVisible(queue.isEmpty());
    const int active = session_->activeJobs();
    summary_->setText(active ? QString("%1 active").arg(active) : queue.isEmpty() ? QString() : "idle");
}

void JobsPanel::syncHistory()
{
    while (QLayoutItem* item = historyRows_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QVector<HistoryEntry>& history = session_->history();
    for (int i = 0; i < history.size(); ++i) {
        auto* row = new HistoryRow(history[i].id, history[i].name, historyMeta(*session_, i), {48, 27});
        connect(row, &HistoryRow::clicked, this, [this, i] { session_->openHistoryEntry(i); });
        historyRows_->addWidget(row);
    }
}

#include "jobs_panel.moc"
