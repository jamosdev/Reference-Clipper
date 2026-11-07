// Slow per-clip work on background threads: filmstrip, waveform, gallery.
#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>
#include <thread>

#include "media/probe.h"

class ClipAssets : public QObject {
    Q_OBJECT
public:
    static constexpr int kFilmstripFrames = 18;
    static constexpr int kWaveformBins = 1200;

    explicit ClipAssets(QObject* parent = nullptr);
    ~ClipAssets() override;

    void load(const rc::ClipInfo& clip);
    void clear();

signals:
    void filmstripFrame(int index, const QImage& image);
    void waveform(const QVector<float>& peaks);

private:
    std::thread filmstrip_, wave_;
    std::atomic<bool> cancel_{false};
};

struct GalleryItem {
    QString path;
    QString name;
    QString duration;
    QString meta;
    QImage thumb;
};

class GalleryScanner : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxItems = 32;

    explicit GalleryScanner(QObject* parent = nullptr);
    ~GalleryScanner() override;

    void scan(const QString& directory);

signals:
    void found(const GalleryItem& item);

private:
    std::thread thread_;
    std::atomic<bool> cancel_{false};
};

