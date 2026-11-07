#include "app/job_queue.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <algorithm>

#include "media/frame_image.h"

JobQueue::JobQueue(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<rc::EncodeProgress>();
    qRegisterMetaType<rc::EncodeResult>();
}

JobQueue::~JobQueue()
{
    waiting_.clear();
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

void JobQueue::enqueue(int id, const rc::EncodeRequest& request, const QString& thumbPath)
{
    waiting_.push_back({id, request, thumbPath});
    if (active_ < 0) startNext();
}

void JobQueue::cancel(int id)
{
    if (id == active_) {
        cancel_ = true;
        return;
    }
    waiting_.erase(std::remove_if(waiting_.begin(), waiting_.end(), [id](const Waiting& w) { return w.id == id; }),
                   waiting_.end());
}

void JobQueue::startNext()
{
    if (thread_.joinable()) thread_.join();
    if (waiting_.empty()) return;
    Waiting job = std::move(waiting_.front());
    waiting_.pop_front();
    active_ = job.id;
    cancel_ = false;
    emit started(job.id);

    thread_ = std::thread([this, job = std::move(job)] {
        const int id = job.id;
        try {
            // Taken first so the queue row has a picture while the encode runs.
            const rc::Span first = rc::sortedByStart(job.request.spans).front();
            const rc::RgbImage image = rc::extractFrame(job.request.source, first.start, 320, false);
            if (!image.empty()) {
                const QImage thumb = QImage(reinterpret_cast<const uchar*>(image.pixels.data()), image.width,
                                            image.height, QImage::Format_RGB32).copy();
                QDir().mkpath(QFileInfo(job.thumbPath).path());
                thumb.save(job.thumbPath, "JPEG", 85);
                QMetaObject::invokeMethod(this, [this, id, thumb] { emit thumbnail(id, thumb); }, Qt::QueuedConnection);
            }
        } catch (const std::exception&) {
            // A missing thumbnail must not fail the job.
        }

        const auto onProgress = [this, id](const rc::EncodeProgress& p) {
            QMetaObject::invokeMethod(this, [this, id, p] { emit progress(id, p); }, Qt::QueuedConnection);
        };
        const rc::EncodeResult result = rc::encodeClip(job.request, onProgress, cancel_);
        QMetaObject::invokeMethod(this, [this, id, result] {
            active_ = -1;
            emit finished(id, result);
            if (active_ < 0) startNext();
        }, Qt::QueuedConnection);
    });
}
