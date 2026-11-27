#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace acgdl {

    struct Box {
        float minX, minY, maxX, maxY;
    };

    // Todo lo que el bot necesita saber del nivel, ordenado por minX.
    struct LevelData {
        std::vector<Box> hazards; // pinchos, sierras...
        std::vector<Box> solids;  // bloques
        std::vector<Box> orbs;    // orbes (hay que pulsar encima)
        std::vector<Box> pads;    // pads (se activan solos)
        float maxHazardW = 0.f;
        float maxSolidW = 0.f;
        int totalObjects = 0;
        bool ready = false;

        std::string summary() const;
    };

    // Lee todos los objetos del nivel y los clasifica.
    LevelData scanLevel(GJBaseGameLayer* layer);

}
