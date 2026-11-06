#include "Macro.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace fw {
namespace {

// ---------------------------------------------------------------- GDR2 ----

struct Reader {
    uint8_t const* d;
    size_t n;
    size_t p = 0;
    bool fail = false;

    Reader(uint8_t const* data, size_t size) : d(data), n(size) {}

    uint8_t u8() {
        if (p >= n) { fail = true; return 0; }
        return d[p++];
    }
    uint64_t varint() {
        uint64_t r = 0;
        int shift = 0;
        for (;;) {
            if (p >= n) { fail = true; return 0; }
            uint8_t b = d[p++];
            if (shift < 64) r |= uint64_t(b & 0x7F) << shift;
            if (!(b & 0x80)) break;
            shift += 7;
            if (shift > 70) { fail = true; return 0; }
        }
        return r;
    }
    std::string cstr() {
        std::string s;
        for (;;) {
            if (p >= n) { fail = true; return s; }
            char c = static_cast<char>(d[p++]);
            if (c == 0) break;
            s.push_back(c);
        }
        return s;
    }
    void skip(uint64_t k) {
        if (k > n - p) { fail = true; p = n; return; }
        p += static_cast<size_t>(k);
    }
};

enum class NumMode { BigEndian, LittleEndian, Varint };

struct Gdr2Try {
    bool ok = false;
    bool full = false;  // every byte of the file was used
    bool platformer = false;
    std::string err;
    Macro m;
};

bool rawNum(Reader& r, int bytes, bool big, uint64_t& out) {
    if (static_cast<size_t>(bytes) > r.n - r.p) { r.fail = true; return false; }
    out = 0;
    for (int i = 0; i < bytes; ++i) out = (out << 8) | r.d[r.p + (big ? i : bytes - 1 - i)];
    r.p += bytes;
    return true;
}

// Strings are a varint length + bytes in the files bots write today.
std::string lstr(Reader& r) {
    uint64_t len = r.varint();
    std::string s;
    if (r.fail || len > r.n - r.p) { r.fail = true; return s; }
    s.assign(reinterpret_cast<char const*>(r.d + r.p), static_cast<size_t>(len));
    r.p += static_cast<size_t>(len);
    return s;
}

// packing 0: one varint, (frameDelta << 1) | down   (what Eclipse/xdBot files use)
// packing 1: README table, byte0 = cont | down<<1 | delta<<2, rest is a varint
// strMode 0: length-prefixed strings, 1: null-terminated strings (README)
Gdr2Try tryGdr2(std::vector<uint8_t> const& data, NumMode mode, bool ext, int packing, int strMode) {
    Gdr2Try t;
    Reader r(data.data(), data.size());
    auto str = [&]() { return strMode == 0 ? lstr(r) : r.cstr(); };
    r.skip(3);  // "GDR"
    uint64_t version = r.varint();
    std::string tag = str();
    str();  // author
    str();  // description
    if (r.fail || version < 1 || version > 255) { t.err = "not a GDR2 header"; return t; }
    if (ext && tag.empty()) return t;  // nothing to test without a custom input tag

    bool big = mode != NumMode::LittleEndian;
    uint64_t u = 0;
    double fps = 0;
    if (mode == NumMode::Varint) r.varint();
    else if (!rawNum(r, 4, big, u)) { t.err = "cut short"; return t; }
    r.varint();  // game version
    if (mode == NumMode::Varint) fps = static_cast<double>(r.varint());
    else {
        if (!rawNum(r, 8, big, u)) { t.err = "cut short"; return t; }
        std::memcpy(&fps, &u, 8);
    }
    if (r.fail || !(fps >= 1 && fps <= 100000)) { t.err = "bad TPS"; return t; }

    r.varint();  // seed
    r.varint();  // coins
    r.u8();      // ldm
    bool plat = r.u8() != 0;
    std::string botName = str();
    r.varint();  // bot version
    r.varint();  // level id
    std::string levelName = str();
    uint64_t extSize = r.varint();
    r.skip(extSize);
    uint64_t deaths = r.varint();
    if (r.fail || deaths > r.n - r.p) { t.err = "broken death list"; return t; }
    for (uint64_t i = 0; i < deaths; ++i) r.varint();
    uint64_t total = r.varint();
    uint64_t p1 = r.varint();
    if (r.fail || total > r.n - r.p || p1 > total) { t.err = "broken input count"; return t; }
    if (plat) { t.platformer = true; t.err = "Platformer macros are not supported."; return t; }

    int64_t frame = 0;
    size_t presses = 0;
    for (uint64_t i = 0; i < total; ++i) {
        if (i == p1) frame = 0;  // P2 deltas start over
        bool down = false;
        uint64_t delta = 0;
        if (packing == 0) {
            uint64_t packed = r.varint();
            delta = packed >> 1;
            down = (packed & 1) != 0;
        } else {
            uint8_t b0 = r.u8();
            down = ((b0 >> 1) & 1) != 0;
            delta = (b0 >> 2) & 0x3F;
            if (b0 & 1) delta |= r.varint() << 6;
        }
        if (ext) r.skip(r.varint());
        if (r.fail) { t.err = "inputs cut short"; return t; }
        frame += static_cast<int64_t>(delta);
        if (frame > 0x7fffffffLL) { t.err = "bad frame"; return t; }
        MacroInput in;
        in.tick = frame;
        in.down = down;
        in.p2 = i >= p1;
        in.button = 1;
        if (down) ++presses;
        t.m.inputs.push_back(in);
    }
    if (presses == 0) { t.err = "no presses (wrong layout?)"; return t; }

    t.m.format = "GDR2";
    t.m.bot = botName;
    t.m.level = levelName;
    t.m.tps = fps;
    t.ok = true;
    t.full = r.p == r.n;
    return t;
}

bool parseGdr2(std::vector<uint8_t> const& data, Macro& m, std::string& err) {
    Gdr2Try fallback;
    bool haveFallback = false;
    std::string firstErr;
    bool sawPlatformer = false;
    bool first = true;

    // Try the layout the bots write today first, then the alternatives. The first
    // layout that explains every byte of the file wins.
    for (int strMode : {0, 1}) {
        for (bool ext : {false, true}) {
            for (NumMode mode : {NumMode::BigEndian, NumMode::LittleEndian, NumMode::Varint}) {
                for (int packing : {0, 1}) {
                    bool isMain = first;
                    first = false;
                    Gdr2Try t = tryGdr2(data, mode, ext, packing, strMode);
                    if (isMain && t.platformer) sawPlatformer = true;
                    if (!t.ok) {
                        if (firstErr.empty()) firstErr = t.err;
                        continue;
                    }
                    if (t.full) {
                        m = std::move(t.m);
                        return true;
                    }
                    if (isMain) { fallback = std::move(t); haveFallback = true; }
                }
            }
        }
    }
    if (haveFallback) {
        m = std::move(fallback.m);
        return true;
    }
    err = sawPlatformer ? "Platformer macros are not supported."
                        : "Could not read this GDR2 file: " + (firstErr.empty() ? std::string("unknown layout") : firstErr);
    return false;
}

// ---------------------------------------------------- generic value tree ----

struct Val {
    enum class T { Nil, Bool, Int, Dbl, Str, Arr, Obj } t = T::Nil;
    bool b = false;
    int64_t i = 0;
    double d = 0;
    std::string s;
    std::vector<Val> a;           // array items / object values
    std::vector<std::string> k;   // object keys

    Val const* get(char const* key) const {
        if (t != T::Obj) return nullptr;
        for (size_t x = 0; x < k.size(); ++x)
            if (k[x] == key) return &a[x];
        return nullptr;
    }
    double num(double def) const {
        switch (t) {
            case T::Int: return static_cast<double>(i);
            case T::Dbl: return d;
            case T::Bool: return b ? 1.0 : 0.0;
            default: return def;
        }
    }
    bool boolean(bool def) const {
        switch (t) {
            case T::Bool: return b;
            case T::Int: return i != 0;
            case T::Dbl: return d != 0;
            default: return def;
        }
    }
};

struct JsonParser {
    char const* p;
    char const* e;
    int depth = 0;
    bool fail = false;

    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }

    static void utf8(std::string& s, uint32_t c) {
        if (c < 0x80) s.push_back(static_cast<char>(c));
        else if (c < 0x800) {
            s.push_back(static_cast<char>(0xC0 | (c >> 6)));
            s.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            s.push_back(static_cast<char>(0xE0 | (c >> 12)));
            s.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }

    bool str(std::string& out) {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e && *p != '"') {
            if (*p == '\\') {
                ++p;
                if (p >= e) return false;
                switch (*p) {
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'u': {
                        if (e - p < 5) return false;
                        uint32_t c = 0;
                        for (int x = 1; x <= 4; ++x) {
                            char h = p[x];
                            c <<= 4;
                            if (h >= '0' && h <= '9') c |= h - '0';
                            else if (h >= 'a' && h <= 'f') c |= h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') c |= h - 'A' + 10;
                            else return false;
                        }
                        utf8(out, c);
                        p += 4;
                        break;
                    }
                    default: out.push_back(*p); break;
                }
                ++p;
            } else {
                out.push_back(*p++);
            }
        }
        if (p >= e) return false;
        ++p;
        return true;
    }

    bool value(Val& v) {
        if (++depth > 64) return false;
        ws();
        if (p >= e) return false;
        bool ok = false;
        char c = *p;
        if (c == '{') {
            ++p;
            v.t = Val::T::Obj;
            ws();
            if (p < e && *p == '}') { ++p; ok = true; }
            else {
                for (;;) {
                    ws();
                    std::string key;
                    if (!str(key)) break;
                    ws();
                    if (p >= e || *p != ':') break;
                    ++p;
                    Val child;
                    if (!value(child)) break;
                    v.k.push_back(std::move(key));
                    v.a.push_back(std::move(child));
                    ws();
                    if (p < e && *p == ',') { ++p; continue; }
                    if (p < e && *p == '}') { ++p; ok = true; }
                    break;
                }
            }
        } else if (c == '[') {
            ++p;
            v.t = Val::T::Arr;
            ws();
            if (p < e && *p == ']') { ++p; ok = true; }
            else {
                for (;;) {
                    Val child;
                    if (!value(child)) break;
                    v.a.push_back(std::move(child));
                    ws();
                    if (p < e && *p == ',') { ++p; continue; }
                    if (p < e && *p == ']') { ++p; ok = true; }
                    break;
                }
            }
        } else if (c == '"') {
            v.t = Val::T::Str;
            ok = str(v.s);
        } else if (e - p >= 4 && std::strncmp(p, "true", 4) == 0) {
            v.t = Val::T::Bool; v.b = true; p += 4; ok = true;
        } else if (e - p >= 5 && std::strncmp(p, "false", 5) == 0) {
            v.t = Val::T::Bool; v.b = false; p += 5; ok = true;
        } else if (e - p >= 4 && std::strncmp(p, "null", 4) == 0) {
            v.t = Val::T::Nil; p += 4; ok = true;
        } else {
            char const* s = p;
            bool isFloat = false;
            while (p < e && (std::strchr("+-0123456789.eE", *p) != nullptr)) {
                if (*p == '.' || *p == 'e' || *p == 'E') isFloat = true;
                ++p;
            }
            if (p > s) {
                std::string tok(s, p);
                char* endp = nullptr;
                double dv = std::strtod(tok.c_str(), &endp);
                if (endp && *endp == 0) {
                    if (isFloat) { v.t = Val::T::Dbl; v.d = dv; }
                    else { v.t = Val::T::Int; v.i = static_cast<int64_t>(dv); }
                    ok = true;
                }
            }
        }
        --depth;
        return ok;
    }
};

struct MpReader {
    uint8_t const* d;
    size_t n;
    size_t p = 0;
    int depth = 0;
    bool fail = false;

    uint64_t be(int bytes) {
        if (static_cast<size_t>(bytes) > n - p) { fail = true; p = n; return 0; }
        uint64_t r = 0;
        for (int x = 0; x < bytes; ++x) r = (r << 8) | d[p++];
        return r;
    }
    void readStr(Val& v, uint64_t len) {
        v.t = Val::T::Str;
        if (len > n - p) { fail = true; return; }
        v.s.assign(reinterpret_cast<char const*>(d + p), static_cast<size_t>(len));
        p += static_cast<size_t>(len);
    }
    void readArr(Val& v, uint64_t count) {
        v.t = Val::T::Arr;
        if (count > n - p) { fail = true; return; }
        for (uint64_t x = 0; x < count && !fail; ++x) {
            Val c;
            read(c);
            v.a.push_back(std::move(c));
        }
    }
    void readMap(Val& v, uint64_t count) {
        v.t = Val::T::Obj;
        if (count > n - p) { fail = true; return; }
        for (uint64_t x = 0; x < count && !fail; ++x) {
            Val key, val;
            read(key);
            read(val);
            if (key.t != Val::T::Str) { fail = true; return; }
            v.k.push_back(std::move(key.s));
            v.a.push_back(std::move(val));
        }
    }
    void read(Val& v) {
        if (fail) return;
        if (++depth > 64) { fail = true; return; }
        if (p >= n) { fail = true; --depth; return; }
        uint8_t b = d[p++];
        if (b <= 0x7F) { v.t = Val::T::Int; v.i = b; }
        else if (b >= 0xE0) { v.t = Val::T::Int; v.i = static_cast<int8_t>(b); }
        else if (b >= 0x80 && b <= 0x8F) readMap(v, b & 0x0F);
        else if (b >= 0x90 && b <= 0x9F) readArr(v, b & 0x0F);
        else if (b >= 0xA0 && b <= 0xBF) readStr(v, b & 0x1F);
        else {
            switch (b) {
                case 0xC0: v.t = Val::T::Nil; break;
                case 0xC2: v.t = Val::T::Bool; v.b = false; break;
                case 0xC3: v.t = Val::T::Bool; v.b = true; break;
                case 0xC4: { uint64_t l = be(1); if (l > n - p) fail = true; else p += l; break; }
                case 0xC5: { uint64_t l = be(2); if (l > n - p) fail = true; else p += l; break; }
                case 0xC6: { uint64_t l = be(4); if (l > n - p) fail = true; else p += l; break; }
                case 0xCA: {
                    uint32_t bits = static_cast<uint32_t>(be(4));
                    float f; std::memcpy(&f, &bits, 4);
                    v.t = Val::T::Dbl; v.d = f; break;
                }
                case 0xCB: {
                    uint64_t bits = be(8);
                    double f; std::memcpy(&f, &bits, 8);
                    v.t = Val::T::Dbl; v.d = f; break;
                }
                case 0xCC: v.t = Val::T::Int; v.i = static_cast<int64_t>(be(1)); break;
                case 0xCD: v.t = Val::T::Int; v.i = static_cast<int64_t>(be(2)); break;
                case 0xCE: v.t = Val::T::Int; v.i = static_cast<int64_t>(be(4)); break;
                case 0xCF: v.t = Val::T::Int; v.i = static_cast<int64_t>(be(8)); break;
                case 0xD0: v.t = Val::T::Int; v.i = static_cast<int8_t>(be(1)); break;
                case 0xD1: v.t = Val::T::Int; v.i = static_cast<int16_t>(be(2)); break;
                case 0xD2: v.t = Val::T::Int; v.i = static_cast<int32_t>(be(4)); break;
                case 0xD3: v.t = Val::T::Int; v.i = static_cast<int64_t>(be(8)); break;
                case 0xD9: readStr(v, be(1)); break;
                case 0xDA: readStr(v, be(2)); break;
                case 0xDB: readStr(v, be(4)); break;
                case 0xDC: readArr(v, be(2)); break;
                case 0xDD: readArr(v, be(4)); break;
                case 0xDE: readMap(v, be(2)); break;
                case 0xDF: readMap(v, be(4)); break;
                default: fail = true; break;
            }
        }
        --depth;
    }
};

bool fromVal(Val const& root, Macro& m, std::string& err) {
    if (root.t != Val::T::Obj) {
        err = "Macro file is not a GDR object.";
        return false;
    }
    auto const* inputs = root.get("inputs");
    if (!inputs || inputs->t != Val::T::Arr) {
        err = "Macro file has no \"inputs\" list (not a GDR macro?).";
        return false;
    }
    if (auto const* f = root.get("framerate")) {
        double fr = f->num(240.0);
        m.tps = fr > 0 ? fr : 240.0;
    }
    if (auto const* pl = root.get("platformer")) m.platformer = pl->boolean(false);
    if (auto const* bot = root.get("bot")) {
        if (auto const* n = bot->get("name")) m.bot = n->s;
    }
    if (auto const* lv = root.get("level")) {
        if (auto const* n = lv->get("name")) m.level = n->s;
    }
    if (m.platformer) {
        err = "Platformer macros are not supported.";
        return false;
    }
    for (auto const& in : inputs->a) {
        if (in.t != Val::T::Obj) continue;
        MacroInput mi;
        auto const* fr = in.get("frame");
        if (!fr) continue;
        mi.tick = static_cast<int64_t>(fr->num(0));
        auto const* btn = in.get("btn");
        if (!btn) btn = in.get("button");
        mi.button = btn ? static_cast<int>(btn->num(1)) : 1;
        auto const* p2 = in.get("2p");
        if (!p2) p2 = in.get("player2");
        mi.p2 = p2 ? p2->boolean(false) : false;
        auto const* dn = in.get("down");
        mi.down = dn ? dn->boolean(false) : false;
        m.inputs.push_back(mi);
    }
    return true;
}

}  // namespace

bool parseMacro(std::vector<uint8_t> const& data, Macro& out, std::string& err) {
    Macro m;
    m.name = out.name;
    if (data.size() < 4) {
        err = "File is empty or too small.";
        return false;
    }

    if (std::memcmp(data.data(), "GDR", 3) == 0) {
        if (!parseGdr2(data, m, err)) return false;
    } else {
        size_t i = 0;
        while (i < data.size() && (data[i] == ' ' || data[i] == '\n' || data[i] == '\r' || data[i] == '\t' || data[i] == 0xEF || data[i] == 0xBB || data[i] == 0xBF)) ++i;
        Val root;
        if (i < data.size() && data[i] == '{') {
            JsonParser jp{reinterpret_cast<char const*>(data.data()) + i,
                          reinterpret_cast<char const*>(data.data()) + data.size()};
            if (!jp.value(root)) {
                err = "JSON macro is broken.";
                return false;
            }
            m.format = "GDR1 JSON";
        } else {
            MpReader mp{data.data(), data.size()};
            mp.read(root);
            if (mp.fail) {
                err = "Unknown macro format (not GDR2, GDR JSON or GDR msgpack).";
                return false;
            }
            m.format = "GDR1 msgpack";
        }
        if (!fromVal(root, m, err)) return false;
    }

    // keep jump inputs only
    std::vector<MacroInput> jump;
    for (auto const& in : m.inputs)
        if (in.button == 1) jump.push_back(in);
    std::stable_sort(jump.begin(), jump.end(), [](MacroInput const& a, MacroInput const& b) {
        return a.tick < b.tick;
    });
    if (jump.empty()) {
        err = "The macro has no jump inputs.";
        return false;
    }
    m.inputs = std::move(jump);
    m.lastTick = m.inputs.back().tick;
    for (auto const& in : m.inputs) {
        if (in.down) ++m.presses; else ++m.releases;
    }
    if (m.presses == 0) {
        err = "The macro has no press inputs, only " + std::to_string(m.releases) + " releases (wrong format?).";
        return false;
    }
    out = std::move(m);
    return true;
}

bool loadMacroFile(std::filesystem::path const& path, Macro& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "Could not open the file.";
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Macro m;
    m.name = path.filename().string();
    if (!parseMacro(data, m, err)) return false;
    out = std::move(m);
    return true;
}

}  // namespace fw
