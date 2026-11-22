# StationBuilder 3D prototype (raylib)

A feel for StationBuilder in 3D: the station and its ships drawn with raylib, with Dear ImGui and the game's amber
theme for the panel. Items are boxes for now: each has its footprint, a height and a colour taken from its art.

## Running it

- **VS Code:** pick the "3D prototype (raylib)" launch configuration and press F5. It builds the prototype first.
- **By hand:** run `build.bat run` in this folder. The first build downloads raylib 5.5 and Dear ImGui 1.91.5 into
  `build/raylib3d` at the repository's root.

It is a CMake project of its own and never touches the game's build.

## What it shows

- **Building view** (isometric):
  - An orthographic camera at the game's angle (30 degrees down, 2:1 diamonds), turning a quarter at a time.
  - The decks above the one shown are cut away, and the walls are cut low.
  - The tile under the mouse is picked from the deck's plane. A left click lays or lifts a floor (only where nothing
    else is built): it is there to feel picking, not to build the game.
- **Space view:**
  - A perspective camera orbiting the station.
  - Stars, a planet and the sun, drawn as a sky that never gets closer.
  - The docked ship at its jetty, and the free ship flying a banked, rising and falling loop with its engine lit.
- **Going between them:** zooming far out of the building view goes on into space; zooming right in from space comes
  back to the deck. V switches straight away.
- **Lighting** (the panel's LIGHTING section turns each part on and off):
  - **The sun** casts shadows from a shadow map. The roofs keep it out, so the rooms are lit by their own lights.
    "Sunlight into the cut-away rooms" lets it in to show the walls' shadows.
  - **Point lights:** lamps, heaters, generators' status lights, flight consoles, the smelter, the docks' beacons and
    the flying ship's engine. Each casts shadows: walls keep a lamp's light in its room, and machines throw shadows
    across the floor.
  - **Glow:** lamps, heater tops, screens, the smelter's mouth, sparks and the engine's plume bloom.
  - **Particles:** steam from the heat exchangers, smoke from the smelter, sparks from a cable, the engine's plume.
  - **Emergency power:** the lamps go out and pulse red, and the generators blink red.
  - **Ambient:** how much light there is with no lamp, for darker or lighter rooms.

## Controls

| | Building view | Space view |
| --- | --- | --- |
| Mouse wheel | Zoom (far out: on into space) | Zoom (right in: back to the deck) |
| Right drag | Move the deck | Orbit |
| Q, E | Turn a quarter | |
| Page Up, Page Down | Deck | |
| Left click | Lay or lift a floor | |
| F | | Follow the flying ship |
| V | Switch views | Switch views |
| Tab | Next vessel | Next vessel |
| L | Emergency power on or off | Emergency power on or off |
| K | Sparks from a cable on the deck shown | |

## The scene

`scene.txt` is written by the game's SceneExport tool (`tools/sceneExport.cpp`). The format is described at the top
of that file.

1. Build the tool: `cmake --build build/debug --target SceneExport`.
2. Run it from the `StationBuilder/` folder:
   - `..\build\debug\SceneExport.exe` writes the README's station, with a ship docked at its jetty and one out in space.
   - `..\build\debug\SceneExport.exe save` writes the station and ships you saved in the game.

## Files

| File | What it does |
| --- | --- |
| `demo.cpp` | The scene, meshes, cameras, lighting, shadows, glow, particles and panel. |
| `imguiRaylib.cpp` | Dear ImGui on raylib: input from raylib, drawing through `rlgl`. Crisp on scaled displays. |
| `../../StationBuilder/uiTheme.cpp` | The game's own theme, used as it is. |

`demo shots <folder>` runs it hidden and saves a few pictures of a short tour (for checking a build).

## How the lighting works

- **The sun's shadows:** the scene is drawn from the sun into a 4096-pixel depth map each frame, round what the camera
  sees. The far sides of things are drawn, which keeps the lit faces free of speckle ("acne").
- **The lights' shadows:** each light has a cube of six depth pictures round it, kept as tiles of one 4096-pixel
  atlas. A cube is drawn in its vessel's own frame, because nothing in a vessel moves against its own lights. So the
  cubes are drawn once at the start, and again for a vessel only when it is built on. A spark's flash has a cube of
  its own, drawn when the sparks fly.
- **The shader** lights each point with ambient light, the sun (looked up in its shadow map) and up to 32 of the
  vessel's lights (each looked up in its cube), with a little shine on the metal.
- **Glow:** the glowing parts are drawn alone into a half-size picture (everything else black, to hide what is
  behind), blurred tight and wide, and added on top.
- **Particles** are soft dots that always face the camera: added for fire and sparks, blended back to front for
  steam and smoke.
