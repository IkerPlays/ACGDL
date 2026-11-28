#include "LoadingPopup.hpp"
#include "BotState.hpp"

#include <Geode/ui/NineSlice.hpp>

using namespace geode::prelude;

namespace acgdl {

    static constexpr float BAR_W = 300.f;
    static constexpr float BAR_H = 18.f;

    bool LoadingPopup::init() {
        if (!Popup::init(400.f, 220.f)) return false;

        // fondo totalmente negro a pantalla completa
        auto win = CCDirector::get()->getWinSize();
        auto black = CCLayerColor::create({0, 0, 0, 255});
        black->setContentSize(win);
        this->addChild(black, -10);

        m_bgSprite->setVisible(false);
        m_closeBtn->setVisible(false);

        auto title = CCLabelBMFont::create("AUTO COMPLETE", "goldFont.fnt");
        title->setScale(0.85f);
        m_mainLayer->addChildAtPosition(title, Anchor::Center, {0.f, 74.f});

        m_status = CCLabelBMFont::create("Resolviendo el nivel", "bigFont.fnt");
        m_status->setScale(0.42f);
        m_mainLayer->addChildAtPosition(m_status, Anchor::Center, {0.f, 44.f});

        // barra de progreso
        auto barBg = NineSlice::create("square02b_001.png");
        barBg->setContentSize({BAR_W + 6.f, BAR_H + 6.f});
        barBg->setColor({25, 25, 35});
        barBg->setOpacity(230);
        m_mainLayer->addChildAtPosition(barBg, Anchor::Center, {0.f, 14.f});

        m_fillNode = NineSlice::create("square02b_001.png");
        m_fillNode->setContentSize({12.f, BAR_H});
        m_fillNode->setColor({90, 225, 130});
        m_fillNode->ignoreAnchorPointForPosition(false);
        m_fillNode->setAnchorPoint({0.f, 0.5f});
        m_mainLayer->addChildAtPosition(m_fillNode, Anchor::Center, {-BAR_W / 2, 14.f});

        m_percent = CCLabelBMFont::create("0%", "bigFont.fnt");
        m_percent->setScale(0.36f);
        m_mainLayer->addChildAtPosition(m_percent, Anchor::Center, {0.f, 14.f});

        m_stats = CCLabelBMFont::create("", "chatFont.fnt");
        m_stats->setScale(0.7f);
        m_mainLayer->addChildAtPosition(m_stats, Anchor::Center, {0.f, -14.f});

        m_diag = CCLabelBMFont::create("", "chatFont.fnt");
        m_diag->setScale(0.6f);
        m_diag->setColor({255, 190, 90});
        m_mainLayer->addChildAtPosition(m_diag, Anchor::Center, {0.f, -34.f});

        m_info = CCLabelBMFont::create(stats().info.c_str(), "chatFont.fnt");
        m_info->setScale(0.55f);
        m_info->setOpacity(130);
        m_mainLayer->addChildAtPosition(m_info, Anchor::Center, {0.f, -52.f});

        auto skipSpr = ButtonSprite::create("Saltar", "goldFont.fnt", "GJ_button_04.png", 0.6f);
        auto skip = Button::createWithNode(skipSpr, [](Button*) {
            stats().skipRequested = true;
        });
        m_mainLayer->addChildAtPosition(skip, Anchor::Center, {-70.f, -86.f});

        auto cancelSpr = ButtonSprite::create("Apagar bot", "goldFont.fnt", "GJ_button_06.png", 0.6f);
        auto cancel = Button::createWithNode(cancelSpr, [](Button*) {
            stats().cancelRequested = true;
        });
        m_mainLayer->addChildAtPosition(cancel, Anchor::Center, {70.f, -86.f});

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
        m_fillNode->setContentSize({std::max(12.f, BAR_W * pct / 100.f), BAR_H});
        m_percent->setString(fmt::format("{}%", static_cast<int>(pct)).c_str());

        // texto de estado segun lo que esta haciendo
        char const* what = "Resolviendo el nivel";
        if (st.diag.rfind("Verificando", 0) == 0) what = "Verificando la solucion";
        else if (st.diag.rfind("Reparando", 0) == 0 || st.diag.rfind("La repeticion", 0) == 0) what = "Reparando un tramo";
        m_dots = (m_dots + 1) % 60;
        std::string dots(static_cast<size_t>(m_dots / 15), '.');
        m_status->setString((std::string(what) + dots).c_str());

        m_diag->setString(st.diag.c_str());
        m_stats->setString(fmt::format(
            "Reintentos {}   |   Checkpoints {}   |   {} s",
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
