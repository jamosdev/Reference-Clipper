#include "app/source_view.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

#include "app/session.h"
#include "app/theme.h"

namespace {

constexpr int kHandleReach = 7;   // pixels either side of an edge that grab it
constexpr int kDragThreshold = 4;
constexpr int kGripLength = 26;
constexpr int kGripWidth = 4;

}  // namespace

SourceView::SourceView(Session* session, QWidget* parent) : FrameView(parent), session_(session)
{
    setMouseTracking(true);
    connect(session_, &Session::cropChanged, this, qOverload<>(&QWidget::update));
}

QRect SourceView::pictureRect() const
{
    if (!session_->hasClip()) return rect();
    const rc::Dims d = session_->clip().display;
    QRect picture(QPoint(0, 0), QSize(d.w, d.h).scaled(size(), Qt::KeepAspectRatio));
    picture.moveCenter(rect().center());
    return picture;
}

QPoint SourceView::widgetPoint(int sourceX, int sourceY) const
{
    const QRect picture = pictureRect();
    const rc::Dims d = session_->clip().display;
    return {picture.left() + int(std::lround(double(sourceX) * picture.width() / d.w)),
            picture.top() + int(std::lround(double(sourceY) * picture.height() / d.h))};
}

QPoint SourceView::sourcePoint(const QPoint& position) const
{
    const QRect picture = pictureRect();
    const rc::Dims d = session_->clip().display;
    const double x = std::clamp(double(position.x() - picture.left()) / picture.width(), 0.0, 1.0);
    const double y = std::clamp(double(position.y() - picture.top()) / picture.height(), 0.0, 1.0);
    return {int(std::lround(x * d.w)), int(std::lround(y * d.h))};
}

QRect SourceView::cropRect() const
{
    const auto& crop = session_->crop();
    if (!crop) return {};
    return QRect(widgetPoint(crop->x, crop->y), widgetPoint(crop->right(), crop->bottom())).adjusted(0, 0, -1, -1);
}

int SourceView::edgesAt(const QPoint& p) const
{
    const QRect box = cropRect();
    if (box.isNull() || !box.adjusted(-kHandleReach, -kHandleReach, kHandleReach, kHandleReach).contains(p)) return 0;
    int edges = 0;
    if (std::abs(p.x() - box.left()) <= kHandleReach) edges |= rc::EdgeLeft;
    else if (std::abs(p.x() - box.right()) <= kHandleReach) edges |= rc::EdgeRight;
    if (std::abs(p.y() - box.top()) <= kHandleReach) edges |= rc::EdgeTop;
    else if (std::abs(p.y() - box.bottom()) <= kHandleReach) edges |= rc::EdgeBottom;
    return edges;
}

void SourceView::updateCursor(const QPoint& position)
{
    if (!session_->hasClip()) {
        unsetCursor();
        return;
    }
    const int edges = edgesAt(position);
    const bool horizontal = edges & (rc::EdgeLeft | rc::EdgeRight);
    const bool vertical = edges & (rc::EdgeTop | rc::EdgeBottom);
    if (horizontal && vertical) {
        const bool falling = (edges & rc::EdgeLeft) == bool(edges & rc::EdgeTop) * rc::EdgeLeft;
        setCursor(falling ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    } else if (horizontal) {
        setCursor(Qt::SizeHorCursor);
    } else if (vertical) {
        setCursor(Qt::SizeVerCursor);
    } else if (cropRect().contains(position)) {
        setCursor(Qt::SizeAllCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
}

void SourceView::mousePressEvent(QMouseEvent* event)
{
    if (!session_->hasClip() || event->button() != Qt::LeftButton) return;
    pressed_ = event->pos();
    edges_ = edgesAt(pressed_);
    if (session_->crop()) startCrop_ = *session_->crop();
    if (edges_) gesture_ = Gesture::Resizing;
    else if (cropRect().contains(pressed_)) gesture_ = Gesture::Moving;
    else gesture_ = Gesture::Pressed;
}

void SourceView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint position = event->pos();
    if (gesture_ == Gesture::None) {
        updateCursor(position);
        return;
    }
    const rc::Dims display = session_->clip().display;
    const QPoint source = sourcePoint(position);
    const QPoint origin = sourcePoint(pressed_);

    switch (gesture_) {
    case Gesture::Pressed:
        if ((position - pressed_).manhattanLength() < kDragThreshold) break;
        gesture_ = Gesture::Drawing;
        [[fallthrough]];
    case Gesture::Drawing:
        session_->setCrop(rc::cropBetween(origin.x(), origin.y(), source.x(), source.y(), display));
        break;
    case Gesture::Resizing:
        session_->setCrop(rc::resizeCrop(startCrop_, edges_, source.x(), source.y(), display));
        break;
    case Gesture::Moving:
        session_->setCrop(rc::moveCrop(startCrop_, source.x() - origin.x(), source.y() - origin.y(), display));
        break;
    default: break;
    }
}

void SourceView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    if (gesture_ == Gesture::Pressed) session_->clearCrop();  // a click outside the crop
    gesture_ = Gesture::None;
    updateCursor(event->pos());
}

void SourceView::paintOverlay(QPainter& p)
{
    const auto& crop = session_->crop();
    if (!session_->hasClip() || !crop) return;
    const QRect picture = pictureRect();
    const QRect box = cropRect();

    const QColor shade(0, 0, 0, 150);
    p.fillRect(QRect(picture.topLeft(), QPoint(picture.right(), box.top() - 1)), shade);
    p.fillRect(QRect(QPoint(picture.left(), box.bottom() + 1), picture.bottomRight()), shade);
    p.fillRect(QRect(QPoint(picture.left(), box.top()), QPoint(box.left() - 1, box.bottom())), shade);
    p.fillRect(QRect(QPoint(box.right() + 1, box.top()), QPoint(picture.right(), box.bottom())), shade);

    const Tokens& t = Theme::tokens();
    p.setPen(QPen(t.accent, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(box);

    const QPoint centre = box.center();
    const int longest = std::min({kGripLength, box.width() / 2, box.height() / 2});
    const auto grip = [&](const QRect& rect) {
        p.fillRect(rect, t.accent);
        p.setPen(QPen(t.bg, 1));
        p.drawRect(rect.adjusted(-1, -1, 0, 0));
    };
    grip(QRect(box.left() - kGripWidth / 2, centre.y() - longest / 2, kGripWidth, longest));
    grip(QRect(box.right() - kGripWidth / 2 + 1, centre.y() - longest / 2, kGripWidth, longest));
    grip(QRect(centre.x() - longest / 2, box.top() - kGripWidth / 2, longest, kGripWidth));
    grip(QRect(centre.x() - longest / 2, box.bottom() - kGripWidth / 2 + 1, longest, kGripWidth));
}
