// Hard-cut detection, and growing a scene outward from a point in time.
#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

#include "core/crop.h"
#include "media/probe.h"

namespace rc {

// Cut times in [from, to): each is the time of the first frame of a new scene.
// With a crop, only that region counts, which is what makes a cut visible in a
// 4:3 picture sitting inside a mostly static 9:16 frame.
std::vector<double> detectCuts(const ClipInfo& clip, double from, double to, const std::optional<Crop>& crop,
                               const std::atomic<bool>& cancel);

struct SceneRange {
    double start = 0;
    double end = 0;
};

// The scene containing `time`, saturating at `lower` and `upper`. Searches both
// directions in growing chunks, reporting the range so far after each, so a
// caller can show the edges moving. `onUpdate` runs on the calling thread.
SceneRange findScene(const ClipInfo& clip, double time, double lower, double upper, const std::optional<Crop>& crop,
                     const std::atomic<bool>& cancel, const std::function<void(const SceneRange&)>& onUpdate);

}  // namespace rc
