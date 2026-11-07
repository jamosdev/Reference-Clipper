// The source timeline: ruler, filmstrip, waveform, spans and playhead.
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QVector>
#include <QWidget>

#include "app/assets.h"

class Session;

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(Session* session, QWidget* parent = nullptr);

    bool editingSpan() const { return drag_.kind != Drag::None && drag_.kind != Drag::Head; }
    int xForTime(double seconds) const;

signals:
    void editingChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Drag {
        enum Kind { None, Head, Span, Left, Right, New } kind = None;
        int64_t id = 0;
        double offset = 0;
        double length = 0;
    };

    QRect track() const;
    double timeForX(int x) const;
    Drag hitTest(const QPoint& pos, double time) const;
    void resetAssets();

    Session* session_;
    int pressX_ = 0;
    double pressTime_ = 0;
    double dragTime_ = 0;
    QElapsedTimer pressClock_;
    QVector<QImage> frames_;
    QVector<float> peaks_;
    Drag drag_;
};
