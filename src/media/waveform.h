#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace rc {

// Peak per bin, 0..1, across the clip. `onUpdate` receives partial results as
// decoding proceeds, so the strip can fill in from the left. Decoding a long
// file's audio takes seconds and runs off the UI thread.
std::vector<float> computeWaveform(const std::string& path, double duration, int bins,
                                   const std::atomic<bool>& cancel,
                                   const std::function<void(const std::vector<float>&)>& onUpdate);

}  // namespace rc
