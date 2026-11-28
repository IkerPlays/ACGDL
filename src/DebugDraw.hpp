#pragma once

#include <Geode/Geode.hpp>
#include "LevelScanner.hpp"

namespace acgdl::dbg {

    void reset();

    // Dibuja sobre el nivel lo que el bot "ve": hitboxes de pinchos (rojo), bloques (azul),
    // orbes (amarillo), pads (rosa), el jugador (verde) y la trayectoria prevista (blanco/rojo).
    void draw(PlayLayer* pl, LevelData const& d, std::vector<cocos2d::CCPoint> const& path, bool pathOk);

}
