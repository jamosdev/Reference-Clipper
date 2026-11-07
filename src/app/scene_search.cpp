#include "app/scene_search.h"

#include <QMetaObject>

#include "media/scene.h"

SceneSearch::SceneSearch(QObject* parent) : QObject(parent) {}

SceneSearch::~SceneSearch()
{
    stop();
}

void SceneSearch::stop()
{
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

void SceneSearch::start(int64_t spanId, const rc::ClipInfo& clip, double time, rc::FreeRange range,
                        const std::optional<rc::Crop>& crop)
{
    stop();
    cancel_ = false;
    thread_ = std::thread([this, spanId, clip, time, range, crop] {
        const auto publish = [this, spanId](const rc::SceneRange& scene) {
            QMetaObject::invokeMethod(this, [this, spanId, scene] { emit progress(spanId, scene.start, scene.end); },
                                      Qt::QueuedConnection);
        };
        try {
            rc::findScene(clip, time, range.lower, range.upper, crop, cancel_, publish);
        } catch (const std::exception& e) {
            qWarning("scene search: %s", e.what());
        }
        QMetaObject::invokeMethod(this, [this, spanId] { emit finished(spanId); }, Qt::QueuedConnection);
    });
}
