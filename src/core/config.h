// ~/.config/reference-clipper/config.toml. Only the flat subset of TOML this
// file needs: `key = "string"` and `key = ["a", "b"]`.
#pragma once

#include <string>
#include <vector>

namespace rc {

struct Config {
    std::string outDir;  // empty means the default, resolved by the app
    std::string layout = "classic";
    std::string theme = "system";
    std::vector<std::string> recent;
};

Config parseConfig(const std::string& text);
std::string serializeConfig(const Config& config);

}  // namespace rc
