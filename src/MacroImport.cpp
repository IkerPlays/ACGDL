#include "MacroImport.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace geode::prelude;
namespace fs = std::filesystem;

namespace acgdl::macro {

    namespace {

        // ---------------- lector binario con comprobacion de limites ----------------
        struct Rd {
            std::vector<uint8_t> const& b;
            size_t i = 0;
            bool ok = true;

            explicit Rd(std::vector<uint8_t> const& data) : b(data) {}

            bool empty() const { return i >= b.size(); }

            uint8_t u8() {
                if (i >= b.size()) { ok = false; return 0; }
                return b[i++];
            }
            uint64_t varint() {
                uint64_t out = 0;
                for (int k = 0; k < 10; k++) {
                    uint8_t byte = u8();
                    if (!ok) return 0;
                    out |= static_cast<uint64_t>(byte & 0x7F) << (7 * k);
                    if (!(byte & 0x80)) return out;
                }
                ok = false;
                return 0;
            }
            std::string str() {
                uint64_t n = varint();
                if (!ok || n > b.size() - i) { ok = false; return {}; }
                std::string s(reinterpret_cast<char const*>(&b[i]), static_cast<size_t>(n));
                i += static_cast<size_t>(n);
                return s;
            }
            void skip(uint64_t n) {
                if (n > b.size() - i) { ok = false; i = b.size(); return; }
                i += static_cast<size_t>(n);
            }
            float f32be() {
                if (b.size() - i < 4) { ok = false; return 0.f; }
                uint32_t v = (uint32_t(b[i]) << 24) | (uint32_t(b[i + 1]) << 16) | (uint32_t(b[i + 2]) << 8) | b[i + 3];
                i += 4;
                float f;
                std::memcpy(&f, &v, 4);
                return f;
            }
            double f64be() {
                if (b.size() - i < 8) { ok = false; return 0.0; }
                uint64_t v = 0;
                for (int k = 0; k < 8; k++) v = (v << 8) | b[i + k];
                i += 8;
                double d;
                std::memcpy(&d, &v, 8);
                return d;
            }
        };

        // ---------------- GDR v2 (binario, Eclipse / xdBot) ----------------
        bool parseGdr2(std::vector<uint8_t> const& data, Macro& m) {
            if (data.size() < 8 || std::memcmp(data.data(), "GDR", 3) != 0) return false;
            Rd r(data);
            r.skip(3);
            if (r.varint() != 2) return false;
            std::string inputTag = r.str();
            r.str();           // autor
            r.str();           // descripcion
            r.f32be();         // duracion
            r.varint();        // version del juego
            m.framerate = r.f64be();
            r.varint();        // seed
            r.varint();        // monedas
            r.u8();            // ldm
            bool platformer = r.u8() != 0;
            r.str();           // bot
            r.varint();        // version del bot
            m.levelId = static_cast<int>(r.varint());
            m.levelName = r.str();
            uint64_t extLen = r.varint();
            r.skip(extLen);
            uint64_t deaths = r.varint();
            for (uint64_t k = 0; k < deaths && r.ok; k++) r.varint();
            r.varint();                      // numero de entradas
            uint64_t p1Count = r.varint();   // entradas del jugador 1
            if (!r.ok) return false;

            std::vector<std::pair<int, bool>> s1, s2;
            uint64_t frame = 0;
            uint64_t remaining = p1Count;
            bool inP2 = false;
            while (!r.empty() && r.ok) {
                uint64_t packed = r.varint();
                uint64_t delta = platformer ? (packed >> 3) : (packed >> 1);
                bool down = (packed & 1) != 0;
                frame += delta;
                if (!inP2 && remaining == 0) {
                    inP2 = true;
                    frame = delta;
                }
                (inP2 ? s2 : s1).emplace_back(static_cast<int>(frame), down);
                if (!inP2) {
                    remaining--;
                    if (remaining == 0 && p1Count > 0) frame = 0;
                }
                if (!inputTag.empty()) r.skip(r.varint());
            }
            // el flujo con mas pulsaciones es el del jugador principal
            m.events = s1.size() >= s2.size() ? std::move(s1) : std::move(s2);
            return r.ok && !m.events.empty();
        }

        // ---------------- GDR v1 (MessagePack) ----------------
        struct Val {
            enum Kind { Nil, Bool, Int, Flt, Str, Arr, Map } kind = Nil;
            int64_t i = 0;
            double f = 0.0;
            std::string s;
            std::vector<Val> arr;
            std::vector<std::pair<std::string, Val>> map;

            Val const* get(char const* key) const {
                for (auto const& kv : map) if (kv.first == key) return &kv.second;
                return nullptr;
            }
            double num() const { return kind == Flt ? f : static_cast<double>(i); }
        };

        struct Mp {
            std::vector<uint8_t> const& b;
            size_t i = 0;
            bool ok = true;
            int depth = 0;

            explicit Mp(std::vector<uint8_t> const& data) : b(data) {}

            uint8_t u8() { if (i >= b.size()) { ok = false; return 0; } return b[i++]; }
            uint64_t be(int n) {
                uint64_t v = 0;
                for (int k = 0; k < n; k++) v = (v << 8) | u8();
                return v;
            }
            std::string bytes(uint64_t n) {
                if (n > b.size() - i) { ok = false; return {}; }
                std::string s(reinterpret_cast<char const*>(&b[i]), static_cast<size_t>(n));
                i += static_cast<size_t>(n);
                return s;
            }

            // `store` = false: solo avanza (para saltar bloques enormes como frameFixes)
            void value(Val& out, bool store) {
                if (!ok || ++depth > 64) { ok = false; return; }
                uint8_t t = u8();
                if (t <= 0x7f) { out.kind = Val::Int; out.i = t; }
                else if (t >= 0xe0) { out.kind = Val::Int; out.i = static_cast<int8_t>(t); }
                else if (t >= 0xa0 && t <= 0xbf) { out.kind = Val::Str; auto s = bytes(t & 0x1f); if (store) out.s = std::move(s); }
                else if (t >= 0x90 && t <= 0x9f) arr(out, t & 0x0f, store);
                else if (t >= 0x80 && t <= 0x8f) map(out, t & 0x0f, store);
                else switch (t) {
                    case 0xc0: out.kind = Val::Nil; break;
                    case 0xc2: out.kind = Val::Bool; out.i = 0; break;
                    case 0xc3: out.kind = Val::Bool; out.i = 1; break;
                    case 0xc4: bytes(be(1)); break;
                    case 0xc5: bytes(be(2)); break;
                    case 0xc6: bytes(be(4)); break;
                    case 0xca: { uint32_t v = static_cast<uint32_t>(be(4)); float f; std::memcpy(&f, &v, 4); out.kind = Val::Flt; out.f = f; break; }
                    case 0xcb: { uint64_t v = be(8); double d; std::memcpy(&d, &v, 8); out.kind = Val::Flt; out.f = d; break; }
                    case 0xcc: out.kind = Val::Int; out.i = static_cast<int64_t>(be(1)); break;
                    case 0xcd: out.kind = Val::Int; out.i = static_cast<int64_t>(be(2)); break;
                    case 0xce: out.kind = Val::Int; out.i = static_cast<int64_t>(be(4)); break;
                    case 0xcf: out.kind = Val::Int; out.i = static_cast<int64_t>(be(8)); break;
                    case 0xd0: out.kind = Val::Int; out.i = static_cast<int8_t>(be(1)); break;
                    case 0xd1: out.kind = Val::Int; out.i = static_cast<int16_t>(be(2)); break;
                    case 0xd2: out.kind = Val::Int; out.i = static_cast<int32_t>(be(4)); break;
                    case 0xd3: out.kind = Val::Int; out.i = static_cast<int64_t>(be(8)); break;
                    case 0xd9: { out.kind = Val::Str; auto s = bytes(be(1)); if (store) out.s = std::move(s); break; }
                    case 0xda: { out.kind = Val::Str; auto s = bytes(be(2)); if (store) out.s = std::move(s); break; }
                    case 0xdb: { out.kind = Val::Str; auto s = bytes(be(4)); if (store) out.s = std::move(s); break; }
                    case 0xdc: arr(out, be(2), store); break;
                    case 0xdd: arr(out, be(4), store); break;
                    case 0xde: map(out, be(2), store); break;
                    case 0xdf: map(out, be(4), store); break;
                    default: ok = false; break;
                }
                depth--;
            }

            void arr(Val& out, uint64_t n, bool store) {
                out.kind = Val::Arr;
                if (n > b.size()) { ok = false; return; }
                for (uint64_t k = 0; k < n && ok; k++) {
                    Val v;
                    value(v, store);
                    if (store) out.arr.push_back(std::move(v));
                }
            }

            void map(Val& out, uint64_t n, bool store) {
                out.kind = Val::Map;
                if (n > b.size()) { ok = false; return; }
                for (uint64_t k = 0; k < n && ok; k++) {
                    Val key;
                    value(key, true);
                    bool keep = store && key.kind == Val::Str && key.s != "frameFixes";
                    Val v;
                    value(v, keep);
                    if (keep) out.map.emplace_back(std::move(key.s), std::move(v));
                }
            }
        };

        bool parseGdr1(std::vector<uint8_t> const& data, Macro& m) {
            Mp mp(data);
            Val root;
            mp.value(root, true);
            if (!mp.ok || root.kind != Val::Map) return false;

            if (auto fr = root.get("framerate")) m.framerate = fr->num();
            if (m.framerate < 30.0) m.framerate = 240.0;
            if (auto lv = root.get("level"); lv && lv->kind == Val::Map) {
                if (auto n = lv->get("name")) m.levelName = n->s;
                if (auto id = lv->get("id")) m.levelId = static_cast<int>(id->i);
            }
            auto inp = root.get("inputs");
            if (!inp || inp->kind != Val::Arr) return false;

            std::vector<std::pair<int, bool>> s1, s2;
            for (auto const& e : inp->arr) {
                if (e.kind != Val::Map) continue;
                auto fr = e.get("frame");
                if (!fr) continue;
                bool p2 = false;
                if (auto v = e.get("2p")) p2 = v->i != 0;
                else if (auto v2 = e.get("player2")) p2 = v2->i != 0;
                bool down = false;
                if (auto d = e.get("down")) down = d->i != 0;
                // solo el boton principal (1)
                int btn = 1;
                if (auto bt = e.get("btn")) btn = static_cast<int>(bt->i);
                else if (auto bt2 = e.get("button")) btn = static_cast<int>(bt2->i);
                if (btn != 1) continue;
                (p2 ? s2 : s1).emplace_back(static_cast<int>(fr->i), down);
            }
            m.events = s1.size() >= s2.size() ? std::move(s1) : std::move(s2);
            return !m.events.empty();
        }

        std::string normalize(std::string const& s) {
            std::string out;
            for (unsigned char c : s) {
                if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
            }
            return out;
        }

        bool readFile(fs::path const& p, std::vector<uint8_t>& out) {
            std::error_code ec;
            auto size = fs::file_size(p, ec);
            if (ec || size == 0 || size > 12ull * 1024 * 1024) return false;
            std::ifstream f(p, std::ios::binary);
            if (!f) return false;
            out.resize(static_cast<size_t>(size));
            f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
            return f.good() || f.eof();
        }

        void searchDir(fs::path const& dir, std::vector<fs::path>& files) {
            std::error_code ec;
            if (!fs::exists(dir, ec)) return;
            fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
            int count = 0;
            for (; !ec && it != end && count < 3000; it.increment(ec)) {
                if (it.depth() > 4) { it.disable_recursion_pending(); continue; }
                count++;
                if (!it->is_regular_file(ec)) continue;
                auto ext = it->path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
                if (ext == ".gdr" || ext == ".gdr2") files.push_back(it->path());
            }
        }

    }

    std::string folder() {
        return (Mod::get()->getSaveDir() / "macros").string();
    }

    bool find(std::string const& levelName, int levelId, Macro& out) {
        std::vector<fs::path> dirs;
        dirs.push_back(fs::path(folder()));
        for (char const* id : {"eclipse.eclipse-menu", "zilko.xdbot"}) {
            if (auto mod = Loader::get()->getLoadedMod(id)) {
                dirs.push_back(fs::path(mod->getSaveDir().string()));
                dirs.push_back(fs::path(mod->getConfigDir(false).string()));
            }
        }

        std::vector<fs::path> files;
        for (auto const& d : dirs) searchDir(d, files);
        std::sort(files.begin(), files.end());
        files.erase(std::unique(files.begin(), files.end()), files.end());

        std::string wanted = normalize(levelName);
        bool found = false;
        for (auto const& f : files) {
            std::vector<uint8_t> data;
            if (!readFile(f, data)) continue;

            Macro m;
            bool ok = (data.size() >= 3 && std::memcmp(data.data(), "GDR", 3) == 0)
                ? parseGdr2(data, m)
                : parseGdr1(data, m);
            if (!ok) continue;

            bool matches = false;
            if (levelId > 0 && m.levelId > 0 && m.levelId == levelId) matches = true;
            if (!matches && !wanted.empty()) {
                std::string a = normalize(m.levelName);
                std::string fn = normalize(f.stem().string());
                matches = (!a.empty() && a == wanted) || (fn.find(wanted) != std::string::npos);
            }
            if (!matches) continue;

            m.file = f.filename().string();
            m.lastFrame = m.events.empty() ? 0 : m.events.back().first;
            // si hay varios, el que llega mas lejos (carrera completa)
            if (!found || m.lastFrame > out.lastFrame) {
                out = std::move(m);
                found = true;
            }
        }
        return found;
    }

    std::vector<uint8_t> toInputs(Macro const& m, int offset) {
        std::vector<uint8_t> out;
        if (m.events.empty()) return out;
        double scale = 240.0 / std::max(30.0, m.framerate);
        auto stepOf = [&](int frame) {
            return static_cast<int>(std::lround(static_cast<double>(frame) * scale)) + offset;
        };
        int last = stepOf(m.events.back().first);
        out.assign(static_cast<size_t>(std::max(1, last + 120)), 0);
        bool cur = false;
        size_t ei = 0;
        for (int step = 0; step < static_cast<int>(out.size()); step++) {
            while (ei < m.events.size() && stepOf(m.events[ei].first) <= step) {
                cur = m.events[ei].second;
                ei++;
            }
            out[static_cast<size_t>(step)] = cur ? 1 : 0;
        }
        return out;
    }

}

$execute {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(acgdl::macro::folder()), ec);
    log::info("[AutoComplete] carpeta de macros: {}", acgdl::macro::folder());
}
