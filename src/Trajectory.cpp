#include "Trajectory.hpp"
#include "LevelScanner.hpp"

#include <Geode/modify/RingObject.hpp>

#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;

namespace acgdl::traj {

    namespace {
        PlayerObject* s_fake = nullptr;
        PlayerCheckpoint* s_cp = nullptr;
        bool s_active = false;
        bool s_cancel = false;
        bool s_allowOrbs = false;
        float s_delta = 0.25f; // en GD 2.2 el jugador se actualiza en unidades de frame a 60 fps (0.25 = un paso a 240 Hz)
    }

    bool active() { return s_active; }
    bool ready() { return s_fake != nullptr && s_cp != nullptr; }
    float stepDelta() { return s_delta; }
    void setDelta(float dt) { if (dt > 0.f && dt < 4.f) s_delta = dt; }
    void markDeath() { s_cancel = true; }
    bool orbsAllowed() { return s_allowOrbs; }

    void reset() {
        s_active = false;
        s_cancel = false;
        if (s_fake) {
            s_fake->removeFromParent();
            s_fake->release();
            s_fake = nullptr;
        }
        if (s_cp) {
            s_cp->release();
            s_cp = nullptr;
        }
    }

    bool init(PlayLayer* pl) {
        reset();
        if (!pl || !pl->m_objectLayer) return false;

        s_fake = PlayerObject::create(1, 1, pl, pl, true);
        if (!s_fake) return false;
        s_fake->retain();
        s_fake->setPosition({0.f, 105.f});
        s_fake->setVisible(false);
        pl->m_objectLayer->addChild(s_fake);

#ifdef GEODE_IS_IOS
        // iOS: el juego no expone el guardado de estado del jugador, sin simulador
        reset();
        return false;
#else
        s_cp = PlayerCheckpoint::create();
        if (!s_cp) {
            reset();
            return false;
        }
        s_cp->retain();
        return true;
#endif
    }

    SimResult simulate(
        PlayLayer* pl, PlayerObject* real, int n, float maxX,
        std::function<bool(int, PlayerObject*)> const& holdAt,
        bool startHolding,
        std::vector<cocos2d::CCPoint>* path,
        std::vector<GameObject*> const* touchables,
        bool allowOrbs
    ) {
        SimResult res;
        if (!ready() || !pl || !real) return res;

        // los orbes y pads tocados por el jugador falso marcan flags en el objeto real: se restauran al acabar
        struct Saved {
            GameObject* obj;
            bool isActivated;
            bool effect;
            bool activated, p1, p2;
        };
        std::vector<Saved> saved;
        if (allowOrbs && touchables) {
            saved.reserve(touchables->size());
            for (auto o : *touchables) {
                if (!o) continue;
                Saved sv{o, o->m_isActivated, false, false, false, false};
                if (auto e = typeinfo_cast<EffectGameObject*>(o)) {
                    sv.effect = true;
                    sv.activated = e->m_activated;
                    sv.p1 = e->m_activatedByPlayer1;
                    sv.p2 = e->m_activatedByPlayer2;
                }
                saved.push_back(sv);
            }
        }

        s_allowOrbs = allowOrbs;
        s_active = true;
        s_cancel = false;

        // copiar el estado real al jugador falso con el sistema de checkpoints del juego
#ifndef GEODE_IS_IOS
        real->saveToCheckpoint(s_cp);
        s_fake->loadFromCheckpoint(s_cp);
#endif
        s_fake->releaseButton(PlayerButton::Jump);
        bool holding = false;
        if (startHolding) {
            holding = true;
            s_fake->pushButton(PlayerButton::Jump);
        }
        if (path) path->clear();

        for (int i = 0; i < n; i++) {
            s_fake->m_collisionLogTop->removeAllObjects();
            s_fake->m_collisionLogBottom->removeAllObjects();
            s_fake->m_collisionLogLeft->removeAllObjects();
            s_fake->m_collisionLogRight->removeAllObjects();

            pl->checkCollisions(s_fake, s_delta, false);
            if (s_cancel) {
                res.died = true;
                break;
            }

            bool want = holdAt(i, s_fake);
            if (want != holding) {
                holding = want;
                if (want) s_fake->pushButton(PlayerButton::Jump);
                else s_fake->releaseButton(PlayerButton::Jump);
            }

            s_fake->update(s_delta);
            s_fake->updateRotation(s_delta);
            s_fake->updatePlayerScale();
            res.steps = i + 1;
            if (path) path->push_back(s_fake->getPosition());

            if (s_cancel) {
                res.died = true;
                break;
            }
            if (s_fake->getPositionX() > maxX) break;
        }

        res.end = s_fake->getPosition();
        s_fake->releaseButton(PlayerButton::Jump);
        s_active = false;
        s_allowOrbs = false;

        for (auto& sv : saved) {
            sv.obj->m_isActivated = sv.isActivated;
            if (sv.effect) {
                auto e = static_cast<EffectGameObject*>(sv.obj);
                e->m_activated = sv.activated;
                e->m_activatedByPlayer1 = sv.p1;
                e->m_activatedByPlayer2 = sv.p2;
            }
        }
        return res;
    }

}

// ---- Hooks: mientras se simula, el nivel real no se toca ----

class $modify(ACTrajBase, GJBaseGameLayer) {
    void collisionCheckObjects(PlayerObject* p0, std::vector<GameObject*>* objects, int p2, float p3) {
        if (acgdl::traj::active()) {
            std::vector<GameObject*> disabled;
            for (auto obj : *objects) {
                if (!obj) continue;
                auto t = obj->m_objectType;
                bool keep = t == GameObjectType::Solid || t == GameObjectType::Hazard ||
                            t == GameObjectType::Slope || t == GameObjectType::AnimatedHazard;
                if (!keep && acgdl::traj::orbsAllowed()) {
                    keep = acgdl::isOrbType(t) || acgdl::isPadType(t);
                }
                if (keep) continue;
                if (obj->m_isDisabled || obj->m_isDisabled2) continue;
                disabled.push_back(obj);
                obj->m_isDisabled = true;
                obj->m_isDisabled2 = true;
            }
            GJBaseGameLayer::collisionCheckObjects(p0, objects, p2, p3);
            for (auto obj : disabled) {
                obj->m_isDisabled = false;
                obj->m_isDisabled2 = false;
            }
            return;
        }
        GJBaseGameLayer::collisionCheckObjects(p0, objects, p2, p3);
    }

    bool canBeActivatedByPlayer(PlayerObject* p0, EffectGameObject* p1) {
        if (acgdl::traj::active()) return false;
        return GJBaseGameLayer::canBeActivatedByPlayer(p0, p1);
    }

    void playerTouchedRing(PlayerObject* p0, RingObject* p1) {
        if (!acgdl::traj::active() || acgdl::traj::orbsAllowed()) GJBaseGameLayer::playerTouchedRing(p0, p1);
    }

    void gameEventTriggered(GJGameEvent p0, int p1, int p2) {
        if (!acgdl::traj::active()) GJBaseGameLayer::gameEventTriggered(p0, p1, p2);
    }

    void playerTouchedTrigger(PlayerObject* p0, EffectGameObject* p1) {
        if (!acgdl::traj::active()) GJBaseGameLayer::playerTouchedTrigger(p0, p1);
    }
};

class $modify(ACTrajPlayer, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);
        if (!acgdl::traj::active()) acgdl::traj::setDelta(dt);
    }

    void playSpiderDashEffect(cocos2d::CCPoint p0, cocos2d::CCPoint p1) {
        if (!acgdl::traj::active()) PlayerObject::playSpiderDashEffect(p0, p1);
    }

    void incrementJumps() {
        if (!acgdl::traj::active()) PlayerObject::incrementJumps();
    }

    void ringJump(RingObject* p0, bool p1) {
        if (!acgdl::traj::active() || acgdl::traj::orbsAllowed()) PlayerObject::ringJump(p0, p1);
    }

    void spawnCircle() {
        if (!acgdl::traj::active()) PlayerObject::spawnCircle();
    }
};

class $modify(ACTrajStreak, HardStreak) {
    void addPoint(cocos2d::CCPoint p0) {
        if (!acgdl::traj::active()) HardStreak::addPoint(p0);
    }
};

class $modify(ACTrajObject, GameObject) {
    void playShineEffect() {
        if (!acgdl::traj::active()) GameObject::playShineEffect();
    }
};

class $modify(ACTrajEffect, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* p0, int p1, std::vector<int> const* p2) {
        if (!acgdl::traj::active()) EffectGameObject::triggerObject(p0, p1, p2);
    }
};

class $modify(ACTrajRing, RingObject) {
    void spawnCircle() {
        if (!acgdl::traj::active()) RingObject::spawnCircle();
    }
};
