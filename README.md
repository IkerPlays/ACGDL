# Auto Complete (Geode)

Mod de Geode para Geometry Dash (PC y móvil) que **lee el nivel al entrar** y lo juega solo.

## Qué hace

1. **Lee el nivel**: al cargar, escanea todos los objetos y los clasifica (pinchos, bloques, orbes, pads) y muestra un resumen.
2. **Se autocompleta**:
   - Cubo / robot: salta pinchos (centrando el salto en grupos), paredes y pulsa orbes.
   - Bola / spider: cambia de lado antes de los pinchos.
   - Nave, wave, UFO, swing: buscan el pasillo libre entre obstáculos.
   - Los pads se activan solos.
3. **Modo rescate** (opcional): si el bot fallaría, ignora la muerte para que el nivel se complete igual. Ese intento **no se guarda**.
4. Botón **Auto: ON/OFF** en el menú de pausa (táctil) y ajustes en el menú del mod.

## Ajustes

| Ajuste | Por defecto | Descripción |
|---|---|---|
| Auto-completar niveles | ON | Activa el bot al entrar a un nivel |
| Mostrar resumen del nivel | ON | Notificación con lo leído |
| Modo rescate | ON | Ignora muertes que el bot no pueda evitar |
| Modo seguro | ON | No guarda progreso ni sube records mientras juega el bot |

## Compilar

El workflow `.github/workflows/build.yml` compila en GitHub Actions para Windows, macOS, iOS, Android 32 y Android 64 y junta todo en un único `.geode`.

1. Sube a GitHub (cada push dispara el build) o ve a **Actions → Build Geode Mod → Run workflow**.
2. Descarga el artifact **Build Output** → `ikerplays.autocomplete.geode`.
3. Impórtalo: en PC, copia el `.geode` a `Geometry Dash/geode/mods/`; en Android, a `Android/media/com.geode.launcher/game/geode/mods/` (o ábrelo con el launcher de Geode).

## Limitaciones

Es un bot por reglas (no una simulación exacta), así que niveles muy difíciles (timings de frames, dual con modos distintos, gaps sin suelo) pueden requerir el modo rescate.
