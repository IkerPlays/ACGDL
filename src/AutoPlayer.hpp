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
        int orbSteps = 0;
        int holdLeft = 0;     // pasos que quedan de pulsacion mantenida (robot)

        void reset() { *this = BotMemory(); }
    };

    struct DecideOpts {
        bool wasHolding = false;
        bool ignoreOrbs = false;
        bool dual = false;
        PlayerObject* p2 = nullptr;
        // variaciones de la busqueda sobre los orbes
        uint32_t orbSeed = 0;
        int orbSkipPct = 0;  // % de orbes que se ignoran
        int orbDelay = 0;    // pasos de espera dentro del orbe antes de pulsar
    };

    // Decide si el boton de salto debe estar pulsado en este paso de fisica (1/240 s).
    bool decideHold(PlayerObject* player, LevelData const& data, BotMemory& mem, int step, DecideOpts opts);

}
