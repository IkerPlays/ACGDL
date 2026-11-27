#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

// Boton en el menu de pausa para activar/desactivar el bot (funciona en movil).
class $modify(ACPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = this->getChildByID("right-button-menu");
        if (!menu) menu = this->getChildByID("left-button-menu");
        if (!menu) return;

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
        menu->updateLayout();
    }
};
