#include "app/assets.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>

#include "core/quality.h"
#include "core/timecode.h"
#include "media/frame_image.h"
#include "media/waveform.h"

namespace {

QImage toImage(const rc::RgbImage& image)
{
    if (image.empty()) return {};
    return QImage(reinterpret_cast<const uchar*>(image.pixels.data()), image.width, image.height,
                  QImage::Format_RGB32).copy();
}

QString formatFps(double fps)
{
    return QString::number(fps, 'f', 2).remove(QRegularExpression("\\.?0+$"));
}

}  // namespace

ClipAssets::ClipAssets(QObject* parent) : QObject(parent) {}

ClipAssets::~ClipAssets()
{
    clear();
}

void ClipAssets::clear()
{
    cancel_ = true;
    if (filmstrip_.joinable()) filmstrip_.join();
    if (wave_.joinable()) wave_.join();
    cancel_ = false;
}

void ClipAssets::load(const rc::ClipInfo& clip)
{
    clear();
    filmstrip_ = std::thread([this, clip] {
        try {
            for (int i = 0; i < kFilmstripFrames && !cancel_; ++i) {
                const double t = clip.duration * (i + 0.5) / kFilmstripFrames;
                const QImage image = toImage(rc::extractFrame(clip.path, t, 160, true));
                QMetaObject::invokeMethod(this, [this, i, image] { emit filmstripFrame(i, image); },
                                          Qt::QueuedConnection);
            }
        } catch (const std::exception&) {
            // The strip stays blank; the clip itself still works.
        }
    });
    if (!clip.hasAudio) return;
    wave_ = std::thread([this, clip] {
        const auto publish = [this](const std::vector<float>& peaks) {
            QVector<float> copy(peaks.begin(), peaks.end());
            QMetaObject::invokeMethod(this, [this, copy] { emit waveform(copy); }, Qt::QueuedConnection);
        };
        try {
            publish(rc::computeWaveform(clip.path, clip.duration, kWaveformBins, cancel_, publish));
        } catch (const std::exception&) {
        }
    });
}

GalleryScanner::GalleryScanner(QObject* parent) : QObject(parent) {}

GalleryScanner::~GalleryScanner()
{
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

void GalleryScanner::scan(const QString& directory)
{
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
    cancel_ = false;
    thread_ = std::thread([this, directory] {
        static const QStringList filters{"*.mp4", "*.mkv", "*.webm", "*.mov", "*.m4v", "*.avi"};
        const QFileInfoList files = QDir(directory).entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Time);
        int shown = 0;
        for (const QFileInfo& file : files) {
            if (cancel_ || shown >= kMaxItems) return;
            try {
                const rc::ClipInfo clip = rc::probeClip(file.absoluteFilePath().toStdString());
                GalleryItem item;
                item.path = file.absoluteFilePath();
                item.name = file.fileName();
                item.duration = QString::fromStdString(rc::shortTimecode(clip.duration));
                item.meta = QString("%1×%2 · %3 fps · %4")
                                .arg(clip.display.w)
                                .arg(clip.display.h)
                                .arg(formatFps(clip.fps), QString::fromStdString(rc::formatFileSize(clip.sizeBytes)));
                item.thumb = toImage(rc::extractFrame(clip.path, clip.duration * 0.1, 300, true));
                ++shown;
                QMetaObject::invokeMethod(this, [this, item] { emit found(item); }, Qt::QueuedConnection);
            } catch (const std::exception&) {
                // Not a usable video; leave it out of the gallery.
            }
        }
    });
}
