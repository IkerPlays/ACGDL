#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <deque>
#include <vector>

#include "AutoPlayer.hpp"
#include "BotState.hpp"
#include "LevelScanner.hpp"

using namespace geode::prelude;

static bool botOn() {
    return Mod::get()->getSettingValue<bool>("auto-play");
}

static bool searchOn() {
    return Mod::get()->getSettingValue<bool>("rewind");
}

class $modify(ACPlayLayer, PlayLayer) {
    struct Fields {
        acgdl::LevelData data;
        bool holding = false;
        bool finished = false;
        int frame = 0;

        // busqueda con checkpoints de practica
        float lastCpX = -1000.f;
        float failX = -1.f;
        int fails = 0;
        int mutLeft = 0;
        int mutType = 0;
        bool mutFlip = false;
        int mutDelay = 0;
        std::deque<bool> hist;
        uint32_t rng = 0x9E3779B9u;

        uint32_t next() {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return rng;
        }
        float rand01() { return (next() & 0xFFFFFF) / 16777216.f; }
    };

    bool searching() {
        return botOn() && searchOn() && m_isPracticeMode;
    }

    void setHold(bool v) {
        auto f = m_fields.self();
        if (v != f->holding) {
            f->holding = v;
            this->handleButton(v, 1, true);
        }
    }

    // Al terminar de cargar el nivel: lo leemos entero.
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        m_fields->data = acgdl::scanLevel(this);
        acgdl::stats() = acgdl::BotStats();
        acgdl::stats().inLevel = true;

        // El rebobinado usa el modo practica real del juego (checkpoints oficiales).
        if (botOn() && searchOn() && !m_isPracticeMode) {
            this->togglePracticeMode(true);
        }

        if (botOn() && Mod::get()->getSettingValue<bool>("show-info")) {
            Notification::create(
                m_fields->data.summary(), NotificationIcon::Info, 4.f
            )->show();
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        auto f = m_fields.self();
        f->holding = false;
        f->frame = 0;
        f->hist.clear();
        f->finished = false;
        f->lastCpX = m_player1 ? m_player1->getPositionX() : -1000.f;

        // tras una muerte: probar otra forma de resolver el tramo
        if (f->fails > 0) {
            int back = std::min(f->fails / 4, 16);
            f->mutType = static_cast<int>(f->next() % 3);
            f->mutLeft = 50 + back * 14 + static_cast<int>(f->next() % 80);
            f->mutFlip = false;
            f->mutDelay = 1 + static_cast<int>(f->next() % 7);
        } else {
            f->mutLeft = 0;
        }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto f = m_fields.self();
        if (!f->data.ready || f->finished) return;

        if (!botOn() || !m_player1 || m_player1->m_isDead) {
            this->setHold(false);
            return;
        }

        bool search = this->searching();

        // Modo seguro sin rebobinado: no se guarda nada.
        if (!search && Mod::get()->getSettingValue<bool>("safe-mode")) {
            m_isTestMode = true;
        }

        float px = m_player1->getPositionX();
        acgdl::stats().progressX = px;

        // si ya pasamos el punto de fallo, se reinicia el contador
        if (f->failX >= 0.f && px > f->failX + 220.f) {
            f->fails = 0;
            f->failX = -1.f;
        }

        // checkpoint oficial de practica cada ~45 unidades
        if (search && px - f->lastCpX >= 45.f) {
            this->markCheckpoint();
            f->lastCpX = px;
            acgdl::stats().snapshots = m_checkpointArray ? static_cast<int>(m_checkpointArray->count()) : 0;
        }

        bool want = acgdl::decideHold(m_player1, f->data, f->frame++);

        f->hist.push_back(want);
        if (f->hist.size() > 16) f->hist.pop_front();

        if (f->mutLeft > 0) {
            f->mutLeft--;
            if (f->mutType == 0) {
                // alternar la decision en momentos aleatorios
                if (f->rand01() < 0.05f) f->mutFlip = !f->mutFlip;
                want = want != f->mutFlip;
            } else if (f->mutType == 1) {
                // reaccionar tarde
                size_t d = static_cast<size_t>(f->mutDelay);
                if (f->hist.size() > d) want = f->hist[f->hist.size() - 1 - d];
                else want = false;
            } else {
                // no pulsar nada durante un rato
                want = false;
            }
        }

        this->setHold(want);
    }

    // Cuando el bot muere en practica, el juego vuelve al ultimo checkpoint.
    // Si falla varias veces seguidas en el mismo punto, se descartan checkpoints para ir mas atras.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto f = m_fields.self();
        if (this->searching() && f->data.ready && !f->finished &&
            m_player1 && !m_player1->m_isDead && object != m_anticheatSpike) {
            float dx = m_player1->getPositionX();
            if (f->failX < 0.f || dx > f->failX + 60.f) {
                f->failX = dx;
                f->fails = 0;
            }
            f->fails++;
            acgdl::stats().rewinds++;

            if (f->fails % 4 == 0 && m_checkpointArray && m_checkpointArray->count() > 1) {
                m_checkpointArray->removeLastObject();
            }
            this->setHold(false);
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        auto f = m_fields.self();
        this->setHold(false);
        f->finished = true;
        PlayLayer::levelComplete();
    }

    void onQuit() {
        acgdl::stats().inLevel = false;
        PlayLayer::onQuit();
    }
};
