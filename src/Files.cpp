#include "Files.hpp"

#include <algorithm>
#include <cctype>

using namespace geode::prelude;
namespace fs = std::filesystem;

namespace fw {

static std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static void scan(std::vector<MacroFile>& out, fs::path const& root, std::string const& source, bool allowJson, int maxDepth) {
    std::error_code ec;
    if (!fs::exists(root, ec) || ec) return;

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_directory(e2)) {
            if (it.depth() >= maxDepth) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(e2)) continue;
        auto ext = lower(it->path().extension().string());
        if (ext != ".gdr" && ext != ".gdr2" && !(allowJson && ext == ".json")) continue;

        MacroFile f;
        f.path = it->path();
        f.source = source;
        std::error_code e3;
        auto rel = fs::relative(f.path, root, e3);
        f.label = (e3 || rel.empty()) ? f.path.filename().string() : rel.string();
        f.size = it->file_size(e3);
        f.time = it->last_write_time(e3);
        out.push_back(std::move(f));
    }
}

std::vector<MacroFile> listMacroFiles() {
    std::vector<MacroFile> out;
    auto own = Mod::get()->getSaveDir();
    auto modsSave = own.parent_path();

    std::error_code ec;
    fs::create_directories(own / "macros", ec);

    scan(out, modsSave / "zilko.xdbot", "xdBot", false, 3);
    scan(out, modsSave / "eclipse.eclipse-menu", "Eclipse", false, 3);
    scan(out, own / "macros", "Own", true, 3);

    std::sort(out.begin(), out.end(), [](MacroFile const& a, MacroFile const& b) {
        return a.time > b.time;
    });
    return out;
}

}  // namespace fw
