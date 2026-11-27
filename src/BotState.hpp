#pragma once

#include <string>

namespace acgdl {

    // Estado compartido entre el bot, la pantalla de carga y el panel.
    struct BotStats {
        // 0 = apagado, 1 = resolviendo, 2 = ejecutando solucion, 3 = jugando en directo
        int phase = 0;
        float percent = 0.f;     // 0..100
        int attempts = 0;        // reintentos del solver
        int checkpoints = 0;
        int steps = 0;           // pasos de la solucion
        int resolves = 0;
        bool inLevel = false;
        bool skipRequested = false;   // saltar la resolucion y jugar en directo
        bool cancelRequested = false; // apagar el bot en este nivel
        bool resolveRequested = false; // volver a resolver el nivel
        std::string info;             // resumen del nivel
        std::string diag;             // ultimo fallo: modo de juego y objeto que mata
    };

    BotStats& stats();

}
