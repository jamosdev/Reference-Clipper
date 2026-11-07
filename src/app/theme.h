// The Industry design tokens as Qt palette, stylesheet and paint colours.
#pragma once

#include <QColor>
#include <QObject>
#include <QString>

struct Tokens {
    QColor bg, surface, text, accent, accent100, accent500, accent600, accent700, divider, cornerMark;
};

class Theme : public QObject {
    Q_OBJECT
public:
    enum class Mode { System, Light, Dark };

    static Theme& instance();
    static const Tokens& tokens() { return instance().tokens_; }
    static QColor faded(const QColor& color, double alpha);

    void install();  // call once, after QApplication exists
    void setMode(Mode mode);
    Mode mode() const { return mode_; }

    static Mode modeFromName(const QString& name);
    static QString nameOf(Mode mode);

signals:
    void changed();

private:
    void apply();

    Mode mode_ = Mode::System;
    Tokens tokens_;
};
