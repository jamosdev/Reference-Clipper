// The input frame, where the crop is drawn and adjusted.
#pragma once

#include "app/preview_panel.h"

class Session;

class SourceView : public FrameView {
    Q_OBJECT
public:
    explicit SourceView(Session* session, QWidget* parent = nullptr);

    // Where a displayed source pixel falls on this widget.
    QPoint widgetPoint(int sourceX, int sourceY) const;

protected:
    void paintOverlay(QPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class Gesture { None, Pressed, Drawing, Resizing, Moving };

    QRect pictureRect() const;
    QPoint sourcePoint(const QPoint& position) const;
    QRect cropRect() const;
    int edgesAt(const QPoint& position) const;
    void updateCursor(const QPoint& position);

    Session* session_;
    Gesture gesture_ = Gesture::None;
    QPoint pressed_;
    rc::Crop startCrop_;
    int edges_ = 0;
};
