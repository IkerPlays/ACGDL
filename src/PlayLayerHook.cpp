#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <unordered_set>
#include <vector>

#include "AutoPlayer.hpp"
#include "BotState.hpp"
#include "LevelScanner.hpp"
#include "LoadingPopup.hpp"
#include "Trajectory.hpp"
#include "DebugDraw.hpp"

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
        int deathStep = 0;

        // mutaciones
        int mutLeft = 0;
        int mutType = 0;
        int mutDelay = 4;
        int pulseLeft = 0;
        bool mutFlip = false;
        int mutBase = 0;
        std::vector<std::pair<int, int>> sched; // (inicio, duracion) relativos a mutBase
        int perP = 8;
        int perD = 4;
        int perPhase = 0;
        uint32_t orbSeed = 0;
        int orbSkipPct = 0;
        int orbDelay = 0;
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

        // esperar a que el nivel arranque de verdad tras reiniciar desde el principio
        bool needReady = false;
        float readyWait = 0.f;

        // verificacion desde cero
        int replayUntil = 0;
        bool verifyFull = false;
        int verifyFails = 0;

        // simulador de trayectoria (MPC)
        bool trajOn = false;
        bool mpcDisabled = false;
        std::vector<uint8_t> mpcPlan;
        size_t mpcIdx = 0;
        bool predValid = false;
        int predStep = 0;
        cocos2d::CCPoint pred;
        int checks = 0;
        int mismatch = 0;
        std::unordered_set<uint64_t> bad;       // planes del simulador que acabaron en muerte
        std::vector<uint64_t> recentPlans;      // ultimas decisiones tomadas
        std::vector<cocos2d::CCPoint> lastPath; // ultima trayectoria prevista (depuracion)
        bool lastPathOk = false;
        float bestPercent = 0.f;
        bool trajChecked = false;
        float cpSpacing = 45.f;
        acgdl::Humanizer hum;
        int liveDeaths = 0;
        bool ignoreSaved = false;
        int lastSaveStep = 0;
    };

    Run g;

    bool botOn() { return Mod::get()->getSettingValue<bool>("auto-play"); }
    bool searchOn() { return Mod::get()->getSettingValue<bool>("rewind"); }
    bool safeOn() { return Mod::get()->getSettingValue<bool>("safe-mode"); }
    bool levelReady(PlayLayer* pl) {
        return pl->m_started && pl->m_player1 && !pl->m_player1->m_isLocked;
    }

    // Parametros de las entradas humanas (en pasos de 1/240 s).
    bool humanOn() { return Mod::get()->getSettingValue<bool>("human"); }
    void humanParams(int& minHold, int& minRel) {
        int64_t ms = Mod::get()->getSettingValue<int64_t>("human-ms");
        minHold = std::max<int>(1, static_cast<int>(ms * 240 / 1000));
        // donde el bot falla repetidamente, se relajan los minimos para poder pasar
        int f = std::max(g.fails, g.liveDeaths);
        if (f >= 40) minHold = std::max(2, minHold / 4);
        else if (f >= 12) minHold = std::max(4, minHold * 6 / 10);
        minRel = minHold;
    }

    bool saveOn() { return Mod::get()->getSettingValue<bool>("save-solutions"); }

    std::string solKey(PlayLayer* pl) {
        std::string name = pl->m_level ? std::string(pl->m_level->m_levelName) : std::string("?");
        return fmt::format("sol-{}-{}-{}", name, g.data.totalObjects, static_cast<int>(g.data.endX));
    }

    // Guarda la solucion como longitudes de tramos (pulsado / soltado): ocupa muy poco.
    std::string encodeRuns(std::vector<uint8_t> const& in, bool complete) {
        std::string out = complete ? "C" : "P";
        if (in.empty()) return out;
        out += in[0] ? '1' : '0';
        out += ':';
        size_t run = 1;
        for (size_t i = 1; i < in.size(); i++) {
            if (in[i] == in[i - 1]) {
                run++;
            } else {
                out += std::to_string(run);
                out += ',';
                run = 1;
            }
        }
        out += std::to_string(run);
        return out;
    }

    bool decodeRuns(std::string const& s, std::vector<uint8_t>& out, bool& complete) {
        out.clear();
        if (s.size() < 4 || (s[0] != 'C' && s[0] != 'P') || s[2] != ':') return false;
        complete = s[0] == 'C';
        bool val = s[1] == '1';
        size_t i = 3;
        while (i < s.size()) {
            size_t j = s.find(',', i);
            if (j == std::string::npos) j = s.size();
            int run = std::atoi(s.substr(i, j - i).c_str());
            if (run <= 0 || run > 5000000) return false;
            out.insert(out.end(), static_cast<size_t>(run), val ? 1 : 0);
            val = !val;
            i = j + 1;
        }
        return !out.empty();
    }

    void saveSolution(PlayLayer* pl, bool complete) {
        if (!saveOn() || g.inputs.size() < 240) return;
        Mod::get()->setSavedValue<std::string>(solKey(pl), encodeRuns(g.inputs, complete));
    }

    bool loadSolution(PlayLayer* pl) {
        auto str = Mod::get()->getSavedValue<std::string>(solKey(pl), "");
        if (str.empty()) return false;
        bool complete = false;
        std::vector<uint8_t> v;
        if (!decodeRuns(str, v, complete)) return false;
        g.inputs = std::move(v);
        return true;
    }

    void startReplay(PlayLayer* pl, int until, bool full);

    // Cada modo tiene su ritmo natural, medido en macros reales de extreme demons
    // (wave y nave ~55 ms minimo, saltos ~25-30 ms; las pausas son mas largas).
    // Los valores son multiplicadores sobre el minimo base (ajuste "human-ms").
    void humanForPlayer(PlayerObject* q, int& mh, int& mr) {
        float hm = 1.f, rm = 1.f;
        if (q->m_isDart) { hm = 1.8f; rm = 1.8f; }
        else if (q->m_isShip) { hm = 1.8f; rm = 2.0f; }
        else if (q->m_isBird) { hm = 1.0f; rm = 1.7f; }
        else if (q->m_isBall || q->m_isSpider) { hm = 1.0f; rm = 1.3f; }
        else if (q->m_isSwing) { hm = 1.1f; rm = 1.7f; }
        else if (q->m_isRobot) { hm = 1.3f; rm = 1.7f; }
        else { hm = 0.85f; rm = 1.7f; }
        // donde se falla repetidamente, humanParams ya ha relajado mh; se respeta la proporcion
        mh = std::max(2, static_cast<int>(static_cast<float>(mh) * hm + 0.5f));
        mr = std::max(2, static_cast<int>(static_cast<float>(mr) * rm + 0.5f));
    }

    bool trajSetting() { return Mod::get()->getSettingValue<bool>("trajectory"); }

    // Primer obstaculo no modelado (portal, orbe, pad) en el carril del jugador.
    float nextStopX(acgdl::LevelData const& d, float px, float py, float limit) {
        auto it = std::lower_bound(d.stops.begin(), d.stops.end(), px - 40.f,
            [](acgdl::Stop const& s, float v) { return s.box.minX < v; });
        for (; it != d.stops.end(); ++it) {
            if (it->box.minX > limit) break;
            auto const& b = it->box;
            if (b.maxY + it->slack >= py - 20.f && b.minY - it->slack <= py + 20.f) return b.minX - 25.f;
        }
        return limit;
    }

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

    // Longitud real del nivel segun el juego (el porcentaje de GD es x / longitud).
    float levelLen(PlayLayer* pl) {
        float len = pl->m_levelLength;
        return len > 100.f ? len : std::max(1.f, g.data.endX);
    }

    void updatePercent(PlayLayer* pl) {
        if (!pl->m_player1) return;
        acgdl::stats().percent = std::clamp(pl->getCurrentPercent(), 0.f, 100.f);
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
        g.trajChecked = false;
        mute(false);
        g.step = 0;
        g.acc = 0.0;
        g.holding = false;
        g.mem.reset();
        restartClean(pl);
        g.needReady = true;
        g.readyWait = 0.f;
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
        g.bestPercent = 0.f;
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

        g.needReady = true;
        g.readyWait = 0.f;
        g.trajOn = trajSetting() && acgdl::traj::init(pl);
        g.mpcDisabled = false;
        g.mpcPlan.clear();
        g.mpcIdx = 0;
        g.predValid = false;
        g.checks = 0;
        g.mismatch = 0;
        log::info("[AutoComplete] simulador de trayectoria: {}", g.trajOn ? "activo" : "no disponible");
        g.settle = 4;
        g.replayUntil = 0;
        g.verifyFull = false;
        g.verifyFails = 0;
        st.diag = fmt::format("Longitud del nivel (juego): {}  |  objetos hasta x={}",
            static_cast<int>(pl->m_levelLength), static_cast<int>(g.data.endX));
        log::info("[AutoComplete] solver iniciado ({})", st.diag);
        if (auto popup = acgdl::LoadingPopup::create()) {
            popup->show();
        }

        // si ya hay una solucion (o progreso) guardado de este nivel, se retoma
        if (saveOn() && !g.ignoreSaved && loadSolution(pl)) {
            st.diag = fmt::format("Solucion guardada encontrada ({} pasos): verificando...", g.inputs.size());
            log::info("[AutoComplete] solucion guardada: {} pasos", g.inputs.size());
            startReplay(pl, static_cast<int>(g.inputs.size()), true);
        }
        g.ignoreSaved = false;
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
        g.mpcPlan.clear();
        g.mpcIdx = 0;
        g.predValid = false;

        mute(true);
        pl->removeAllCheckpoints();
        pl->resetLevelFromStart();
        g.needReady = true;
        g.readyWait = 0.f;
        g.settle = 3;
        log::info("[AutoComplete] repeticion desde cero: {} pasos (completa: {})", until, full);
        st.diag = full ? fmt::format("Verificando la solucion desde cero ({} pasos)...", until)
                       : fmt::format("Reparando: repitiendo {} pasos ya verificados...", until);

        if (!wasLoading) {
            if (auto popup = acgdl::LoadingPopup::create()) popup->show();
        }
    }

    // La repeticion desde cero fallo en el paso k: se conserva lo verificado antes y se resuelve de nuevo desde ahi.
    void repair(PlayLayer* pl, int k) {
        g.verifyFails++;
        acgdl::stats().diag = fmt::format("La repeticion fallo en el paso {} (x={}); reparando (intento {})...",
            k, pl->m_player1 ? static_cast<int>(pl->m_player1->getPositionX()) : 0, g.verifyFails);
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
        saveSolution(pl, true);
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
        g.needReady = true;
        g.readyWait = 0.f;
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

        acgdl::traj::reset();
        acgdl::dbg::reset();
        g = Run();
        auto& st = acgdl::stats();
        st = acgdl::BotStats();
        st.inLevel = true;

        g.data = acgdl::scanLevel(this);
        // niveles enormes: checkpoints mas espaciados (cada uno es caro). Igual en resolver, repetir y ejecutar.
        g.cpSpacing = g.data.totalObjects > 100000 ? 120.f : g.data.totalObjects > 40000 ? 75.f : 45.f;
        st.info = g.data.summary();
        log::info("[AutoComplete] objetos: {}", g.data.typeReport);

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
        g.hum = acgdl::Humanizer();
        g.mem.reset();
        g.mpcPlan.clear();
        g.mpcIdx = 0;
        g.predValid = false;

        g.lastCpX = m_player1 ? m_player1->getPositionX() : -1000.f;
        if (g.step == 0 || g.phase == PH_PLAY || g.phase == PH_LIVE) {
            g.needReady = true;
            g.readyWait = 0.f;
        }
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
        if (acgdl::traj::active()) {
            acgdl::traj::markDeath();
            return;
        }
        if (g.phase != PH_OFF && object != m_anticheatSpike) {
            if (g.phase == PH_LIVE) {
                g.liveDeaths++;
                // en directo: recordar los planes del simulador que acabaron en muerte (aprende entre intentos)
                for (size_t i = g.recentPlans.size() > 3 ? g.recentPlans.size() - 3 : 0; i < g.recentPlans.size(); i++) {
                    g.bad.insert(g.recentPlans[i]);
                }
                g.recentPlans.clear();
            }
            if (g.phase == PH_SOLVE || g.phase == PH_REPLAY || g.phase == PH_PLAY) {
                if (!g.died) {
                    g.died = true;
                    g.deathX = m_player1 ? m_player1->getPositionX() : 0.f;
                    g.deathStep = g.step;
                    if (m_player1) {
                        auto p = m_player1;
                        char const* mode = p->m_isDart ? "wave" : p->m_isShip ? "nave" : p->m_isBird ? "ufo"
                            : p->m_isBall ? "bola" : p->m_isRobot ? "robot" : p->m_isSpider ? "spider"
                            : p->m_isSwing ? "swing" : "cubo";
                        acgdl::stats().diag = fmt::format(
                            "Ultimo fallo: {} x={} y={} vel={:.1f} | objeto id {} tipo {}",
                            mode, static_cast<int>(g.deathX), static_cast<int>(p->getPositionY()),
                            p->m_playerSpeed,
                            object ? object->m_objectID : -1,
                            object ? static_cast<int>(object->m_objectType) : -1
                        );
                        if (object) acgdl::stats().diag += fmt::format(" ({})", acgdl::typeName(static_cast<int>(object->m_objectType)));
                    }
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
        acgdl::traj::reset();
        acgdl::dbg::reset();
        g.trajOn = false;
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
        // los ultimos planes del simulador no funcionaron: no repetirlos
        for (size_t i = g.recentPlans.size() > 3 ? g.recentPlans.size() - 3 : 0; i < g.recentPlans.size(); i++) {
            g.bad.insert(g.recentPlans[i]);
        }
        g.recentPlans.clear();
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
        g.mpcPlan.clear();
        g.mpcIdx = 0;
        g.predValid = false;
        pl->resetLevel();

        // otra forma de resolver el tramo
        int len = std::max(0, g.deathStep - target);
        int back = std::min(g.fails / 6, 16);
        g.mutBase = target;
        g.mutType = static_cast<int>(nextRand() % 12);
        g.orbSeed = nextRand();
        g.orbSkipPct = (nextRand() % 3 == 0) ? 0 : static_cast<int>(nextRand() % 70);
        g.orbDelay = (nextRand() % 2 == 0) ? 0 : static_cast<int>(nextRand() % 15) - 6;
        g.mutFlip = false;
        g.pulseLeft = 0;
        g.mutDelay = 4 + static_cast<int>(nextRand() % 25);

        if (g.mutType >= 5 && g.mutType <= 7) {
            // programa de pulsaciones: 1-4 pulsaciones en la ventana hasta el punto de fallo
            g.mutLeft = std::clamp(len + 40, 100, 1200) + static_cast<int>(nextRand() % 80);
            g.sched.clear();
            int n = 1 + static_cast<int>(nextRand() % 4);
            for (int i = 0; i < n; i++) {
                int start = static_cast<int>(nextRand() % static_cast<uint32_t>(len + 20));
                int dur = 2 + static_cast<int>(nextRand() % 40);
                g.sched.emplace_back(start, dur);
            }
        } else if (g.mutType == 8 || g.mutType == 9) {
            // patron periodico de mantener/soltar (util en nave y wave)
            g.mutLeft = std::clamp(len + 40, 100, 1200) + static_cast<int>(nextRand() % 80);
            g.perP = 4 + static_cast<int>(nextRand() % 56);
            g.perD = 1 + static_cast<int>(nextRand() % static_cast<uint32_t>(g.perP - 1));
            g.perPhase = static_cast<int>(nextRand() % static_cast<uint32_t>(g.perP));
        } else if (g.mutType >= 10) {
            // pulsaciones alineadas con los orbes de la ventana (combos de orbes)
            g.mutLeft = std::clamp(len + 40, 100, 1200) + static_cast<int>(nextRand() % 80);
            g.sched.clear();
            auto p1 = pl->m_player1;
            float px0 = p1 ? p1->getPositionX() : 0.f;
            float spd = (p1 && p1->m_playerSpeed > 0.f) ? p1->m_playerSpeed : 0.9f;
            float perStep = spd * 346.2f / 240.f;
            float reach = px0 + static_cast<float>(len + 30) * perStep + 40.f;
            for (auto const& o : g.data.orbs) {
                if (o.maxX < px0) continue;
                if (o.minX > reach) break;
                if (rand01() < 0.8f) {
                    int t0 = static_cast<int>((o.minX - 10.f - 15.f - px0) / perStep);
                    int jitter = static_cast<int>(nextRand() % 11) - 4;
                    int dur = (nextRand() % 2 == 0) ? 3 + static_cast<int>(nextRand() % 8)
                                                    : 20 + static_cast<int>(nextRand() % 80);
                    g.sched.emplace_back(std::max(0, t0 + jitter), dur);
                }
            }
            // y alguna pulsacion extra al azar
            int extra = static_cast<int>(nextRand() % 3);
            for (int i = 0; i < extra; i++) {
                g.sched.emplace_back(
                    static_cast<int>(nextRand() % static_cast<uint32_t>(len + 20)),
                    2 + static_cast<int>(nextRand() % 40));
            }
        } else {
            g.mutLeft = 160 + back * 40 + static_cast<int>(nextRand() % 300);
        }

        // con el simulador, la busqueda sistematica va primero; el azar solo tras muchos fallos
        if (g.trajOn && !g.mpcDisabled && g.fails <= 10) g.mutLeft = 0;
    }

    // Politica con simulador: prueba el comportamiento del bot y variaciones (pulsar/soltar,
    // doble toque) sobre un jugador falso y elige la que sobrevive. Recuerda los planes que
    // acabaron en muerte para no repetirlos (busqueda sistematica).
    bool mpcStep(PlayLayer* pl, PlayerObject* p, bool base, acgdl::DecideOpts o) {
        constexpr int HORIZON = 84;
        constexpr int COMMIT = 6;

        if (g.mpcIdx < g.mpcPlan.size()) return g.mpcPlan[g.mpcIdx++] != 0;

        // comprobar la prediccion anterior contra la realidad
        if (g.predValid && g.predStep == g.step) {
            g.checks++;
            auto pos = p->getPosition();
            if (std::fabs(pos.x - g.pred.x) > 5.f || std::fabs(pos.y - g.pred.y) > 5.f) g.mismatch++;
            if (g.checks >= 40 && g.mismatch * 100 > g.checks * 35) {
                g.mpcDisabled = true;
                acgdl::stats().diag = "Simulador de trayectoria desactivado: no coincide con el juego";
                log::warn("[AutoComplete] simulador desactivado ({} de {} predicciones fallaron)", g.mismatch, g.checks);
            }
        }
        g.predValid = false;
        if (g.mpcDisabled || !acgdl::traj::ready()) return base;

        float px = p->getPositionX();
        float py = p->getPositionY();
        float spd = p->m_playerSpeed > 0.f ? p->m_playerSpeed : 0.9f;
        float perStep = spd * 346.2f / 240.f;
        float limit = px + HORIZON * perStep;

        bool orbsIn = Mod::get()->getSettingValue<bool>("trajectory-orbs");
        float stopX = limit;
        {
            auto const& list = orbsIn ? g.data.portalStops : g.data.stops;
            auto it = std::lower_bound(list.begin(), list.end(), px - 40.f,
                [](acgdl::Stop const& s2, float v) { return s2.box.minX < v; });
            for (; it != list.end(); ++it) {
                if (it->box.minX > limit) break;
                auto const& b2 = it->box;
                if (b2.maxY + it->slack >= py - 20.f && b2.minY - it->slack <= py + 20.f) {
                    stopX = b2.minX - 25.f;
                    break;
                }
            }
        }
        int n = std::min(HORIZON, static_cast<int>((stopX - px) / perStep));
        if (n < 18) return base; // demasiado cerca de algo que el simulador no modela

        // objetos cuyo estado se restaura tras simular
        std::vector<GameObject*> touch;
        if (orbsIn) {
            float x0 = px - 80.f, x1 = limit + 80.f;
            for (size_t i = 0; i < g.data.orbs.size(); i++) {
                if (g.data.orbs[i].maxX < x0) continue;
                if (g.data.orbs[i].minX > x1) break;
                touch.push_back(g.data.orbObjs[i]);
            }
            for (size_t i = 0; i < g.data.pads.size(); i++) {
                if (g.data.pads[i].maxX < x0) continue;
                if (g.data.pads[i].minX > x1) break;
                touch.push_back(g.data.padObjs[i]);
            }
        }

        struct Cand { int kind; int t; int d; int t2; int d2; };
        std::vector<Cand> cands;
        cands.push_back({0, 0, 0, 0, 0}); // el bot tal cual
        cands.push_back({1, 0, 0, 0, 0}); // mantener
        cands.push_back({2, 0, 0, 0, 0}); // soltar
        static const int ds[] = {2, 5, 10, 20, 40};
        static const int ts[] = {0, 3, 6, 10, 16, 24};
        for (int d : ds) {
            for (int t : ts) {
                cands.push_back({3, t, d, 0, 0}); // pulsar durante [t, t+d)
                cands.push_back({4, t, d, 0, 0}); // soltar durante [t, t+d)
            }
        }
        // durante un dash hay que decidir CUANDO soltar: se prueban momentos mas tardios
        if (p->m_isDashing) {
            static const int dashT[] = {32, 40, 50, 60, 72};
            for (int t : dashT) {
                cands.insert(cands.begin() + 3, {4, t, 60, 0, 0});
            }
        }
        // dobles toques (saltos dobles, rampas seguidas): dos pulsaciones separadas
        static const int t1s[] = {0, 6, 14};
        static const int d1s[] = {3, 8};
        static const int gaps[] = {4, 10, 18, 30};
        static const int d2s[] = {3, 8, 20};
        for (int t1 : t1s) for (int d1 : d1s) for (int gp : gaps) for (int d2 : d2s) {
            cands.push_back({5, t1, d1, t1 + d1 + gp, d2});
        }

        auto keyOf = [&](Cand const& c) {
            return (static_cast<uint64_t>(g.step) << 32) ^ (static_cast<uint64_t>(c.kind) << 28) ^
                   (static_cast<uint64_t>(c.t) << 21) ^ (static_cast<uint64_t>(c.d) << 14) ^
                   (static_cast<uint64_t>(c.t2) << 7) ^ static_cast<uint64_t>(c.d2);
        };

        bool h0 = g.holding;
        std::vector<uint8_t> acts;
        std::vector<cocos2d::CCPoint> path;

        int minHold = 1, minRel = 1;
        bool human = humanOn();
        humanParams(minHold, minRel);

        auto run = [&](Cand const& c) {
            acgdl::Humanizer hm = g.hum;
            acgdl::BotMemory mem = g.mem;
            acgdl::DecideOpts oo = o;
            bool last = h0;
            acts.assign(static_cast<size_t>(n), 0);
            return acgdl::traj::simulate(pl, p, n, stopX, [&](int i, PlayerObject* f) {
                bool w;
                if (c.kind == 1) {
                    w = true;
                } else if (c.kind == 2) {
                    w = false;
                } else {
                    oo.wasHolding = last;
                    w = acgdl::decideHold(f, g.data, mem, g.step + i, oo);
                    if (c.kind == 3 && i >= c.t && i < c.t + c.d) w = true;
                    if (c.kind == 4 && i >= c.t && i < c.t + c.d) w = false;
                    if (c.kind == 5 && ((i >= c.t && i < c.t + c.d) || (i >= c.t2 && i < c.t2 + c.d2))) w = true;
                }
                if (human) {
                    int mh = minHold, mr = minRel;
                    humanForPlayer(f, mh, mr);
                    w = acgdl::humanize(hm, w, mh, mr, f->m_isDashing);
                }
                last = w;
                if (i < n) acts[static_cast<size_t>(i)] = w ? 1 : 0;
                return w;
            }, h0, &path, orbsIn ? &touch : nullptr, orbsIn);
        };

        auto t0 = std::chrono::steady_clock::now();
        auto elapsed = [&]() {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        };

        int bestScore = -1;
        uint64_t bestKey = 0;
        std::vector<uint8_t> bestActs;
        std::vector<cocos2d::CCPoint> bestPath;
        bool survivor = false;
        size_t evaluated = 0;

        // 1.a pasada: planes que no han matado antes; 2.a: todos si no queda ninguno
        for (int pass = 0; pass < 2 && !survivor && bestActs.empty(); pass++) {
            for (auto const& c : cands) {
                if (pass == 0 && g.bad.count(keyOf(c))) continue;
                // presupuesto: poco si el plan base sobrevive, mas si hace falta buscar
                bool needMore = bestScore < n + 1000;
                double budget = needMore && evaluated >= 26 ? 0.035 : 0.012;
                if (evaluated >= 26 && elapsed() > budget) break;
                if (evaluated >= cands.size()) break;
                auto r = run(c);
                evaluated++;
                int score = r.died ? r.steps : n + 1000;
                if (score > bestScore) {
                    bestScore = score;
                    bestKey = keyOf(c);
                    bestActs = acts;
                    bestPath = path;
                }
                if (!r.died) { survivor = true; break; }
            }
        }
        if (bestActs.empty()) return base;

        g.lastPath = bestPath;
        g.lastPathOk = survivor;
        g.recentPlans.push_back(bestKey);
        if (g.recentPlans.size() > 6) g.recentPlans.erase(g.recentPlans.begin());

        size_t commit = std::min(static_cast<size_t>(COMMIT), bestActs.size());
        g.mpcPlan.assign(bestActs.begin(), bestActs.begin() + static_cast<long>(commit));
        g.mpcIdx = 1;
        if (bestPath.size() >= commit) {
            g.pred = bestPath[commit - 1];
            g.predStep = g.step + static_cast<int>(commit);
            g.predValid = true;
        }
        return g.mpcPlan[0] != 0;
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
        g.bestPercent = std::max(g.bestPercent, pl->getCurrentPercent());

        if (g.failX >= 0.f && px > g.failX + 220.f) {
            g.fails = 0;
            g.failX = -1.f;
            g.bad.clear();
        }

        // checkpoint oficial de practica (solo sin boton pulsado, para que el estado se reproduzca igual)
        if (px - g.lastCpX >= g.cpSpacing && !g.holding && !p->m_isDead && g.step > 0) {
            if (pl->markCheckpoint()) g.cpStep.push_back(g.step);
            g.lastCpX = px;
        }

        acgdl::DecideOpts o;
        o.wasHolding = g.holding;
        o.ignoreOrbs = g.mutLeft > 0 && g.mutType == 4;
        if (g.mutLeft > 0) {
            o.orbSeed = g.orbSeed;
            o.orbSkipPct = g.orbSkipPct;
            o.orbDelay = g.orbDelay;
        }
        o.dual = pl->m_gameState.m_isDualMode;
        o.p2 = pl->m_player2;
        bool want = acgdl::decideHold(p, g.data, g.mem, g.step, o);
        if (g.trajOn && g.mutLeft == 0 && !p->m_isDead) want = this->mpcStep(pl, p, want, o);

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
                case 5:
                case 6:
                case 7:
                case 10:
                case 11: {
                    int t = g.step - g.mutBase;
                    want = false;
                    for (auto const& pr : g.sched) {
                        if (t >= pr.first && t < pr.first + pr.second) want = true;
                    }
                    break;
                }
                case 8:
                case 9: {
                    int t = g.step - g.mutBase + g.perPhase;
                    want = (t % g.perP) < g.perD;
                    break;
                }
                default:
                    break;
            }
        }

        {
            int minHold = 1, minRel = 1;
            humanParams(minHold, minRel);
            humanForPlayer(p, minHold, minRel);
            if (humanOn()) want = acgdl::humanize(g.hum, want, minHold, minRel, p->m_isDashing);
            else g.hum.cur = want;
        }
        setHold(pl, want);
        g.inputs.push_back(want ? 1 : 0);

        GJBaseGameLayer::update(STEP);
        g.step++;

        // progreso guardado cada ~10 s de nivel (permite continuar niveles muy largos)
        if (std::abs(g.step - g.lastSaveStep) >= 2400) {
            saveSolution(pl, false);
            g.lastSaveStep = g.step;
        }

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
        g.bestPercent = std::max(g.bestPercent, pl->getCurrentPercent());
        if (px - g.lastCpX >= g.cpSpacing && !g.holding && !p->m_isDead && g.step > 0) {
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
            if (px - g.lastCpX >= g.cpSpacing && !g.holding && g.step > 0) {
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
                o.dual = pl->m_gameState.m_isDualMode;
                o.p2 = pl->m_player2;
                want = acgdl::decideHold(p, g.data, g.mem, g.step, o);
                if (g.phase == PH_LIVE) {
                    if (!g.trajChecked) {
                        g.trajChecked = true;
                        g.trajOn = trajSetting() && acgdl::traj::init(pl);
                    }
                    if (g.trajOn && !g.mpcDisabled) want = this->mpcStep(pl, p, want, o);
                    int minHold = 1, minRel = 1;
                    humanParams(minHold, minRel);
                    humanForPlayer(p, minHold, minRel);
                    if (humanOn()) want = acgdl::humanize(g.hum, want, minHold, minRel, p->m_isDashing);
                    else g.hum.cur = want;
                }
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
            g.ignoreSaved = true;
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

        // el nivel tarda un instante (tiempo real) en arrancar tras reiniciar desde cero:
        // hasta entonces no se avanza, para que simulacion y ejecucion empiecen en el mismo punto
        if (g.needReady) {
            g.readyWait += dt;
            if (levelReady(pl)) {
                g.needReady = false;
            } else if (g.readyWait > 2.f) {
                g.needReady = false;
                log::warn("[AutoComplete] el nivel no marco inicio tras 2 s, se continua");
            } else {
                return;
            }
        }

        if (g.phase == PH_SOLVE || g.phase == PH_REPLAY) {
            st.elapsed += dt;
            double budget = static_cast<double>(Mod::get()->getSettingValue<int64_t>("solve-budget-ms")) / 1000.0;
            auto t0 = std::chrono::steady_clock::now();
            do {
                if (g.phase == PH_REPLAY) this->replayStep(pl);
                else this->simStep(pl);
            } while ((g.phase == PH_SOLVE || g.phase == PH_REPLAY) &&
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < budget);

            if (g.phase == PH_SOLVE || g.phase == PH_REPLAY) {
                st.percent = std::clamp(g.bestPercent, 0.f, 100.f);
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
        if (Mod::get()->getSettingValue<bool>("debug-draw")) {
            acgdl::dbg::draw(pl, g.data, g.lastPath, g.lastPathOk);
        }
    }
};
