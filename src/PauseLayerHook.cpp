#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include "Panel.hpp"

using namespace geode::prelude;

// Botones en el menu de pausa (funcionan tocando, sirve en movil y tablet).
class $modify(ACPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = this->getChildByID("right-button-menu");
        if (!menu) menu = this->getChildByID("left-button-menu");
        if (!menu) return;

        // Interruptor rapido
        auto on = ButtonSprite::create("Auto: ON");
        auto off = ButtonSprite::create("Auto: OFF");
        on->setScale(0.6f);
        off->setScale(0.6f);
        auto toggler = CCMenuItemExt::createToggler(on, off, [](CCMenuItemToggler* t) {
            // dentro del callback isToggled() aun es el valor anterior
            Mod::get()->setSettingValue<bool>("auto-play", !t->isToggled());
        });
        toggler->toggle(Mod::get()->getSettingValue<bool>("auto-play"));
        toggler->setID("auto-complete-toggle"_spr);
        menu->addChild(toggler);

        // Panel completo
        auto spr = ButtonSprite::create("Panel AC");
        spr->setScale(0.6f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
            acgdl::Panel::open();
        });
        btn->setID("auto-complete-panel"_spr);
        menu->addChild(btn);

        menu->updateLayout();
    }
};
