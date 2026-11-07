#include "core/config.h"

#include <cctype>
#include <optional>
#include <sstream>

namespace rc {
namespace {

void skipSpace(const std::string& s, size_t& i)
{
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

std::optional<std::string> readString(const std::string& s, size_t& i)
{
    if (i >= s.size() || s[i] != '"') return std::nullopt;
    std::string out;
    for (++i; i < s.size(); ++i) {
        if (s[i] == '"') {
            ++i;
            return out;
        }
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            switch (s[i]) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            default: out += s[i];
            }
        } else {
            out += s[i];
        }
    }
    return std::nullopt;
}

std::string quoted(const std::string& value)
{
    std::string out = "\"";
    for (char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
        }
    }
    return out + '"';
}

}  // namespace

Config parseConfig(const std::string& text)
{
    Config config;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        size_t i = 0;
        skipSpace(line, i);
        if (i >= line.size() || line[i] == '#') continue;
        const size_t eq = line.find('=', i);
        if (eq == std::string::npos) continue;
        std::string key = line.substr(i, eq - i);
        while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
        i = eq + 1;
        skipSpace(line, i);

        if (key == "recent" && i < line.size() && line[i] == '[') {
            ++i;
            std::vector<std::string> items;
            while (true) {
                skipSpace(line, i);
                auto item = readString(line, i);
                if (!item) break;
                items.push_back(std::move(*item));
                skipSpace(line, i);
                if (i >= line.size() || line[i] != ',') break;
                ++i;
            }
            config.recent = std::move(items);
            continue;
        }
        const auto value = readString(line, i);
        if (!value) continue;
        if (key == "out_dir") config.outDir = *value;
        else if (key == "layout") config.layout = *value;
        else if (key == "theme") config.theme = *value;
    }
    return config;
}

std::string serializeConfig(const Config& config)
{
    std::string out;
    out += "out_dir = " + quoted(config.outDir) + "\n";
    out += "layout = " + quoted(config.layout) + "\n";
    out += "theme = " + quoted(config.theme) + "\n";
    out += "recent = [";
    for (size_t i = 0; i < config.recent.size(); ++i) {
        if (i) out += ", ";
        out += quoted(config.recent[i]);
    }
    return out + "]\n";
}

}  // namespace rc
