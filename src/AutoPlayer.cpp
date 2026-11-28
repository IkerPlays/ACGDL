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

        int orbUnderPlayer(LevelData const& d, float px, float py) {
            float lo = px - 70.f;
            for (size_t i = firstFrom(d.orbs, lo); i < d.orbs.size(); i++) {
                Box const& o = d.orbs[i];
                if (o.minX > px + 70.f) break;
                bool hitX = px + HALF >= o.minX - 10.f && px - HALF <= o.maxX + 10.f;
                bool hitY = py + HALF >= o.minY - 10.f && py - HALF <= o.maxY + 10.f;
                if (hitX && hitY) return static_cast<int>(i);
            }
            return -1;
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

        // Planificador de vuelo (nave, wave, ufo, swing): busca un camino libre entre
        // pinchos y bloques en una rejilla por delante y devuelve la altura objetivo.
        bool planPath(LevelData const& d, float px, float py, float ups, float& outY) {
            constexpr float DX = 15.f;
            constexpr float DY = 15.f;
            constexpr float BOX = 13.f;
            constexpr int R = 41;
            constexpr int MID = 20;
            constexpr int MAXC = 28;

            int cols = std::clamp(static_cast<int>(std::ceil(ups * 0.65f / DX)), 8, MAXC);
            float xmin = px - BOX;
            float xmax = px + cols * DX + BOX;
            float ymin = py - MID * DY - BOX;
            float ymax = py + MID * DY + BOX;

            static std::vector<Box> nearBoxes;
            nearBoxes.clear();
            auto gather = [&](std::vector<Box> const& v, float maxW) {
                for (size_t i = firstFrom(v, xmin - maxW); i < v.size(); i++) {
                    Box const& b = v[i];
                    if (b.minX > xmax) break;
                    if (b.maxX < xmin) continue;
                    if (b.maxY < ymin || b.minY > ymax) continue;
                    nearBoxes.push_back(b);
                }
            };
            gather(d.hazards, d.maxHazardW);
            gather(d.solids, d.maxSolidW);

            static bool blocked[MAXC + 1][R];
            static bool reach[MAXC + 1][R];
            static bool via[MAXC + 1][R];

            for (int c = 0; c <= cols; c++) {
                float cx = px + c * DX;
                for (int r = 0; r < R; r++) {
                    float cy = py + (r - MID) * DY;
                    bool bl = (cy - BOX < 0.f); // suelo
                    if (!bl) {
                        for (auto const& b : nearBoxes) {
                            if (b.maxX > cx - BOX && b.minX < cx + BOX &&
                                b.maxY > cy - BOX && b.minY < cy + BOX) {
                                bl = true;
                                break;
                            }
                        }
                    }
                    blocked[c][r] = bl;
                    reach[c][r] = false;
                    via[c][r] = false;
                }
            }

            reach[0][MID] = true;
            for (int c = 1; c <= cols; c++) {
                for (int r = 0; r < R; r++) {
                    if (blocked[c][r]) continue;
                    if (reach[c - 1][r] || (r > 0 && reach[c - 1][r - 1]) || (r < R - 1 && reach[c - 1][r + 1])) {
                        reach[c][r] = true;
                    }
                }
            }

            int end = cols;
            while (end > 0) {
                bool any = false;
                for (int r = 0; r < R; r++) {
                    if (reach[end][r]) { any = true; break; }
                }
                if (any) break;
                end--;
            }
            if (end == 0) return false;

            for (int r = 0; r < R; r++) via[end][r] = reach[end][r];
            for (int c = end - 1; c >= 0; c--) {
                for (int r = 0; r < R; r++) {
                    via[c][r] = reach[c][r] &&
                        (via[c + 1][r] || (r > 0 && via[c + 1][r - 1]) || (r < R - 1 && via[c + 1][r + 1]));
                }
            }

            int m = std::min(end, 3);
            int best = -1;
            int bestScore = -1000000;
            for (int r = 0; r < R; r++) {
                if (!via[m][r]) continue;
                int clearance = 0;
                for (int k = -4; k <= 4; k++) {
                    int rr = r + k;
                    if (rr >= 0 && rr < R && via[m][rr]) clearance++;
                }
                int score = clearance * 10 - std::abs(r - MID);
                if (score > bestScore) {
                    bestScore = score;
                    best = r;
                }
            }
            if (best < 0) return false;
            outY = py + (best - MID) * DY;
            return true;
        }

        float flyTarget(LevelData const& d, float px, float py, float ups) {
            float target;
            if (planPath(d, px, py, ups, target)) return target;
            bool bounded;
            return corridorTarget(d, px, py, ups * 0.4f, bounded);
        }

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

        // Dash: hay que mantener pulsado hasta que termine, o se cancela.
        if (p->m_isDashing) return true;

        // Robot: la altura del salto depende de cuanto se mantiene.
        if (p->m_isRobot && mem.holdLeft > 0) {
            mem.holdLeft--;
            return true;
        }

        // ---- Wave: mantener la altura del pasillo ----
        if (p->m_isDart) {
            float target = flyTarget(d, px, py, ups);
            bool wantUp = py < target - 2.f;
            return flipped ? !wantUp : wantUp;
        }

        // ---- Nave: control con prediccion de velocidad ----
        if (p->m_isShip) {
            float target = flyTarget(d, px, py, ups);
            float pred = py + vy * 30.f;
            bool wantUp = pred < target;
            return flipped ? !wantUp : wantUp;
        }

        // ---- UFO: toques repetidos (30 por segundo) ----
        if (p->m_isBird) {
            float target = flyTarget(d, px, py, ups);
            float pred = py + vy * 30.f;
            bool need = flipped ? (pred > target + 8.f) : (pred < target - 8.f);
            return need && (step % 8 < 2);
        }

        // ---- Swing: cambia de direccion al acercarse a los limites ----
        if (p->m_isSwing) {
            float target = flyTarget(d, px, py, ups);
            bool goingUp = vy > 0.f;
            float pred = py + vy * 30.f;
            bool needFlip = goingUp ? pred > target + 30.f : pred < target - 30.f;
            return needFlip && (step % 8 < 2);
        }

        // ---- Cubo / bola / robot / spider ----

        // Orbes: hay que SOLTAR y volver a PULSAR al entrar (flanco de pulsacion).
        // La busqueda puede ignorar algunos orbes o esperar dentro del orbe antes de pulsar.
        if (!opts.ignoreOrbs) {
            int oi = orbUnderPlayer(d, px, py);
            if (oi >= 0 && opts.orbSkipPct > 0) {
                uint32_t h = (static_cast<uint32_t>(oi) * 2654435761u) ^ opts.orbSeed;
                h ^= h >> 15;
                h *= 2246822519u;
                h ^= h >> 13;
                if (static_cast<int>(h % 100u) < opts.orbSkipPct) oi = -1;
            }
            if (oi >= 0) {
                if (!mem.inOrb) {
                    mem.inOrb = true;
                    mem.pressedOrb = false;
                    mem.orbSteps = 0;
                }
                mem.orbSteps++;
                if (!mem.pressedOrb) {
                    if (mem.orbSteps <= opts.orbDelay) return false;
                    if (opts.wasHolding) return false; // soltar primero
                    mem.pressedOrb = true;
                    return true;
                }
                return true;
            }
            mem.inOrb = false;
            mem.pressedOrb = false;
            mem.orbSteps = 0;
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

        if (evalFor(p)) {
            if (p->m_isRobot) mem.holdLeft = 14;
            return true;
        }

        auto q2 = opts.p2;
        if (opts.dual && q2 && q2 != p && !q2->m_isShip && !q2->m_isBird && !q2->m_isDart && !q2->m_isSwing) {
            return evalFor(q2);
        }
        return false;
    }

}
