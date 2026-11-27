#pragma once

#include <Geode/Geode.hpp>

namespace acgdl {

    // Panel de control. Se abre con F8, el boton flotante del menu o el de la pausa.
    class Panel : public geode::Popup {
    protected:
        cocos2d::CCLabelBMFont* m_state = nullptr;
        cocos2d::CCLabelBMFont* m_stats = nullptr;

        bool init();
        void tick(float dt);
        ~Panel();

    public:
        static Panel* create();
        static void open();
    };

}
