#pragma once

#include <Geode/Geode.hpp>
#include <functional>

namespace acgdl::traj {

    struct SimResult {
        int steps = 0;        // pasos simulados
        bool died = false;    // murio antes de acabar
        cocos2d::CCPoint end; // posicion final
    };

    // Crea el jugador falso. Llamar con el nivel ya arrancado.
    bool init(PlayLayer* pl);
    void reset();
    bool ready();

    // true mientras se simula (para que los hooks no toquen el nivel real)
    bool active();
    void markDeath();
    bool orbsAllowed();

    float stepDelta();
    void setDelta(float dt);

    // Simula hasta `n` pasos del jugador `real` con la funcion de pulsacion `holdAt(i)`.
    // `decide` (opcional) se llama con el jugador falso antes de cada paso y puede
    // sobreescribir la pulsacion (sirve para simular la politica del bot).
    // Se detiene si el jugador falso pasa de maxX.
    SimResult simulate(
        PlayLayer* pl, PlayerObject* real, int n, float maxX,
        std::function<bool(int step, PlayerObject* fake)> const& holdAt,
        bool startHolding = false,
        std::vector<cocos2d::CCPoint>* path = nullptr,
        std::vector<GameObject*> const* touchables = nullptr,
        bool allowOrbs = false
    );

}
