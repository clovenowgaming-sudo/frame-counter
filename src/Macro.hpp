#pragma once
// Pure C++ (no Geode) so the parser can be unit tested anywhere.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fw {

struct MacroInput {
    int64_t tick = 0;
    bool down = false;
    bool p2 = false;
    int button = 1;
};

struct Macro {
    std::string name;    // file name
    std::string format;  // GDR2 / GDR1 JSON / GDR1 msgpack
    std::string bot;     // bot name stored in the file (if any)
    std::string level;   // level name stored in the file (if any)
    double tps = 240.0;
    bool platformer = false;
    std::vector<MacroInput> inputs;  // jump inputs only, sorted by tick
    int64_t lastTick = 0;
};

bool parseMacro(std::vector<uint8_t> const& data, Macro& out, std::string& err);
bool loadMacroFile(std::filesystem::path const& path, Macro& out, std::string& err);

}  // namespace fw
