#pragma once
#include <Geode/Geode.hpp>

#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "Macro.hpp"
#include "Search.hpp"

namespace fw {

enum class Stage { Idle, Calibrate, Counting, Finished, Failed, Cancelled };

struct WindowResult {
    size_t idx = 0;          // index into Macro::inputs
    int low = 0;             // lowest shift that survived (<= 0)
    int high = 0;            // highest shift that survived (>= 0)
    bool limLow = false;     // search stopped by a limit instead of a death
    bool limHigh = false;
    int window() const { return high - low + 1; }
};

struct Summary {
    size_t counted = 0;
    size_t skipped = 0;
    int tightest = 0;            // smallest window in ticks (0 = none)
    size_t tightestResult = 0;   // index into results()
    std::map<int, int> histogram;  // window ticks -> amount of inputs
};

std::string fpText(double tps, int windowTicks);

class Counter {
public:
    static Counter& get();

    // ---- macro
    bool loadMacro(std::filesystem::path const& path, std::string& err);
    bool hasMacro() const { return m_hasMacro; }
    Macro const& macro() const { return m_macro; }

    // ---- lifecycle
    bool start(PlayLayer* pl, std::string& err);
    void cancel();
    void pump();  // call every frame while running()
    bool running() const { return m_stage == Stage::Calibrate || m_stage == Stage::Counting; }
    Stage stage() const { return m_stage; }
    std::string const& message() const { return m_message; }

    // ---- results
    bool hasResults() const { return m_hasResults; }
    std::vector<WindowResult> const& results() const { return m_results; }
    Summary summary() const;
    double resultTps() const { return m_resultTps; }
    Macro const& resultMacro() const { return m_resultMacro; }  // the macro the results belong to
    bool exportResults(std::string& pathOut);

    // ---- progress (for the counting screen)
    std::string statusLine1() const;
    std::string statusLine2() const;
    std::string statusLine3() const;

    // ---- hooks
    bool ownsLayer(cocos2d::CCNode* node) const { return m_pl != nullptr && static_cast<cocos2d::CCNode*>(m_pl) == node; }
    bool inPump() const { return m_inPump; }
    void beforeStep(GJBaseGameLayer* gj);
    void afterStep();
    bool onDeath();
    bool onComplete();

private:
    Counter() = default;

    void beginNextTrial();
    void checkTrial();
    void endTrial(bool survived);
    void endCalibrationTrial(bool survived);
    void finishCalibration();
    void nextInput();
    void setupInput(size_t idx);
    void storeResult();
    void conclude(Stage s, std::string msg);
    void cleanup();

    // macro
    Macro m_macro;
    bool m_hasMacro = false;

    // run state
    geode::Ref<PlayLayer> m_plRef;
    PlayLayer* m_pl = nullptr;
    Stage m_stage = Stage::Idle;
    std::string m_message;
    bool m_inPump = false;

    // settings snapshot
    int m_survive = 240;
    int m_maxSearch = 30;
    bool m_countReleases = false;

    // calibration
    std::vector<int> m_calOffsets{0, 1, -1, 2, -2};
    size_t m_calIdx = 0;
    int m_calBestOffset = 0;
    int64_t m_calBestDeath = -1;
    int m_cal = 0;
    int64_t m_baseDeath = INT64_MAX;

    // counting
    std::vector<size_t> m_todo;
    size_t m_todoPos = 0;
    size_t m_eligible = 0;
    WindowSearch m_search;
    std::vector<WindowResult> m_results;

    // current trial
    bool m_trialLive = false;
    std::vector<MacroInput> m_trial;
    size_t m_pos = 0;
    int m_trialCal = 0;
    int64_t m_tick = 0;
    int64_t m_endTick = 0;
    bool m_died = false;
    bool m_complete = false;
    int64_t m_deathTick = -1;
    int m_stall = 0;
    int m_trialCount = 0;

    // results kept after the run
    bool m_hasResults = false;
    double m_resultTps = 240.0;
    std::string m_resultName;
    Macro m_resultMacro;
    size_t m_resultSkipped = 0;
    int64_t m_resultBaseDeath = INT64_MAX;
    int m_resultSurvive = 240;

    std::chrono::steady_clock::time_point m_startTime;
};

}  // namespace fw
