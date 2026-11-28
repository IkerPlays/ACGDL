#pragma once

#include <Geode/Geode.hpp>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace acgdl::macro {

    struct Macro {
        std::string file;
        std::string levelName;
        int levelId = 0;
        double framerate = 240.0;
        std::vector<std::pair<int, bool>> events; // (frame, pulsado) del jugador principal
        int lastFrame = 0;
    };

    // Carpeta donde dejar macros (.gdr / .gdr2). Tambien se busca en las carpetas de Eclipse y xdBot.
    std::string folder();

    // Busca un macro del nivel indicado. Devuelve false si no hay ninguno.
    bool find(std::string const& levelName, int levelId, Macro& out);

    // Convierte el macro en una pulsacion por paso de fisica (240 Hz).
    // `offset` mueve las pulsaciones unos pasos (los bots cuentan los frames distinto).
    std::vector<uint8_t> toInputs(Macro const& m, int offset);

}
