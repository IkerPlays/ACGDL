#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace acgdl {

    struct Box {
        float minX, minY, maxX, maxY;
    };

    // Cosas que un simulador de trayectoria no modela (portales, orbes, pads):
    // se corta el horizonte antes de llegar a ellas.
    struct Stop {
        Box box;
        float slack;
    };

    // Todo lo que el bot necesita saber del nivel, ordenado por minX.
    struct LevelData {
        std::vector<Box> hazards; // pinchos, sierras...
        std::vector<Box> solids;  // bloques
        std::vector<Box> orbs;    // orbes (hay que pulsar encima)
        std::vector<Box> pads;    // pads (se activan solos)
        std::vector<Stop> stops;        // portales + orbes + pads, ordenados por minX
        std::vector<Stop> portalStops;  // solo portales
        std::vector<GameObject*> orbObjs; // paralelo a orbs
        std::vector<GameObject*> padObjs; // paralelo a pads
        int slopes = 0;
        int animatedHazards = 0;
        std::string typeReport;           // recuento por tipo de objeto (para el log)
        float maxHazardW = 0.f;
        float maxSolidW = 0.f;
        float endX = 1.f;
        int totalObjects = 0;
        bool ready = false;

        std::string summary() const;
    };

    bool isOrbType(GameObjectType t);
    bool isPadType(GameObjectType t);
    char const* typeName(int type);

    // Lee todos los objetos del nivel y los clasifica.
    LevelData scanLevel(GJBaseGameLayer* layer);

}
