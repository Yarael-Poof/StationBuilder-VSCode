#pragma once
#include "simulation.h"
#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class station;
class electricalNetwork;
class fluidNetwork;
class tileObject;
struct materialInfo;

using vec2 = sf::Vector2<double>;

//the numbers flight works with
namespace shipTuning
{
	const double tileMetres = 2.0;          //a tile's side
	//(an item's own mass, and its default: shipParts::kg in tileCatalogue.h)
	const double defaultMaxSpeed = 40.0;    //m/s, the autopilot's speed limit until it's changed
	const double arriveRadius = 3.0;        //m: close enough to a target
	const double arriveSpeed = 0.3;         //m/s: slow enough there
	const double velocityTime = 1.5;        //s the autopilot takes to correct its speed
	const double turnMargin = 0.17;         //rad (10 degrees): the main engines fire only when pointing this close
	const double brakeShare = 0.6;          //of the main engines' acceleration the autopilot plans to brake with
	const double trailSeconds = 1.0;        //game seconds between trail points on the nav map
	const int trailPoints = 240;
	//docking
	const double dockStandoff = 25.0;       //m out from the dock along its face: where it lines up
	const double dockSpeed = 0.6;           //m/s at most closing in
	const double latchDistance = 0.3;       //m from its berth, to latch
	const double latchSpeed = 0.15;         //m/s
	const double latchAngle = 0.0175;       //rad (1 degree) off the dock's heading
	const double captureDistance = 2.0;     //by hand ("Dock now"): looser
	const double captureSpeed = 0.5;
	const double captureAngle = 0.052;      //3 degrees
}

//what the autopilot is doing
enum class flightOrder
{
	none,       //nothing: it drifts
	goTo,       //to a point, and stop there
	stop,       //take its speed off where it is
	hold,       //stay at a point (where it was when told)
	dock,       //to a dock: to its stand-off point, line up square to it, close in on the thrusters (the world latches it)
};

//what the controls ask for in manual (each tick, from the keyboard): 0..1 or -1..1
struct manualControls
{
	double thrust = 0.0;    //main engines, forwards
	double back = 0.0;      //thrusters, backwards
	double turn = 0.0;      //-1 anticlockwise .. 1 clockwise (as seen on the nav map)
	double slide = 0.0;     //thrusters sideways: -1 left .. 1 right of forwards
};

//one cell of a vessel's outline, seen from above (for the nav map): where it is (from the centre of mass, unrotated)
struct outlineCell
{
	vec2 local;
	enum class kind { floor, frame, equipment } what = kind::floor;
};

//what the flight panel shows
struct flightStatus
{
	double mass = 0.0;            //kg, with what it holds
	double dryMass = 0.0;         //kg, without its fuel
	double fuelKg = 0.0;          //fuel its engines and thrusters can reach
	double mainThrust = 0.0;      //N, all main engines (the way they push most)
	double thrusterThrust = 0.0;  //N, manoeuvring thrusters, each way
	double gyroTorque = 0.0;      //N m
	double inertia = 0.0;         //kg m2
	double exhaustVelocity = 0.0; //m/s, the fuelled main engines' (thrust weighted)
	double deltaV = 0.0;          //m/s the fuel aboard is good for with the fuelled main engines (infinite: all electric)
	double electricThrust = 0.0;  //N of the main thrust that needs no fuel (electric engines)
	double engineWatts = 0.0;     //W the main engines draw at full thrust
	bool needsFuel = false;       //some engine or thruster burns fuel
	int engines = 0, thrusters = 0, gyroscopes = 0, helms = 0;
	bool helmPowered = false;     //a flight console has power: it takes orders
	double throttle = 0.0;        //main engines now, 0..1
	double thrusterUse = 0.0;     //thrusters now, 0..1
	double turning = 0.0;         //gyroscopes now, -1..1
	double engineTorque = 0.0;    //N m the engines' push turns it by (off its centre of mass) at full thrust
	bool arrived = false;
	std::string state;            //"burning", "turning to burn", "braking", "holding", "no flight console" ...
};

//a vessel's motion in the local area, and its flight. the area is flat, x right and y down as the nav map draws it.
//a vessel's own frame is its grid seen from above (x along the grid's x, y along its y, in metres from its centre of
//mass); its heading turns that frame into the area's. the station keeps still (it can't move); a ship has engines.
//each tick (after the machines, so it uses this tick's power and liquids): the orders (autopilot) or the controls
//(manual) set the engines, thrusters and gyroscopes, fuel comes from their liquid ports, power from the electrical
//network (engines and gyroscopes only draw it while working), and the forces and turning move the ship
class flightSystem : public simulationSystem
{
public:
	flightSystem(station& _station, electricalNetwork* _power, fluidNetwork* _liquid, bool _canMove);
	void tick(double _dt) override;
	//finds its parts again if the station changed (each tick does this itself)
	void update();

	//where it is and how it moves
	vec2 position;
	vec2 velocity;
	double heading = 0.0;   //rad, clockwise on the nav map
	double spin = 0.0;      //rad/s
	bool canMove() const { return movable; }

	//orders: the autopilot takes them; manual steers by the controls instead
	bool manual = false;
	manualControls controls;
	flightOrder order = flightOrder::none;
	vec2 target;            //goTo, hold
	double maxSpeed = shipTuning::defaultMaxSpeed;
	void goTo(vec2 _target);
	void stopHere();        //stop: take the speed off
	void holdHere();        //hold where it is now
	void setManual(bool _manual);

	//docking. the world says where the dock wants it (its berth: where its centre of mass goes, the heading, and the way
	//out from the dock) and latches it once it is there; while docked it doesn't fly (the world holds it in place)
	bool docked = false;
	vec2 berth;
	double berthHeading = 0.0;
	vec2 berthOutwards;
	int dockPhase = 0;      //0 on its way to the stand-off point, 1 lining up there, 2 closing in
	void dockAt(vec2 _berth, double _heading, vec2 _outwards);
	//closing in and close enough to latch
	bool readyToLatch() const;
	//a tile's middle in its own frame (m from its centre of mass)
	vec2 localOfTile(sf::Vector2i _tile) const;

	const flightStatus& status() const { return stat; }
	const std::vector<outlineCell>& outline() const { return cells; }
	//its size seen from above: the furthest cell from its centre of mass (m)
	double radius() const { return extent; }
	//a point of its own frame in the area, and a direction
	vec2 toArea(vec2 _local) const;
	vec2 directionToArea(vec2 _local) const;
	//where its centre of mass is on its grid (tile coordinates, 1-based like the grid's)
	sf::Vector2<double> centreOnGrid() const;
	//the way its main engines push, in its own frame (unit), (0,0) with none
	vec2 forwards() const { return mainDirection; }
	const std::deque<vec2>& trail() const { return trailPoints; }
	void clearTrail() { trailPoints.clear(); }

private:
	struct part
	{
		tileObject* object = nullptr;
		int level = 0;
		vec2 local;                 //its middle, in metres from the grid's corner (then from the centre of mass)
		vec2 push;                  //engines: the way it pushes, in the ship's frame (unit)
		int fuelNode = -1;          //its liquid port's node in the liquid network
		const materialInfo* fuel = nullptr;
		double newtons = 0.0;
		double exhaustVelocity = 0.0;
		double torque = 0.0;        //gyroscopes
		bool electric = false;      //an engine or thrusters with no fuel: power alone
		double watts = 0.0;         //its electrical load
		int device = -1;            //its load in the electrical network, -1 without one (found again each tick)
		sf::Vector2i tile;          //the tile it was found on
		sf::Vector2i fuelTile{ -1, -1 };   //its liquid port's tile
	};
	void rebuild();
	//its load in the electrical network: _tile is the tile it is on (a machine: any of its tiles)
	int findDevice(const tileObject& _object, int _level, sf::Vector2i _tile) const;
	double powerShare(const part& _part) const;   //0..1 of its load it gets
	void setIdle(const part& _part, bool _idle);
	//each part's load in the electrical network and its fuel port's node in the liquid network, which both number
	//them afresh whenever they rebuild (and they rebuild after this does, the tick a station changes)
	void findLinks();
	void weigh();                   //mass and fuel, as they change each tick
	//the autopilot: the acceleration it wants (area frame), the heading it wants, whether it is only turning yet, and
	//whether it may use the main engines (not when lining up or closing in on a dock)
	void autopilot(vec2& _wantedAcceleration, double& _wantedHeading, bool& _turnOnly, bool& _mainAllowed);
	//the torque that turns it to _heading: as fast as it can stop turning in time, gently near it
	double steerTo(double _heading) const;
	void fly(double _dt);

	station* stationRef;
	electricalNetwork* power;
	fluidNetwork* liquids;
	bool movable;
	bool built = false;
	std::uint64_t seenRevision = 0;

	std::vector<part> engines, thrusterParts, gyros, helms;
	std::vector<tileObject*> holders;   //everything that holds materials (its mass changes)
	std::vector<outlineCell> cells;
	double structureKg = 0.0;           //its own mass, without what it holds
	vec2 gridCentre;                    //centre of mass from the grid's corner (m)
	double extent = 0.0;
	vec2 mainDirection;
	double inertia = 1.0;
	flightStatus stat;
	double sinceTrail = 0.0;
	std::deque<vec2> trailPoints;
	double heldHeading = 0.0;           //the heading the autopilot keeps when it has nothing to turn for
	bool headingHeld = false;
};
