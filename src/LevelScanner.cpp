#include "LevelScanner.hpp"

#include <algorithm>
#include <cmath>
#include <map>

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

    bool isOrbType(GameObjectType t) {
        return t == GameObjectType::YellowJumpRing ||
               t == GameObjectType::PinkJumpRing ||
               t == GameObjectType::RedJumpRing ||
               t == GameObjectType::GravityRing ||
               t == GameObjectType::GreenRing ||
               t == GameObjectType::DropRing ||
               t == GameObjectType::DashRing ||
               t == GameObjectType::GravityDashRing ||
               t == GameObjectType::CustomRing ||
               t == GameObjectType::SpiderOrb ||
               t == GameObjectType::TeleportOrb;
    }

    bool isPadType(GameObjectType t) {
        return t == GameObjectType::YellowJumpPad ||
               t == GameObjectType::PinkJumpPad ||
               t == GameObjectType::RedJumpPad ||
               t == GameObjectType::GravityPad ||
               t == GameObjectType::SpiderPad;
    }

    static bool isPortal(GameObject* obj) {
        switch (obj->m_objectType) {
            case GameObjectType::InverseGravityPortal:
            case GameObjectType::NormalGravityPortal:
            case GameObjectType::ShipPortal:
            case GameObjectType::CubePortal:
            case GameObjectType::InverseMirrorPortal:
            case GameObjectType::NormalMirrorPortal:
            case GameObjectType::BallPortal:
            case GameObjectType::RegularSizePortal:
            case GameObjectType::MiniSizePortal:
            case GameObjectType::UfoPortal:
            case GameObjectType::DualPortal:
            case GameObjectType::SoloPortal:
            case GameObjectType::WavePortal:
            case GameObjectType::RobotPortal:
            case GameObjectType::TeleportPortal:
            case GameObjectType::SpiderPortal:
            case GameObjectType::SwingPortal:
            case GameObjectType::GravityTogglePortal:
                return true;
            default:
                break;
        }
        // portales de velocidad
        int id = obj->m_objectID;
        return id == 200 || id == 201 || id == 202 || id == 203 || id == 1334;
    }

    char const* typeName(int t) {
        switch (t) {
            case 0: return "bloque";
            case 2: return "pincho";
            case 3: case 4: return "portal gravedad";
            case 5: return "portal nave";
            case 6: return "portal cubo";
            case 7: return "decoracion";
            case 8: return "pad amarillo";
            case 9: return "pad rosa";
            case 10: return "pad gravedad";
            case 11: return "orbe amarillo";
            case 12: return "orbe rosa";
            case 13: return "orbe gravedad";
            case 14: case 15: return "portal espejo";
            case 16: return "portal bola";
            case 17: case 18: return "portal tamano";
            case 19: return "portal ufo";
            case 20: return "modificador";
            case 21: return "rompible";
            case 22: return "moneda secreta";
            case 23: case 24: return "portal dual";
            case 25: return "rampa";
            case 26: return "portal wave";
            case 27: return "portal robot";
            case 28: return "teletransporte";
            case 29: return "orbe verde";
            case 30: return "coleccionable";
            case 31: return "moneda";
            case 32: return "orbe negro";
            case 33: return "portal spider";
            case 34: return "pad rojo";
            case 35: return "orbe rojo";
            case 36: return "orbe custom";
            case 37: return "orbe dash";
            case 38: return "orbe dash gravedad";
            case 39: return "colision";
            case 40: return "especial";
            case 41: return "portal swing";
            case 43: return "orbe spider";
            case 44: return "pad spider";
            case 46: return "orbe teletransporte";
            case 47: return "hazard animado";
            default: return "otro";
        }
    }

    static void sortBoxes(std::vector<Box>& v) {
        std::sort(v.begin(), v.end(), [](Box const& a, Box const& b) {
            return a.minX < b.minX;
        });
    }

    LevelData scanLevel(GJBaseGameLayer* layer) {
        LevelData d;
        if (!layer || !layer->m_objects) return d;

        std::vector<std::pair<Box, GameObject*>> orbTmp, padTmp;
        std::map<int, int> counts;
        for (auto obj : CCArrayExt<GameObject*>(layer->m_objects)) {
            if (!obj) continue;
            d.totalObjects++;
            d.endX = std::max(d.endX, obj->getPositionX());

            auto type = obj->m_objectType;
            if (type == GameObjectType::Hazard || type == GameObjectType::AnimatedHazard) {
                Box b = toBox(obj);
                d.maxHazardW = std::max(d.maxHazardW, std::min(b.maxX - b.minX, 600.f));
                d.hazards.push_back(b);
            } else if (type == GameObjectType::Solid) {
                Box b = toBox(obj);
                d.maxSolidW = std::max(d.maxSolidW, std::min(b.maxX - b.minX, 600.f));
                d.solids.push_back(b);
            } else if (isOrbType(type)) {
                Box b = toBox(obj);
                orbTmp.emplace_back(b, obj);
                d.stops.push_back({b, 50.f});
            } else if (isPadType(type)) {
                Box b = toBox(obj);
                padTmp.emplace_back(b, obj);
                d.stops.push_back({b, 30.f});
            } else if (isPortal(obj)) {
                Stop st{toBox(obj), 40.f};
                d.stops.push_back(st);
                d.portalStops.push_back(st);
            }
            if (type == GameObjectType::Slope) d.slopes++;
            if (type == GameObjectType::AnimatedHazard) d.animatedHazards++;
            counts[static_cast<int>(type)]++;
        }

        sortBoxes(d.hazards);
        sortBoxes(d.solids);
        auto byX = [](std::pair<Box, GameObject*> const& a, std::pair<Box, GameObject*> const& b) {
            return a.first.minX < b.first.minX;
        };
        std::sort(orbTmp.begin(), orbTmp.end(), byX);
        std::sort(padTmp.begin(), padTmp.end(), byX);
        for (auto& pr : orbTmp) { d.orbs.push_back(pr.first); d.orbObjs.push_back(pr.second); }
        for (auto& pr : padTmp) { d.pads.push_back(pr.first); d.padObjs.push_back(pr.second); }
        std::sort(d.portalStops.begin(), d.portalStops.end(), [](Stop const& a, Stop const& b) {
            return a.box.minX < b.box.minX;
        });
        for (auto const& [t, n] : counts) {
            if (t == 7 || t == 20) continue; // decoracion y modificadores: ruido
            d.typeReport += fmt::format("{}={} ", typeName(t), n);
        }
        std::sort(d.stops.begin(), d.stops.end(), [](Stop const& a, Stop const& b) {
            return a.box.minX < b.box.minX;
        });
        d.ready = true;
        return d;
    }

    std::string LevelData::summary() const {
        return fmt::format(
            "Nivel leido: {} objetos | {} pinchos | {} bloques | {} rampas | {} orbes | {} pads",
            totalObjects, hazards.size(), solids.size(), slopes, orbs.size(), pads.size()
        );
    }

}
