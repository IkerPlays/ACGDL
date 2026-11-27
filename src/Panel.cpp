#include "Panel.hpp"
#include "BotState.hpp"

using namespace geode::prelude;

namespace acgdl {

    static Panel* s_open = nullptr;

    static void addRow(CCMenu* menu, CCSize size, int row, char const* title, char const* key) {
        float y = size.height - 62.f - row * 36.f;

        auto label = CCLabelBMFont::create(title, "bigFont.fnt");
        label->setScale(0.42f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({24.f, y});
        menu->addChild(label);

        auto on = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        auto off = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
        std::string k = key;
        auto toggler = CCMenuItemExt::createToggler(on, off, [k](CCMenuItemToggler* t) {
            // dentro del callback isToggled() aun es el valor anterior
            Mod::get()->setSettingValue<bool>(k, !t->isToggled());
        });
        toggler->toggle(Mod::get()->getSettingValue<bool>(key));
        toggler->setPosition({size.width - 38.f, y});
        menu->addChild(toggler);
    }

    bool Panel::init() {
        if (!Popup::init(310.f, 250.f)) return false;
        this->setTitle("Auto Complete");

        auto menu = CCMenu::create();
        menu->setContentSize(m_size);
        menu->ignoreAnchorPointForPosition(false);
        menu->setAnchorPoint({0.5f, 0.5f});
        menu->setPosition(m_size / 2);
        m_mainLayer->addChild(menu);

        addRow(menu, m_size, 0, "Auto-completar", "auto-play");
        addRow(menu, m_size, 1, "Rebobinado (anti-muerte)", "rewind");
        addRow(menu, m_size, 2, "Resumen del nivel", "show-info");
        addRow(menu, m_size, 3, "Modo seguro (no guardar)", "safe-mode");

        auto& st = stats();
        std::string info = st.inLevel
            ? fmt::format("Rebobinados: {}  |  Puntos: {}  |  X: {}", st.rewinds, st.snapshots, static_cast<int>(st.progressX))
            : "Entra a un nivel para ver estadisticas";
        auto lbl = CCLabelBMFont::create(info.c_str(), "chatFont.fnt");
        lbl->setScale(0.8f);
        lbl->setPosition({m_size.width / 2, 26.f});
        menu->addChild(lbl);

        return true;
    }

    Panel::~Panel() {
        if (s_open == this) s_open = nullptr;
    }

    Panel* Panel::create() {
        auto ret = new Panel();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void Panel::open() {
        if (s_open) return; // ya abierto
        auto p = Panel::create();
        if (!p) return;
        s_open = p;
        p->show();
    }

}
