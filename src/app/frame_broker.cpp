#include "app/frame_broker.h"

#include <QMetaObject>
#include <algorithm>
#include <utility>

#include "media/frame_image.h"
#include "media/stream_reader.h"

namespace {

constexpr double kEpsilon = 1e-4;
constexpr double kForwardDecodeLimit = 2.0;  // past this a seek is cheaper than decoding through

QImage toImage(const rc::RgbImage& image)
{
    if (image.empty()) return {};
    return QImage(reinterpret_cast<const uchar*>(image.pixels.data()), image.width, image.height,
                  QImage::Format_RGB32).copy();
}

QSize fitted(const QSize& box, rc::Dims source)
{
    const QSize size = QSize(source.w, source.h).scaled(box, Qt::KeepAspectRatio);
    return size.expandedTo({2, 2});
}

}  // namespace

FrameBroker::FrameBroker(QObject* parent) : QObject(parent) {}

FrameBroker::~FrameBroker()
{
    close();
}

void FrameBroker::open(const rc::ClipInfo& clip)
{
    close();
    stop_ = false;
    thread_ = std::thread([this, clip] { run(clip); });
}

void FrameBroker::close()
{
    if (!thread_.joinable()) return;
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        pending_.reset();
    }
    wake_.notify_all();
    thread_.join();
}

void FrameBroker::request(const FrameRequest& request)
{
    {
        std::lock_guard lock(mutex_);
        pending_ = request;
    }
    wake_.notify_all();
}

void FrameBroker::deliver(double time, QImage display, QImage scaled)
{
    QMetaObject::invokeMethod(this, [this, time, display = std::move(display), scaled = std::move(scaled)] {
        emit frameReady(time, display, scaled);
    }, Qt::QueuedConnection);
}

void FrameBroker::run(rc::ClipInfo clip)
{
    try {
        rc::StreamReader reader(clip.path, AVMEDIA_TYPE_VIDEO);
        rc::FrameImager displayImager, scaledImager;
        rc::FramePtr current = rc::makeFrame(), following = rc::makeFrame();
        bool haveCurrent = false, haveFollowing = false;
        double currentTime = 0, followingTime = 0;

        const auto superseded = [this] {
            std::lock_guard lock(mutex_);
            return stop_ || pending_.has_value();
        };
        const auto readFollowing = [&] {
            haveFollowing = reader.next(following.get());
            if (haveFollowing) followingTime = reader.timeOf(following.get());
        };
        const auto seekTo = [&](double t) {
            reader.seekBefore(t);
            haveCurrent = reader.next(current.get());
            if (haveCurrent) currentTime = reader.timeOf(current.get());
            readFollowing();
        };

        while (true) {
            FrameRequest request;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return stop_ || pending_.has_value(); });
                if (stop_) return;
                request = *pending_;
                pending_.reset();
            }

            const double t = request.time;
            const bool onCurrent = haveCurrent && t >= currentTime - kEpsilon
                                   && (!haveFollowing || t < followingTime - kEpsilon);
            if (!onCurrent) {
                const bool nearby = haveCurrent && t > currentTime && t - currentTime < kForwardDecodeLimit;
                if (!nearby) seekTo(t);
                bool abandoned = false;
                while (haveFollowing && followingTime <= t + kEpsilon) {
                    if (superseded()) {
                        abandoned = true;
                        break;
                    }
                    std::swap(current, following);
                    currentTime = followingTime;
                    haveCurrent = true;
                    readFollowing();
                }
                if (abandoned) continue;
            }
            if (!haveCurrent) continue;

            const QSize box = fitted(request.display, clip.display);
            const auto quality = request.fast ? rc::ScaleQuality::Fast : rc::ScaleQuality::Lanczos;
            QImage display = toImage(displayImager.convert(current.get(), {box.width(), box.height()},
                                                           clip.rotation, quality));
            QImage scaled;
            if (request.scaled.isValid() && !superseded())
                scaled = toImage(scaledImager.convert(current.get(), {request.scaled.width(), request.scaled.height()},
                                                      clip.rotation, rc::ScaleQuality::Lanczos));
            deliver(currentTime, std::move(display), std::move(scaled));
        }
    } catch (const std::exception& e) {
        const QString message = QString::fromUtf8(e.what());
        QMetaObject::invokeMethod(this, [this, message] { emit failed(message); }, Qt::QueuedConnection);
    }
}
