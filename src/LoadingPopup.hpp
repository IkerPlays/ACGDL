#pragma once

#include <Geode/Geode.hpp>

namespace acgdl {

    // Pantalla negra de carga mientras el bot resuelve el nivel en segundo plano.
    class LoadingPopup : public geode::Popup {
    protected:
        cocos2d::CCLayerColor* m_fill = nullptr;
        cocos2d::CCLabelBMFont* m_percent = nullptr;
        cocos2d::CCLabelBMFont* m_status = nullptr;
        cocos2d::CCLabelBMFont* m_stats = nullptr;
        cocos2d::CCLabelBMFont* m_info = nullptr;
        int m_dots = 0;

        bool init();
        void keyBackClicked() override {}
        void tick(float dt);

    public:
        static LoadingPopup* create();
        void finish();
    };

}
