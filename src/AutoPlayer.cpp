#include "AutoPlayer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace geode::prelude;

namespace acgdl {

    namespace {

        constexpr float HALF = 15.f;           // medio cubo
        constexpr float UNITS_PER_SPEED = 346.2f; // m_playerSpeed -> unidades/segundo
        constexpr float JUMP_TIME = 0.39f;     // duracion de un salto de cubo

        struct Cluster {
            bool found = false;
            float start = 0.f;
            float end = 0.f;
        };

        // Primer indice con minX >= x
        size_t firstFrom(std::vector<Box> const& v, float x) {
            auto it = std::lower_bound(v.begin(), v.end(), x, [](Box const& b, float val) {
                return b.minX < val;
            });
            return static_cast<size_t>(it - v.begin());
        }

        bool inLane(Box const& b, float py, float below, float above) {
            return b.maxY >= py - below && b.minY <= py + above;
        }

        // Agrupa pinchos pegados que estan en el mismo carril que el jugador.
        Cluster findCluster(std::vector<Box> const& v, float px, float py, float look, float gap, float below = 18.f, float above = 22.f) {
            Cluster c;
            for (size_t i = firstFrom(v, px - 10.f); i < v.size(); i++) {
                Box const& b = v[i];
                if (b.minX > px + look) break;
                if (!inLane(b, py, below, above)) continue;
                if (!c.found) {
                    c.found = true;
                    c.start = b.minX;
                    c.end = b.maxX;
                } else if (b.minX - c.end <= gap) {
                    c.end = std::max(c.end, b.maxX);
                } else {
                    break;
                }
            }
            return c;
        }

        // Hay una pared de bloques delante (a la altura del jugador)?
        bool wallAhead(LevelData const& d, float px, float py, float dist) {
            size_t i = firstFrom(d.solids, px);
            for (; i < d.solids.size(); i++) {
                Box const& b = d.solids[i];
                if (b.minX - (px + HALF) > dist) break;
                if (b.minX < px + HALF - 2.f) continue;
                if (b.maxY > py - 12.f && b.minY < py + 12.f) return true;
            }
            return false;
        }

        bool orbUnderPlayer(LevelData const& d, float px, float py) {
            float lo = px - 70.f;
            for (size_t i = firstFrom(d.orbs, lo); i < d.orbs.size(); i++) {
                Box const& o = d.orbs[i];
                if (o.minX > px + 70.f) break;
                bool hitX = px + HALF >= o.minX - 10.f && px - HALF <= o.maxX + 10.f;
                bool hitY = py + HALF >= o.minY - 10.f && py - HALF <= o.maxY + 10.f;
                if (hitX && hitY) return true;
            }
            return false;
        }

        // Altura objetivo para modos de vuelo: pasillo libre entre obstaculos cercanos.
        float corridorTarget(LevelData const& d, float px, float py, float look, bool& bounded) {
            float low = -std::numeric_limits<float>::infinity();
            float high = std::numeric_limits<float>::infinity();

            auto scan = [&](std::vector<Box> const& v, float maxW) {
                size_t i = firstFrom(v, px - HALF - maxW);
                for (; i < v.size(); i++) {
                    Box const& b = v[i];
                    if (b.minX > px + look) break;
                    if (b.maxX < px - HALF) continue;
                    float cy = (b.minY + b.maxY) * 0.5f;
                    if (cy > py) high = std::min(high, b.minY);
                    else low = std::max(low, b.maxY);
                }
            };
            scan(d.hazards, d.maxHazardW);
            scan(d.solids, d.maxSolidW);

            bool hasLow = std::isfinite(low);
            bool hasHigh = std::isfinite(high);
            bounded = hasLow || hasHigh;

            if (hasLow && hasHigh) return (low + high) * 0.5f;
            if (hasLow) return std::max(py, low + 45.f);
            if (hasHigh) return std::min(py, high - 45.f);
            return py;
        }

    }

    namespace {

        // Pasillo del jugador: suelo y techo mas cercanos por delante.
        bool corridorBounds(LevelData const& d, float px, float py, float look, float& low, float& high) {
            low = -std::numeric_limits<float>::infinity();
            high = std::numeric_limits<float>::infinity();
            size_t i = firstFrom(d.solids, px - HALF - d.maxSolidW);
            for (; i < d.solids.size(); i++) {
                Box const& b = d.solids[i];
                if (b.minX > px + look) break;
                if (b.maxX < px - HALF) continue;
                float cy = (b.minY + b.maxY) * 0.5f;
                if (cy > py) high = std::min(high, b.minY);
                else low = std::max(low, b.maxY);
            }
            return std::isfinite(low) && std::isfinite(high) && high - low > 40.f;
        }

        // Al cambiar de lado (bola/spider), el otro lado tiene pinchos justo donde aterrizaria?
        bool oppositeBlocked(LevelData const& d, float px, float py, float ups) {
            float low, high;
            if (!corridorBounds(d, px, py, ups * 0.6f, low, high)) return false;
            float destY = py < (low + high) * 0.5f ? high - HALF : low + HALF;
            float x0 = px + HALF + ups * 0.04f;
            float x1 = px + HALF + ups * 0.40f;
            for (size_t i = firstFrom(d.hazards, x0 - d.maxHazardW); i < d.hazards.size(); i++) {
                Box const& h = d.hazards[i];
                if (h.minX > x1) break;
                if (h.maxX < x0) continue;
                if (h.maxY >= destY - 18.f && h.minY <= destY + 18.f) return true;
            }
            return false;
        }

    }

    bool decideHold(PlayerObject* p, LevelData const& d, BotMemory& mem, int step, DecideOpts opts) {
        float px = p->getPositionX();
        float py = p->getPositionY();
        float speed = p->m_playerSpeed > 0.f ? p->m_playerSpeed : 0.9f;
        float ups = speed * UNITS_PER_SPEED;
        bool flipped = p->m_isUpsideDown;

        // velocidad vertical medida (pantalla), por paso
        if (!mem.init) {
            mem.init = true;
            mem.vy = 0.f;
        } else {
            mem.vy = py - mem.lastY;
        }
        mem.lastY = py;
        float vy = mem.vy;

        // ---- Wave: mantener la altura del pasillo ----
        if (p->m_isDart) {
            bool bounded;
            float target = corridorTarget(d, px, py, ups * 0.35f, bounded);
            bool wantUp = py < target - 2.f;
            return flipped ? !wantUp : wantUp;
        }

        // ---- Nave: control con prediccion de velocidad ----
        if (p->m_isShip) {
            bool bounded;
            float target = corridorTarget(d, px, py, ups * 0.45f, bounded);
            float pred = py + vy * 30.f;
            bool wantUp = pred < target;
            return flipped ? !wantUp : wantUp;
        }

        // ---- UFO: toques repetidos (30 por segundo) ----
        if (p->m_isBird) {
            bool bounded;
            float target = corridorTarget(d, px, py, ups * 0.45f, bounded);
            float pred = py + vy * 30.f;
            bool need = flipped ? (pred > target + 8.f) : (pred < target - 8.f);
            return need && (step % 8 < 2);
        }

        // ---- Swing: cambia de direccion al acercarse a los limites ----
        if (p->m_isSwing) {
            bool bounded;
            float target = corridorTarget(d, px, py, ups * 0.45f, bounded);
            bool goingUp = vy > 0.f;
            float pred = py + vy * 30.f;
            bool needFlip = goingUp ? pred > target + 30.f : pred < target - 30.f;
            return needFlip && (step % 8 < 2);
        }

        // ---- Cubo / bola / robot / spider ----

        // Orbes: hay que SOLTAR y volver a PULSAR al entrar (flanco de pulsacion)
        if (!opts.ignoreOrbs) {
            if (orbUnderPlayer(d, px, py)) {
                if (!mem.inOrb) {
                    mem.inOrb = true;
                    mem.pressedOrb = false;
                }
                if (!mem.pressedOrb) {
                    if (opts.wasHolding) return false; // soltar primero
                    mem.pressedOrb = true;
                    return true;
                }
                return true;
            }
            mem.inOrb = false;
            mem.pressedOrb = false;
        }

        // Evalua pinchos y paredes para un jugador (en dual un clic afecta a los dos).
        auto evalFor = [&](PlayerObject* q) -> bool {
            if (!q->m_isOnGround) return false;
            float qy = q->getPositionY();
            bool qflip = q->m_isUpsideDown;
            float below = qflip ? 22.f : 18.f; // con gravedad invertida el suelo esta arriba
            float above = qflip ? 18.f : 22.f;

            float jumpDist = ups * JUMP_TIME;
            float look = jumpDist + 120.f;
            Cluster c = findCluster(d.hazards, px, qy, look, 45.f, below, above);
            if (c.found) {
                float width = c.end - c.start;
                float center = (c.start + c.end) * 0.5f;
                if (q->m_isBall || q->m_isSpider) {
                    float dist = c.start - (px + HALF);
                    if (dist <= ups * 0.10f) {
                        // cambiar de lado solo si el otro lado esta libre (o ya no queda tiempo)
                        if (oppositeBlocked(d, px, qy, ups) && dist > ups * 0.03f) return false;
                        return true;
                    }
                } else if (width > jumpDist * 0.75f) {
                    if (c.start - (px + HALF) <= jumpDist * 0.30f) return true;
                } else if (center - px <= jumpDist * 0.5f) {
                    return true;
                }
            }
            return wallAhead(d, px, qy, ups * 0.07f);
        };

        if (evalFor(p)) return true;

        auto q2 = opts.p2;
        if (opts.dual && q2 && q2 != p && !q2->m_isShip && !q2->m_isBird && !q2->m_isDart && !q2->m_isSwing) {
            return evalFor(q2);
        }
        return false;
    }

}
