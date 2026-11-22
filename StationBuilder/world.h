#pragma once
#include "station.h"
#include "electricalNetwork.h"
#include "fluidNetwork.h"
#include "thermalNetwork.h"
#include "logistics.h"
#include "processing.h"
#include "flight.h"
#include <SFML/Graphics/View.hpp>
#include <memory>
#include <string>
#include <vector>

class simulationManager;
class gameManager;
class textureManager;
struct vessel;

//a docking point on a vessel: where it is, the face a ship docks against, and its item (the docks are found again
//whenever the vessel changes)
struct dockPoint
{
	vessel* owner = nullptr;
	int level = 0;
	sf::Vector2i tile;          //the tile it docks at
	int face = 0;               //the face it docks on: 0 UL, 1 UR, 2 LL, 3 LR
	tileObject* object = nullptr;
	bool same(const dockPoint& _other) const { return owner == _other.owner && level == _other.level && tile == _other.tile; }
};

//the numbers docking works with
namespace dockTuning
{
	const double cargoRate = 20.0;      //kg/s of solids through a dock
	const double liquidRate = 20.0;     //L/s of liquid
	const double undockSpeed = 0.3;     //m/s it is pushed off at
}

//a ship docked to a vessel that keeps still (the station), or on its way to dock there. docked, it lies square to the
//host with its docking point's face against the dock's: its grid is the host's moved by whole tiles (offset) and whole
//decks (deckOffset), so it can be drawn beside the host
struct dockLink
{
	vessel* ship = nullptr;
	vessel* host = nullptr;
	int shipLevel = 0;
	sf::Vector2i shipTile;      //the ship's docking point
	int hostLevel = 0;
	sf::Vector2i hostTile;      //the host's dock
	bool docked = false;        //false: on its way (the autopilot's dock order)
	sf::Vector2i offset;        //a ship tile + offset = the host tile it lies over
	int deckOffset = 0;         //a ship deck + deckOffset = the host deck beside it
	//transfers through the dock, while docked: +1 to the ship, -1 to the host, 0 off
	int cargo = 0;
	int liquid = 0;
	double cargoRate = 0.0;     //as of the last tick: kg/s, + to the ship
	double liquidRate = 0.0;    //L/s, + to the ship
	std::string cargoNote;      //why nothing moves, or what does
	std::string liquidNote;
};

//another vessel drawn beside the one on screen (a docked ship, or the station it is docked to): its tile + offset is the
//viewed vessel's tile beside it, its deck + deckOffset the viewed deck. dockTile is where they meet (viewed grid), for
//the drawing order
struct besideVessel
{
	vessel* other = nullptr;
	sf::Vector2i offset;
	int deckOffset = 0;
	sf::Vector2i dockTile;
};

//something with decks in the local area: the station, or a ship. its decks, its own simulations (wired the way the
//station's always were), where it is and how it moves (flight), and what the screen last showed of it
struct vessel
{
	vessel(textureManager& _textures, const std::string& _name, bool _ship);
	vessel(const vessel&) = delete;
	vessel& operator=(const vessel&) = delete;

	std::string name;
	bool ship = false;
	station layout;              //its decks
	electricalNetwork power;
	fluidNetwork liquid;
	thermalNetwork heat;
	logisticsNetwork items;
	processingSystem machines;
	flightSystem flight;         //a ship's engines and motion; the station just keeps still
	//the screen's view of it when it was last looked at (each vessel keeps its own), and the revision its line art
	//was last drawn for
	sf::View camera;
	bool cameraSet = false;
	std::uint64_t lineArtRevision = 0;
	//its docking points, found again when its revision changes (world::docksOf)
	std::vector<dockPoint> dockCache;
	std::uint64_t dockRevision = ~std::uint64_t(0);
};

//every vessel in the local area: the station first, then the ships. each one's simulations tick with the one clock.
//the screen shows one vessel at a time (viewed): building, the tile panel and the views all work on that one
class world
{
public:
	world(textureManager& _textures, simulationManager& _simulation);
	~world();

	//adds a vessel (and its simulations to the clock). it lives as long as the world, or until clearShips
	vessel& add(const std::string& _name, bool _ship);
	//a new ship with the starter layout (shipLayout::starter), a little way from the others, facing up the map
	vessel& newShip();
	//takes away every ship (before loading)
	void clearShips();

	int count() const { return int(vessels.size()); }
	vessel& at(int _index) { return *vessels[size_t(_index)]; }
	vessel& home() { return *vessels.front(); }
	//every vessel's decks, for reloading the content files into all of them
	std::vector<station*> stations();

	//the vessel on screen. a change asked for during a frame happens at the start of the next (takeViewChange), so
	//nothing that frame is left holding the old one half way
	vessel& viewed() { return *vessels[size_t(viewedIndex)]; }
	int viewedNumber() const { return viewedIndex; }
	void requestView(int _index) { wantedView = _index; }
	//the vessel to switch to, or -1: call at the start of a frame, then setViewed
	int takeViewChange() { const int v = wantedView; wantedView = -1; return v; }
	void setViewed(int _index);
	std::string nextShipName() const;
	int numberOf(const vessel& _vessel) const;

	//---- docking (docking.cpp) ----
	//a vessel's docking points
	const std::vector<dockPoint>& docksOf(vessel& _vessel);
	//the docks a ship could head for: every dock on a vessel that keeps still, with whether it can (and why not)
	struct dockChoice
	{
		dockPoint dock;
		bool usable = false;
		std::string label;      //"Haxel_Port: Docking jetty at 33,24"
		std::string whyNot;     //"taken by Ship 2", "its docking collar faces the wrong way: ..."
	};
	std::vector<dockChoice> dockChoices(vessel& _ship);
	//sends a ship to dock (the autopilot flies it in and it latches); false and why if it can't
	bool startDocking(vessel& _ship, const dockPoint& _dock, std::string* _whyNot = nullptr);
	//docks it at once if it is close enough, square enough and slow enough (flying by hand)
	bool dockNow(vessel& _ship, const dockPoint& _dock, std::string* _whyNot = nullptr);
	//lets it go: it is pushed off and backs out to the stand-off point
	void undock(vessel& _ship);
	//its link (docked or on its way), nullptr if none
	dockLink* linkOf(const vessel& _ship);
	const std::vector<dockLink>& links() const { return dockLinks; }
	//the vessels to draw beside one (docked to it, or it is docked to)
	std::vector<besideVessel> beside(vessel& _vessel);
	//each tick, before the vessels' own systems: docked ships held in place, transfers, ships arriving latched
	void tickDocking(double _dt);
	//(loading) a docked ship put back at its dock without flying in
	bool relink(vessel& _ship, int _shipLevel, sf::Vector2i _shipTile, vessel& _host, int _hostLevel, sf::Vector2i _hostTile, int _cargo, int _liquid);

private:
	//where a ship goes to dock with its docking point _own against _dock: its centre of mass, heading, the way out,
	//its tile offset and deck offset. false if the faces don't meet
	bool berth(vessel& _ship, const dockPoint& _own, const dockPoint& _dock, vec2& _position, double& _heading, vec2& _outwards,
		sf::Vector2i& _offset, int& _deckOffset);
	//the ship's docking point that fits _dock (its face opposite), and why not if none
	bool fittingDock(vessel& _ship, const dockPoint& _dock, dockPoint& _own, std::string* _whyNot);
	//a dock point again by where it is (false if it has gone)
	bool findDock(vessel& _vessel, int _level, sf::Vector2i _tile, dockPoint& _found);
	void moveCargo(dockLink& _link, const dockPoint& _shipDock, const dockPoint& _hostDock, double _dt);
	void moveLiquid(dockLink& _link, const dockPoint& _shipDock, const dockPoint& _hostDock, double _dt);
	void dropLinks(const vessel& _vessel);

	//runs tickDocking in the simulation, before every vessel's systems
	struct dockTicker : public simulationSystem
	{
		world* owner = nullptr;
		void tick(double _dt) override { owner->tickDocking(_dt); }
	} ticker;
	std::vector<dockLink> dockLinks;

	textureManager* textures;
	simulationManager* sim;
	std::vector<std::unique_ptr<vessel>> vessels;
	int viewedIndex = 0;
	int wantedView = -1;
	int shipsMade = 0;
};

namespace shipLayout
{
	const int gridSize = 24;   //a new ship's grid
	//a small working ship on one deck: a walled cabin with a flight console, a generator, a gyroscope, a light and a
	//crate; frames behind it with a propellant tank, a main engine pushing it forwards (towards the grid's upper left)
	//and manoeuvring thrusters, piped and wired; and a docking collar on its nose, facing forwards, tubed to the crate
	//and piped to the fuel. it can fly and dock as it is
	void starter(station& _ship);
}

//saving and loading the whole area: every vessel's decks (the station's in its own folder, as before; ships' in
//ships/<name>/), and <station>.world, a TOML file of the ships, where they are and what they were told to do. an older
//save without a .world file loads as just the station
namespace worldFiles
{
	void save(world& _world, gameManager& _files);
	//false (nothing changed) if the station's decks couldn't be loaded
	bool load(world& _world, gameManager& _files, textureManager& _textures);
}
