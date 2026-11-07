// Decodes single frames off the UI thread. Requests coalesce: the worker only
// ever serves the newest one, and drops work a newer request has superseded,
// so dragging the playhead or playing 4K cannot build up a backlog.
#pragma once

#include <QImage>
#include <QObject>
#include <QSize>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

#include "core/crop.h"
#include "media/probe.h"

struct FrameRequest {
    double time = 0;
    QSize display;   // input pane size in device pixels
    QSize output;    // if valid, also render the output picture at this size
    std::optional<rc::Crop> crop;  // applies to the output picture only
    bool fast = false;
};

class FrameBroker : public QObject {
    Q_OBJECT
public:
    explicit FrameBroker(QObject* parent = nullptr);
    ~FrameBroker() override;

    void open(const rc::ClipInfo& clip);
    void close();
    void request(const FrameRequest& request);

signals:
    void frameReady(double time, const QImage& display, const QImage& output);
    void failed(const QString& message);

private:
    void run(rc::ClipInfo clip);
    void deliver(double time, QImage display, QImage output);

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<FrameRequest> pending_;
    bool stop_ = false;
};
