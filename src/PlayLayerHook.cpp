#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <vector>

#include "AutoPlayer.hpp"
#include "BotState.hpp"
#include "LevelScanner.hpp"
#include "LoadingPopup.hpp"

using namespace geode::prelude;

namespace {

    // Un paso de fisica = 1/240 s. Tanto el solver como la ejecucion usan
    // SIEMPRE este paso fijo, asi la solucion se reproduce igual.
    constexpr float STEP = 1.f / 240.f;

    enum Phase { PH_OFF = 0, PH_SOLVE = 1, PH_PLAY = 2, PH_LIVE = 3, PH_WAIT = 4, PH_REPLAY = 5 };

    struct Run {
        acgdl::LevelData data;
        int phase = PH_OFF;
        acgdl::BotMemory mem;
        bool holding = false;
        bool finished = false;
        int step = 0;

        // solver
        std::vector<uint8_t> inputs;
        std::vector<int> cpStep;
        float lastCpX = -1000.f;
        float failX = -1.f;
        float deathX = 0.f;
        float bestX = 0.f;
        int fails = 0;
        int totalFails = 0;
        bool died = false;
        bool simDone = false;

        // mutaciones
        int mutLeft = 0;
        int mutType = 0;
        int mutDelay = 4;
        int pulseLeft = 0;
        bool mutFlip = false;
        std::deque<uint8_t> hist;
        uint32_t rng = 0x9E3779B9u;

        // ejecucion
        double acc = 0.0;
        int playDeaths = 0;
        int resolves = 0;
        float savedSfx = 1.f;
        bool muted = false;

        // espera a que el juego haya arrancado de verdad
        bool startReady = false;
        int waitFrames = 0;

        // frames sin llamar a la fisica tras reiniciar el nivel (deja que el juego termine de armarse)
        int settle = 0;

        // verificacion desde cero
        int replayUntil = 0;
        bool verifyFull = false;
        int verifyFails = 0;
    };

    Run g;

    bool botOn() { return Mod::get()->getSettingValue<bool>("auto-play"); }
    bool searchOn() { return Mod::get()->getSettingValue<bool>("rewind"); }
    bool safeOn() { return Mod::get()->getSettingValue<bool>("safe-mode"); }

    uint32_t nextRand() {
        g.rng ^= g.rng << 13;
        g.rng ^= g.rng >> 17;
        g.rng ^= g.rng << 5;
        return g.rng;
    }
    float rand01() { return (nextRand() & 0xFFFFFF) / 16777216.f; }

    void setHold(PlayLayer* pl, bool v) {
        if (v != g.holding) {
            g.holding = v;
            pl->handleButton(v, 1, true);
        }
    }

    // Silencia efectos y musica mientras se resuelve en segundo plano.
    void mute(bool on) {
        auto fmod = FMODAudioEngine::sharedEngine();
        if (!fmod) return;
        if (on && !g.muted) {
            g.savedSfx = fmod->m_sfxVolume;
            g.muted = true;
        }
        if (on) {
            fmod->setEffectsVolume(0.f);
            fmod->pauseAllMusic(true);
        } else if (g.muted) {
            fmod->setEffectsVolume(g.savedSfx);
            g.muted = false;
        }
    }

    void updatePercent(PlayLayer* pl) {
        if (!pl->m_player1) return;
        float x = pl->m_player1->getPositionX();
        acgdl::stats().percent = std::clamp(x / std::max(1.f, g.data.endX) * 100.f, 0.f, 100.f);
    }

    void restartClean(PlayLayer* pl) {
        pl->removeAllCheckpoints();
        pl->togglePracticeMode(false);
        pl->resetLevelFromStart();
        if (safeOn()) pl->m_isTestMode = true;
    }

    // Vuelve a jugar normal en directo (sin solucion previa).
    void goLive(PlayLayer* pl) {
        auto& st = acgdl::stats();
        g.phase = PH_LIVE;
        st.phase = PH_LIVE;
        mute(false);
        g.step = 0;
        g.acc = 0.0;
        g.holding = false;
        g.mem.reset();
        restartClean(pl);
        g.settle = 4;
        log::info("[AutoComplete] modo directo");
    }

    void leaveBot(PlayLayer* pl) {
        auto& st = acgdl::stats();
        bool wasSolving = g.phase == PH_SOLVE || g.phase == PH_REPLAY;
        g.phase = PH_OFF;
        st.phase = PH_OFF;
        mute(false);
        if (wasSolving) {
            pl->removeAllCheckpoints();
            pl->togglePracticeMode(false);
            pl->resetLevelFromStart();
        }
    }

    // Empieza (o reinicia) la resolucion en segundo plano.
    void beginSolve(PlayLayer* pl) {
        auto& st = acgdl::stats();
        g.phase = PH_SOLVE;
        g.inputs.clear();
        g.cpStep.clear();
        g.hist.clear();
        g.lastCpX = -1000.f;
        g.failX = -1.f;
        g.fails = 0;
        g.totalFails = 0;
        g.bestX = 0.f;
        g.step = 0;
        g.mutLeft = 0;
        g.died = false;
        g.simDone = false;
        g.finished = false;
        g.holding = false;
        g.playDeaths = 0;
        g.mem.reset();
        g.rng ^= static_cast<uint32_t>(g.resolves * 2654435761u + 12345u);

        st.phase = PH_SOLVE;
        st.percent = 0.f;
        st.attempts = 0;
        st.checkpoints = 0;
        st.steps = 0;

        mute(true);
        if (!pl->m_isPracticeMode) pl->togglePracticeMode(true);
        pl->resetLevelFromStart();

        g.settle = 4;
        g.replayUntil = 0;
        g.verifyFull = false;
        g.verifyFails = 0;
        log::info("[AutoComplete] solver iniciado");
        if (auto popup = acgdl::LoadingPopup::create()) {
            popup->show();
        }
    }

    // Repite desde cero (con los mismos pasos fijos) las primeras `until` entradas.
    // Si until == total, es una verificacion completa de la solucion.
    void startReplay(PlayLayer* pl, int until, bool full) {
        auto& st = acgdl::stats();
        bool wasLoading = st.phase == PH_SOLVE;
        g.phase = PH_REPLAY;
        st.phase = PH_SOLVE;
        g.replayUntil = until;
        g.verifyFull = full;
        g.step = 0;
        g.cpStep.clear();
        g.hist.clear();
        g.lastCpX = -1000.f;
        g.failX = -1.f;
        g.fails = 0;
        g.mutLeft = 0;
        g.died = false;
        g.simDone = false;
        g.finished = false;
        g.holding = false;
        g.mem.reset();

        mute(true);
        pl->removeAllCheckpoints();
        pl->resetLevelFromStart();
        g.settle = 3;
        log::info("[AutoComplete] repeticion desde cero: {} pasos (completa: {})", until, full);

        if (!wasLoading) {
            if (auto popup = acgdl::LoadingPopup::create()) popup->show();
        }
    }

    // La repeticion desde cero fallo en el paso k: se conserva lo verificado antes y se resuelve de nuevo desde ahi.
    void repair(PlayLayer* pl, int k) {
        g.verifyFails++;
        if (g.verifyFails > 80) {
            goLive(pl);
            return;
        }
        int back = 360 + 120 * std::min(g.verifyFails, 12);
        int keep = std::clamp(k - back, 0, static_cast<int>(g.inputs.size()));
        g.inputs.resize(static_cast<size_t>(keep));
        startReplay(pl, keep, false);
    }

    // La solucion ya esta: se ejecuta de verdad desde el principio.
    void finishSolve(PlayLayer* pl) {
        auto& st = acgdl::stats();
        g.phase = PH_PLAY;
        st.phase = PH_PLAY;
        st.steps = static_cast<int>(g.inputs.size());
        st.percent = 0.f;
        mute(false);
        g.step = 0;
        g.acc = 0.0;
        g.holding = false;
        g.playDeaths = 0;
        g.died = false;
        g.simDone = false;
        g.cpStep.clear();
        g.lastCpX = -1000.f;
        g.mem.reset();
        // se ejecuta en el MISMO modo (practica) y con las mismas condiciones que la verificacion
        pl->removeAllCheckpoints();
        pl->resetLevelFromStart();
        g.settle = 4;
        log::info("[AutoComplete] solucion verificada: {} pasos, {} reintentos", g.inputs.size(), g.totalFails);

        Notification::create(
            fmt::format("Solucion lista ({} pasos). Ejecutando...", g.inputs.size()),
            NotificationIcon::Success, 3.f
        )->show();
    }

}

class $modify(ACPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        g = Run();
        auto& st = acgdl::stats();
        st = acgdl::BotStats();
        st.inLevel = true;

        g.data = acgdl::scanLevel(this);
        st.info = g.data.summary();

        if (!botOn()) return;

        if (searchOn()) {
            // se resuelve cuando el juego haya arrancado (ver startGame / update)
            g.phase = PH_WAIT;
            log::info("[AutoComplete] nivel leido, esperando arranque");
        } else {
            g.phase = PH_LIVE;
            st.phase = PH_LIVE;
            if (safeOn()) m_isTestMode = true;
            if (Mod::get()->getSettingValue<bool>("show-info")) {
                Notification::create(st.info, NotificationIcon::Info, 4.f)->show();
            }
        }
    }

    void startGame() {
        PlayLayer::startGame();
        if (g.phase == PH_WAIT) g.startReady = true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (g.phase == PH_OFF || g.phase == PH_WAIT) return;

        this->handleButton(false, 1, true);
        g.holding = false;
        g.mem.reset();

        g.lastCpX = m_player1 ? m_player1->getPositionX() : -1000.f;
        if (g.phase == PH_SOLVE || g.phase == PH_REPLAY) {
            auto fmod = FMODAudioEngine::sharedEngine();
            if (fmod) fmod->pauseAllMusic(true);
        } else {
            g.step = 0;
            g.acc = 0.0;
            g.finished = false;
        }
    }

    // Durante la resolucion no se "muere": el solver rebobina. Ejecutando, se cuentan los fallos.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (g.phase != PH_OFF && object != m_anticheatSpike) {
            if (g.phase == PH_SOLVE || g.phase == PH_REPLAY || g.phase == PH_PLAY) {
                if (!g.died) {
                    g.died = true;
                    g.deathX = m_player1 ? m_player1->getPositionX() : 0.f;
                }
                return;
            }
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        if (g.phase == PH_SOLVE || g.phase == PH_REPLAY) {
            g.simDone = true;
            return;
        }
        if (g.phase != PH_OFF) {
            this->handleButton(false, 1, true);
            g.holding = false;
            g.finished = true;
            acgdl::stats().percent = 100.f;
        }
        PlayLayer::levelComplete();
    }

    void onQuit() {
        if (g.phase == PH_SOLVE || g.phase == PH_REPLAY) mute(false);
        g.phase = PH_OFF;
        acgdl::stats().phase = PH_OFF;
        acgdl::stats().inLevel = false;
        PlayLayer::onQuit();
    }
};

class $modify(ACBase, GJBaseGameLayer) {
    // ---- un paso del solver ----
    void rollback(PlayLayer* pl) {
        g.died = false;
        if (g.failX < 0.f || g.deathX > g.failX + 60.f) {
            g.failX = g.deathX;
            g.fails = 0;
        }
        g.fails++;
        g.totalFails++;

        // si sigue fallando en el mismo punto, se descartan checkpoints para ir mas atras
        if (g.fails % 6 == 0 && !g.cpStep.empty() && pl->m_checkpointArray &&
            pl->m_checkpointArray->count() > 0) {
            pl->removeCheckpoint(false); // el juego tambien limpia el objeto fisico del checkpoint
            g.cpStep.pop_back();
        }

        int target = g.cpStep.empty() ? 0 : g.cpStep.back();
        g.step = target;
        g.inputs.resize(static_cast<size_t>(target));
        g.hist.clear();
        pl->resetLevel();

        // otra forma de resolver el tramo
        int back = std::min(g.fails / 6, 16);
        g.mutType = static_cast<int>(nextRand() % 5);
        g.mutLeft = 160 + back * 40 + static_cast<int>(nextRand() % 300);
        g.mutFlip = false;
        g.pulseLeft = 0;
        g.mutDelay = 4 + static_cast<int>(nextRand() % 25);
    }

    void simStep(PlayLayer* pl) {
        auto p = pl->m_player1;
        if (!p) {
            GJBaseGameLayer::update(STEP);
            return;
        }
        if (g.died) {
            this->rollback(pl);
            return;
        }

        float px = p->getPositionX();
        g.bestX = std::max(g.bestX, px);

        if (g.failX >= 0.f && px > g.failX + 220.f) {
            g.fails = 0;
            g.failX = -1.f;
        }

        // checkpoint oficial de practica (solo sin boton pulsado, para que el estado se reproduzca igual)
        if (px - g.lastCpX >= 45.f && !g.holding && !p->m_isDead && g.step > 0) {
            if (pl->markCheckpoint()) g.cpStep.push_back(g.step);
            g.lastCpX = px;
        }

        acgdl::DecideOpts o;
        o.wasHolding = g.holding;
        o.ignoreOrbs = g.mutLeft > 0 && g.mutType == 4;
        bool want = acgdl::decideHold(p, g.data, g.mem, g.step, o);

        g.hist.push_back(want ? 1 : 0);
        if (g.hist.size() > 64) g.hist.pop_front();

        if (g.mutLeft > 0) {
            g.mutLeft--;
            switch (g.mutType) {
                case 0:
                    if (rand01() < 0.012f) g.mutFlip = !g.mutFlip;
                    want = want != g.mutFlip;
                    break;
                case 1: {
                    size_t d = static_cast<size_t>(g.mutDelay);
                    want = g.hist.size() > d ? g.hist[g.hist.size() - 1 - d] != 0 : false;
                    break;
                }
                case 2:
                    want = false;
                    break;
                case 3:
                    if (g.pulseLeft > 0) {
                        g.pulseLeft--;
                        want = true;
                    } else if (rand01() < 0.012f) {
                        g.pulseLeft = 4 + static_cast<int>(nextRand() % 50);
                    }
                    break;
                default:
                    break;
            }
        }

        setHold(pl, want);
        g.inputs.push_back(want ? 1 : 0);

        GJBaseGameLayer::update(STEP);
        g.step++;

        if (g.simDone) {
            // solucion encontrada: se verifica repitiendola entera desde cero
            startReplay(pl, static_cast<int>(g.inputs.size()), true);
        } else if (g.step > 240 * 1200 || g.totalFails > 40000) {
            goLive(pl);
        }
    }

    // ---- un paso de repeticion desde cero ----
    void replayStep(PlayLayer* pl) {
        auto p = pl->m_player1;
        if (!p) {
            GJBaseGameLayer::update(STEP);
            return;
        }
        if (g.died) {
            g.died = false;
            repair(pl, g.step);
            return;
        }
        if (g.simDone) {
            finishSolve(pl);
            return;
        }
        if (g.step >= g.replayUntil) {
            if (g.verifyFull) {
                repair(pl, g.step); // se acabaron los pasos y no se completo
            } else {
                // prefijo reproducido: seguir buscando desde este punto
                g.phase = PH_SOLVE;
                g.mem.reset();
                g.mutLeft = 0;
                g.fails = 0;
                g.failX = -1.f;
            }
            return;
        }

        float px = p->getPositionX();
        g.bestX = std::max(g.bestX, px);
        if (px - g.lastCpX >= 45.f && !g.holding && !p->m_isDead && g.step > 0) {
            if (pl->markCheckpoint()) g.cpStep.push_back(g.step);
            g.lastCpX = px;
        }

        setHold(pl, g.inputs[static_cast<size_t>(g.step)] != 0);
        GJBaseGameLayer::update(STEP);
        g.step++;
    }

    // ---- un paso de ejecucion (solucion o en directo) ----
    void playStep(PlayLayer* pl) {
        auto p = pl->m_player1;
        if (p && g.phase == PH_PLAY && !p->m_isDead) {
            // mismas condiciones que la repeticion de verificacion (checkpoints incluidos)
            float px = p->getPositionX();
            if (px - g.lastCpX >= 45.f && !g.holding && g.step > 0) {
                pl->markCheckpoint();
                g.lastCpX = px;
            }
        }
        if (p && !p->m_isDead) {
            bool want = false;
            if (g.phase == PH_PLAY) {
                want = static_cast<size_t>(g.step) < g.inputs.size() && g.inputs[g.step] != 0;
            } else {
                acgdl::DecideOpts o;
                o.wasHolding = g.holding;
                want = acgdl::decideHold(p, g.data, g.mem, g.step, o);
            }
            setHold(pl, want);
        }
        GJBaseGameLayer::update(STEP);
        g.step++;
    }

    void update(float dt) {
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this || g.phase == PH_OFF || !g.data.ready) {
            GJBaseGameLayer::update(dt);
            return;
        }

        auto& st = acgdl::stats();

        // esperando a que el nivel arranque: el juego corre normal unos frames
        if (g.phase == PH_WAIT) {
            GJBaseGameLayer::update(dt);
            if (!botOn()) {
                g.phase = PH_OFF;
                return;
            }
            if (g.startReady && ++g.waitFrames > 30) {
                beginSolve(pl);
            }
            return;
        }

        // peticiones del panel / pantalla de carga
        if (!botOn() || st.cancelRequested) {
            st.cancelRequested = false;
            leaveBot(pl);
            GJBaseGameLayer::update(dt);
            return;
        }
        if (st.skipRequested) {
            st.skipRequested = false;
            if (g.phase == PH_SOLVE || g.phase == PH_REPLAY) goLive(pl);
        }
        if (st.resolveRequested) {
            st.resolveRequested = false;
            g.resolves++;
            st.resolves = g.resolves;
            beginSolve(pl);
        }

        // la ejecucion fallo: se repara solo el tramo malo (el resto ya esta verificado)
        if (g.phase == PH_PLAY && g.died) {
            g.died = false;
            repair(pl, g.step);
        }

        if (g.finished) {
            GJBaseGameLayer::update(dt);
            return;
        }

        // tras reiniciar el nivel se deja pasar unos frames sin tocar la fisica
        if (g.settle > 0) {
            g.settle--;
            return;
        }

        if (g.phase == PH_SOLVE || g.phase == PH_REPLAY) {
            auto t0 = std::chrono::steady_clock::now();
            do {
                if (g.phase == PH_REPLAY) this->replayStep(pl);
                else this->simStep(pl);
            } while ((g.phase == PH_SOLVE || g.phase == PH_REPLAY) &&
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 0.012);

            if (g.phase == PH_SOLVE || g.phase == PH_REPLAY) {
                st.percent = std::clamp(g.bestX / std::max(1.f, g.data.endX) * 100.f, 0.f, 100.f);
                st.attempts = g.totalFails;
                st.checkpoints = static_cast<int>(g.cpStep.size());
                st.steps = g.step;
            }
            return; // el tiempo real no mueve el juego mientras se resuelve
        }

        g.acc += std::min(dt, 0.1f);
        int n = 0;
        while (g.acc >= STEP && n++ < 60 && !g.finished) {
            this->playStep(pl);
            g.acc -= STEP;
        }
        updatePercent(pl);
    }
};
