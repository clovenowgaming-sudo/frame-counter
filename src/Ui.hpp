#pragma once
#include <Geode/Geode.hpp>

#include <string>
#include <vector>

#include "Files.hpp"

namespace fw {

using namespace geode::prelude;

// Full-screen dim layer that swallows touches. Panels are built on top of it
// with plain cocos nodes so they do not depend on the Popup API of a specific
// Geode version.
class Overlay : public CCLayerColor {
protected:
    CCMenu* m_menu = nullptr;

    bool initOverlay(GLubyte opacity, ccColor3B color = ccColor3B{0, 0, 0}) {
        if (!CCLayerColor::initWithColor(ccColor4B{color.r, color.g, color.b, opacity})) return false;
        this->setTouchEnabled(true);
        this->setKeypadEnabled(true);
        m_menu = CCMenu::create();
        m_menu->setPosition({0.f, 0.f});
        m_menu->setTouchPriority(-610);
        this->addChild(m_menu, 10);
        return true;
    }

    void registerWithTouchDispatcher() override {
        CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -600, true);
    }
    bool ccTouchBegan(CCTouch*, CCEvent*) override { return true; }
    void keyBackClicked() override { this->onBack(); }
    virtual void onBack() { this->close(); }

public:
    void show() {
        if (auto scene = CCDirector::get()->getRunningScene()) scene->addChild(this, 10000);
    }
    void close() {
        this->retain();
        this->removeFromParentAndCleanup(true);
        this->autorelease();  // stay alive until the end of the frame
    }
};

class MainPanel : public Overlay {
    std::vector<MacroFile> m_files;
    size_t m_page = 0;
    int m_selected = -1;
    CCLabelBMFont* m_info = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCMenu* m_listMenu = nullptr;

    bool setup();
    void rebuildList();
    void setInfo(std::string const& text, ccColor3B color);
    void refreshInfo();

    void onFile(CCObject* sender);
    void onPrev(CCObject*);
    void onNext(CCObject*);
    void onRefresh(CCObject*);
    void onCount(CCObject*);
    void onResults(CCObject*);
    void onClose(CCObject*) { this->close(); }

public:
    static MainPanel* create();
};

class CountingLayer : public Overlay {
    CCLabelBMFont* m_l1 = nullptr;
    CCLabelBMFont* m_l2 = nullptr;
    CCLabelBMFont* m_l3 = nullptr;
    bool m_finished = false;

    bool setup();
    void update(float dt) override;
    void finish();
    void onCancel(CCObject*);
    void onBack() override { this->onCancel(nullptr); }

public:
    static CountingLayer* create();
};

class ResultsPanel : public Overlay {
    std::vector<size_t> m_order;
    size_t m_page = 0;
    int m_sort = 0;  // 0 = input order, 1 = tightest first
    CCNode* m_rows = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCLabelBMFont* m_note = nullptr;

    bool setup();
    void rebuildOrder();
    void rebuildRows();

    void onPrev(CCObject*);
    void onNext(CCObject*);
    void onSort(CCObject*);
    void onExport(CCObject*);
    void onClose(CCObject*) { this->close(); }

public:
    static ResultsPanel* create();
};

}  // namespace fw
