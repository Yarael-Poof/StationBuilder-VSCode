# Content files

Everything you can build, and every material, is defined in the TOML files in this folder. The game
reads every `.toml` file here and in any subfolder, in path order, when it starts.

- **F5** reads the files again while the game runs. Placed items take on the new numbers, art and settings at once.
- **F6** opens the Content panel. It lists problems found in the files (errors in red, warnings in amber), every
  item and material, and a preview of the item you pick.
- **F7** opens the content editor, a form for any item or material (see below). It writes these same files.

A file can hold any mix of the sections below. Splitting them into files (`power.toml`, `liquids.toml`, ...) is
only for tidiness, so put new things wherever suits you, for example in a subfolder of your own.

When something is wrong, the message names the file, line and field, e.g.
`content/power.toml:14 [item.generator60]` / `electrical.generator.hz: must be a number above 0`.

- **An item with an error** is left out, or keeps its previous definition when reloading.
- **Everything else still loads.**
- **Unknown fields** get a warning, so typos show up.

TOML basics: `key = value`, text in `"quotes"`, `#` starts a comment, `[item.cable]` starts a table, and
`{ a = 1, b = 2 }` is a small table on one line. `electrical.load = { watts = 60 }` is the same as
`electrical = { load = { watts = 60 } }`.

## The in-game editor (F7)

The editor changes the files for you. Open it with F7, with **Edit it** in the Content panel (F6), or with the **edit**
button by an object's name in a tile's panel.

- **The list** on the left has every item (by build menu tab) and every material. Pick one to change it, or make a new
  one with **New item** / **New material**, or **Duplicate** the one you have open.
- **The form** in the middle has every field below, in sections; **open all** shows them all.
- **The preview** on the right shows the art fitted to the footprint, as the game places it, with its ports. `<` and
  `>` turn it. **Click a face** of the footprint to add a port there (it carries what "new ports carry" says). Click a
  port again to change which way it goes. Right-click a port to remove it.
- Below the preview are the **problems** the loader would find as you type, and **the TOML it writes**. Save is
  only allowed with no errors.
- **Save** writes the item's table into its file, then reloads everything as F5 does. A new item goes in the file
  picked at the bottom: any content file, or a new one (`content/custom.toml` unless you choose).
- **Revert** goes back to what the file has. **Delete...** takes the table out of its file.

What saving does to a file:

- Only that item's table (and its recipe tables) is replaced. Everything else in the file stays exactly as it was.
- Comments above the table stay. Comments inside it are lost, because the table is written out again from the form.
- Fields left at their defaults are not written.
- The first time a file is changed while the game runs, its old text is kept beside it as `<file>.bak`. The game
  doesn't read `.bak` files.
- An item's key can't be changed once it exists, because saves refer to it by its key. Duplicate it to make one with
  a new key.
- Changing an item's layer, wall edge or footprint, or whether it is a machine, is saved. The game still needs a
  restart to use it, as with F5.

The editor doesn't change families (`[turns]`, `[kinds]`), the menu's tab order or aliases: edit those in the files.

---

## Items: `[item.<key>]`

The key is the item's name in save files. It must start with a letter, then letters, digits and `_` only.
**Don't rename the key of something already placed in a save:** the save would no longer find it.

| Field | Meaning | Default |
|---|---|---|
| `name` | Title in the build menu and panels | the key |
| `tab` | Build menu tab it appears in. Leave it out to keep the item out of the menu (the other walls and pumps, which R turns to) | not in the menu |
| `layer` | Which slot of a tile it takes: `"fixture"` (any number per tile, needs a floor or wall), `"floor"` (one per tile), `"wall"` (one per edge), `"background"` (the blank tile) | `"fixture"` |
| `mount` | `"outside"`: it goes on a bare tile (no floor) beside the hull, or on the roof of the deck below (see Outside) | `"inside"` |
| `edge` | Walls only: the tile edge it stands on, `"UL"`, `"UR"`, `"LL"` or `"LR"` | required for walls |
| `art` | Image file, a path from the game folder (where `tiles/` is), e.g. `"tiles/heater.png"`. 64 x 64 pixels for one tile; the floor diamond's centre is at (32, 46.5). A machine can give a list of 4, one per quarter turn (see Machines) | required |
| `blocked` | The red message when it can't be placed | "cannot place *name* here" |
| `shape` | `true` for line pieces: placed as Auto, straight, corner or T (Shift+R), turned with R | `false` |
| `draw_arms` | How the art shows its links: `"cross"` (the art is a four-way cross, cut down to the arms that actually link), `"cross_upright"` (the same, keeping an upright like a riser's), or another item's key (the art is drawn over that item's arms, e.g. a pump over `"pipe"` stubs) | as drawn |
| `electrical` | What it does with electricity (below) | nothing |
| `fluid` | What it does with liquid (below) | nothing |
| `thermal` | Heat capacity, walls and air ducts (below) | nothing |
| `storage` | A container of solids: `{ litres = 1000, kg = 2000 }` (below) | not storage |
| `items` | An item tube: `items.tube = { kg_per_s = 2 }`, or a tube riser `items.riser = { kg_per_s = 2 }` (below) | not a tube |
| `settings` | Values the player can change on each placed one (below) | none |
| `footprint` | `[w, h]`: tiles it covers, w along the lower right, h along the lower left (see Machines) | `[1, 1]` |
| `ports` | Its connections, one per face (see Machines) | none |
| `rotates` | `true` to turn with R even with no ports and one tile big | `false` |
| `recipe` | What a machine makes: one `[[item.<key>.recipe]]` table per recipe (see Recipes) | makes nothing |
| `max_temperature` | Machines that make things: they stop while their air is hotter than this, C (see Recipes) | no limit |

The layer, wall edge and footprint of an item that is already loaded can't change with F5, nor can whether it is a
machine; those need a restart.

### `electrical`: give one or more of these

Every circuit returns through the station hull, so a generator or load sits between its tile and ground.
Loads and generators connect through a cable on their own tile.

| Table | Fields |
|---|---|
| `electrical.conductor` (a cable) | `ohms` per tile (above 0, required), `henries` per tile, `amps` rating (the power view colours by it) |
| `electrical.riser` (a cable that also links to a riser directly above or below) | the same as `conductor` |
| `electrical.generator` | `volts`, `hz`, `va` (its rating), `ohms` internal resistance (all above 0, required), `henries` internal, `efficiency` 0..1 (what it burns that becomes electricity; the rest is waste heat on its tile) |
| `electrical.load` | either `watts` (a resistive load drawing that at 230 V, or at `volts` if given) or `ohms`; plus `henries` and `farads` for motors and the like (they lower the power factor) |

### `fluid`: give exactly one

| Table | Fields |
|---|---|
| `fluid.pipe` | `conductance` in (L/s)/kPa for one tile (above 0, required), `litres` it holds |
| `fluid.riser` | the same as `pipe`, and it also links to a pipe riser directly above or below |
| `fluid.pump` | `kpa` head with nothing flowing, `flow` L/s it can push against no pressure, `conductance`, `litres`. Give it ports: one liquid `in` and one `out`; it pushes from one to the other (and R turns it). A pump with no ports takes `pushes = "UL"` / `"UR"` / `"LL"` / `"LR"` and joins pipes in line (front and back) |
| `fluid.tank` | `litres` it holds, `conductance`, `liquid` (the liquid material it is placed full of; water if it doesn't say) |
| `fluid.exchanger` | `watts_per_kelvin` between its tile's air and the liquid, `conductance`, `litres` |
| `fluid.radiator` | `area_m2` radiating (both faces, required), `emissivity` 0..1 (default 0.9), `conductance`, `litres`. Outside items only, in practice (see Outside) |

Pipes, pumps, tanks and exchangers are perfectly insulated: only exchangers move heat between liquid and air.

**Liquids in a network.**

- A pipe network holds whatever liquid its tanks hold, and only one.
- If tanks holding different liquids end up joined, the network is *mixed*. Nothing flows until they're separated
  again or one of the tanks is drained. The liquid view shows a mixed network red.
- An empty tank takes whatever liquid fills it.
- Each tank keeps its liquid and how much it holds, and saves them: `tank{@coolant=850}`, or `tank{@empty}`.
- For testing, the tile panel can fill a tank with any liquid, or drain it.

**Radiators** (`fluid.radiator = { area_m2 = 6, emissivity = 0.9, conductance = 1, litres = 20 }`) shed the liquid's
heat to space as e sigma A (T^4 - Ts^4), with T the liquid's temperature and Ts deep space (-270 C). A hot radiator
sheds far more than a cool one: 6 m2 sheds about 2.3 kW with the liquid at 20 C and 3.8 kW at 60 C. `area_m2` counts
both faces. Like an exchanger, a radiator only works on a full network with liquid flowing through it.

### `thermal`: any of these

| Field | Meaning |
|---|---|
| `mass` | J/K it adds to its tile's heat capacity (a floor is 5000: the deck and its air) |
| `wall` | W/K through it, for walls (0.5 is a well insulated wall) |
| `duct` | W/K of air between its tile and the same tile one level up or down, when both have one (an air duct riser) |

### `storage`: a container of solids

`storage = { litres = 1000, kg = 2000 }`: it holds as much as fits in `litres` (1000 L = 1 m3), each material taking
`kg / density` of it, and if it gives `kg`, no more than that mass. It holds several solids at once; liquids go in tanks.

- **Saving.** What it holds is kept on it and saved with it: `crate{@iron_ore=500;@ice=120}`. If a save holds more than
  it fits now (its size changed), it keeps what fits and says so.
- **The tile panel** shows how full it is and what it holds. For testing, it can put a chosen solid in, take it out or
  empty it.
- **Bigger containers** can be machines with a footprint, and item ports for the item tubes (see `storage.toml`).

### `items`: item tubes

An item tube is a line piece like a pipe: give it `shape = true` and `draw_arms = "cross"`, and
`items.tube = { kg_per_s = 2 }` for how fast it carries. A tube riser (`items.riser`) also links to a tube riser
directly above or below. Each tile takes one tube.

**What joins up.** Tubes join containers into an items network: through their item ports (`carries = "items"`), or,
for a one-tile container with no ports, on any side. Each tick a network moves solids at up to its slowest tube's rate
in total (ports touching with no tube between them: 1 kg/s):

1. from `out` ports to `in` ports;
2. what's left over, from `out` ports to containers that go both ways (one-tile containers with no ports: buffers);
3. from buffers to `in` ports that still have room.

**The rules.**

- Buffers never pass things between themselves, and a container never sends to itself.
- A container only takes what it has room for, and only its filter's material if it has one. The filter is picked in
  its tile panel and saved as `accept=ice`.
- When several containers could send or take, the first in line changes every tick, so it evens out.
- An output whose network has receivers but none of them will take it is **blocked**.

The logistics view (I key, or the crate button) shows tubes lit while they move things, item ports, each container's
kg/s in or out, and blocked outputs.

### `settings`: a list, one table per setting

```toml
settings = [
    { key = "on", label = "Running", toggle = true, default = true },
    { key = "v", label = "Output voltage", default = 230, min = 210, max = 250, step = 1, unit = "V" },
]
```

- A **toggle** has `toggle = true` and a `default` of `true` or `false`.
- A **number** has `default`, `min`, `max`, `step` (default 1) and `unit`.
- `hidden = true` keeps a setting out of the tile panel.
- Keys are lower-case letters, digits and `_`. Only values that differ from the default go into save files.

Some keys mean something to the simulations:

| Key | On | Meaning |
|---|---|---|
| `on` | generators, loads, pumps | switched on (a toggle) |
| `v` | generators | output voltage |
| `t` | loads | a thermostat: it only draws power while its tile is colder than this |
| `s` | pumps | speed, % of full |
| `c` | line pieces | reserved for the shape (use `shape = true`) |
| `r` | machines | reserved for the turn (set by R when placed) |
| `rc` | machines that make things | reserved for the recipe (picked in the tile panel) |

---

## Machines: footprints, turns and ports

An item with `ports`, a `footprint` bigger than one tile, or `rotates = true` is a **machine**.

**Placing and removing it**

- R turns it a quarter clockwise before it is placed. The turn is saved as its hidden setting `r`.
- It is placed with the tile you point at in its middle.
- A machine bigger than one tile needs every tile of its footprint clear of other fixtures, and has them to itself
  while it stands.
- The bulldozer on any of its tiles removes all of it.

**Its picture.** It is drawn from its front tile (the lowest on screen), so its art can be bigger than one tile:

- For a footprint of w x h, the art is `32 x (w + h)` pixels wide and as tall as you like.
- The footprint's lower corner sits at the bottom of the image, `32 x w` pixels from its left edge. A 1 x 1 item is
  the usual 64 x 64.
- On a quarter turn a non-square machine's art is mirrored left to right, which fits the turned footprint.
- `art` can instead be a list of 4 pictures, unturned and then 1, 2 and 3 quarter turns clockwise, if it should look
  different each way (the pump's arrow).

**Ports.** These are its connections, one table each:

```toml
ports = [
    { tile = [0, 0], side = "UL", carries = "liquid", flow = "in" },
    { tile = [1, 0], side = "LR", carries = "liquid", flow = "out" },
    { tile = [1, 0], side = "UR", carries = "power" },
]
```

| Field | Meaning | Default |
|---|---|---|
| `tile` | Which tile of the footprint, unturned: `[0, 0]` is its upper tile, x along the lower right, y along the lower left | `[0, 0]` |
| `side` | The face of that tile: `"UL"`, `"UR"`, `"LL"` or `"LR"`. It must be an outside face of the machine | required |
| `carries` | `"power"`, `"liquid"` or `"items"` (for item tubes) | required |
| `flow` | `"in"`, `"out"` or `"both"` (`"any"` is the same as `"both"`) | `"both"` |

The game marks ports on the placement ghost, round a selected machine, and in the power, liquid and logistics views.
Each port gets a tag labelled **In**, **Out** or **Any** (for `both`), pointing into its face, out of it, or both ways.
Tags are yellow for power, blue for liquid and orange for items. `flow = "any"` is another way to write `"both"`.

How a machine joins the networks:

- **It connects only through its ports.** A cable or pipe on the tile across a port's face, pointing back at it,
  joins it.
- **Power:** one power port at most, used by its `electrical.load` or `electrical.generator`.
  - A machine bigger than one tile needs a power port to use power.
  - A one-tile machine can still use a cable on its own tile, as other devices do.
  - Its waste heat warms every tile it covers.
- **Liquid:** its fluid behaviour (tank, exchanger, pipe body) is on the tile of its first liquid port.
  - Inside a big machine, its liquid ports are one network (a big tank), or pushed from `in` to `out` by
    `fluid.pump`.
  - A machine with liquid ports but no `fluid` behaviour keeps them as separate networks: a machine that makes
    things moves liquid through them itself (see Recipes below).
  - A pump needs one liquid port `in` and one `out`.
- **Ports of the same kind on one tile share that tile's connection.** Give separate inputs and outputs their own
  tiles.
- **Heat capacity** (`thermal.mass`) is shared over its tiles.

See `machines.toml` for a large tank, a large generator and a plate heat exchanger.

## Outside: things mounted on the hull

`mount = "outside"` puts an item outside the station:

- **Where it goes:** on bare tiles (no floor) in the deck's area, either beside the hull (a floored tile next to one of
  its tiles) or on the roof of the deck below (a floor directly under one of its tiles).
- **What can't share its tiles:** no cable, pipe or tube can go on a bare tile, and no floor can be laid under it.
- **Connections:** it reaches the networks only through its ports. Face a port at the hull, with the cable, pipe or
  tube on the floor across it. The loader insists on the ports it needs: a power port to use or give power, liquid
  ports for liquid, item ports for storage.
- **Exterior view:** outside items show there (O).

See `outside.toml`:

- **Radiator panel and large radiator:** liquid in at one end and out at the other, ports on the hull side.
- **Solar array:** a generator in constant sunlight.
- **Docking jetty:** a cargo hold reached through its item port at the hull end, ready for ships.

## Recipes: machines that make things

A machine with one or more `[[item.<key>.recipe]]` tables makes things. Each placed one runs one recipe, picked in
its tile panel (saved as its hidden setting `rc`).

```toml
[[item.smelter.recipe]]
name = "Smelt iron"
time = 8                         # seconds a cycle takes at full power
in = { iron_ore = 10 }           # what a cycle takes
out = { iron = 6, slag = 4 }     # what a cycle gives
heat = 2000                      # W it gives off while working (optional; negative cools)
```

| Field | Meaning | Default |
|---|---|---|
| `name` | Shown in the tile panel's recipe list | `Recipe 1`, ... |
| `time` | Seconds one cycle takes at full power, above 0 | required |
| `in` | Materials a cycle takes: kg of a solid, L of a liquid | nothing |
| `out` | Materials a cycle gives, the same way | nothing |
| `heat` | W of heat it gives off while working, on top of its power's own heat | 0 |

A recipe needs `in`, `out` or both: a mining intake has only `out`.

**How it runs.**

- It holds up to two cycles of each input and each output, in its own buffers. A cycle starts once it has a whole
  cycle of every input and room for a whole cycle of every output.
- It works at the speed its power allows: at full power a cycle takes `time`; at half power, twice as long. It takes
  its inputs and gives its outputs when the cycle finishes.
- With nothing to do (waiting for an input, its output full, switched off, too hot) it switches its power off, so it
  costs nothing while it waits.
- `max_temperature = 60` on the item: it stops while the air on any of its tiles is hotter than this (C), and starts
  again 2 C below it.
- An `on` setting switches it off and on.

**Where things go in and out.**

- **Solids** through item ports (`carries = "items"`): an `in` port takes the solids its recipe uses, an `out` port
  gives what it has made, and a `both` port does both. The item tubes treat it like a container (see `items` above).
- **Liquids** through liquid ports, straight from and to the tanks on that pipe network (up to 2 L/s a port). It
  takes only from a full network of the liquid it needs, and gives only to a network holding that liquid or none yet.
- The loader checks it has the ports it needs: a recipe that gives a liquid needs a liquid port with `flow = "out"`
  or `"both"`, and so on.
- A machine that makes things can't also be storage, a tank or a pump.

**Saving.** What is in its buffers is saved with it: `@iron_ore=10` for an input, `@>iron=6` for an output. How far
through its cycle it is isn't saved; a loaded machine starts its cycle again.

**The tile panel** shows its recipe (with a list to change it), what it is doing, how far through its cycle it is,
and its buffers. Changing the recipe sends inputs the new one doesn't use back out through its output port. For
testing, it can fill its inputs and empty its outputs. The logistics view (I) labels every machine with what it is
doing.

See `industry.toml` for a mining intake, a smelter and a recycler.

## Aliases: `[aliases]`

An old key, from saves made before an item changed, and what it loads as now: an item, with settings if needed.

```toml
[aliases]
pumpLR = "pump{r=2}"   # the old pump for each direction is now one pump, turned
```

## Families

**`[turns.<name>]`**: items that are one menu entry, which R turns through (walls: each edge is its own item). Give 4
keys, clockwise from the upper left: UL, UR, LR, LL. The first is the one shown in the menu (give it a `tab`, and leave the others
without one).

```toml
[turns.Wall]
items = ["wallUL", "wallUR", "wallLR", "wallLL"]
```

**`[kinds.<name>]`**: items that are one menu entry, whose kind the player picks in the build menu and can change on a
placed one from its tile panel (the riser: cable, pipe or air duct).

```toml
[kinds.Riser]
items = ["riser", "riserPipe", "riserDuct"]
labels = ["Cable", "Pipe", "Air duct"]
```

## The menu: `[menu]`

```toml
[menu]
tabs = ["Floors", "Walls", "Cables"]   # tabs in this order; any others follow in the order met
```

## Materials: `[material.<key>]`

| Field | Meaning | Default |
|---|---|---|
| `name` | Shown in panels | the key |
| `phase` | `"solid"`, `"liquid"` or `"gas"` | required |
| `colour` | `"#rrggbb"`, used in the views | white |
| `density` | kg/m3 (solids: as they pack, gaps included) | required |
| `heat_capacity` | J/(kg K) | required |

Liquids are what pipes and tanks hold. Their heat per litre (`heat_capacity` x `density` / 1000: water 4180
J/K, the coolant 3531 J/K) sets how much heat each litre carries away from an exchanger. Their `colour` tints
the pipes in the liquid view. A liquid can be added with no code: give it a `[material]` table, and a tank with
`liquid = "<its key>"` to start it off. Solids are what containers hold (`storage`). Their `density`, as they pack
with the gaps, sets how many litres each kg takes up.

---

## An example: a bigger heater

Put this in a new file, e.g. `content/mine.toml`, and press F5 in the game:

```toml
[item.bigHeater]
name = "Big heater"
tab = "Heating"
art = "tiles/heater.png"
electrical.load = { watts = 5000 }
thermal = { mass = 3000 }
settings = [
    { key = "on", label = "Running", toggle = true, default = true },
    { key = "t", label = "Thermostat", default = 18, min = 5, max = 40, step = 1, unit = "°C" },
]
```
