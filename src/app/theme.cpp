#include "app/theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleHints>

namespace {

Tokens lightTokens()
{
    Tokens t;
    t.bg = QColor("#f2f2f3");
    t.surface = QColor("#e9e9ea");
    t.text = QColor("#1d1f20");
    t.accent = QColor("#5980a6");
    t.accent100 = QColor("#eef6ff");
    t.accent500 = QColor("#749dc4");
    t.accent600 = QColor("#597ea3");
    t.accent700 = QColor("#416180");
    t.divider = Theme::faded(t.text, 0.16);
    t.cornerMark = Theme::faded(t.text, 0.55);
    return t;
}

Tokens darkTokens()
{
    Tokens t;
    t.bg = QColor("#1b1d1f");
    t.surface = QColor("#25282b");
    t.text = QColor("#e4e6e8");
    t.accent = QColor("#7fa6cf");
    t.accent100 = QColor("#243a50");
    t.accent500 = QColor("#749dc4");
    t.accent600 = QColor("#94bce3");
    t.accent700 = QColor("#b5d9fd");
    t.divider = Theme::faded(t.text, 0.18);
    t.cornerMark = Theme::faded(t.text, 0.55);
    return t;
}

QString css(const QColor& c)
{
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alphaF(), 0, 'f', 3);
}

QString styleSheet(const Tokens& t)
{
    QString s = R"(
QMainWindow, QDialog, QMessageBox { background: @bg; }
QToolTip { background: @bg; color: @text; border: 1px solid @divider; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: 0; }

QMenuBar { background: @bg; color: @text; border-bottom: 1px solid @divider; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 4px 9px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: @hover; }
QMenu { background: @bg; color: @text; border: 1px solid @divider; padding: 4px 0; }
QMenu::item { padding: 6px 28px 6px 14px; }
QMenu::item:selected { background: @accent100; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @divider; margin: 4px 0; }
QMenu::section { padding: 6px 14px 2px; color: @accent; font-size: 10px; }

QPushButton { background: transparent; color: @text; border: 1px solid @divider; padding: 4px 10px; border-radius: 0; }
QPushButton:hover { background: @hover; }
QPushButton:focus { border-color: @accent; outline: none; }
QPushButton:disabled { color: @disabled; }
QPushButton[kind="primary"] { background: @accent; color: @bg; border-color: @accent; }
QPushButton[kind="primary"]:hover { background: @accent600; border-color: @accent600; }
QPushButton[kind="primary"]:pressed { background: @accent700; border-color: @accent700; }
QPushButton[kind="primary"]:disabled { background: @accentDisabled; border-color: transparent; color: @bg; }
QPushButton[marks="true"] { margin: 6px; }
QPushButton[kind="ghost"] { background: transparent; color: @accent; border-color: transparent; padding: 1px 6px; }
QPushButton[kind="ghost"]:hover { background: @hover; }
QPushButton[seg="true"] { border: 0; padding: 2px 8px; }
QPushButton[seg="true"]:checked { background: @accent; color: @bg; }

QFrame[seg="true"] { border: 1px solid @divider; }

QLineEdit { background: transparent; color: @text; border: 1px solid @divider; padding: 5px 7px; border-radius: 0;
            selection-background-color: @accent; selection-color: @bg; }
QLineEdit:focus { border-color: @accent; }
QLineEdit:read-only { color: @muted; }

QSlider::groove:horizontal { height: 2px; background: @divider; }
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -6px 0; background: @accent; border: 1px solid @bg; }

QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar::handle:vertical { background: @divider; min-height: 24px; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 0; }
QScrollBar::handle:horizontal { background: @divider; min-width: 24px; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; width: 0; height: 0; }

QLabel { color: @text; background: transparent; }
QLabel[role="kicker"] { color: @accent; }
QLabel[role="caption"] { color: @caption; }
QLabel[role="muted"] { color: @muted; }
QLabel[role="faint"] { color: @faint; }

QWidget[divider="bottom"] { border-bottom: 1px solid @divider; }
QWidget[divider="top"] { border-top: 1px solid @divider; }
QWidget[divider="both"] { border-top: 1px solid @divider; border-bottom: 1px solid @divider; }
QWidget[plate="true"] { border: 1px solid @divider; }
QWidget[surface="true"] { background: @surface; }
)";
    const std::pair<const char*, QColor> values[] = {
        {"@bg", t.bg},
        {"@surface", t.surface},
        {"@text", t.text},
        {"@accentDisabled", Theme::faded(t.accent, 0.45)},
        {"@accent100", t.accent100},
        {"@accent600", t.accent600},
        {"@accent700", t.accent700},
        {"@accent", t.accent},
        {"@divider", t.divider},
        {"@hover", Theme::faded(t.text, 0.07)},
        {"@disabled", Theme::faded(t.text, 0.45)},
        {"@caption", Theme::faded(t.text, 0.6)},
        {"@muted", Theme::faded(t.text, 0.55)},
        {"@faint", Theme::faded(t.text, 0.5)},
    };
    for (const auto& [key, color] : values) s.replace(key, css(color));
    return s;
}

}  // namespace

Theme& Theme::instance()
{
    static Theme theme;
    return theme;
}

QColor Theme::faded(const QColor& color, double alpha)
{
    QColor c = color;
    c.setAlphaF(float(alpha));
    return c;
}

void Theme::install()
{
    const QStringList fonts{":/fonts/Barlow-Regular.ttf", ":/fonts/Barlow-Medium.ttf", ":/fonts/Barlow-Bold.ttf",
                            ":/fonts/BarlowCondensed-SemiBold.ttf"};
    for (const QString& font : fonts) QFontDatabase::addApplicationFont(font);
    QFont base("Barlow");
    base.setPixelSize(13);
    QApplication::setFont(base);

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (mode_ == Mode::System) apply();
    });
    apply();
}

void Theme::setMode(Mode mode)
{
    mode_ = mode;
    apply();
}

void Theme::apply()
{
    const bool dark = mode_ == Mode::Dark
                      || (mode_ == Mode::System
                          && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    tokens_ = dark ? darkTokens() : lightTokens();

    QPalette p;
    p.setColor(QPalette::Window, tokens_.bg);
    p.setColor(QPalette::Base, tokens_.bg);
    p.setColor(QPalette::AlternateBase, tokens_.surface);
    p.setColor(QPalette::Button, tokens_.bg);
    p.setColor(QPalette::ToolTipBase, tokens_.bg);
    p.setColor(QPalette::WindowText, tokens_.text);
    p.setColor(QPalette::Text, tokens_.text);
    p.setColor(QPalette::ButtonText, tokens_.text);
    p.setColor(QPalette::ToolTipText, tokens_.text);
    p.setColor(QPalette::Highlight, tokens_.accent);
    p.setColor(QPalette::HighlightedText, tokens_.bg);
    p.setColor(QPalette::PlaceholderText, faded(tokens_.text, 0.5));
    QApplication::setPalette(p);
    qApp->setStyleSheet(styleSheet(tokens_));
    emit changed();
}

Theme::Mode Theme::modeFromName(const QString& name)
{
    if (name == "light") return Mode::Light;
    if (name == "dark") return Mode::Dark;
    return Mode::System;
}

QString Theme::nameOf(Mode mode)
{
    switch (mode) {
    case Mode::Light: return "light";
    case Mode::Dark: return "dark";
    default: return "system";
    }
}
