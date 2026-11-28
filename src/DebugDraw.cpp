#include "DebugDraw.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace acgdl::dbg {

    namespace {
        CCDrawNode* s_node = nullptr;

        size_t firstFrom(std::vector<Box> const& v, float x) {
            auto it = std::lower_bound(v.begin(), v.end(), x, [](Box const& b, float val) {
                return b.minX < val;
            });
            return static_cast<size_t>(it - v.begin());
        }

        void boxes(std::vector<Box> const& v, float maxW, float x0, float x1, float py, float yRange,
                   ccColor4F fill, ccColor4F border) {
            for (size_t i = firstFrom(v, x0 - maxW); i < v.size(); i++) {
                Box const& b = v[i];
                if (b.minX > x1) break;
                if (b.maxX < x0) continue;
                if (b.maxY < py - yRange || b.minY > py + yRange) continue;
                s_node->drawRect({b.minX, b.minY}, {b.maxX, b.maxY}, fill, 0.6f, border);
            }
        }
    }

    void reset() {
        if (s_node) {
            s_node->removeFromParent();
            s_node->release();
            s_node = nullptr;
        }
    }

    void draw(PlayLayer* pl, LevelData const& d, std::vector<CCPoint> const& path, bool pathOk) {
        if (!pl || !pl->m_player1 || !pl->m_objectLayer) return;

        if (!s_node) {
            s_node = CCDrawNode::create();
            if (!s_node) return;
            s_node->retain();
            pl->m_objectLayer->addChild(s_node, 1000);
        }
        s_node->clear();

        float px = pl->m_player1->getPositionX();
        float py = pl->m_player1->getPositionY();
        float x0 = px - 120.f;
        float x1 = px + 520.f;
        float yr = 260.f;

        boxes(d.solids, d.maxSolidW, x0, x1, py, yr, {0.2f, 0.4f, 1.f, 0.10f}, {0.3f, 0.5f, 1.f, 0.5f});
        boxes(d.hazards, d.maxHazardW, x0, x1, py, yr, {1.f, 0.1f, 0.1f, 0.22f}, {1.f, 0.2f, 0.2f, 0.9f});
        boxes(d.orbs, 60.f, x0, x1, py, yr, {1.f, 0.9f, 0.1f, 0.25f}, {1.f, 0.95f, 0.2f, 0.9f});
        boxes(d.pads, 60.f, x0, x1, py, yr, {1.f, 0.4f, 0.8f, 0.25f}, {1.f, 0.5f, 0.85f, 0.9f});

        // jugador
        CCRect r = pl->m_player1->getObjectRect();
        s_node->drawRect({r.getMinX(), r.getMinY()}, {r.getMaxX(), r.getMaxY()},
                         {0.2f, 1.f, 0.3f, 0.15f}, 0.7f, {0.3f, 1.f, 0.4f, 0.95f});

        // trayectoria prevista por el simulador
        if (!path.empty()) {
            ccColor4F col = pathOk ? ccColor4F{1.f, 1.f, 1.f, 0.9f} : ccColor4F{1.f, 0.3f, 0.3f, 0.9f};
            CCPoint prev = {px, py};
            for (auto const& pt : path) {
                s_node->drawSegment(prev, pt, 0.7f, col);
                prev = pt;
            }
        }
    }

}
