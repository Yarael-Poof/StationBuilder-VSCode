#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include "tileObject.h"

//what an item does in the electrical network. everything false = not electrical.
//every circuit returns through the station ground (the hull), so sources and loads sit between their tile's
//node and ground, and cables link the node to the nodes of neighbouring tiles that also have a cable.
//impedances are R + jwL (+ 1/(jwC)), worked out at the frequency of whatever generator drives the network.
struct electricalProperties
{
	//conductor (cable): links this tile to neighbouring tiles that also hold a conductor.
	//several on one tile act in parallel
	bool conductor = false;
	double resistance = 0.0;        //ohms for one tile's length, must be above 0
	double inductance = 0.0;        //henries for one tile's length
	double ratedCurrent = 0.0;      //amps it is rated to carry (0 = no rating). the power view colours cables by it
	//a riser: as well as its neighbours on the same level, it links through the floor to a riser on the same
	//tile of the level directly above or below. that vertical run has the riser's own impedance
	bool vertical = false;

	//source (generator): an EMF behind an internal impedance
	bool source = false;
	double emf = 0.0;               //volts rms with nothing connected
	double frequency = 0.0;         //Hz
	double ratedPower = 0.0;        //VA (apparent power) it can supply before it is overloaded
	double sourceResistance = 0.0;  //internal ohms, must be above 0
	double sourceInductance = 0.0;  //internal henries
	//fraction of what it burns that becomes electricity; the rest is waste heat on its tile.
	//0 = no waste heat counted (only its internal resistance warms it)
	double efficiency = 0.0;

	//load: an impedance that takes power from the network. its resistance turns that power into heat
	bool load = false;
	double loadResistance = 0.0;    //ohms
	double loadInductance = 0.0;    //henries, 0 = none (motors are inductive)
	double loadCapacitance = 0.0;   //farads, 0 = none

	bool isElectrical() const { return conductor || source || load; }
};

//one-line ways to fill in electricalProperties for the catalogue rows
namespace electrical
{
	const double nominalVolts = 230.0;  //the station's normal supply voltage, used for ratings and the menu text
	const double nominalHertz = 50.0;

	electricalProperties conductor(double _ohmsPerTile, double _henriesPerTile = 0.0, double _ratedAmps = 0.0);
	//a conductor that also links to a riser on the level above or below (impedance per tile, and per level)
	electricalProperties riser(double _ohms, double _henries = 0.0, double _ratedAmps = 0.0);
	electricalProperties generator(double _volts, double _hertz, double _voltAmps, double _internalOhms, double _internalHenries = 0.0,
		double _efficiency = 0.0);
	electricalProperties load(double _ohms, double _henries = 0.0, double _farads = 0.0);
	//a plain resistive load that draws _watts when supplied with _volts, e.g. a 60 W light at 230 V
	electricalProperties resistiveLoad(double _watts, double _volts = nominalVolts);
	//real power a load takes from the nominal supply (230 V, 50 Hz): its rating. 0 if it isn't a load
	double ratedLoadWatts(const electricalProperties& _properties);
	//short description for the build menu preview, e.g. "Load: 60 W at 230 V". "" if not electrical
	std::string summary(const electricalProperties& _properties);
}

//what an item does in the fluid (coolant) simulation. every fluid item holds liquid and links its tile to
//neighbouring tiles that also hold one, so pumps, tanks and heat exchangers sit in a pipe run as part of it
struct fluidProperties
{
	bool carriesLiquid = false;     //any fluid item
	double conductance = 0.0;       //(L/s)/kPa for one tile's length: how easily liquid flows through it
	double volume = 0.0;            //L of liquid it holds when the network is full (tanks: see capacity)
	//pump: pushes liquid from its tile towards the neighbour on side pumpSide (0 UL, 1 UR, 2 LL, 3 LR).
	//a straight pump curve: pumpHead kPa with nothing flowing, pumpFlow L/s against no pressure
	bool pump = false;
	int pumpSide = -1;
	double pumpHead = 0.0;
	double pumpFlow = 0.0;
	//tank: holds up to capacity L, and is placed full
	bool tank = false;
	double capacity = 0.0;
	std::string liquid;             //tanks: the liquid material a newly placed one is full of (content: fluid.tank.liquid)
	//heat exchanger: moves heat between its tile's air and the liquid, W/K
	double exchange = 0.0;
	//a pipe riser: as well as its neighbours, it links through the floor to a pipe riser on the same tile of the
	//level directly above or below
	bool vertical = false;
	//radiator: radiates the liquid's heat out to space from radiatingArea m2 (both faces counted) at emissivity (0..1),
	//as e sigma A (T^4 - Ts^4): a hot radiator sheds far more than a cool one. 0 area: not a radiator
	double radiatingArea = 0.0;
	double emissivity = 0.0;
};

namespace fluid
{
	fluidProperties pipe(double _conductance, double _litres);
	//a pipe that also links to a pipe riser on the level above or below
	fluidProperties riser(double _conductance, double _litres);
	fluidProperties pump(int _side, double _headKPa, double _flowLitresPerSecond, double _conductance, double _litres);
	fluidProperties tank(double _capacityLitres, double _conductance);
	fluidProperties exchanger(double _wattsPerKelvin, double _conductance, double _litres);
	fluidProperties radiator(double _areaM2, double _emissivity, double _conductance, double _litres);
	//what a radiator sees: deep space (C). nothing warms it back (the sunlight is taken as edge-on to its faces)
	const double spaceSinkCelsius = -270.0;
	const double stefanBoltzmann = 5.670374419e-8;   //W/(m2 K4)
	//W a radiator sheds to space with its liquid at _celsius (negative if it were colder than space)
	double radiatedWatts(const fluidProperties& _properties, double _celsius);
	//short description for the build menu preview, e.g. "Tank: holds 1000 L (placed full)". "" if not a fluid item
	std::string summary(const fluidProperties& _properties);
}

//what an item does in the thermal simulation. a tile with a floor holds air and has a temperature; everything
//on it adds to how much heat it takes to warm it. walls slow heat across the tile edge they stand on
struct thermalProperties
{
	double heatCapacity = 0.0;      //J/K this item adds to its tile (a floor: the air and deck itself)
	double wallConductance = 0.0;   //walls only: W/K through the wall (between rooms, or out to space)
	//an air duct riser: W/K of air moving through it to an air duct riser on the same tile of the level directly
	//above or below (in place of the deck's own slow conduction). both tiles need floors: they hold the air
	double ductConductance = 0.0;
};

namespace thermal
{
	//an item that adds heat capacity to its tile
	thermalProperties mass(double _joulesPerKelvin);
	//a wall: heat passes through it at _wattsPerKelvin
	thermalProperties wall(double _wattsPerKelvin);
	//an air duct riser: air (and its heat) moves through it at _wattsPerKelvin to a duct riser above or below
	thermalProperties duct(double _wattsPerKelvin);
	//short description for the build menu preview, e.g. "Air duct: 200 W/K to a duct above or below". "" if nothing to say
	std::string summary(const thermalProperties& _properties);
}

//what an item holds of solid materials (ore, ice, parts...): a container. it holds as much as fits in its volume, each
//material taking kg / density of it, and optionally no more than a mass. liquids go in tanks instead
struct storageProperties
{
	double litres = 0.0;    //its volume, L (1000 L = 1 m3). 0: it isn't storage
	double kg = 0.0;        //the most it can carry, kg. 0: no limit but its volume
	bool isStore() const { return litres > 0.0; }
};

//an item tube: a line piece (like a pipe) that joins containers' item ports into a network, which moves solids from
//outputs to inputs. a network moves at most its slowest tube's rate, in total
struct itemsProperties
{
	bool tube = false;
	bool vertical = false;     //a tube riser: also links to a tube riser directly above or below
	double rate = 0.0;         //kg/s it can carry
};
namespace items
{
	//"Item tube: 2.0 kg/s". "" if it isn't a tube
	std::string summary(const itemsProperties& _properties);
}

//one thing a machine can make: what a cycle takes, how long it takes at full power, and what it gives
struct recipeInfo
{
	std::string name;
	double seconds = 1.0;                                     //one cycle at full power
	std::vector<std::pair<std::string, double>> inputs;       //material key, amount a cycle: kg of a solid, L of a liquid
	std::vector<std::pair<std::string, double>> outputs;
	double heat = 0.0;                                        //W it gives off while working, as well as what its power becomes
};

//a value every placed item of a type carries on its own: a generator's on/off switch, its output voltage...
//the catalogue declares them, each tileObject keeps its own values, and saves store the ones that differ
//from the default inline after the item's key, e.g. "generator{on=0;v=240}"
struct itemSetting
{
	enum class kind { toggle, number };
	std::string key;              //short name in save files: lower case letters, digits and _ only
	std::string label;            //shown in the settings panel
	kind type = kind::number;
	double defaultValue = 0.0;    //toggles: 1 = on, 0 = off
	double minimum = 0.0;         //numbers only: values are clamped to minimum .. maximum
	double maximum = 0.0;
	double step = 1.0;            //numbers only: how far one click of the panel moves it
	std::string unit;             //numbers only, e.g. "V"
	bool hidden = false;          //kept and saved, but not shown in the settings panel (set by placement, e.g. a cable's shape)
};

//one-line ways to declare settings for the catalogue rows
namespace settings
{
	itemSetting toggle(const std::string& _key, const std::string& _label, bool _defaultOn);
	itemSetting number(const std::string& _key, const std::string& _label, double _default, double _minimum, double _maximum,
		double _step, const std::string& _unit);
}

//a connection on one face of a machine: what passes through it, and which way. the networks link a machine's tile
//to its neighbour only through its ports
struct portInfo
{
	enum class kind { power, liquid, items };
	enum class direction { in, out, both };
	sf::Vector2i cell;              //which tile of the machine's footprint (0,0 = its upper tile; x along LR, y along LL)
	int side = 0;                   //the face of that tile it is on: 0 UL, 1 UR, 2 LL, 3 LR
	kind carries = kind::liquid;
	direction flow = direction::both;
};
const char* portKindName(portInfo::kind _kind);
const char* portFlowName(portInfo::direction _flow);

//a ship's main engine (content: engine = { newtons, exhaust_velocity, fuel, pushes }): it pushes the ship towards one
//side of its grid (pushes, unturned: "UL", "UR", "LL" or "LR"; R turns it), burning fuel, a liquid it takes through its
//liquid port, at newtons / exhaust_velocity kg/s. its power (electrical.load) is only drawn while it fires. with no
//fuel it is electric: it runs on its power alone (full thrust at full power), burning nothing
struct engineProperties
{
	double newtons = 0.0;          //full thrust
	double exhaustVelocity = 0.0;  //m/s: how far a kg of fuel goes (the rocket equation's v_e); unused when electric
	std::string fuel;              //the liquid it burns (a material's key); "" = electric
	int pushes = 0;                //0 UL, 1 UR, 2 LL, 3 LR, unturned
	bool isEngine() const { return newtons > 0.0; }
	bool electric() const { return fuel.empty(); }
};

//a docking point (content: dock = { tile = [x, y], face = "LR" }): a ship docks here, its own docking point's face
//against this one's (the faces opposite each other, the two grids square). tile is a tile of its footprint, face the
//outside face of that tile it docks on; both turn with the item
struct dockProperties
{
	bool isDock = false;
	sf::Vector2i cell;          //unturned
	int face = 3;               //0 UL, 1 UR, 2 LL, 3 LR, unturned
};

//what drives a light, or steam and smoke (content: follows = "power" ...)
//  power:   its share of its rated power (a lamp, a heater: off while its thermostat has it off)
//  running: a generator: its colour while it runs, red and blinking while it is switched off or has tripped
//  working: a machine that makes things: how fast it works
//  always:  always on
//  blink:   a beacon, a short flash every second or so
enum class lightDriver { power, running, working, always, blink };
const char* lightDriverName(lightDriver _driver);
//the driver with this name, or -1
int lightDriverFromName(const std::string& _name);

//the light an item gives, and which parts of its art glow (content: light = { colour, brightness, reach, height,
//follows, flicker, emergency, glow }). the lighting (lighting.h) draws it: it lights the room the item is in
struct lightProperties
{
	bool gives = false;                         //it has a light table
	sf::Color colour = sf::Color(255, 255, 255);
	double brightness = 1.0;                    //1: a lamp's. 0: its art glows, but it lights nothing round it
	double reach = 6.0;                         //m: how far its light reaches
	double height = 20.0;                       //px above the floor diamond: where the light is in its art
	lightDriver follows = lightDriver::power;
	double flicker = 0.0;                       //0..1: firelight
	bool emergency = false;                     //a lamp: it pulses red while its power has failed
	double glow = 0.8;                          //its art's pixels at least this bright (0..1, and coloured or near white)
	                                            //glow, unless it has a <art>_glow.png. 0: nothing glows
};

//steam or smoke an item gives off (content: emits = { kind, at, follows, rate })
struct emitProperties
{
	std::string kind;                           //"steam" or "smoke"; "" for none
	sf::Vector2f at = sf::Vector2f(-1.f, -1.f); //where in its art (px from its top left, unturned); below 0: the art's top
	lightDriver follows = lightDriver::always;
	double rate = 1.0;                          //1: a puff of steam 12 times a second, of smoke 7 times
	bool emits() const { return !kind.empty(); }
};

//manoeuvring thrusters (content: thrusters = { newtons, exhaust_velocity, fuel }): small jets that push the ship any
//way, through its centre of mass (so they don't turn it), for fine moves. fuel through their liquid port; with no fuel
//they are electric, on their power alone
struct thrusterProperties
{
	double newtons = 0.0;          //in each direction
	double exhaustVelocity = 0.0;
	std::string fuel;              //"" = electric
	bool isThruster() const { return newtons > 0.0; }
	bool electric() const { return fuel.empty(); }
};

//one entry per thing that can be placed on a tile. the build menu, the placement rules, the save/load keys
//and the simulations all come from this list, which is read from the content files (StationBuilder/content/*.toml,
//see content.h and content/README.md): a new item is a new [item.<key>] table there, no code
struct tileTypeInfo
{
	std::string key;            //its name in saves and the texture key its art is registered under
	std::string name;           //title shown in the build menu and in the preview
	std::string category;       //tab in the build menu. "" means the item is not shown in the menu
	tileLayer layer = tileLayer::fixture;   //which slot on a tile it goes in
	int wallEdge = -1;          //walls only: 0 = UL, 1 = UR, 2 = LL, 3 = LR, otherwise -1
	std::string blockedMessage; //red warning shown next to the tile when this item can't be placed there
	electricalProperties electrical = {};  //leave out for items that aren't electrical
	std::vector<itemSetting> settings = {}; //leave out for items with nothing to set
	thermalProperties thermal = {};         //leave out for items that don't matter to the heat simulation
	fluidProperties fluid = {};             //leave out for items that have nothing to do with liquids
	storageProperties storage = {};         //leave out for items that don't store solids
	itemsProperties items = {};             //leave out for items that aren't item tubes
	std::vector<recipeInfo> recipes;        //a machine that makes things: what it can make (see recipes:: below)
	//mounted outside the hull (content: mount = "outside"): it goes on a bare tile, one with no floor, either beside the
	//hull (a floored tile next to it on the same deck) or on the roof of the deck below. everything else needs a floor or wall
	bool outside = false;
	double maxTemperature = 0.0;            //recipes stop while its tiles' air is hotter than this (C); 0 = no limit
	//a frame (content: frame = true, a floor): open structure, outside the hull. it holds no air, and takes what can be
	//out in the open: cables, pipes, item tubes, risers, pumps, and anything mounted outside
	bool frame = false;
	double kg = -1.0;                       //its own mass (content: kg); below 0: a default for its layer (shipTuning)
	engineProperties engine = {};           //a ship's main engine
	thrusterProperties thrusters = {};      //manoeuvring thrusters
	double gyroTorque = 0.0;                //N m a gyroscope turns its ship with (content: gyroscope = { newton_metres })
	dockProperties dock = {};               //a docking point
	bool helm = false;                      //a flight console: a ship takes orders only with one that has power
	lightProperties light = {};             //the light it gives, and what of its art glows (lighting.h)
	emitProperties emits = {};              //steam or smoke it gives off

	std::string art;            //image file, relative to the game folder (where tiles/ is)
	//how the art shows its links (see lineArt.h): "" as it is; "cross": four-way art cut down to the arms it links
	//on; "cross_upright": the same, keeping a riser's upright; or another item's key: the art drawn over that
	//item's arms (a pump on its pipe stubs)
	std::string drawArms;
	std::string origin;         //where it was defined, "content/power.toml:12", for messages
	//true once its definition has gone from the content files (after a reload): what is placed keeps working,
	//but it is no longer in the menu and can't be placed again
	bool removed = false;

	//machines: items with ports, or bigger than one tile (see footprint:: below). they turn with R
	sf::Vector2i footprint = sf::Vector2i(1, 1);   //tiles along x (towards LR) and y (towards LL), unturned
	std::vector<portInfo> ports;                   //unturned
	bool rotates = false;                          //turns with R (every machine does; set for others by rotates = true)
	std::vector<std::string> turnArt;              //art for 1, 2 and 3 quarter turns, if it has its own (else art is used)
};

//what something is made of: liquids in pipes and tanks, solids in storage. read from [material.<key>] tables in the
//content files. (liquids and solids are used from the content plan's subtasks 2 and 4 on)
struct materialInfo
{
	enum class phase { solid, liquid, gas };
	std::string key;
	std::string name;
	phase state = phase::solid;
	sf::Color colour = sf::Color::White;
	double density = 1000.0;         //kg/m3 (solids: as it packs, with the gaps)
	double heatCapacity = 1000.0;    //J/(kg K)
	std::string origin;
	bool removed = false;

	//J/K one litre of it takes to warm by a degree (water: 4180)
	double heatPerLitre() const { return heatCapacity * density / 1000.0; }
	//kg in one litre
	double kilogramsPerLitre() const { return density / 1000.0; }
};
const char* phaseName(materialInfo::phase _phase);

//the setting with this key on a type, or nullptr
const itemSetting* findSetting(const tileTypeInfo& _type, const std::string& _key);

//which sides of its tile a cable, riser or pipe connects on. sides are numbered like the wall slots, 0 UL, 1 UR,
//2 LL, 3 LR, and a mask has bit (1 << side) for each. two neighbouring pieces link only if both connect on the
//side between them. a piece's mask is its hidden setting "c": 15 (all four) is "Auto" and joins anything next to it
namespace connections
{
	const int all = 15;
	const char* const settingKey = "c";
	//the mask a cable, riser or pipe has (Auto if it has no "c" setting), 0 for anything else
	int sides(const tileTypeInfo& _type, const std::vector<double>& _settings);
	//the side opposite _side: UL <-> LR, UR <-> LL
	int opposite(int _side);
	//turns a mask a quarter turn clockwise on screen: UL -> UR -> LR -> LL -> UL
	int rotate(int _mask);
	//"upper left, lower right" etc.; "none" for 0, "all sides" for 15
	std::string describe(int _mask);

	//the shapes a cable or pipe can be placed as, picked with Shift+R and turned with R
	enum class shape { automatic, straight, corner, junction };
	const char* shapeName(shape _shape);
	shape nextShape(shape _shape);
	//the mask of a shape at a rotation (0 = its first; Auto ignores rotation)
	int shapeMask(shape _shape, int _rotation);
	//how many different rotations a shape has: Auto 1, straight 2, corner and junction 4
	int rotations(shape _shape);
}

//items placed as one menu entry that R turns: the walls (which edge) and the pumps (which way they push). keys run
//clockwise from the upper left side: UL, UR, LR, LL. the first key is the one shown in the build menu.
//declared in the content files as [turns.<name>]
struct rotationFamily
{
	std::string name;                 //shown in the build menu, e.g. "Wall"
	std::vector<std::string> keys;    //its items, clockwise
};
//the family an item belongs to, or nullptr
const rotationFamily* familyOf(const std::string& _key);
//the item _steps quarter turns clockwise from _key in its family (_key itself if it has none)
std::string rotateKey(const std::string& _key, int _steps);

//items placed as one menu entry whose kind is picked in the build menu (and changed on a placed one from its tile
//panel): the riser, which carries a cable, a pipe or an air duct. the first key is the one shown in the build menu.
//declared in the content files as [kinds.<name>]
struct kindFamily
{
	std::string name;                   //shown in the build menu, e.g. "Riser"
	std::vector<std::string> keys;      //its items
	std::vector<std::string> labels;    //what each one is, for the choice: "Cable", "Pipe", "Air duct"
};
//the kind family an item belongs to, or nullptr
const kindFamily* kindFamilyOf(const std::string& _key);
//where _key is in its kind family (0 for the first), -1 if it has none
int kindIndex(const std::string& _key);

//every known type, in the order the content files define them (tabs and the items in them appear in this order).
//entries never move or go away while the program runs (a reload updates them in place), so pointers to them stay good
const std::deque<tileTypeInfo>& tileCatalogue();
//the entry for a texture key, or nullptr if the key is not in the catalogue
const tileTypeInfo* findTileType(const std::string& _key);
//the warning text for a key that was refused, with a generic fallback
std::string blockedMessageFor(const std::string& _key);
//every known material, in the order the content files define them
const std::deque<materialInfo>& materialCatalogue();
//the material with this key, or nullptr
const materialInfo* findMaterial(const std::string& _key);

//goes up every time the content files are (re)loaded. a definition changes in place on a reload, so the networks
//compare this as well as the station's revision to know their impedances, volumes... may have changed
std::uint64_t catalogueGeneration();

//what the content loader (content.cpp) fills in. nothing else should change these
namespace catalogueStore
{
	std::deque<tileTypeInfo>& types();
	std::deque<materialInfo>& materials();
	std::vector<rotationFamily>& turns();
	std::vector<kindFamily>& kinds();
	std::uint64_t& generation();
	//the build menu's tabs in the order the files ask for ([menu] tabs = [...]); others follow in the order met
	std::vector<std::string>& tabOrder();
	//old keys and what they load as: "pumpLR" -> { "pump", "r=2" } (content: [aliases] pumpLR = "pump{r=2}")
	std::map<std::string, std::pair<std::string, std::string>>& aliases();
}

//an old key (from an older save) as the item and settings it is now ("pumpLR" -> "pump", "r=2"); a key with no alias
//comes back as it is, with no settings
std::string resolveAlias(const std::string& _key, std::string& _settings);

//machines that make things. a placed one runs the recipe its hidden "rc" setting picks. it keeps an input buffer
//(tileObject::contents()) and an output buffer (tileObject::outputs()), each holding up to bufferCycles cycles' worth:
//solids come in and go out through its item ports (the tubes), liquids through its liquid ports. a cycle finishes,
//turning a cycle of inputs into a cycle of outputs, once it has run its seconds at full power
namespace recipes
{
	const char* const settingKey = "rc";
	const double bufferCycles = 2.0;
	bool isProcessor(const tileTypeInfo& _type);
	//the recipe a placed machine runs, nullptr if it has none
	const recipeInfo* current(const tileObject& _machine);
	int currentIndex(const tileObject& _machine);
	//how much of a material a cycle of _recipe takes (in) or gives (out); 0 if none
	double inputPerCycle(const recipeInfo& _recipe, const std::string& _material);
	double outputPerCycle(const recipeInfo& _recipe, const std::string& _material);
	//room in its input buffer for a material its recipe takes (0 for anything else)
	double inputRoom(const tileObject& _machine, const materialInfo& _material);
	//puts up to _amount of an input in (as much as there is room for). returns what went in
	double putInput(tileObject& _machine, const materialInfo& _material, double _amount);
	//takes up to _amount of something from its output buffer. returns what came out
	double takeOutput(tileObject& _machine, const std::string& _material, double _amount);
	//the first input it hasn't a full cycle of ("" if it has them all), and the first output with no room for a cycle more
	std::string missingInput(const tileObject& _machine, const recipeInfo& _recipe);
	std::string fullOutput(const tileObject& _machine, const recipeInfo& _recipe);
	//one cycle: its inputs taken from the input buffer, its outputs added to the output buffer
	void runCycle(tileObject& _machine, const recipeInfo& _recipe);
	//picks another recipe: inputs the new one doesn't use move to the output buffer (to go back out), and the cycle in
	//progress starts again
	void choose(tileObject& _machine, int _recipe);
	//"Recipes: Smelt iron (8 s)". "" if it has none
	std::string summary(const tileTypeInfo& _type);
	//"10 kg Iron ore + 2 L Water" (amounts per cycle)
	std::string describe(const std::vector<std::pair<std::string, double>>& _amounts);
}

//solids in a placed container (an item with storage). what it holds is kept on the object (tileObject::contents(), kg of
//each material), so it is saved with it: "crate{@iron_ore=500;@ice=120}". a part of a big container works on the
//container itself. the item tubes and machines will use these to move materials in and out
namespace storage
{
	//"Storage: 1000 L, up to 2000 kg". "" if it isn't storage
	std::string summary(const storageProperties& _properties);
	bool isStore(const tileObject& _object);
	double usedLitres(const tileObject& _store);
	double usedKg(const tileObject& _store);
	//kg of one material in it
	double amountOf(const tileObject& _store, const std::string& _material);
	//kg of a solid that would still fit, by its volume and its mass limit (0 for a liquid, or if it isn't storage)
	double room(const tileObject& _store, const materialInfo& _material);
	//puts up to _kg of a solid in, as much as fits. returns the kg that went in
	double put(tileObject& _store, const materialInfo& _material, double _kg);
	//takes up to _kg of a material out. returns the kg that came out
	double take(tileObject& _store, const std::string& _material, double _kg);
	void empty(tileObject& _store);
}

//a ship's parts and frames, in words: "Engine: 120 kN, burns 13.3 kg/s of Propellant (exhaust 9000 m/s)", "Electric
//engine: 15 kN on its power alone", thrusters, gyroscope, flight console, frame (a line each). "" for none of them
namespace shipParts
{
	std::string summary(const tileTypeInfo& _type);
	//an item's own mass when its content file gives none (kg)
	const double floorKg = 400.0;
	const double wallKg = 150.0;
	const double frameKg = 100.0;
	const double lineKg = 20.0;      //a tile of cable, pipe or tube
	const double fixtureKg = 300.0;
	double defaultKg(const tileTypeInfo& _type);
	//its own mass: its content file's kg, or the default for what it is
	double kg(const tileTypeInfo& _type);
	//a placed docking point: the tile it docks at and the face, as turned. _tile is the tile the object was found on
	//(a machine: any of its tiles). false if it isn't a dock
	bool placedDock(const tileObject& _object, sf::Vector2i _tile, sf::Vector2i& _dockTile, int& _face);
}

//machines: footprints, turns and ports. a machine's turn (0..3 quarter turns clockwise on screen) is its hidden "r"
//setting. its footprint is w x h tiles unturned (x towards the lower right, y towards the lower left); turned a
//quarter it is h x w. the placed machine is kept on its front tile (the lowest on screen, drawn last), with a part
//marker on each of its other tiles
namespace footprint
{
	const char* const rotationKey = "r";
	//items with ports, or bigger than one tile, or that say rotates = true
	bool isMachine(const tileTypeInfo& _type);
	bool isMultiTile(const tileTypeInfo& _type);
	int cellCount(const tileTypeInfo& _type);
	//its size after _turns quarter turns: w x h, or h x w
	sf::Vector2i turnedSize(const tileTypeInfo& _type, int _turns);
	//where an unturned cell ends up after _turns quarter turns (in the turned footprint)
	sf::Vector2i turnCell(sf::Vector2i _cell, sf::Vector2i _size, int _turns);
	//a side after _turns quarter turns clockwise: UL -> UR -> LR -> LL -> UL
	int turnSide(int _side, int _turns);
	//its ports after _turns: cells in the turned footprint, sides turned
	std::vector<portInfo> turnedPorts(const tileTypeInfo& _type, int _turns);
	//the footprint's upper tile when it is placed by pointing at _pointed (the pointed tile is at its middle)
	sf::Vector2i originFor(sf::Vector2i _pointed, sf::Vector2i _turnedSize);
	//the turned cell of its first port carrying _kind (in the order the content file gives them), (-1,-1) if none
	sf::Vector2i mainCell(const tileTypeInfo& _type, int _turns, portInfo::kind _kind);
	//the step to the neighbour across a side: UL (-1,0), UR (0,-1), LL (0,1), LR (1,0)
	sf::Vector2i step(int _side);
	//the sides of a placed machine's tile (the tile _object is on: the machine's own, or a part's) that have its ports of
	//_kind, turned as it is placed. a mask: bit 0 UL, 1 UR, 2 LL, 3 LR. 0 for anything that isn't a machine
	int portSidesOnTile(const tileObject& _object, portInfo::kind _kind);
	//true if this tile of a machine is where its _kind behaviour lives: the tile of its first port of that kind
	//(power: its device; liquid: its tank, volume, exchanger or pump). always true for a one-tile item
	bool isMainTile(const tileObject& _object, portInfo::kind _kind);
}
