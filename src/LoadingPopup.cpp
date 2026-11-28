#include "LoadingPopup.hpp"
#include "BotState.hpp"

using namespace geode::prelude;

namespace acgdl {

    static constexpr float BAR_W = 280.f;
    static constexpr float BAR_H = 14.f;

    bool LoadingPopup::init() {
        if (!Popup::init(380.f, 210.f)) return false;

        // fondo totalmente negro a pantalla completa
        auto win = CCDirector::get()->getWinSize();
        auto black = CCLayerColor::create({0, 0, 0, 255});
        black->setContentSize(win);
        this->addChild(black, -10);

        m_bgSprite->setVisible(false);
        m_closeBtn->setVisible(false);

        auto title = CCLabelBMFont::create("AUTO COMPLETE", "goldFont.fnt");
        title->setScale(0.9f);
        m_mainLayer->addChildAtPosition(title, Anchor::Center, {0.f, 78.f});

        m_status = CCLabelBMFont::create("Cargando", "bigFont.fnt");
        m_status->setScale(0.55f);
        m_mainLayer->addChildAtPosition(m_status, Anchor::Center, {0.f, 40.f});

        // barra de progreso
        auto barBg = CCLayerColor::create({45, 45, 55, 255});
        barBg->setContentSize({BAR_W + 4.f, BAR_H + 4.f});
        barBg->ignoreAnchorPointForPosition(false);
        barBg->setAnchorPoint({0.f, 0.5f});
        m_mainLayer->addChildAtPosition(barBg, Anchor::Center, {-BAR_W / 2 - 2.f, 6.f});

        m_fill = CCLayerColor::create({80, 220, 120, 255});
        m_fill->setContentSize({1.f, BAR_H});
        m_fill->ignoreAnchorPointForPosition(false);
        m_fill->setAnchorPoint({0.f, 0.5f});
        m_mainLayer->addChildAtPosition(m_fill, Anchor::Center, {-BAR_W / 2, 6.f});

        m_percent = CCLabelBMFont::create("0%", "bigFont.fnt");
        m_percent->setScale(0.5f);
        m_mainLayer->addChildAtPosition(m_percent, Anchor::Center, {0.f, -18.f});

        m_stats = CCLabelBMFont::create("", "chatFont.fnt");
        m_stats->setScale(0.8f);
        m_mainLayer->addChildAtPosition(m_stats, Anchor::Center, {0.f, -44.f});

        m_info = CCLabelBMFont::create(stats().info.c_str(), "chatFont.fnt");
        m_info->setScale(0.65f);
        m_info->setOpacity(150);
        m_mainLayer->addChildAtPosition(m_info, Anchor::Center, {0.f, -62.f});

        m_diag = CCLabelBMFont::create("", "chatFont.fnt");
        m_diag->setScale(0.6f);
        m_diag->setColor({255, 190, 90});
        m_mainLayer->addChildAtPosition(m_diag, Anchor::Center, {0.f, -80.f});

        auto skipSpr = ButtonSprite::create("Saltar", "goldFont.fnt", "GJ_button_04.png", 0.7f);
        auto skip = Button::createWithNode(skipSpr, [](Button*) {
            stats().skipRequested = true;
        });
        m_mainLayer->addChildAtPosition(skip, Anchor::Center, {-70.f, -112.f});

        auto cancelSpr = ButtonSprite::create("Apagar bot", "goldFont.fnt", "GJ_button_06.png", 0.7f);
        auto cancel = Button::createWithNode(cancelSpr, [](Button*) {
            stats().cancelRequested = true;
        });
        m_mainLayer->addChildAtPosition(cancel, Anchor::Center, {70.f, -112.f});

        this->schedule(schedule_selector(LoadingPopup::tick), 0.05f);
        return true;
    }

    void LoadingPopup::tick(float) {
        auto& st = stats();
        if (st.phase != 1) {
            this->finish();
            return;
        }

        float pct = std::clamp(st.percent, 0.f, 100.f);
        m_fill->setContentSize({std::max(1.f, BAR_W * pct / 100.f), BAR_H});
        m_percent->setString(fmt::format("{}%", static_cast<int>(pct)).c_str());

        m_dots = (m_dots + 1) % 60;
        std::string dots(static_cast<size_t>(m_dots / 15), '.');
        m_status->setString(("Cargando" + dots).c_str());

        m_diag->setString(st.diag.c_str());
        m_stats->setString(fmt::format(
            "Reintentos: {}   |   Puntos de control: {}   |   Tiempo: {} s",
            st.attempts, st.checkpoints, static_cast<int>(st.elapsed)
        ).c_str());
    }

    void LoadingPopup::finish() {
        this->unschedule(schedule_selector(LoadingPopup::tick));
        this->onClose(nullptr);
    }

    LoadingPopup* LoadingPopup::create() {
        auto ret = new LoadingPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

}
