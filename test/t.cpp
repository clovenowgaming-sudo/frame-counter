#include "../src/Macro.hpp"
#include "../src/Search.hpp"
#include <cstdio>
#include <cassert>
#include <iostream>
using namespace fw;
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        Macro m; std::string err;
        if (!loadMacroFile(argv[i], m, err)) { printf("%s: ERR %s\n", argv[i], err.c_str()); continue; }
        printf("%s: %s tps=%.0f inputs=%zu last=%lld bot=%s lvl=%s | ", argv[i], m.format.c_str(), m.tps, m.inputs.size(), (long long)m.lastTick, m.bot.c_str(), m.level.c_str());
        for (size_t k = 0; k < m.inputs.size() && k < 8; ++k) printf("[%lld %s%s] ", (long long)m.inputs[k].tick, m.inputs[k].down?"D":"U", m.inputs[k].p2?"2":"");
        printf("\n");
    }
    // search sim: window [-a, +b]
    for (int a = 0; a <= 5; ++a) for (int b = 0; b <= 5; ++b) for (int minS : {-3, -10}) for (int maxS : {0, 2, 10}) {
        WindowSearch s; s.begin(minS, maxS);
        int trials = 0;
        while (s.next()) { ++trials; bool ok = (s.cur >= -a && s.cur <= b); s.report(ok); assert(trials < 100); }
        int expLow = std::max(-a, minS), expHigh = std::min(b, maxS);
        assert(s.low == expLow && s.high == expHigh);
    }
    puts("search ok");
}
