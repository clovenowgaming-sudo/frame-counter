#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Counter.hpp"
#include "Ui.hpp"

using namespace geode::prelude;

// Drives the replay: injects the (shifted) inputs on every physics tick and
// stops the normal frame loop while we step the level ourselves.
class $modify(FWGameLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto& c = fw::Counter::get();
        // half ticks are not full physics ticks, only count/inject on full ones
        bool mine = !isHalfTick && c.running() && c.ownsLayer(this);
        if (mine) c.beforeStep(this);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (mine) c.afterStep();
    }

    void update(float dt) {
        auto& c = fw::Counter::get();
        if (c.running() && c.ownsLayer(this) && !c.inPump()) return;
        GJBaseGameLayer::update(dt);
    }
};

// Deaths and level completion are only recorded while counting, they never
// play out (no death animation, no end screen).
class $modify(FWPlayLayer, PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& c = fw::Counter::get();
        if (object != m_anticheatSpike && c.running() && c.ownsLayer(this) && c.onDeath()) return;
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        auto& c = fw::Counter::get();
        if (c.running() && c.ownsLayer(this) && c.onComplete()) return;
        PlayLayer::levelComplete();
    }

    void startMusic() {
        auto& c = fw::Counter::get();
        if (c.running() && c.ownsLayer(this)) return;
        PlayLayer::startMusic();
    }
};

class $modify(FWPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setTouchPriority(-510);

        auto spr = ButtonSprite::create("FP", 40, true, "goldFont.fnt", "GJ_button_01.png", 30.f, .8f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FWPauseLayer::onFrameWindow));
        btn->setPosition({winSize.width - 34.f, 34.f});
        menu->addChild(btn);
        this->addChild(menu, 100);
    }

    void onFrameWindow(CCObject*) {
        fw::MainPanel::create()->show();
    }
};
