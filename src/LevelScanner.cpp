#include "LevelScanner.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace acgdl {

    static Box toBox(GameObject* obj) {
        CCPoint p = obj->getPosition();
        CCRect r = obj->getObjectRect();

        bool bad = r.size.width <= 0.f || r.size.height <= 0.f ||
                   std::fabs(r.getMidX() - p.x) > 80.f ||
                   std::fabs(r.getMidY() - p.y) > 80.f;
        if (bad) {
            return {p.x - 15.f, p.y - 15.f, p.x + 15.f, p.y + 15.f};
        }
        return {r.getMinX(), r.getMinY(), r.getMaxX(), r.getMaxY()};
    }

    static bool isOrb(GameObjectType t) {
        return t == GameObjectType::YellowJumpRing ||
               t == GameObjectType::PinkJumpRing ||
               t == GameObjectType::RedJumpRing ||
               t == GameObjectType::GravityRing ||
               t == GameObjectType::GreenRing ||
               t == GameObjectType::DropRing ||
               t == GameObjectType::DashRing ||
               t == GameObjectType::GravityDashRing;
    }

    static bool isPad(GameObjectType t) {
        return t == GameObjectType::YellowJumpPad ||
               t == GameObjectType::PinkJumpPad ||
               t == GameObjectType::RedJumpPad ||
               t == GameObjectType::GravityPad;
    }

    static void sortBoxes(std::vector<Box>& v) {
        std::sort(v.begin(), v.end(), [](Box const& a, Box const& b) {
            return a.minX < b.minX;
        });
    }

    LevelData scanLevel(GJBaseGameLayer* layer) {
        LevelData d;
        if (!layer || !layer->m_objects) return d;

        for (auto obj : CCArrayExt<GameObject*>(layer->m_objects)) {
            if (!obj) continue;
            d.totalObjects++;
            d.endX = std::max(d.endX, obj->getPositionX());

            auto type = obj->m_objectType;
            if (type == GameObjectType::Hazard) {
                Box b = toBox(obj);
                d.maxHazardW = std::max(d.maxHazardW, std::min(b.maxX - b.minX, 600.f));
                d.hazards.push_back(b);
            } else if (type == GameObjectType::Solid) {
                Box b = toBox(obj);
                d.maxSolidW = std::max(d.maxSolidW, std::min(b.maxX - b.minX, 600.f));
                d.solids.push_back(b);
            } else if (isOrb(type)) {
                d.orbs.push_back(toBox(obj));
            } else if (isPad(type)) {
                d.pads.push_back(toBox(obj));
            }
        }

        sortBoxes(d.hazards);
        sortBoxes(d.solids);
        sortBoxes(d.orbs);
        sortBoxes(d.pads);
        d.ready = true;
        return d;
    }

    std::string LevelData::summary() const {
        return fmt::format(
            "Nivel leido: {} objetos | {} pinchos | {} bloques | {} orbes | {} pads",
            totalObjects, hazards.size(), solids.size(), orbs.size(), pads.size()
        );
    }

}
