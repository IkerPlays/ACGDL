#pragma once

#include <Geode/Geode.hpp>
#include "LevelScanner.hpp"

namespace acgdl {

    // Memoria entre pasos (velocidad vertical medida, estado de orbes).
    struct BotMemory {
        bool init = false;
        float lastY = 0.f;
        float vy = 0.f;       // unidades por paso (240 pasos/s)
        bool inOrb = false;
        bool pressedOrb = false;

        void reset() { *this = BotMemory(); }
    };

    struct DecideOpts {
        bool wasHolding = false;
        bool ignoreOrbs = false;
        bool dual = false;
        PlayerObject* p2 = nullptr;
    };

    // Decide si el boton de salto debe estar pulsado en este paso de fisica (1/240 s).
    bool decideHold(PlayerObject* player, LevelData const& data, BotMemory& mem, int step, DecideOpts opts);

}
