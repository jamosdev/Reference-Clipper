// Small building blocks shared by the panels.
#pragma once

#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QVector>

namespace ui {

QFont monoFont(int pixelSize);
QFont headingFont(int pixelSize);
QFont bodyFont(int pixelSize);
QString tildePath(const QString& path);
QPixmap iconPixmap(const QString& name, const QColor& color, int size, qreal ratio = 1);

// role: kicker, caption, muted or faint; see the stylesheet in theme.cpp.
QLabel* kicker(const QString& text, QWidget* parent = nullptr);
QLabel* label(const QString& text, const char* role, const QFont& font, QWidget* parent = nullptr);
QPushButton* button(const QString& text, const char* kind = "secondary", QWidget* parent = nullptr);
QWidget* headerRow(const QVector<QWidget*>& items, QWidget* parent = nullptr);
QWidget* spacer(QWidget* parent = nullptr);

}  // namespace ui

// Draws the `+` registration marks of the blueprint style, centred on a rect's corners.
void paintCornerMarks(class QPainter& painter, const QRect& box);

// A blueprint frame: hairline border and four corner marks. Marks sit outside
// the frame, so the frame is inset by kOverhang and panels use smaller gaps.
class Panel : public QWidget {
    Q_OBJECT
public:
    static constexpr int kOverhang = 6;
    explicit Panel(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

// A primary button with corner marks, which need the QSS margin set by `marks`.
class MarkedButton : public QPushButton {
    Q_OBJECT
public:
    explicit MarkedButton(const QString& text, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

// A square button showing a Lucide icon in the current text colour.
class IconButton : public QPushButton {
    Q_OBJECT
public:
    IconButton(const QString& icon, const QString& tooltip, QWidget* parent = nullptr);

private:
    void refresh();
    QString icon_;
};

// One line of text that elides instead of forcing its parent wider.
class ElidedLabel : public QWidget {
    Q_OBJECT
public:
    explicit ElidedLabel(const QFont& font, const char* role = nullptr, QWidget* parent = nullptr);
    void setText(const QString& text);
    QString text() const { return text_; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString text_;
    double opacity_ = 1;
};

// Mutually exclusive buttons in one hairline frame.
class Segmented : public QFrame {
    Q_OBJECT
public:
    Segmented(const QStringList& options, int fontSize, bool stretch, QWidget* parent = nullptr);
    void setCurrent(int index);
    int current() const;

signals:
    void picked(int index);

private:
    QVector<QPushButton*> buttons_;
};

class Toast : public QWidget {
    Q_OBJECT
public:
    explicit Toast(QWidget* parent);
    void show(const QString& text, int milliseconds);
    void reposition();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString text_;
    class QTimer* timer_;
};
