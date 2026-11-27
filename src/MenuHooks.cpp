#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/Keyboard.hpp>

#include "Panel.hpp"

using namespace geode::prelude;

// Boton flotante semitransparente en el menu principal (movil / tablet).
class $modify(ACMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto win = CCDirector::get()->getWinSize();

        auto spr = ButtonSprite::create("AC");
        spr->setScale(0.7f);
        spr->setOpacity(170);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
            acgdl::Panel::open();
        });
        btn->setID("auto-complete-floating"_spr);

        auto menu = CCMenu::create();
        menu->setID("auto-complete-floating-menu"_spr);
        menu->addChild(btn);
        menu->setPosition({win.width - 34.f, win.height - 70.f});
        this->addChild(menu, 100);

        return true;
    }
};

// Tecla F8 abre el panel en PC (en cualquier pantalla, incluso jugando).
$execute {
    geode::KeyboardInputEvent().listen([](geode::KeyboardInputData& data) {
        if (data.action == geode::KeyboardInputData::Action::Press &&
            data.key == cocos2d::enumKeyCodes::KEY_F8) {
            acgdl::Panel::open();
            return true;
        }
        return false;
    }).leak();
}
