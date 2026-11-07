#pragma once

#include <string>

namespace rc {

// mm:ss.ff, where ff counts frames of the clip. Minutes do not roll into hours.
std::string timecode(double seconds, double fps);
// mm:ss
std::string shortTimecode(double seconds);

}  // namespace rc
