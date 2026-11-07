#include "app/preview_panel.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QVBoxLayout>

#include "app/session.h"
#include "app/source_view.h"
#include "app/theme.h"

FrameView::FrameView(QWidget* parent) : QWidget(parent) {}

void FrameView::setImage(const QImage& image)
{
    image_ = image;
    update();
}

void FrameView::setChip(const QString& text)
{
    if (text == chip_) return;
    chip_ = text;
    update();
}

void FrameView::setDecoded(bool decoded)
{
    if (decoded == decoded_) return;
    decoded_ = decoded;
    update();
}

void FrameView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!image_.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QRect target(QPoint(0, 0), image_.size().scaled(size(), Qt::KeepAspectRatio));
        target.moveCenter(rect().center());
        p.drawImage(target, image_);
    }
    paintOverlay(p);

    // In the corner, not centred as in the mock-up: the frame is what's being inspected.
    p.setFont(ui::monoFont(11));
    const QFontMetrics metrics = p.fontMetrics();
    const QRect chip(8, height() - metrics.height() - 14, metrics.horizontalAdvance(chip_) + 16, metrics.height() + 6);
    p.fillRect(chip, QColor(0, 0, 0, 140));
    p.setPen(Qt::white);
    p.drawText(chip, Qt::AlignCenter, chip_);

    if (decoded_) {
        const QString text = "DECODED AV1";
        QFont font = ui::bodyFont(10);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        p.setFont(font);
        const int w = p.fontMetrics().horizontalAdvance(text) + 14;
        const QRect badge(width() - w - 8, 8, w, p.fontMetrics().height() + 4);
        // accent-700 of the light ramp in both themes: white text needs a dark fill.
        p.fillRect(badge, QColor("#416180"));
        p.setPen(Qt::white);
        p.drawText(badge, Qt::AlignCenter, text);
    }

    p.setPen(Theme::tokens().divider);
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

namespace {

// Holds a 16:9 frame, centred and as large as fits.
QRect fitted16x9(const QSize& box)
{
    QRect frame(QPoint(0, 0), QSize(16, 9).scaled(box, Qt::KeepAspectRatio));
    frame.moveCenter(QRect(QPoint(0, 0), box).center());
    return frame;
}

}  // namespace

PreviewPanel::PreviewPanel(Session* session, QWidget* parent) : Panel(parent), session_(session)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    audio_ = new Segmented({"Input", "Output"}, 11, false, this);
    root->addWidget(ui::headerRow({ui::kicker("Preview"), ui::label("input · output side by side · drag on input to crop", "caption", ui::bodyFont(11)),
                                   ui::spacer(), ui::label("Audio", "caption", ui::bodyFont(11)), audio_}));

    auto* body = new QWidget(this);
    body->setAttribute(Qt::WA_StyledBackground);
    body->setProperty("surface", true);
    body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto* columns = new QHBoxLayout(body);
    columns->setContentsMargins(12, 12, 12, 12);
    columns->setSpacing(12);

    const auto makeColumn = [&](const QString& title, ElidedLabel*& caption, QWidget*& box) {
        auto* column = new QVBoxLayout;
        column->setSpacing(5);
        auto* head = new QHBoxLayout;
        head->setSpacing(8);
        QFont kickerFont = ui::bodyFont(10);
        kickerFont.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        head->addWidget(ui::label(title.toUpper(), "caption", kickerFont));
        caption = new ElidedLabel(ui::monoFont(11), "caption");
        head->addWidget(caption, 1);
        column->addLayout(head);
        box = new QWidget;
        box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        box->installEventFilter(this);
        column->addWidget(box, 1);
        columns->addLayout(column, 1);
    };
    makeColumn("Input", inputCaption_, inputBox_);
    makeColumn("Output", outputCaption_, outputBox_);
    input_ = new SourceView(session_, inputBox_);
    output_ = new FrameView(outputBox_);
    root->addWidget(body, 1);

    auto* transport = new QWidget(this);
    transport->setAttribute(Qt::WA_StyledBackground);
    transport->setProperty("divider", "top");
    auto* row = new QHBoxLayout(transport);
    row->setContentsMargins(10, 6, 10, 6);
    row->setSpacing(6);
    auto* start = new IconButton("skip-back", "Start", this);
    auto* back = new IconButton("chevron-left", "Previous frame (←)", this);
    play_ = ui::button("Play spans", "primary", this);
    play_->setFixedHeight(30);
    play_->setMinimumWidth(110);
    auto* forward = new IconButton("chevron-right", "Next frame (→)", this);
    playhead_ = ui::label("00:00.00", nullptr, ui::monoFont(14), this);
    length_ = ui::label("/ 00:00.00", "faint", ui::monoFont(11), this);
    row->addWidget(start);
    row->addWidget(back);
    row->addWidget(play_);
    row->addWidget(forward);
    row->addSpacing(10);
    row->addWidget(playhead_);
    row->addWidget(length_);
    row->addStretch(1);
    row->addWidget(ui::label("Space play · I/O set in/out · ←/→ frame · Del remove span", "caption", ui::bodyFont(11)));
    root->addWidget(transport);

    connect(start, &QPushButton::clicked, session_, &Session::goToFirstSpan);
    connect(back, &QPushButton::clicked, this, [this] { session_->stepFrames(-1); });
    connect(forward, &QPushButton::clicked, this, [this] { session_->stepFrames(1); });
    connect(play_, &QPushButton::clicked, session_, &Session::togglePlay);
    connect(audio_, &Segmented::picked, this, [this](int index) {
        session_->setAudioSource(index == 0 ? AudioSource::Input : AudioSource::Output);
    });

    connect(session_, &Session::clipChanged, this, &PreviewPanel::clipChanged);
    connect(session_, &Session::playheadChanged, this, &PreviewPanel::refresh);
    connect(session_, &Session::outputSettingsChanged, this, &PreviewPanel::refresh);
    connect(session_, &Session::playingChanged, this, [this] {
        play_->setText(session_->playing() ? "Pause" : "Play spans");
    });
    connect(session_, &Session::audioSourceChanged, this, [this] {
        audio_->setCurrent(session_->audioSource() == AudioSource::Input ? 0 : 1);
        syncLabels();
    });

    connect(&sourceFrames_, &FrameBroker::frameReady, this, [this](double, const QImage& display, const QImage& output) {
        input_->setImage(display);
        if (mode_ == OutputMode::Seek) output_->setImage(output.isNull() ? display : output);
    });
    connect(&outputFrames_, &FrameBroker::frameReady, this, [this](double, const QImage& display, const QImage&) {
        if (mode_ == OutputMode::Decoded) output_->setImage(display);
    });

    audio_->setCurrent(0);
}

QSize PreviewPanel::deviceSize(const QWidget* widget) const
{
    return (widget->size() * devicePixelRatioF()).expandedTo({16, 9});
}

void PreviewPanel::resizeEvent(QResizeEvent* event)
{
    Panel::resizeEvent(event);
    refresh();
}

bool PreviewPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Resize) {
        if (watched == inputBox_) input_->setGeometry(fitted16x9(inputBox_->size()));
        if (watched == outputBox_) output_->setGeometry(fitted16x9(outputBox_->size()));
    }
    return Panel::eventFilter(watched, event);
}

void PreviewPanel::clipChanged()
{
    openOutput_.clear();
    outputFrames_.close();
    input_->setImage({});
    output_->setImage({});
    if (session_->hasClip()) sourceFrames_.open(session_->clip());
    else sourceFrames_.close();
    refresh();
}

void PreviewPanel::syncLabels()
{
    if (!session_->hasClip()) return;
    const rc::ClipInfo& clip = session_->clip();
    const FinishedOutput* finished = session_->finishedOutput();
    const QString name = QString::fromStdString(clip.name);
    QString caption = QString("%1 · %2×%3").arg(name).arg(clip.display.w).arg(clip.display.h);
    // First, because long file names elide everything after them.
    if (const auto& crop = session_->crop())
        caption.prepend(QString("crop %1×%2+%3+%4 · ").arg(crop->w).arg(crop->h).arg(crop->x).arg(crop->y));
    inputCaption_->setText(caption);

    const rc::Dims size = finished ? finished->size : session_->outputSize();
    outputCaption_->setText(finished ? QString("%1 · %2×%3").arg(finished->name).arg(size.w).arg(size.h)
                                     : QString("not encoded yet · preview via seek · %1×%2").arg(size.w).arg(size.h));

    const double t = session_->playhead();
    const QString tc = session_->timecode(t);
    playhead_->setText(tc);
    length_->setText("/ " + session_->timecode(clip.duration));
    input_->setChip("seek @ " + tc);
    switch (mode_) {
    case OutputMode::Black:
        output_->setChip(session_->audioSource() == AudioSource::Output ? "black · silence" : "black");
        break;
    case OutputMode::Seek: output_->setChip("seek preview @ " + tc); break;
    case OutputMode::Decoded: output_->setChip("decoded @ " + tc); break;
    }
    output_->setDecoded(mode_ == OutputMode::Decoded);
}

void PreviewPanel::refresh()
{
    if (!session_->hasClip()) return;
    const rc::ClipInfo& clip = session_->clip();
    const double t = session_->playhead();
    const bool fast = session_->playing();
    const FinishedOutput* finished = session_->finishedOutput();

    if (finished && finished->path != openOutput_) {
        rc::ClipInfo info = clip;
        try {
            info = rc::probeClip(finished->path.toStdString());
            outputFrames_.open(info);
            openOutput_ = finished->path;
        } catch (const std::exception&) {
            openOutput_.clear();
        }
    }

    const bool inSpan = rc::anyContains(session_->spans(), t);
    const auto mapped = finished && finished->path == openOutput_
                            ? finished->map.toOutput(t, clip.frame(), session_->playingSpan())
                            : std::nullopt;

    const OutputMode previous = mode_;
    mode_ = !inSpan ? OutputMode::Black : mapped ? OutputMode::Decoded : OutputMode::Seek;
    if (mode_ == OutputMode::Black) output_->setImage({});
    else if (previous != mode_ && previous == OutputMode::Black) output_->setImage({});

    FrameRequest request;
    request.time = t;
    request.display = deviceSize(input_);
    request.fast = fast;
    if (mode_ == OutputMode::Seek) {
        const rc::Dims size = session_->outputSize();
        request.crop = session_->crop();
        // Paused: the real output size, with the real scaler. Playing: just enough
        // to show, and with no crop the input image can stand in for it.
        if (!fast) request.output = QSize(size.w, size.h);
        else if (request.crop) request.output = QSize(size.w, size.h).scaled(deviceSize(output_), Qt::KeepAspectRatio);
    }
    sourceFrames_.request(request);

    if (mode_ == OutputMode::Decoded) {
        FrameRequest out;
        out.time = *mapped;
        out.display = deviceSize(output_);
        out.fast = fast;
        outputFrames_.request(out);
    }
    syncLabels();
}
