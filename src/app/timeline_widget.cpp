#include "app/timeline_widget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

#include "app/session.h"
#include "app/theme.h"
#include "app/widgets.h"
#include "core/timecode.h"

namespace {

constexpr int kSideMargin = 14;
constexpr int kTopMargin = 10;
constexpr int kBottomMargin = 18;
constexpr int kRulerHeight = 14;
constexpr int kFilmstripTop = 32;
constexpr int kFilmstripHeight = 44;
constexpr int kWaveformTop = 80;
constexpr int kWaveformHeight = 26;
constexpr int kSpanTop = 30;
constexpr int kSpanHeight = 76;
constexpr int kHandleHalf = 5;
constexpr int kRulerTicks = 8;

}  // namespace

TimelineWidget::TimelineWidget(Session* session, QWidget* parent) : QWidget(parent), session_(session)
{
    setMinimumHeight(kTopMargin + kSpanTop + kSpanHeight + kBottomMargin);
    setMouseTracking(true);
    frames_.resize(ClipAssets::kFilmstripFrames);

    for (auto signal : {&Session::spansChanged, &Session::selectionChanged, &Session::playheadChanged})
        connect(session_, signal, this, qOverload<>(&QWidget::update));
    connect(session_, &Session::clipChanged, this, &TimelineWidget::resetAssets);
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    connect(session_->assets(), &ClipAssets::filmstripFrame, this, [this](int index, const QImage& image) {
        if (index >= 0 && index < frames_.size()) frames_[index] = image;
        update();
    });
    connect(session_->assets(), &ClipAssets::waveform, this, [this](const QVector<float>& peaks) {
        peaks_ = peaks;
        update();
    });
}

void TimelineWidget::resetAssets()
{
    frames_.fill(QImage());
    peaks_.clear();
    drag_ = {};
    update();
}

QRect TimelineWidget::track() const
{
    return rect().adjusted(kSideMargin, kTopMargin, -kSideMargin, -kBottomMargin);
}

int TimelineWidget::xForTime(double seconds) const
{
    const QRect t = track();
    const double duration = session_->hasClip() ? session_->clip().duration : 1;
    return t.left() + int(seconds / duration * t.width());
}

double TimelineWidget::timeForX(int x) const
{
    const QRect t = track();
    const double fraction = std::clamp(double(x - t.left()) / t.width(), 0.0, 1.0);
    return fraction * session_->clip().duration;
}

TimelineWidget::Drag TimelineWidget::hitTest(const QPoint& pos, double time) const
{
    const QRect t = track();
    const int top = t.top() + kSpanTop;
    if (pos.y() < top || pos.y() >= top + kSpanHeight) return {};

    // The selected span sits on top, so it gets the first look.
    auto spans = session_->sortedSpans();
    std::stable_partition(spans.begin(), spans.end(),
                          [this](const rc::Span& s) { return s.id == session_->selectedSpan(); });
    for (const rc::Span& span : spans) {
        const int left = xForTime(span.start);
        const int right = xForTime(span.end);
        if (std::abs(pos.x() - left) <= kHandleHalf) return {Drag::Left, span.id, 0, span.length()};
        if (std::abs(pos.x() - right) <= kHandleHalf) return {Drag::Right, span.id, 0, span.length()};
        if (pos.x() > left && pos.x() < right) return {Drag::Span, span.id, time - span.start, span.length()};
    }
    return {};
}

void TimelineWidget::mousePressEvent(QMouseEvent* event)
{
    if (!session_->hasClip() || event->button() != Qt::LeftButton) return;
    const double time = timeForX(event->pos().x());
    session_->pause();
    drag_ = hitTest(event->pos(), time);
    if (drag_.kind == Drag::None) {
        drag_.kind = Drag::Head;
        session_->seek(time);
    } else {
        session_->selectSpan(drag_.id);
        emit editingChanged();
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!session_->hasClip()) return;
    const double time = timeForX(event->pos().x());

    if (drag_.kind == Drag::None) {
        const Drag hover = hitTest(event->pos(), time);
        setCursor(hover.kind == Drag::Left || hover.kind == Drag::Right ? Qt::SizeHorCursor
                  : hover.kind == Drag::Span                            ? Qt::OpenHandCursor
                                                                        : Qt::CrossCursor);
        return;
    }
    if (drag_.kind == Drag::Head) {
        session_->seek(time);
        return;
    }
    const auto spans = session_->spans();
    const auto it = std::find_if(spans.begin(), spans.end(), [this](const rc::Span& s) { return s.id == drag_.id; });
    if (it == spans.end()) return;

    const double frame = session_->clip().frame();
    const double duration = session_->clip().duration;
    switch (drag_.kind) {
    case Drag::Left:
        session_->editSpan(it->id, std::min(time, it->end - frame), it->end, true, false);
        break;
    case Drag::Right:
        session_->editSpan(it->id, it->start, std::max(time, it->start + frame), false, true);
        break;
    default: {
        const double start = std::clamp(time - drag_.offset, 0.0, std::max(0.0, duration - drag_.length));
        session_->editSpan(it->id, start, start + drag_.length, false, false);
    }
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent*)
{
    drag_ = {};
    emit editingChanged();
}

void TimelineWidget::leaveEvent(QEvent*)
{
    unsetCursor();
}

void TimelineWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (!session_->hasClip()) return;
    const Tokens& tokens = Theme::tokens();
    const QRect t = track();
    const double duration = session_->clip().duration;

    // Ruler
    p.setFont(ui::monoFont(9));
    p.setPen(Theme::faded(tokens.text, 0.55));
    for (int i = 0; i < kRulerTicks; ++i) {
        const int x = t.left() + t.width() * i / kRulerTicks;
        p.drawLine(x, t.top(), x, t.top() + kRulerHeight);
        p.drawText(QRect(x + 3, t.top(), 60, kRulerHeight), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromStdString(rc::shortTimecode(duration * i / kRulerTicks)));
    }

    // Filmstrip: each frame covers its cell, cropped rather than squeezed.
    const QRect strip(t.left(), t.top() + kFilmstripTop, t.width(), kFilmstripHeight);
    p.fillRect(strip, tokens.surface);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setClipRect(strip);
    for (int i = 0; i < frames_.size(); ++i) {
        const int x0 = strip.left() + strip.width() * i / int(frames_.size());
        const int x1 = strip.left() + strip.width() * (i + 1) / int(frames_.size());
        const QRect cell(x0, strip.top(), x1 - x0, strip.height());
        const QImage& image = frames_[i];
        if (!image.isNull()) {
            const QSize fill = QSize(cell.size()).scaled(image.size(), Qt::KeepAspectRatio);
            QRect source(QPoint(0, 0), fill);
            source.moveCenter(image.rect().center());
            p.drawImage(cell, image, source);
        }
        p.setPen(QColor(0, 0, 0, 64));
        p.drawLine(cell.topRight(), cell.bottomRight());
    }
    p.setClipping(false);
    p.setPen(tokens.divider);
    p.setBrush(Qt::NoBrush);
    p.drawRect(strip.adjusted(0, 0, -1, -1));

    // Waveform
    const QRect wave(t.left(), t.top() + kWaveformTop, t.width(), kWaveformHeight);
    if (!peaks_.isEmpty()) {
        constexpr int kBarStep = 3;
        const int bars = std::max(1, wave.width() / kBarStep);
        const int bins = int(peaks_.size());
        for (int bar = 0; bar < bars; ++bar) {
            const int from = bins * bar / bars;
            const int to = std::max(from + 1, bins * (bar + 1) / bars);
            float peak = 0;
            for (int i = from; i < to && i < bins; ++i) peak = std::max(peak, peaks_[i]);
            const int height = std::max(1, int(peak * wave.height()));
            p.fillRect(wave.left() + bar * kBarStep, wave.center().y() - height / 2, kBarStep - 1, height, tokens.accent500);
        }
    } else if (!session_->clip().hasAudio) {
        p.setFont(ui::bodyFont(10));
        p.setPen(Theme::faded(tokens.text, 0.5));
        p.drawText(wave, Qt::AlignCenter, "no audio");
    }
    p.setPen(tokens.divider);
    p.setBrush(Qt::NoBrush);
    p.drawRect(wave.adjusted(0, 0, -1, -1));

    // Spans
    const auto spans = session_->sortedSpans();
    const int top = t.top() + kSpanTop;
    for (size_t i = 0; i < spans.size(); ++i) {
        const rc::Span& span = spans[i];
        const bool selected = span.id == session_->selectedSpan();
        const int left = xForTime(span.start);
        const int width = std::max(2, xForTime(span.end) - left);
        const QRect box(left, top, width, kSpanHeight);

        p.fillRect(box, Theme::faded(tokens.accent, 0.18));
        p.setPen(QPen(tokens.accent, selected ? 2 : 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(box.adjusted(0, 0, -1, -1));

        const QString caption = QString("%1 · %2").arg(i + 1).arg(session_->timecode(span.length()));
        p.setFont(ui::headingFont(12));
        const QRect tag(left, top - 15, p.fontMetrics().horizontalAdvance(caption) + 8, 14);
        p.fillRect(tag, tokens.bg);
        p.setPen(tokens.accent);
        p.drawText(tag, Qt::AlignCenter, caption);

        for (int x : {box.left(), box.right()}) {
            const QRect grip(x - 2, box.center().y() - 13, 4, 26);
            p.fillRect(grip, tokens.accent);
            p.setPen(tokens.bg);
            p.drawRect(grip.adjusted(-1, -1, 0, 0));
        }
    }

    // Playhead
    const int x = xForTime(session_->playhead());
    p.setPen(QPen(tokens.text, 2));
    p.drawLine(x, t.top(), x, top + kSpanHeight);
    QPainterPath cap;
    cap.moveTo(x - 5, t.top() - 2);
    cap.lineTo(x + 6, t.top() - 2);
    cap.lineTo(x + 0.5, t.top() + 5);
    cap.closeSubpath();
    p.setRenderHint(QPainter::Antialiasing);
    p.fillPath(cap, tokens.text);
}
