#include "Panel.hpp"
#include "BotState.hpp"

#include <Geode/ui/NineSlice.hpp>
#include <Geode/ui/Toggler.hpp>

using namespace geode::prelude;

namespace acgdl {

    static Panel* s_open = nullptr;

    static char const* phaseName(int p) {
        switch (p) {
            case 1: return "Resolviendo el nivel...";
            case 2: return "Ejecutando la solucion";
            case 3: return "Jugando en directo";
            default: return "En espera";
        }
    }

    static ccColor3B phaseColor(int p) {
        switch (p) {
            case 1: return {255, 200, 60};
            case 2: return {90, 235, 130};
            case 3: return {110, 190, 255};
            default: return {190, 190, 200};
        }
    }

    struct Entry {
        char const* title;
        char const* desc;
        char const* key;
    };

    // Dos columnas de tarjetas, caben en la pantalla de GD (320 de alto).
    static void addCard(CCLayer* layer, float cx, float cy, Entry const& e) {
        auto bg = NineSlice::create("square02b_001.png");
        bg->setContentSize({206.f, 28.f});
        bg->setColor({0, 0, 0});
        bg->setOpacity(95);
        layer->addChildAtPosition(bg, Anchor::Center, {cx, cy});

        auto name = CCLabelBMFont::create(e.title, "bigFont.fnt");
        name->limitLabelWidth(118.f, 0.36f, 0.2f);
        layer->addChildAtPosition(name, Anchor::Center, {cx - 98.f, cy}, {0.f, 0.5f});

        std::string title = e.title;
        std::string desc = e.desc;
        auto info = Button::createWithSpriteFrameName("GJ_infoIcon_001.png", [title, desc](Button*) {
            FLAlertLayer::create(title.c_str(), desc, "OK")->show();
        });
        info->setScale(0.5f);
        layer->addChildAtPosition(info, Anchor::Center, {cx + 56.f, cy});

        std::string key = e.key;
        auto toggler = geode::Toggler::createWithStandardSprites(0.62f, [key](geode::Toggler*, bool on) {
            Mod::get()->setSettingValue<bool>(key, on);
        });
        toggler->setToggled(Mod::get()->getSettingValue<bool>(e.key));
        layer->addChildAtPosition(toggler, Anchor::Center, {cx + 84.f, cy});
    }

    bool Panel::init() {
        if (!Popup::init(440.f, 300.f)) return false;
        this->setTitle("Auto Complete", "goldFont.fnt", 0.8f, 20.f);

        // estado
        auto chip = NineSlice::create("square02b_001.png");
        chip->setContentSize({230.f, 22.f});
        chip->setColor({0, 0, 0});
        chip->setOpacity(110);
        m_mainLayer->addChildAtPosition(chip, Anchor::Center, {0.f, 104.f});

        m_state = CCLabelBMFont::create(phaseName(stats().phase), "bigFont.fnt");
        m_state->limitLabelWidth(210.f, 0.38f, 0.2f);
        m_state->setColor(phaseColor(stats().phase));
        m_mainLayer->addChildAtPosition(m_state, Anchor::Center, {0.f, 104.f});

        static Entry const left[] = {
            {"Auto-completar", "El bot juega y resuelve cada nivel al entrar.", "auto-play"},
            {"Resolver en 2o plano", "Pantalla de carga: resuelve el nivel entero y luego lo ejecuta. Sin esto juega en directo.", "rewind"},
            {"Simulador", "Simula caminos con un jugador falso (como Show Trajectory) para elegir las pulsaciones que sobreviven.", "trajectory"},
            {"Simulador: orbes", "Incluye orbes y pads en la simulacion.", "trajectory-orbs"},
            {"Entradas humanas", "Las pulsaciones y las pausas duran un minimo para que parezca una persona jugando.", "human"},
        };
        static Entry const right[] = {
            {"Modo seguro", "No guarda progreso ni records mientras juega el bot.", "safe-mode"},
            {"Guardar soluciones", "Guarda la solucion y el progreso para continuar en otra sesion.", "save-solutions"},
            {"Resumen del nivel", "Muestra lo que se ha leido del nivel al entrar.", "show-info"},
            {"Dibujar hitboxes", "Depuracion: dibuja lo que ve el bot y la trayectoria prevista.", "debug-draw"},
        };
        float y0 = 66.f;
        for (int i = 0; i < 5; i++) addCard(m_mainLayer, -107.f, y0 - i * 32.f, left[i]);
        for (int i = 0; i < 4; i++) addCard(m_mainLayer, 107.f, y0 - i * 32.f, right[i]);

        auto again = Button::createWithNode(
            ButtonSprite::create("Re-resolver", "goldFont.fnt", "GJ_button_01.png", 0.6f),
            [](Button*) {
                if (stats().inLevel) stats().resolveRequested = true;
            }
        );
        m_mainLayer->addChildAtPosition(again, Anchor::Center, {107.f, -62.f});

        auto close = Button::createWithNode(
            ButtonSprite::create("Cerrar", "goldFont.fnt", "GJ_button_06.png", 0.7f),
            [this](Button* b) { this->onClose(b); }
        );
        m_mainLayer->addChildAtPosition(close, Anchor::Center, {0.f, -108.f});

        m_stats = CCLabelBMFont::create("", "chatFont.fnt");
        m_stats->setScale(0.6f);
        m_stats->setOpacity(170);
        m_mainLayer->addChildAtPosition(m_stats, Anchor::Center, {0.f, -136.f});

        this->schedule(schedule_selector(Panel::tick), 0.2f);
        this->tick(0.f);
        return true;
    }

    void Panel::tick(float) {
        auto& st = stats();
        m_state->setString(phaseName(st.phase));
        m_state->setColor(phaseColor(st.phase));
        m_stats->setString(st.inLevel
            ? fmt::format("Progreso {}%   |   Reintentos {}   |   Pasos {}",
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
