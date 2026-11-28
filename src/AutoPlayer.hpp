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

    // Hace las pulsaciones parecidas a las de una persona: una pulsacion o una pausa
    // no pueden ser mas cortas que un minimo (en pasos de 1/240 s).
    struct Humanizer {
        bool cur = false;
        int since = 1000;
        int need = 0;               // pasos que hay que esperar antes del siguiente cambio
        uint32_t seed = 2463534242u; // variacion natural (una persona no pulsa siempre igual)
    };

    inline bool humanize(Humanizer& h, bool want, int minHold, int minRelease, bool force) {
        if (want != h.cur && (force || h.since >= h.need)) {
            h.cur = want;
            h.since = 0;
            // el proximo cambio tardara el minimo mas una variacion de hasta +50%
            int base = want ? minHold : minRelease;
            h.seed ^= h.seed << 13;
            h.seed ^= h.seed >> 17;
            h.seed ^= h.seed << 5;
            h.need = base + static_cast<int>(h.seed % static_cast<uint32_t>(base / 2 + 1));
        }
        h.since++;
        return h.cur;
    }

    struct DecideOpts {
        bool wasHolding = false;
        bool ignoreOrbs = false;
        int steps = 1;       // pasos de fisica desde la decision anterior (1 normalmente, 2 en directo)
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
