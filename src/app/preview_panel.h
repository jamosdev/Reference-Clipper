// Input and output frames side by side, with the transport row.
#pragma once

#include <QImage>
#include <QWidget>

#include "app/frame_broker.h"
#include "app/widgets.h"

class QPushButton;
class Session;
class ElidedLabel;

class FrameView : public QWidget {
    Q_OBJECT
public:
    explicit FrameView(QWidget* parent = nullptr);
    void setImage(const QImage& image);
    void setChip(const QString& text);
    void setDecoded(bool decoded);
    QString chip() const { return chip_; }
    bool hasImage() const { return !image_.isNull(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage image_;
    QString chip_;
    bool decoded_ = false;
};

class PreviewPanel : public Panel {
    Q_OBJECT
public:
    explicit PreviewPanel(Session* session, QWidget* parent = nullptr);

    // For tests.
    FrameView* inputView() const { return input_; }
    FrameView* outputView() const { return output_; }

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class OutputMode { Black, Seek, Decoded };

    void clipChanged();
    void refresh();
    void syncLabels();
    QSize deviceSize(const QWidget* widget) const;

    Session* session_;
    FrameBroker sourceFrames_;
    FrameBroker outputFrames_;
    QString openOutput_;
    OutputMode mode_ = OutputMode::Black;

    FrameView* input_;
    FrameView* output_;
    QWidget* inputBox_;
    QWidget* outputBox_;
    ElidedLabel* inputCaption_;
    ElidedLabel* outputCaption_;
    class Segmented* audio_;
    QPushButton* play_;
    QLabel* playhead_;
    QLabel* length_;
};
