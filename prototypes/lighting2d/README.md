# StationBuilder lighting bench

The game's lighting (`StationBuilder/lighting.h`) on its own. It draws the README's station with its simulation
running, for trying the lighting out and tuning it without the rest of the game.

It began as the 2D lighting prototype, the one compared with the 3D one in `prototypes/raylib3d`. The lighting itself
now lives in the game: F9 turns it on and off there, and Shift+F9 opens its settings. Items' lights, glow, steam and
smoke, and the art's normal and glow maps, are described in `StationBuilder/content/README.md`, under "Lighting".

## Running it

- **VS Code:** pick the "2D lighting prototype (SFML)" launch configuration and press F5. It builds the bench first.
- **By hand:** `cmake --build build/debug --target Lighting2DDemo`, then run `..\build\debug\Lighting2DDemo.exe` from
  the `StationBuilder/` folder (where the art, content and fonts are).

## Controls

| | |
| --- | --- |
| Mouse wheel | Zoom |
| Right drag | Move |
| Page Up, Page Down | Deck |
| L | Emergency power: the generators off, so the lamps pulse red |
| K | Sparks from a cable |
| F9, Shift+F9 | Lighting on or off; its settings |

## Other uses

- `Lighting2DDemo normalmap tiles/heater.png tiles/crate.png ...` writes the normal map the lighting makes for each art
  file beside it (`tiles/heater_normal.png`), as a start to paint over. A map already there is left alone.
- `Lighting2DDemo shots <folder>` runs it hidden and saves a few pictures of a short tour, then prints how long the
  lighting took each frame:
  - the station lit;
  - without normal maps;
  - without walls keeping light in;
  - an emergency with sparks;
  - the steam and the smelter;
  - the deck above;
  - tool highlights, with a deck drawn beside as a docked ship is.
