#include "Counter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

using namespace geode::prelude;

namespace fw {

std::string fpText(double tps, int windowTicks) {
    if (windowTicks <= 0) return "-";
    double fp = tps / static_cast<double>(windowTicks);
    char buf[32];
    if (std::fabs(fp - std::round(fp)) < 0.05) std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(fp)));
    else std::snprintf(buf, sizeof(buf), "%.1f", fp);
    return buf;
}

static std::string fmtTime(double seconds) {
    if (seconds < 0 || seconds > 359999) return "--:--";
    int s = static_cast<int>(seconds);
    char buf[32];
    if (s >= 3600) std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

Counter& Counter::get() {
    static Counter inst;
    return inst;
}

// ------------------------------------------------------------------ macro --

bool Counter::loadMacro(std::filesystem::path const& path, std::string& err) {
    Macro m;
    if (!loadMacroFile(path, m, err)) return false;
    m_macro = std::move(m);
    m_hasMacro = true;
    return true;
}

// -------------------------------------------------------------- lifecycle --

bool Counter::start(PlayLayer* pl, std::string& err) {
    if (running()) {
        err = "A count is already running.";
        return false;
    }
    if (!pl) {
        err = "Open this from the pause menu inside a level.";
        return false;
    }
    if (!m_hasMacro) {
        err = "Pick a macro first.";
        return false;
    }
    if (m_macro.platformer) {
        err = "Platformer macros are not supported.";
        return false;
    }
    if (std::fabs(m_macro.tps - 240.0) > 0.5) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "This macro was recorded at %.0f TPS. Only 240 TPS macros can be counted right now.", m_macro.tps);
        err = buf;
        return false;
    }
    if (m_macro.inputs.empty()) {
        err = "The macro has no inputs.";
        return false;
    }
    if (pl->m_isPracticeMode) {
        err = "Leave practice mode first, the count restarts the level from the beginning.";
        return false;
    }

    auto* mod = Mod::get();
    m_survive = static_cast<int>(std::clamp<int64_t>(mod->getSettingValue<int64_t>("survive-ticks"), 30, 2000));
    m_maxSearch = static_cast<int>(std::clamp<int64_t>(mod->getSettingValue<int64_t>("max-search"), 1, 240));
    m_countReleases = mod->getSettingValue<bool>("count-releases");

    m_pl = pl;
    m_plRef = pl;
    m_results.clear();
    m_hasResults = false;
    m_message.clear();
    m_todo.clear();
    m_todoPos = 0;
    m_eligible = 0;
    m_calIdx = 0;
    m_calBestOffset = 0;
    m_calBestDeath = -1;
    m_cal = 0;
    m_baseDeath = INT64_MAX;
    m_trialLive = false;
    m_trialCount = 0;
    m_stage = Stage::Calibrate;
    m_startTime = std::chrono::steady_clock::now();
    return true;
}

void Counter::cancel() {
    if (running()) conclude(Stage::Cancelled, "Cancelled.");
}

void Counter::conclude(Stage s, std::string msg) {
    m_stage = s;
    m_message = std::move(msg);
    m_trialLive = false;
    if (s == Stage::Finished) {
        m_hasResults = true;
        m_resultTps = m_macro.tps;
        m_resultName = m_macro.name;
        m_resultMacro = m_macro;
        m_resultSkipped = m_eligible > m_todo.size() ? m_eligible - m_todo.size() : 0;
        m_resultBaseDeath = m_baseDeath;
        m_resultSurvive = m_survive;
    }
    this->cleanup();
}

void Counter::cleanup() {
    if (m_pl) {
        PlayLayer* pl = m_pl;
        m_pl = nullptr;  // hooks stop treating this layer as ours
        pl->setVisible(true);
        pl->resetLevel();
        pl->pauseGame(false);
    }
    m_plRef = geode::Ref<PlayLayer>();
}

// ------------------------------------------------------------------- pump --

void Counter::pump() {
    if (!running()) return;
    if (!m_pl) {
        conclude(Stage::Failed, "The level was closed.");
        return;
    }
    auto t0 = std::chrono::steady_clock::now();
    while (running()) {
        if (!m_trialLive) {
            this->beginNextTrial();
            if (!running()) break;
        }

        int64_t before = m_tick;
        m_inPump = true;
        m_pl->update(1.f / static_cast<float>(m_macro.tps));
        m_inPump = false;

        if (m_tick == before) {
            if (++m_stall > 6000) {
                conclude(Stage::Failed, "The level is not advancing (stuck after restart). Try again from a fresh attempt.");
                break;
            }
        } else {
            m_stall = 0;
        }
        this->checkTrial();

        auto el = std::chrono::steady_clock::now() - t0;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(el).count() > 12) break;
    }
}

void Counter::beginNextTrial() {
    m_trial = m_macro.inputs;
    if (m_stage == Stage::Calibrate) {
        m_trialCal = m_calOffsets[m_calIdx];
        m_endTick = m_macro.lastTick + m_trialCal + m_survive;
    } else {
        size_t idx = m_todo[m_todoPos];
        m_trialCal = m_cal;
        int64_t t = std::max<int64_t>(0, m_trial[idx].tick + m_search.cur);
        m_trial[idx].tick = t;
        std::stable_sort(m_trial.begin(), m_trial.end(), [](MacroInput const& a, MacroInput const& b) {
            return a.tick < b.tick;
        });
        m_endTick = t + m_cal + m_survive;
    }
    m_pos = 0;
    m_tick = 0;
    m_died = false;
    m_complete = false;
    m_deathTick = -1;
    m_stall = 0;
    m_trialLive = true;
    ++m_trialCount;
    m_pl->resetLevel();
    m_tick = 0;
}

void Counter::checkTrial() {
    if (!m_trialLive) return;
    if (m_died) {
        this->endTrial(false);
    } else if (m_complete || m_tick >= m_endTick) {
        this->endTrial(true);
    }
}

void Counter::endTrial(bool survived) {
    m_trialLive = false;
    if (m_stage == Stage::Calibrate) {
        this->endCalibrationTrial(survived);
        return;
    }
    m_search.report(survived);
    if (m_search.next()) return;
    this->storeResult();
    ++m_todoPos;
    this->nextInput();
}

void Counter::endCalibrationTrial(bool survived) {
    int64_t death = survived ? INT64_MAX : m_deathTick;
    if (death > m_calBestDeath) {
        m_calBestDeath = death;
        m_calBestOffset = m_calOffsets[m_calIdx];
    }
    ++m_calIdx;
    if (survived || m_calIdx >= m_calOffsets.size()) this->finishCalibration();
}

void Counter::finishCalibration() {
    m_cal = m_calBestOffset;
    m_baseDeath = m_calBestDeath;

    m_todo.clear();
    m_eligible = 0;
    for (size_t i = 0; i < m_macro.inputs.size(); ++i) {
        auto const& in = m_macro.inputs[i];
        if (!m_countReleases && !in.down) continue;
        ++m_eligible;
        if (m_baseDeath != INT64_MAX && in.tick + m_maxSearch + m_survive + 2 >= m_baseDeath) continue;
        m_todo.push_back(i);
    }
    if (m_todo.empty()) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "The macro dies at tick %lld when played normally, so there is nothing to count. "
            "Check that it is the right level and that the macro has no frame fixes or physics bypass.",
            static_cast<long long>(m_baseDeath));
        this->conclude(Stage::Failed, buf);
        return;
    }
    m_stage = Stage::Counting;
    m_todoPos = 0;
    this->nextInput();
}

void Counter::nextInput() {
    while (m_todoPos < m_todo.size()) {
        this->setupInput(m_todo[m_todoPos]);
        if (m_search.next()) return;  // a trial is needed
        this->storeResult();          // window of 1 tick, nothing to search
        ++m_todoPos;
    }
    this->conclude(Stage::Finished, "Done.");
}

void Counter::setupInput(size_t idx) {
    auto const& in = m_macro.inputs[idx];
    int64_t t = in.tick;
    int64_t prevT = -1;
    int64_t nextT = 0;
    bool hasNext = false;
    for (size_t j = idx; j-- > 0;) {
        if (m_macro.inputs[j].p2 == in.p2) { prevT = m_macro.inputs[j].tick; break; }
    }
    for (size_t j = idx + 1; j < m_macro.inputs.size(); ++j) {
        if (m_macro.inputs[j].p2 == in.p2) { nextT = m_macro.inputs[j].tick; hasNext = true; break; }
    }
    int64_t lo = prevT + 1 - t;
    int64_t hi = hasNext ? nextT - 1 - t : m_maxSearch;
    int minS = static_cast<int>(std::clamp<int64_t>(lo, -m_maxSearch, 0));
    int maxS = static_cast<int>(std::clamp<int64_t>(hi, 0, m_maxSearch));
    m_search.begin(minS, maxS);
}

void Counter::storeResult() {
    WindowResult r;
    r.idx = m_todo[m_todoPos];
    r.low = m_search.low;
    r.high = m_search.high;
    r.limLow = m_search.limLow;
    r.limHigh = m_search.limHigh;
    m_results.push_back(r);
}

// ------------------------------------------------------------------ hooks --

void Counter::beforeStep(GJBaseGameLayer* gj) {
    if (!m_trialLive) return;
    while (m_pos < m_trial.size() && m_trial[m_pos].tick + m_trialCal <= m_tick) {
        auto const& in = m_trial[m_pos++];
        gj->handleButton(in.down, in.button, !in.p2);
    }
}

void Counter::afterStep() {
    if (m_trialLive) ++m_tick;
}

bool Counter::onDeath() {
    if (!running()) return false;
    if (m_trialLive && !m_died) {
        m_died = true;
        m_deathTick = m_tick;
    }
    return true;
}

bool Counter::onComplete() {
    if (!running()) return false;
    if (m_trialLive) m_complete = true;
    return true;
}

// ---------------------------------------------------------------- results --

Summary Counter::summary() const {
    Summary s;
    s.counted = m_results.size();
    s.skipped = m_resultSkipped;
    for (size_t i = 0; i < m_results.size(); ++i) {
        int w = m_results[i].window();
        s.histogram[w] += 1;
        if (s.tightest == 0 || w < s.tightest) {
            s.tightest = w;
            s.tightestResult = i;
        }
    }
    return s;
}

bool Counter::exportResults(std::string& pathOut) {
    if (!m_hasResults) return false;
    auto dir = Mod::get()->getSaveDir() / "results";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto stem = std::filesystem::path(m_resultName).stem().string();
    if (stem.empty()) stem = "macro";
    auto file = dir / (stem + "_windows.txt");

    std::ofstream out(file);
    if (!out) return false;

    auto s = this->summary();
    out << "Frame Window Counter\n";
    out << "Macro: " << m_resultName << "\n";
    out << "TPS: " << static_cast<int>(m_resultTps) << "  |  survive " << m_resultSurvive << " ticks after the input\n";
    out << "Counted inputs: " << s.counted << "  |  skipped: " << s.skipped << "\n";
    if (s.tightest > 0) {
        char ms[32];
        std::snprintf(ms, sizeof(ms), "%.2f", s.tightest * 1000.0 / m_resultTps);
        out << "Tightest window: " << s.tightest << " tick(s) = " << fpText(m_resultTps, s.tightest) << " FP = " << ms << " ms\n";
    }
    out << "\nWindow histogram (ticks -> inputs, FP):\n";
    for (auto const& [w, c] : s.histogram) {
        out << "  " << w << " tick(s) = " << fpText(m_resultTps, w) << " FP : " << c << "\n";
    }
    out << "\nidx,tick,type,player,window_ticks,fp,low,high,limited\n";
    for (auto const& r : m_results) {
        auto const& in = m_resultMacro.inputs[r.idx];
        out << r.idx << "," << in.tick << "," << (in.down ? "press" : "release") << "," << (in.p2 ? "P2" : "P1") << ","
            << r.window() << "," << fpText(m_resultTps, r.window()) << "," << r.low << "," << r.high << ","
            << ((r.limLow || r.limHigh) ? "yes" : "no") << "\n";
    }
    pathOut = file.string();
    return true;
}

std::string Counter::statusLine1() const {
    if (m_stage == Stage::Calibrate) return "Checking the macro plays through...";
    return "Counting windows...";
}

std::string Counter::statusLine2() const {
    char buf[200];
    if (m_stage == Stage::Calibrate) {
        std::snprintf(buf, sizeof(buf), "Test run %d of up to %d", static_cast<int>(m_calIdx) + 1, static_cast<int>(m_calOffsets.size()));
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "Input %d / %d   (tick %lld, shift %+d)",
        static_cast<int>(std::min(m_todoPos + 1, m_todo.size())), static_cast<int>(m_todo.size()),
        m_todoPos < m_todo.size() ? static_cast<long long>(m_macro.inputs[m_todo[m_todoPos]].tick) : 0LL,
        m_search.cur);
    return buf;
}

std::string Counter::statusLine3() const {
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_startTime).count();
    std::string s = "Elapsed " + fmtTime(el);
    if (m_stage == Stage::Counting && m_todoPos > 0 && m_todoPos < m_todo.size()) {
        double eta = el / static_cast<double>(m_todoPos) * static_cast<double>(m_todo.size() - m_todoPos);
        s += "   |   ETA ~" + fmtTime(eta);
    }
    return s;
}

}  // namespace fw
