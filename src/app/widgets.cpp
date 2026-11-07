#include "app/widgets.h"

#include <QDir>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPainter>
#include <QStyle>
#include <QSvgRenderer>
#include <QTimer>

#include "app/theme.h"

namespace ui {

QFont monoFont(int pixelSize)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(pixelSize);
    return font;
}

QFont headingFont(int pixelSize)
{
    QFont font("Barlow Condensed");
    font.setStyleName("SemiBold");
    font.setPixelSize(pixelSize);
    return font;
}

QFont bodyFont(int pixelSize)
{
    QFont font("Barlow");
    font.setPixelSize(pixelSize);
    return font;
}

QString tildePath(const QString& path)
{
    const QString home = QDir::homePath();
    if (path == home) return "~";
    return path.startsWith(home + '/') ? '~' + path.mid(home.size()) : path;
}

QPixmap iconPixmap(const QString& name, const QColor& color, int size, qreal ratio)
{
    static const QHash<QString, QString> paths{
        {"folder", R"(<path d="M4 20h16a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13c0 1.1.9 2 2 2Z"/>)"},
        {"film", R"(<rect x="2" y="4" width="20" height="16"/><path d="M2 8h20M2 16h20M6 4v16M18 4v16"/>)"},
        {"skip-back", R"(<path d="M18 20 9 12l9-8z"/><path d="M5 19V5"/>)"},
        {"chevron-left", R"(<path d="m15 18-6-6 6-6"/>)"},
        {"chevron-right", R"(<path d="m9 18 6-6-6-6"/>)"},
    };
    const QString svg = QString(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="%1" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">%2</svg>)")
                            .arg(color.name(), paths.value(name));
    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(0, 0, size, size));
    return pixmap;
}

QLabel* kicker(const QString& text, QWidget* parent)
{
    QFont font = bodyFont(10);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1);
    return label(text.toUpper(), "kicker", font, parent);
}

QLabel* label(const QString& text, const char* role, const QFont& font, QWidget* parent)
{
    auto* widget = new QLabel(text, parent);
    widget->setProperty("role", role);
    widget->setFont(font);
    return widget;
}

QPushButton* button(const QString& text, const char* kind, QWidget* parent)
{
    auto* widget = new QPushButton(text, parent);
    widget->setProperty("kind", kind);
    widget->setCursor(Qt::PointingHandCursor);
    return widget;
}

QWidget* headerRow(const QVector<QWidget*>& items, QWidget* parent)
{
    auto* row = new QWidget(parent);
    row->setAttribute(Qt::WA_StyledBackground);
    row->setProperty("divider", "bottom");
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(8);
    for (QWidget* item : items) layout->addWidget(item);
    return row;
}

QWidget* spacer(QWidget* parent)
{
    auto* widget = new QWidget(parent);
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return widget;
}

}  // namespace ui

void paintCornerMarks(QPainter& painter, const QRect& box)
{
    painter.setPen(QPen(Theme::tokens().cornerMark, 1));
    const QPoint corners[] = {box.topLeft(), box.topRight(), box.bottomLeft(), box.bottomRight()};
    for (const QPoint& c : corners) {
        painter.drawLine(c.x() - 5, c.y(), c.x() + 5, c.y());
        painter.drawLine(c.x(), c.y() - 5, c.x(), c.y() + 5);
    }
}

Panel::Panel(QWidget* parent) : QWidget(parent)
{
    setContentsMargins(kOverhang + 1, kOverhang + 1, kOverhang + 1, kOverhang + 1);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
}

void Panel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QRect box = rect().adjusted(kOverhang, kOverhang, -kOverhang - 1, -kOverhang - 1);
    painter.setPen(QPen(Theme::tokens().divider, 1));
    painter.drawRect(box);
    paintCornerMarks(painter, box);
}

MarkedButton::MarkedButton(const QString& text, QWidget* parent) : QPushButton(text, parent)
{
    setProperty("kind", "primary");
    setProperty("marks", true);
    setCursor(Qt::PointingHandCursor);
}

void MarkedButton::paintEvent(QPaintEvent* event)
{
    QPushButton::paintEvent(event);
    QPainter painter(this);
    paintCornerMarks(painter, rect().adjusted(6, 6, -7, -7));
}

IconButton::IconButton(const QString& icon, const QString& tooltip, QWidget* parent)
    : QPushButton(parent), icon_(icon)
{
    setToolTip(tooltip);
    setFixedSize(30, 30);
    setIconSize({14, 14});
    setCursor(Qt::PointingHandCursor);
    connect(&Theme::instance(), &Theme::changed, this, &IconButton::refresh);
    refresh();
}

void IconButton::refresh()
{
    setIcon(ui::iconPixmap(icon_, Theme::tokens().text, 14, devicePixelRatioF()));
}

ElidedLabel::ElidedLabel(const QFont& font, const char* role, QWidget* parent) : QWidget(parent)
{
    setFont(font);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    const QString name(role);
    opacity_ = name == "caption" ? 0.6 : name == "muted" ? 0.55 : name == "faint" ? 0.5 : 1;
    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void ElidedLabel::setText(const QString& text)
{
    if (text == text_) return;
    text_ = text;
    setToolTip(text);
    updateGeometry();
    update();
}

QSize ElidedLabel::sizeHint() const
{
    return {fontMetrics().horizontalAdvance(text_), fontMetrics().height()};
}

QSize ElidedLabel::minimumSizeHint() const
{
    return {0, fontMetrics().height()};
}

void ElidedLabel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setPen(Theme::faded(Theme::tokens().text, opacity_));
    painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter,
                     fontMetrics().elidedText(text_, Qt::ElideRight, width()));
}

Segmented::Segmented(const QStringList& options, int fontSize, bool stretch, QWidget* parent) : QFrame(parent)
{
    setProperty("seg", true);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    for (int i = 0; i < options.size(); ++i) {
        auto* option = new QPushButton(options[i], this);
        option->setProperty("seg", true);
        option->setCheckable(true);
        option->setAutoExclusive(true);
        option->setFont(ui::bodyFont(fontSize));
        option->setCursor(Qt::PointingHandCursor);
        if (stretch) option->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        connect(option, &QPushButton::clicked, this, [this, i] { emit picked(i); });
        layout->addWidget(option);
        buttons_.push_back(option);
    }
}

void Segmented::setCurrent(int index)
{
    if (index >= 0 && index < buttons_.size()) buttons_[index]->setChecked(true);
}

int Segmented::current() const
{
    for (int i = 0; i < buttons_.size(); ++i)
        if (buttons_[i]->isChecked()) return i;
    return -1;
}

Toast::Toast(QWidget* parent) : QWidget(parent), timer_(new QTimer(this))
{
    setFont(ui::bodyFont(12));
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &QWidget::hide);
    hide();
}

void Toast::show(const QString& text, int milliseconds)
{
    text_ = text;
    timer_->start(milliseconds);
    reposition();
    QWidget::show();
    raise();
    update();
}

void Toast::reposition()
{
    const int margin = Panel::kOverhang;
    const QSize size(fontMetrics().horizontalAdvance(text_) + 28 + 2 * margin, fontMetrics().height() + 16 + 2 * margin);
    resize(size);
    move(parentWidget()->width() - size.width() - 14, parentWidget()->height() - size.height() - 30);
}

void Toast::paintEvent(QPaintEvent*)
{
    const int margin = Panel::kOverhang;
    const Tokens& t = Theme::tokens();
    QPainter painter(this);
    const QRect box = rect().adjusted(margin, margin, -margin - 1, -margin - 1);
    for (int i = 3; i > 0; --i) {  // --shadow-md, stepped
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 14));
        painter.drawRect(box.adjusted(-i, -i + 2, i, i + 2));
    }
    painter.fillRect(box, t.bg);
    painter.setPen(QPen(t.divider, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(box);
    paintCornerMarks(painter, box);
    painter.setPen(t.text);
    painter.drawText(box, Qt::AlignCenter, text_);
}
