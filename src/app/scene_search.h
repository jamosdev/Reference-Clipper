// Runs one scene search at a time off the UI thread.
#pragma once

#include <QObject>
#include <atomic>
#include <optional>
#include <thread>

#include "core/crop.h"
#include "core/timeline.h"
#include "media/probe.h"

class SceneSearch : public QObject {
    Q_OBJECT
public:
    explicit SceneSearch(QObject* parent = nullptr);
    ~SceneSearch() override;

    void start(int64_t spanId, const rc::ClipInfo& clip, double time, rc::FreeRange range,
               const std::optional<rc::Crop>& crop);
    // Asks the running search to stop; `finished` still follows.
    void cancel() { cancel_ = true; }
    // Stops and waits, for when the clip goes away.
    void stop();

signals:
    void progress(int64_t spanId, double start, double end);
    void finished(int64_t spanId);

private:
    std::thread thread_;
    std::atomic<bool> cancel_{false};
};
