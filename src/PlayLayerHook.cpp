#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>

#include "AutoPlayer.hpp"
#include "LevelScanner.hpp"

using namespace geode::prelude;

static bool botOn() {
    return Mod::get()->getSettingValue<bool>("auto-play");
}

class $modify(ACPlayLayer, PlayLayer) {
    struct Fields {
        acgdl::LevelData data;
        bool holding = false;
        bool finished = false;
        int frame = 0;
    };

    // Al terminar de cargar el nivel: lo leemos entero.
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        m_fields->data = acgdl::scanLevel(this);

        if (botOn() && Mod::get()->getSettingValue<bool>("show-info")) {
            Notification::create(
                m_fields->data.summary(), NotificationIcon::Info, 4.f
            )->show();
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        m_fields->holding = false;
        m_fields->finished = false;
        m_fields->frame = 0;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto f = m_fields.self();
        if (!f->data.ready || f->finished) return;

        if (!botOn() || !m_player1 || m_player1->m_isDead) {
            if (f->holding) {
                f->holding = false;
                this->handleButton(false, 1, true);
            }
            return;
        }

        // Modo seguro: mientras juega el bot no se guarda nada.
        if (Mod::get()->getSettingValue<bool>("safe-mode")) {
            m_isTestMode = true;
        }

        bool want = acgdl::decideHold(m_player1, f->data, f->frame++);
        if (want != f->holding) {
            f->holding = want;
            this->handleButton(want, 1, true);
        }
    }

    // Modo rescate: si el bot fallaria, se ignora la muerte.
    // Ese intento deja de guardarse siempre.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto f = m_fields.self();
        if (botOn() &&
            Mod::get()->getSettingValue<bool>("rescue") &&
            f->data.ready && !f->finished &&
            object != m_anticheatSpike) {
            m_isTestMode = true;
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        auto f = m_fields.self();
        if (f->holding) {
            f->holding = false;
            this->handleButton(false, 1, true);
        }
        f->finished = true;
        PlayLayer::levelComplete();
    }
};
