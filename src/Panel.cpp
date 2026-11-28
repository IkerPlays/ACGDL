#include "Panel.hpp"
#include <Geode/ui/Toggler.hpp>
#include "BotState.hpp"

using namespace geode::prelude;

namespace acgdl {

    static Panel* s_open = nullptr;

    static char const* phaseName(int p) {
        switch (p) {
            case 1: return "Resolviendo el nivel...";
            case 2: return "Ejecutando solucion";
            case 3: return "Jugando en directo";
            default: return "En espera";
        }
    }

    static ccColor3B phaseColor(int p) {
        switch (p) {
            case 1: return {255, 200, 60};
            case 2: return {90, 235, 130};
            case 3: return {110, 190, 255};
            default: return {190, 190, 190};
        }
    }

    static void addRow(CCLayer* layer, int row, char const* title, char const* desc, char const* key) {
        float y = 167.f - row * 46.f;

        auto bg = CCLayerColor::create({0, 0, 0, 70});
        bg->setContentSize({300.f, 40.f});
        bg->ignoreAnchorPointForPosition(false);
        bg->setAnchorPoint({0.5f, 0.5f});
        layer->addChildAtPosition(bg, Anchor::Center, {0.f, y});

        auto t = CCLabelBMFont::create(title, "bigFont.fnt");
        t->setScale(0.40f);
        layer->addChildAtPosition(t, Anchor::Center, {-142.f, y + 9.f}, {0.f, 0.5f});

        auto d = CCLabelBMFont::create(desc, "chatFont.fnt");
        d->setScale(0.62f);
        d->setOpacity(170);
        layer->addChildAtPosition(d, Anchor::Center, {-142.f, y - 9.f}, {0.f, 0.5f});

        std::string k = key;
        auto toggler = geode::Toggler::createWithStandardSprites(0.75f, [k](geode::Toggler*, bool on) {
            Mod::get()->setSettingValue<bool>(k, on);
        });
        toggler->setToggled(Mod::get()->getSettingValue<bool>(key));
        layer->addChildAtPosition(toggler, Anchor::Center, {126.f, y});
    }

    bool Panel::init() {
        if (!Popup::init(340.f, 502.f)) return false;
        this->setTitle("Auto Complete");

        m_state = CCLabelBMFont::create(phaseName(stats().phase), "bigFont.fnt");
        m_state->setScale(0.42f);
        m_state->setColor(phaseColor(stats().phase));
        m_mainLayer->addChildAtPosition(m_state, Anchor::Center, {0.f, 209.f});

        addRow(m_mainLayer, 0, "Auto-completar", "El bot juega y resuelve cada nivel al entrar", "auto-play");
        addRow(m_mainLayer, 1, "Resolver en segundo plano", "Pantalla de carga y luego ejecucion real", "rewind");
        addRow(m_mainLayer, 2, "Simulador de trayectoria", "Simula caminos para elegir pulsaciones", "trajectory");
        addRow(m_mainLayer, 3, "Simulador: orbes y pads", "Incluye orbes y pads en la simulacion", "trajectory-orbs");
        addRow(m_mainLayer, 4, "Resumen del nivel", "Muestra lo leido: pinchos, orbes, pads...", "show-info");
        addRow(m_mainLayer, 5, "Modo seguro", "No guarda progreso ni records", "safe-mode");
        addRow(m_mainLayer, 6, "Entradas humanas", "Sin toques de 4 ms ni temblor", "human");
        addRow(m_mainLayer, 7, "Dibujar hitboxes", "Muestra lo que ve el bot y su trayectoria", "debug-draw");

        auto again = Button::createWithNode(
            ButtonSprite::create("Re-resolver", "goldFont.fnt", "GJ_button_01.png", 0.7f),
            [](Button*) {
                if (stats().inLevel) stats().resolveRequested = true;
            }
        );
        m_mainLayer->addChildAtPosition(again, Anchor::Center, {-72.f, -204.f});

        auto close = Button::createWithNode(
            ButtonSprite::create("Cerrar", "goldFont.fnt", "GJ_button_06.png", 0.7f),
            [this](Button* b) { this->onClose(b); }
        );
        m_mainLayer->addChildAtPosition(close, Anchor::Center, {72.f, -204.f});

        m_stats = CCLabelBMFont::create("", "chatFont.fnt");
        m_stats->setScale(0.6f);
        m_stats->setOpacity(170);
        m_mainLayer->addChildAtPosition(m_stats, Anchor::Center, {0.f, -232.f});

        this->schedule(schedule_selector(Panel::tick), 0.2f);
        this->tick(0.f);
        return true;
    }

    void Panel::tick(float) {
        auto& st = stats();
        m_state->setString(phaseName(st.phase));
        m_state->setColor(phaseColor(st.phase));
        m_stats->setString(st.inLevel
            ? fmt::format("Progreso {}%  |  Reintentos {}  |  Pasos {}",
                static_cast<int>(st.percent), st.attempts, st.steps).c_str()
            : "Entra a un nivel para ver estadisticas");
    }

    Panel::~Panel() {
        if (s_open == this) s_open = nullptr;
    }

    Panel* Panel::create() {
        auto ret = new Panel();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void Panel::open() {
        if (s_open) return; // ya abierto
        auto p = Panel::create();
        if (!p) return;
        s_open = p;
        p->show();
    }

}
