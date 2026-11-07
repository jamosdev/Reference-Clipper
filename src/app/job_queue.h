// Runs one encode at a time on a worker thread.
#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <atomic>
#include <deque>
#include <thread>

#include "media/encoder.h"

class JobQueue : public QObject {
    Q_OBJECT
public:
    explicit JobQueue(QObject* parent = nullptr);
    ~JobQueue() override;

    void enqueue(int id, const rc::EncodeRequest& request, const QString& thumbPath);
    void cancel(int id);
    bool busy() const { return active_ >= 0 || !waiting_.empty(); }

signals:
    void started(int id);
    void progress(int id, const rc::EncodeProgress& progress);
    void thumbnail(int id, const QImage& image);
    void finished(int id, const rc::EncodeResult& result);

private:
    struct Waiting {
        int id;
        rc::EncodeRequest request;
        QString thumbPath;
    };

    void startNext();

    std::deque<Waiting> waiting_;
    std::thread thread_;
    std::atomic<bool> cancel_{false};
    int active_ = -1;
};
