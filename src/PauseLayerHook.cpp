#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include "Panel.hpp"

using namespace geode::prelude;

// Un unico boton compacto en el menu de pausa (tactil, sirve en movil y tablet).
// Todos los ajustes estan dentro del panel.
class $modify(ACPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = this->getChildByID("right-button-menu");
        if (!menu) menu = this->getChildByID("left-button-menu");
        if (!menu) return;

        auto spr = ButtonSprite::create("AC", "goldFont.fnt", "GJ_button_01.png", 0.6f);
        spr->setScale(0.7f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
            acgdl::Panel::open();
        });
        btn->setID("auto-complete-panel"_spr);
        menu->addChild(btn);
        menu->updateLayout();
    }
};
