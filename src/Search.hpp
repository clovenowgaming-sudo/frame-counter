#pragma once
// Pure C++ search for the window of one input.
// Shifts are measured in ticks relative to where the input was recorded.
// The unshifted input (shift 0) is assumed to survive.
namespace fw {

struct WindowSearch {
    int low = 0;        // lowest shift that survived (<= 0)
    int high = 0;       // highest shift that survived (>= 0)
    int minShift = 0;   // lowest shift we are allowed to try
    int maxShift = 0;   // highest shift we are allowed to try
    int dir = 1;        // +1 = searching later, -1 = searching earlier
    bool limLow = false;   // stopped by a limit, not by a death
    bool limHigh = false;
    bool done = false;
    int cur = 0;        // shift of the trial to run

    void begin(int minS, int maxS) {
        low = high = 0;
        minShift = minS;
        maxShift = maxS;
        dir = 1;
        limLow = limHigh = false;
        done = false;
        cur = 0;
    }

    // Picks the next shift to try. Returns false when the search is finished.
    bool next() {
        if (done) return false;
        for (;;) {
            if (dir > 0) {
                int n = high + 1;
                if (n > maxShift) { limHigh = true; dir = -1; continue; }
                cur = n;
                return true;
            }
            int n = low - 1;
            if (n < minShift) { limLow = true; done = true; return false; }
            cur = n;
            return true;
        }
    }

    // Reports whether the trial at `cur` survived.
    void report(bool survived) {
        if (survived) {
            if (dir > 0) high = cur; else low = cur;
        } else {
            if (dir > 0) dir = -1; else done = true;
        }
    }

    int window() const { return high - low + 1; }
};

}  // namespace fw
