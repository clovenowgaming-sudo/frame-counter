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

bool readInputs(Reader r, uint64_t total, uint64_t p1, bool extPerInput,
                std::vector<MacroInput>& out, size_t& endPos) {
    out.clear();
    if (total > r.n || p1 > total) return false;
    int64_t prev = 0;
    for (uint64_t i = 0; i < total; ++i) {
        bool p2 = i >= p1;
        if (i == p1) prev = 0;
        uint8_t b0 = r.u8();
        bool cont = (b0 & 1) != 0;
        bool down = ((b0 >> 1) & 1) != 0;
        uint64_t delta = (b0 >> 2) & 0x3F;
        if (cont) delta |= r.varint() << 6;
        if (extPerInput) {
            uint64_t sz = r.varint();
            r.skip(sz);
        }
        if (r.fail) return false;
        prev += static_cast<int64_t>(delta);
        MacroInput in;
        in.tick = prev;
        in.down = down;
        in.p2 = p2;
        in.button = 1;
        out.push_back(in);
    }
    endPos = r.p;
    return true;
}

bool parseGdr2(std::vector<uint8_t> const& data, Macro& m, std::string& err) {
    Reader r(data.data(), data.size());
    r.skip(3);  // "GDR"
    uint64_t version = r.varint();
    (void)version;
    std::string inputTag = r.cstr();
    std::string author = r.cstr();
    std::string desc = r.cstr();
    uint64_t duration = r.varint();
    uint64_t gameVersion = r.varint();
    uint64_t framerate = r.varint();
    uint64_t seed = r.varint();
    uint64_t coins = r.varint();
    (void)duration; (void)gameVersion; (void)seed; (void)coins; (void)author; (void)desc;
    r.u8();  // ldm
    bool platformer = r.u8() != 0;
    std::string botName = r.cstr();
    r.varint();  // bot version
    r.varint();  // level id
    std::string levelName = r.cstr();
    uint64_t extSize = r.varint();
    r.skip(extSize);
    uint64_t deathCount = r.varint();
    if (r.fail || deathCount > data.size()) {
        err = "GDR2 header is broken (cut short or not a GDR2 file).";
        return false;
    }
    for (uint64_t i = 0; i < deathCount; ++i) r.varint();
    uint64_t total = r.varint();
    uint64_t p1 = r.varint();
    if (r.fail) {
        err = "GDR2 data is cut short before the inputs.";
        return false;
    }

    m.format = "GDR2";
    m.bot = botName;
    m.level = levelName;
    m.tps = framerate > 0 ? static_cast<double>(framerate) : 240.0;
    m.platformer = platformer;
    if (platformer) {
        err = "Platformer macros are not supported.";
        return false;
    }

    // The per-input extension block is only present for bots that use a custom
    // input tag, so try the most likely layout first and fall back to the other.
    bool order[2] = {!inputTag.empty(), inputTag.empty()};
    std::vector<MacroInput> inputs;
    size_t endPos = 0;
    bool ok = false;
    for (bool ext : order) {
        if (readInputs(r, total, p1, ext, inputs, endPos) && endPos == data.size()) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        // last resort: accept a clean parse that leaves a few trailing bytes
        for (bool ext : order) {
            if (readInputs(r, total, p1, ext, inputs, endPos)) { ok = true; break; }
        }
    }
    if (!ok) {
        err = "Could not read the GDR2 inputs (unknown input layout).";
        return false;
    }
    m.inputs = std::move(inputs);
    return true;
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
