#pragma once

namespace acgdl {

    // Estadisticas compartidas entre el bot y el panel.
    struct BotStats {
        int rewinds = 0;
        int snapshots = 0;
        float progressX = 0.f;
        bool inLevel = false;
        bool gaveUp = false;
    };

    BotStats& stats();

}
