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

struct Snap {
    CheckpointObject* cp;
    float x;
};

class $modify(ACPlayLayer, PlayLayer) {
    struct Fields {
        acgdl::LevelData data;
        bool holding = false;
        bool finished = false;
        int frame = 0;

        // busqueda con rebobinado
        std::vector<Snap> snaps;
        bool needRewind = false;
        bool gaveUp = false;
        float deathX = 0.f;
        float failX = -1.f;
        float lastSnapX = -1000.f;
        int fails = 0;
        int mutLeft = 0;
        int mutType = 0;
        bool mutFlip = false;
        int mutDelay = 0;
        std::deque<bool> hist;
        uint32_t rng = 0x9E3779B9u;

        ~Fields() {
            for (auto& s : snaps) s.cp->release();
        }

        uint32_t next() {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return rng;
        }
        float rand01() { return (next() & 0xFFFFFF) / 16777216.f; }
    };

    void clearSnaps() {
        auto f = m_fields.self();
        for (auto& s : f->snaps) s.cp->release();
        f->snaps.clear();
        f->lastSnapX = -1000.f;
    }

    void setHold(bool v) {
        auto f = m_fields.self();
        if (v != f->holding) {
            f->holding = v;
            this->handleButton(v, 1, true);
        }
    }

    void takeSnap(float px) {
        auto f = m_fields.self();
        auto cp = this->createCheckpoint();
        if (!cp) return;
        cp->retain();
        f->snaps.push_back({cp, px});
        f->lastSnapX = px;
        if (f->snaps.size() > 260) {
            f->snaps.front().cp->release();
            f->snaps.erase(f->snaps.begin());
        }
    }

    void doRewind() {
        auto f = m_fields.self();
        f->needRewind = false;

        if (f->snaps.empty()) {
            f->gaveUp = true;
            return;
        }

        // nuevo punto de fallo o el mismo de antes?
        if (f->failX < 0.f || f->deathX > f->failX + 60.f) {
            f->failX = f->deathX;
            f->fails = 0;
        }
        f->fails++;
        acgdl::stats().rewinds++;

        if (f->fails > 1500) {
            f->gaveUp = true;
            acgdl::stats().gaveUp = true;
            return;
        }

        // cada vez mas atras si sigue fallando
        size_t back = 1 + std::min(f->fails / 4, 16);
        size_t idx = f->snaps.size() > back ? f->snaps.size() - back : 0;
        while (f->snaps.size() > idx + 1) {
            f->snaps.back().cp->release();
            f->snaps.pop_back();
        }

        this->loadFromCheckpoint(f->snaps[idx].cp);
        f->lastSnapX = f->snaps[idx].x;
        this->setHold(false);
        f->hist.clear();

        // mutacion: otra forma de resolver el tramo
        f->mutType = static_cast<int>(f->next() % 3);
        f->mutLeft = 40 + static_cast<int>(back) * 14 + static_cast<int>(f->next() % 70);
        f->mutFlip = false;
        f->mutDelay = 1 + static_cast<int>(f->next() % 7);
    }

    // Al terminar de cargar el nivel: lo leemos entero.
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        m_fields->data = acgdl::scanLevel(this);
        acgdl::stats() = acgdl::BotStats();
        acgdl::stats().inLevel = true;

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
        f->finished = false;
        f->frame = 0;
        f->needRewind = false;
        f->gaveUp = false;
        f->failX = -1.f;
        f->fails = 0;
        f->mutLeft = 0;
        f->hist.clear();
        this->clearSnaps();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto f = m_fields.self();
        if (!f->data.ready || f->finished) return;

        if (!botOn() || !m_player1 || m_player1->m_isDead) {
            this->setHold(false);
            return;
        }

        // Modo seguro: mientras juega el bot no se guarda nada.
        // Con rebobinado el intento tampoco es legitimo, asi que se marca siempre.
        if (Mod::get()->getSettingValue<bool>("safe-mode") || f->mutLeft > 0 || acgdl::stats().rewinds > 0) {
            m_isTestMode = true;
        }

        bool search = searchOn() && !f->gaveUp;

        if (f->needRewind) {
            this->doRewind();
            return;
        }

        float px = m_player1->getPositionX();
        acgdl::stats().progressX = px;

        // si ya pasamos el punto de fallo, se reinicia el contador
        if (f->failX >= 0.f && px > f->failX + 220.f) {
            f->fails = 0;
            f->failX = -1.f;
        }

        if (search && px - f->lastSnapX >= 45.f) {
            this->takeSnap(px);
            acgdl::stats().snapshots = static_cast<int>(f->snaps.size());
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

    // Si el bot va a morir: no se muere, se rebobina y se prueba otra cosa.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto f = m_fields.self();
        if (botOn() && searchOn() && f->data.ready && !f->finished && !f->gaveUp &&
            object != m_anticheatSpike) {
            if (!f->needRewind) {
                f->needRewind = true;
                f->deathX = m_player1 ? m_player1->getPositionX() : 0.f;
            }
            return;
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
