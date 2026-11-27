#pragma once

#include <Geode/Geode.hpp>

namespace acgdl {

    // Panel con interruptores. Se abre con F8, el boton flotante del menu o el de la pausa.
    class Panel : public geode::Popup {
    protected:
        bool init();
        ~Panel();

    public:
        static Panel* create();
        static void open();
    };

}
