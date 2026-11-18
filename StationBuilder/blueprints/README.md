# Blueprints

A blueprint is a set of tiles' floors, walls and fixtures, kept so it can be placed again. The game reads every `.toml`
file in this folder and its subfolders. `examples/` holds the game's starting production chains.

## In the game

- **C**, or the copy button on the left: the copy tool. Drag over tiles to select them.
- **Ctrl+C**, or **Copy** on the selection's note: copies what is on the selected tiles, then starts placing it.
  A machine reaching outside the selection is left out.
- While placing, a see-through copy follows the mouse. It goes dark where something is in the way, and its outline goes
  red. **Click** to place it, as often as you like. **R** turns it a quarter. **Esc** stops.
- **Ctrl+V** places the last copy again.
- **B**, or the blueprint button: the Blueprints menu. Save your copy there with a name (it becomes a file here), or
  pick any blueprint to place.

When it is placed, floors go first, then walls, then fixtures, each as the build menu would place it:

- Anything a tile won't take is left out, and the note says how much.
- Anything that is already there is left as it is.

Settings come with a copy: shapes, turns, recipes, switches, voltages and crate filters. What containers and tanks hold
doesn't. A tank is placed full of its liquid, as a new one is, unless its settings say `@empty`.

## The file

```toml
name = "Ice to water"
description = "What it is for."
size = [8, 3]
objects = [
    { key = "floor", at = [0, 0] },
    { key = "miningIntake", at = [1, 0], settings = "rc=1" },
    { key = "tank", at = [7, 1], settings = "@empty" },
]
```

- `key` is the item, as in the content files.
- `at` is its tile, counted from the blueprint's upper tile `[0, 0]`: x along the lower right, y along the lower
  left. A machine gives its upper tile.
- `settings` is written the way station saves write them, e.g. `c=10` (a cable's shape), `r=1` (a machine's turn),
  `rc=1` (its recipe), `accept=iron` (a crate's filter).

To share a blueprint, copy its file into someone else's `blueprints/` folder.
