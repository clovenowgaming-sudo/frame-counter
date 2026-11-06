#pragma once
#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace fw {

struct MacroFile {
    std::filesystem::path path;
    std::string source;  // xdBot / Eclipse / Own
    std::string label;   // file name relative to its folder
    uintmax_t size = 0;
    std::filesystem::file_time_type time{};
};

// Scans the xdBot and Eclipse save folders (plus this mod's own "macros" folder).
std::vector<MacroFile> listMacroFiles();

}  // namespace fw
