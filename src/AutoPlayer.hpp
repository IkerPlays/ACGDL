#pragma once

#include <Geode/Geode.hpp>
#include "LevelScanner.hpp"

namespace acgdl {

    // Decide si el boton de salto debe estar pulsado en este frame.
    // `frame` sirve para alternar pulsaciones (ufo, swing).
    bool decideHold(PlayerObject* player, LevelData const& data, int frame);

}
