#include "Ui.hpp"

#include <algorithm>
#include <cstdio>

#include "Counter.hpp"

namespace fw {

namespace {

constexpr float kPanelW = 440.f;
constexpr float kPanelH = 290.f;

CCLabelBMFont* makeLabel(CCNode* parent, std::string const& text, char const* font, CCPoint pos, float scale,
                         ccColor3B color = ccColor3B{255, 255, 255}, CCPoint anchor = CCPoint{0.5f, 0.5f}) {
    auto l = CCLabelBMFont::create(text.c_str(), font);
    l->setAnchorPoint(anchor);
    l->setPosition(pos);
    l->setScale(scale);
    l->setColor(color);
    parent->addChild(l, 5);
    return l;
}

void fitLabel(CCLabelBMFont* l, float maxWidth, float baseScale) {
    l->setScale(baseScale);
    float w = l->getContentSize().width * baseScale;
    if (w > maxWidth && w > 0.f) l->setScale(baseScale * maxWidth / w);
}

void addPanelBg(CCNode* parent, CCPoint center, float w, float h) {
    auto bg = cocos2d::extension::CCScale9Sprite::create("GJ_square01.png", CCRect(0.f, 0.f, 80.f, 80.f));
    bg->setContentSize(CCSize(w, h));
    bg->setPosition(center);
    parent->addChild(bg, 1);
}

CCMenuItemSpriteExtra* addCloseButton(CCMenu* menu, CCObject* target, SEL_MenuHandler cb, CCPoint pos) {
    auto spr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    auto btn = CCMenuItemSpriteExtra::create(spr, target, cb);
    btn->setPosition(pos);
    menu->addChild(btn);
    return btn;
}

CCMenuItemSpriteExtra* addArrow(CCMenu* menu, CCObject* target, SEL_MenuHandler cb, CCPoint pos, bool flip) {
    auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    spr->setFlipX(flip);
    spr->setScale(.8f);
    auto btn = CCMenuItemSpriteExtra::create(spr, target, cb);
    btn->setPosition(pos);
    menu->addChild(btn);
    return btn;
}

PauseLayer* findPauseLayer() {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return nullptr;
    auto children = scene->getChildren();
    if (!children) return nullptr;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        if (auto p = typeinfo_cast<PauseLayer*>(child)) return p;
    }
    return nullptr;
}

std::string shorten(std::string s, size_t maxLen) {
    if (s.size() > maxLen) s = s.substr(0, maxLen - 3) + "...";
    return s;
}

std::string sizeText(uintmax_t bytes) {
    char buf[32];
    if (bytes >= 1024 * 1024) std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1048576.0);
    else std::snprintf(buf, sizeof(buf), "%u KB", static_cast<unsigned>((bytes + 1023) / 1024));
    return buf;
}

}  // namespace

// ============================================================ MainPanel ====

MainPanel* MainPanel::create() {
    auto ret = new MainPanel();
    if (ret->setup()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool MainPanel::setup() {
    if (!this->initOverlay(150)) return false;

    auto win = CCDirector::get()->getWinSize();
    CCPoint c{win.width / 2.f, win.height / 2.f};

    addPanelBg(this, c, kPanelW, kPanelH);
    makeLabel(this, "Frame Window Counter", "goldFont.fnt", {c.x, c.y + 124.f}, .75f);

    m_info = makeLabel(this, "", "chatFont.fnt", {c.x, c.y + 100.f}, .55f);
    makeLabel(this, "Pick a macro from xdBot, Eclipse or this mod's /macros folder", "chatFont.fnt",
              {c.x, c.y - 86.f}, .4f, ccColor3B{170, 170, 170});

    m_listMenu = CCMenu::create();
    m_listMenu->setPosition({0.f, 0.f});
    m_listMenu->setTouchPriority(-610);
    this->addChild(m_listMenu, 10);

    addCloseButton(m_menu, this, menu_selector(MainPanel::onClose), {c.x + kPanelW / 2.f - 8.f, c.y + kPanelH / 2.f - 8.f});
    addArrow(m_menu, this, menu_selector(MainPanel::onPrev), {c.x - 60.f, c.y - 62.f}, false);
    addArrow(m_menu, this, menu_selector(MainPanel::onNext), {c.x + 60.f, c.y - 62.f}, true);
    m_pageLabel = makeLabel(this, "1/1", "goldFont.fnt", {c.x, c.y - 62.f}, .5f);

    auto refresh = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Refresh", 70, true, "bigFont.fnt", "GJ_button_04.png", 26.f, .5f), this,
        menu_selector(MainPanel::onRefresh));
    refresh->setPosition({c.x - 150.f, c.y - 62.f});
    m_menu->addChild(refresh);

    auto count = CCMenuItemSpriteExtra::create(ButtonSprite::create("Count", 100, true, "goldFont.fnt", "GJ_button_01.png", 34.f, .8f),
                                               this, menu_selector(MainPanel::onCount));
    count->setPosition({c.x - 70.f, c.y - 114.f});
    m_menu->addChild(count);

    auto results = CCMenuItemSpriteExtra::create(ButtonSprite::create("Results", 100, true, "goldFont.fnt", "GJ_button_02.png", 34.f, .8f),
                                                 this, menu_selector(MainPanel::onResults));
    results->setPosition({c.x + 70.f, c.y - 114.f});
    m_menu->addChild(results);

    m_files = listMacroFiles();
    this->rebuildList();
    this->refreshInfo();
    return true;
}

void MainPanel::setInfo(std::string const& text, ccColor3B color) {
    m_info->setString(text.c_str());
    m_info->setColor(color);
    fitLabel(m_info, kPanelW - 30.f, .55f);
}

void MainPanel::refreshInfo() {
    auto& ctr = Counter::get();
    if (ctr.hasMacro()) {
        auto const& m = ctr.macro();
        char buf[200];
        std::snprintf(buf, sizeof(buf), "%s  |  %d inputs  |  %.0f TPS  |  %s", shorten(m.name, 28).c_str(),
                      static_cast<int>(m.inputs.size()), m.tps, m.format.c_str());
        this->setInfo(buf, ccColor3B{120, 255, 120});
    } else if (m_files.empty()) {
        this->setInfo("No macros found. Record one with xdBot/Eclipse first.", ccColor3B{255, 200, 90});
    } else {
        this->setInfo("Tap a macro below to load it", ccColor3B{255, 255, 255});
    }
    if (Loader::get()->isModLoaded("syzzi.click_between_frames")) {
        m_info->setString("Click Between Frames is loaded: turn it off for exact counts");
        m_info->setColor(ccColor3B{255, 200, 90});
        fitLabel(m_info, kPanelW - 30.f, .55f);
    }
}

void MainPanel::rebuildList() {
    constexpr size_t perPage = 6;
    m_listMenu->removeAllChildren();

    size_t pages = std::max<size_t>(1, (m_files.size() + perPage - 1) / perPage);
    if (m_page >= pages) m_page = pages - 1;

    auto win = CCDirector::get()->getWinSize();
    CCPoint c{win.width / 2.f, win.height / 2.f};

    for (size_t i = 0; i < perPage; ++i) {
        size_t gi = m_page * perPage + i;
        if (gi >= m_files.size()) break;
        auto const& f = m_files[gi];
        std::string text = "[" + f.source + "] " + shorten(f.label, 40) + "  |  " + sizeText(f.size);
        bool selected = static_cast<int>(gi) == m_selected;
        auto spr = ButtonSprite::create(text.c_str(), 390, true, "chatFont.fnt",
                                        selected ? "GJ_button_01.png" : "GJ_button_04.png", 24.f, .7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(MainPanel::onFile));
        btn->setTag(static_cast<int>(gi));
        btn->setPosition({c.x, c.y + 72.f - static_cast<float>(i) * 22.f});
        m_listMenu->addChild(btn);
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d/%d", static_cast<int>(m_page + 1), static_cast<int>(pages));
    m_pageLabel->setString(buf);
}

void MainPanel::onFile(CCObject* sender) {
    int gi = static_cast<CCNode*>(sender)->getTag();
    if (gi < 0 || static_cast<size_t>(gi) >= m_files.size()) return;
    m_selected = gi;
    std::string err;
    if (!Counter::get().loadMacro(m_files[gi].path, err)) {
        this->setInfo(shorten(m_files[gi].label, 30) + ": " + err, ccColor3B{255, 100, 100});
    } else {
        this->refreshInfo();
    }
    this->rebuildList();
}

void MainPanel::onPrev(CCObject*) {
    if (m_page > 0) --m_page;
    this->rebuildList();
}

void MainPanel::onNext(CCObject*) {
    ++m_page;  // clamped in rebuildList
    this->rebuildList();
}

void MainPanel::onRefresh(CCObject*) {
    m_files = listMacroFiles();
    m_selected = -1;
    this->rebuildList();
    this->refreshInfo();
}

void MainPanel::onCount(CCObject*) {
    auto pl = PlayLayer::get();
    std::string err;
    if (!Counter::get().start(pl, err)) {
        this->setInfo(err, ccColor3B{255, 100, 100});
        return;
    }
    auto pause = findPauseLayer();
    this->close();  // `this` must not be touched after this line
    if (pause) pause->onResume(nullptr);
    pl->setVisible(false);
    CountingLayer::create()->show();
}

void MainPanel::onResults(CCObject*) {
    if (!Counter::get().hasResults()) {
        this->setInfo("No results yet. Press Count first.", ccColor3B{255, 200, 90});
        return;
    }
    ResultsPanel::create()->show();
}

// ========================================================= CountingLayer ====

CountingLayer* CountingLayer::create() {
    auto ret = new CountingLayer();
    if (ret->setup()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CountingLayer::setup() {
    if (!this->initOverlay(255, ccColor3B{10, 10, 18})) return false;

    auto win = CCDirector::get()->getWinSize();
    CCPoint c{win.width / 2.f, win.height / 2.f};

    makeLabel(this, "Frame Window Counter", "goldFont.fnt", {c.x, c.y + 80.f}, .8f);
    m_l1 = makeLabel(this, "", "bigFont.fnt", {c.x, c.y + 30.f}, .55f);
    m_l2 = makeLabel(this, "", "chatFont.fnt", {c.x, c.y}, .7f);
    m_l3 = makeLabel(this, "", "chatFont.fnt", {c.x, c.y - 24.f}, .6f, ccColor3B{180, 180, 180});
    makeLabel(this, "The level is hidden and restarted many times while counting.", "chatFont.fnt",
              {c.x, c.y - 50.f}, .45f, ccColor3B{120, 120, 120});

    auto cancel = CCMenuItemSpriteExtra::create(ButtonSprite::create("Cancel", 110, true, "goldFont.fnt", "GJ_button_06.png", 34.f, .8f),
                                                this, menu_selector(CountingLayer::onCancel));
    cancel->setPosition({c.x, c.y - 100.f});
    m_menu->addChild(cancel);

    this->scheduleUpdate();
    return true;
}

void CountingLayer::update(float) {
    if (m_finished) return;
    auto& ctr = Counter::get();
    ctr.pump();
    if (!ctr.running()) {
        this->finish();
        return;
    }
    m_l1->setString(ctr.statusLine1().c_str());
    m_l2->setString(ctr.statusLine2().c_str());
    m_l3->setString(ctr.statusLine3().c_str());
}

void CountingLayer::onCancel(CCObject*) {
    Counter::get().cancel();
    this->finish();
}

void CountingLayer::finish() {
    if (m_finished) return;
    m_finished = true;
    this->unscheduleUpdate();

    auto& ctr = Counter::get();
    auto stage = ctr.stage();
    std::string msg = ctr.message();
    this->close();  // `this` must not be touched after this line

    if (stage == Stage::Finished) {
        ResultsPanel::create()->show();
    } else if (stage == Stage::Failed) {
        FLAlertLayer::create("Counting failed", msg, "OK")->show();
    }
}

// ========================================================= ResultsPanel ====

ResultsPanel* ResultsPanel::create() {
    auto ret = new ResultsPanel();
    if (ret->setup()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ResultsPanel::setup() {
    if (!this->initOverlay(150)) return false;

    auto& ctr = Counter::get();
    auto const& macro = ctr.resultMacro();
    auto sum = ctr.summary();
    double tps = ctr.resultTps();

    auto win = CCDirector::get()->getWinSize();
    CCPoint c{win.width / 2.f, win.height / 2.f};

    addPanelBg(this, c, kPanelW, kPanelH);
    makeLabel(this, "Window Results", "goldFont.fnt", {c.x, c.y + 124.f}, .75f);

    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s  |  %.0f TPS  |  %d inputs counted%s", shorten(macro.name, 26).c_str(), tps,
                  static_cast<int>(sum.counted), sum.skipped > 0 ? " (some skipped)" : "");
    auto l1 = makeLabel(this, buf, "chatFont.fnt", {c.x, c.y + 102.f}, .5f);
    fitLabel(l1, kPanelW - 30.f, .5f);

    if (sum.tightest > 0 && sum.tightestResult < ctr.results().size()) {
        auto const& tr = ctr.results()[sum.tightestResult];
        auto const& in = macro.inputs[tr.idx];
        std::snprintf(buf, sizeof(buf), "Tightest: %d tick%s = %s FP = %.2f ms  (input #%d, tick %lld)", sum.tightest,
                      sum.tightest == 1 ? "" : "s", fpText(tps, sum.tightest).c_str(), sum.tightest * 1000.0 / tps,
                      static_cast<int>(tr.idx + 1), static_cast<long long>(in.tick));
        auto l2 = makeLabel(this, buf, "goldFont.fnt", {c.x, c.y + 86.f}, .5f);
        fitLabel(l2, kPanelW - 30.f, .5f);
    }

    std::string hist;
    int shown = 0;
    for (auto const& [w, count] : sum.histogram) {
        if (shown++ >= 7) { hist += "..."; break; }
        hist += std::to_string(w) + "f(" + fpText(tps, w) + "FP):" + std::to_string(count) + "  ";
    }
    auto l3 = makeLabel(this, hist, "chatFont.fnt", {c.x, c.y + 70.f}, .45f, ccColor3B{180, 220, 255});
    fitLabel(l3, kPanelW - 30.f, .45f);

    m_rows = CCNode::create();
    this->addChild(m_rows, 5);

    addCloseButton(m_menu, this, menu_selector(ResultsPanel::onClose), {c.x + kPanelW / 2.f - 8.f, c.y + kPanelH / 2.f - 8.f});
    addArrow(m_menu, this, menu_selector(ResultsPanel::onPrev), {c.x - 60.f, c.y - 84.f}, false);
    addArrow(m_menu, this, menu_selector(ResultsPanel::onNext), {c.x + 60.f, c.y - 84.f}, true);
    m_pageLabel = makeLabel(this, "1/1", "goldFont.fnt", {c.x, c.y - 84.f}, .5f);

    auto sort = CCMenuItemSpriteExtra::create(ButtonSprite::create("Sort", 90, true, "goldFont.fnt", "GJ_button_04.png", 30.f, .7f),
                                              this, menu_selector(ResultsPanel::onSort));
    sort->setPosition({c.x - 90.f, c.y - 122.f});
    m_menu->addChild(sort);

    auto exp = CCMenuItemSpriteExtra::create(ButtonSprite::create("Export", 90, true, "goldFont.fnt", "GJ_button_01.png", 30.f, .7f),
                                             this, menu_selector(ResultsPanel::onExport));
    exp->setPosition({c.x + 90.f, c.y - 122.f});
    m_menu->addChild(exp);

    m_note = makeLabel(this, "", "chatFont.fnt", {c.x, c.y - 104.f}, .4f, ccColor3B{170, 170, 170});

    this->rebuildOrder();
    this->rebuildRows();
    return true;
}

void ResultsPanel::rebuildOrder() {
    auto const& res = Counter::get().results();
    m_order.resize(res.size());
    for (size_t i = 0; i < res.size(); ++i) m_order[i] = i;
    if (m_sort == 1) {
        std::stable_sort(m_order.begin(), m_order.end(), [&](size_t a, size_t b) {
            return res[a].window() < res[b].window();
        });
    }
}

void ResultsPanel::rebuildRows() {
    constexpr size_t perPage = 9;
    auto& ctr = Counter::get();
    auto const& res = ctr.results();
    auto const& macro = ctr.resultMacro();
    double tps = ctr.resultTps();

    m_rows->removeAllChildren();

    size_t pages = std::max<size_t>(1, (res.size() + perPage - 1) / perPage);
    if (m_page >= pages) m_page = pages - 1;

    auto win = CCDirector::get()->getWinSize();
    CCPoint c{win.width / 2.f, win.height / 2.f};

    for (size_t i = 0; i < perPage; ++i) {
        size_t oi = m_page * perPage + i;
        if (oi >= m_order.size()) break;
        auto const& r = res[m_order[oi]];
        auto const& in = macro.inputs[r.idx];
        int w = r.window();
        bool limited = r.limLow || r.limHigh;

        char buf[160];
        std::snprintf(buf, sizeof(buf), "#%d   tick %lld   %s%s   %d%s f  =  %s FP   (%+d..%+d)", static_cast<int>(r.idx + 1),
                      static_cast<long long>(in.tick), in.down ? "press" : "release", in.p2 ? " P2" : "", w,
                      limited ? "+" : "", fpText(tps, w).c_str(), r.low, r.high);

        ccColor3B col{120, 255, 120};
        if (w <= 1) col = ccColor3B{255, 90, 90};
        else if (w == 2) col = ccColor3B{255, 160, 60};
        else if (w <= 4) col = ccColor3B{255, 230, 90};

        auto l = makeLabel(m_rows, buf, "chatFont.fnt", {c.x - 200.f, c.y + 48.f - static_cast<float>(i) * 15.f}, .5f, col,
                           CCPoint{0.f, 0.5f});
        fitLabel(l, 400.f, .5f);
    }

    char pb[32];
    std::snprintf(pb, sizeof(pb), "%d/%d", static_cast<int>(m_page + 1), static_cast<int>(pages));
    m_pageLabel->setString(pb);
}

void ResultsPanel::onPrev(CCObject*) {
    if (m_page > 0) --m_page;
    this->rebuildRows();
}

void ResultsPanel::onNext(CCObject*) {
    ++m_page;  // clamped in rebuildRows
    this->rebuildRows();
}

void ResultsPanel::onSort(CCObject*) {
    m_sort = 1 - m_sort;
    m_page = 0;
    this->rebuildOrder();
    this->rebuildRows();
    m_note->setString(m_sort == 1 ? "Sorted: tightest first" : "Sorted: input order");
}

void ResultsPanel::onExport(CCObject*) {
    std::string path;
    if (Counter::get().exportResults(path)) {
        m_note->setString(("Saved: " + path).c_str());
        fitLabel(m_note, kPanelW - 30.f, .4f);
    } else {
        m_note->setString("Could not save the file.");
    }
}

}  // namespace fw
