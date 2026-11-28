# Auto Complete (Geode)

Mod de Geode para Geometry Dash 2.2091 (PC y móvil) que **lee el nivel, lo resuelve en segundo plano y lo ejecuta solo**.

## Cómo funciona

1. **Lee el nivel** al entrar: pinchos, bloques, rampas, orbes, pads y portales.
2. **Pantalla de carga**: resuelve el nivel entero en segundo plano (física a 240 Hz, sin dibujar) probando pulsaciones con checkpoints del modo práctica y retrocediendo cuando falla.
3. **Verifica** la solución repitiéndola desde cero; si falla en un punto, repara solo ese tramo.
4. **La ejecuta de verdad** (se ve jugar solo).

Con el **simulador de trayectoria** (la técnica de los mods *Show Trajectory*) el bot prueba cada decisión sobre un jugador falso con las colisiones reales del juego y elige las pulsaciones que sobreviven. Recuerda los planes que fallaron para no repetirlos. En el modo directo (botón *Saltar*) también lo usa y aprende entre intentos.

## Panel

**F8** en PC, botón **AC** en el menú principal, o **Panel AC** en la pausa.

| Ajuste | Qué hace |
|---|---|
| Auto-completar | Activa el bot al entrar a un nivel |
| Resolver en segundo plano | Pantalla de carga + ejecución real (si no, juega en directo) |
| Simulador de trayectoria | Predice caminos con un jugador falso |
| Simulador: orbes y pads | Incluye orbes y pads en la simulación |
| Resumen del nivel | Muestra lo leído |
| Modo seguro | No guarda progreso ni records |
| Entradas humanas | Pulsaciones y pausas con duración mínima, sin temblor |
| Dibujar hitboxes | Depuración: hitboxes que ve el bot y la trayectoria prevista |

## Macros

Si dejas un macro del nivel (`.gdr` de xdBot/zBot/MegaHack o `.gdr2` de Eclipse) en la carpeta `macros` del mod (la ruta sale en el log de Geode), o ya está en las carpetas de Eclipse o xdBot, el bot lo usa como **solución inicial**: lo repite en segundo plano, prueba varios desfases de frame, comprueba que sigue funcionando en tu versión del juego y repara solo los tramos que fallen.

## Compilar

El workflow de GitHub Actions compila para Windows, macOS, iOS, Android32 y Android64 y publica un release con el `.geode` (la versión sale de `mod.json`).

## Herramientas

`tools/analyze_gmd.py nivel.gmd` lista los modos de juego, orbes, pads y velocidades de un nivel exportado con GDShare.

`tools/analyze_macro.py macro.gdr2 [--gmd nivel.gmd]` mide cómo pulsa una persona (duración de pulsaciones y pausas, por modo de juego si el macro trae posiciones). Los tiempos mínimos de las *entradas humanas* están calibrados con macros reales.

## Limitaciones

- La ejecución corre en modo práctica, así que **no cuenta como nivel completado**.
- En iOS el simulador de trayectoria no está disponible.
- Dual, mirror y portales a mitad de simulación no están modelados al 100 %.
