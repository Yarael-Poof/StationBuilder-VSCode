#pragma once
#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <vector>
#include "simulation.h"
#include "tileNetwork.h"
#include "nodalSolver.h"
#include "tileHeatSource.h"
#include "electricalNetwork.h"
#include "tileCatalogue.h"

class station;
class thermalNetwork;
class tileObject;
struct materialInfo;

//the game-tuned numbers of the liquid (coolant) simulation. what each item does (pipe conductance, pump
//curve, tank size, exchanger strength) and each liquid's heat capacity are in the content files
namespace fluidTuning
{
	const double heatPerLitre = 4180.0;     //J/K per litre for a network with no liquid known (water's)
	const double tankFullPressure = 30.0;   //kPa a full tank pushes out with (half full: half that)
	const double referenceLeak = 1e-9;      //(L/s)/kPa from every tile to the reference, so the matrix always solves
	const double startTemperature = 20.0;   //C: the liquid in a newly placed tank
	const double portConductance = 1.0;     //(L/s)/kPa through a machine's liquid port when it has no fluid behaviour of its own
}

//a liquid network's condition
enum class fluidState
{
	noLiquid,    //no tank, or its tanks are empty
	notEnough,   //its tanks don't hold enough to fill its pipes, pumps and exchangers: nothing flows
	filled,      //full: pumps can move liquid round it
	mixed        //its tanks hold different liquids: nothing flows until they are separated or one is drained
};
const char* fluidStateName(fluidState _state);

struct fluidTank
{
	int node = -1;
	generatorKey key;              //its tile and which of that tile's tanks it is
	tileObject* object = nullptr;  //the placed tank: its liquid and contents are kept on it (and saved with it)
	const materialInfo* liquid = nullptr;   //what it holds, nullptr when empty
	double capacity = 0.0;         //L
	double contents = 0.0;         //L in it, counting what it lends to fill the network's pipes
	double free = 0.0;             //L left once its share of the pipes is filled: what sets its pressure
	double outflow = 0.0;          //L/s out of it into the network (negative: filling up)
	bool blocked = false;          //empty and asked to give, or full and asked to take: shut for this tick
};

struct fluidPump
{
	int node = -1;
	int objectOnNode = 0;          //which of its tile's fluid objects it is
	int pumpOnTile = 0;            //which of its power tile's pumps it is: how its electrical device is found for its power
	int powerLevel = 0;            //the tile its electricity comes in at: its own, or a big pump's power port tile
	sf::Vector2i powerTile;
	int inNode = -1, outNode = -1; //a pump with ports: the nodes of its in and out ports (the same node for a one-tile pump)
	int side = -1;                 //the side it pushes towards (0 UL, 1 UR, 2 LL, 3 LR)
	int edge = -1;                 //the link it pushes along, -1 if nothing on that side carries liquid
	double sign = 1.0;             //+1 if it pushes from the edge's a to b, -1 the other way
	double ratedHead = 0.0;        //kPa
	double ratedFlow = 0.0;        //L/s
	bool switchedOn = true;
	double speed = 1.0;            //its Speed setting, 0..1
	double powerFraction = 0.0;    //how much of its rated electrical power it is getting, 0..1
	double head = 0.0;             //kPa it is actually pushing with: rated x speed x power, 0 when off
	double flow = 0.0;             //L/s through it in the direction it pushes
};

struct fluidExchanger
{
	int node = -1;
	double exchange = 0.0;         //W/K between its tile's air and the liquid
	double heatToAir = 0.0;        //W from the liquid into the air right now (negative: the air is warming the liquid)
};

//a radiator: sheds its liquid's heat to space as e sigma A (T^4 - Ts^4) (fluid::radiatedWatts), so a hot one sheds far
//more than a cool one. it is solved with the rest of the liquid's heat, its T^4 taken as a straight line through this
//tick's temperature (re-drawn every tick)
struct fluidRadiator
{
	int node = -1;
	fluidProperties properties;    //its area and emissivity
	double heatToSpace = 0.0;      //W it is shedding right now
};

//the liquid simulation as a simulation system. pipes, pumps, tanks and heat exchangers form networks over the
//tiles; a network holds the liquid its tanks hold (one liquid per network: tanks of different liquids joined up
//make it "mixed", and nothing flows), and is full when its tanks hold enough to fill everything else. the liquid's
//heat capacity comes from its material. each tick it solves pressures and
//flows (pumps push along their link, tanks push by how full they are), moves liquid between tanks, then solves
//the liquid's temperatures: heat is carried along with the flow, and heat exchangers trade it with their tile's
//air. that heat reaches the thermal simulation through tileHeatSource
class fluidNetwork : public simulationSystem, public tileHeatSource
{
public:
	//_power gives the pumps their electricity (nullptr: pumps always run at full power)
	fluidNetwork(station& _station, const electricalNetwork* _power);
	//where the air temperatures for the heat exchangers come from (nullptr: 20 C everywhere)
	void setTemperatureSource(const thermalNetwork* _heat) { airTemperatures = _heat; }

	void tick(double _dt) override;
	//catch up with station changes now, without any time passing
	void update();

	const tileNetwork& network() const { return net; }
	fluidState islandState(int _island) const;
	//the liquid a network holds (its tanks'), nullptr when it has none or is mixed
	const materialInfo* islandLiquidType(int _island) const;
	//every liquid in a network's tanks: one normally, two or more when mixed
	std::vector<const materialInfo*> islandLiquidTypes(int _island) const;
	//J/K a litre of a network's liquid takes per degree
	double islandHeatPerLitre(int _island) const;
	double islandLiquid(int _island) const;   //L in its tanks
	double islandNeeds(int _island) const;    //L its pipes, pumps and exchangers hold
	double pressure(int _node) const;         //kPa
	double liquidTemperature(int _node) const;
	double flow(int _edge) const;             //L/s from the edge's a to its b
	//L of liquid on a tile: what its pipes, pumps and exchangers hold (when full) plus its tanks' contents
	double liquidOnNode(int _node) const;
	const std::vector<fluidTank>& tanks() const { return tankList; }
	const std::vector<fluidPump>& pumps() const { return pumpList; }
	const std::vector<fluidExchanger>& exchangers() const { return exchangerList; }
	const std::vector<fluidRadiator>& radiators() const { return radiatorList; }
	//W its radiators are shedding to space, all together
	double totalRadiated() const;
	//W from the liquid into the air on a tile, all its exchangers together (negative: the air warms the liquid)
	double heatToAir(int _node) const;
	double totalLiquid() const;               //L in every tank

	//the exchangers' heat, for the thermal simulation
	void forEachTileHeat(const std::function<void(int, sf::Vector2i, double)>& _add) const override;
	//set a tile's liquid temperature, or a tank's contents (tests and debugging). an empty tank given some takes the
	//liquid it is placed with
	void setLiquidTemperature(int _node, double _celsius);
	void setTankContents(int _tank, double _litres);

	//a machine's liquid port on _node: takes up to _litres of _liquid from its network's tanks. only a full network of
	//that liquid gives any, and only what its tanks hold beyond filling the pipes. returns the litres taken
	double takeLiquid(int _node, const materialInfo& _liquid, double _litres);
	//puts up to _litres of _liquid into its network's tanks, as far as they have room: a network of that liquid, or one
	//with no liquid yet (never a mixed one). returns the litres put in
	double putLiquid(int _node, const materialInfo& _liquid, double _litres);

private:
	void refresh();
	void rebuild();
	//finds each tank's placed object again and reads its liquid and contents (after any station change: an object
	//may have been replaced, or drained from its panel, without the network itself changing)
	void bindTanks();
	//works out each network's liquid from its tanks, and whether it is mixed
	void findLiquids();
	//writes each tank's liquid and contents back onto its object
	void storeTanks();
	void drivePumps();
	void solveFlow(double _dt);
	void solveTemperatures(double _dt);
	double airTemperature(int _node) const;   //NaN when the tile has no air

	station* stationRef;
	const electricalNetwork* power;
	const thermalNetwork* airTemperatures = nullptr;
	tileNetwork net;
	std::uint64_t seenRevision = 0;
	bool built = false;

	std::vector<double> tileConductance;   //per node, (L/s)/kPa: all its fluid items in parallel
	std::vector<double> nodeVolume;        //per node, L its non-tank items hold
	std::vector<double> nodeExchange;      //per node, W/K of its exchangers
	std::vector<double> edgeConductance;   //per edge, (L/s)/kPa including any pumps' own resistance
	std::vector<double> edgeHead;          //per edge, kPa pushing from a to b this tick
	std::vector<fluidState> states;        //per island
	std::vector<double> needs, liquid;     //per island, L
	std::vector<std::vector<const materialInfo*>> liquidTypes;   //per island: the liquids in its tanks
	std::vector<double> pressures, temps, flows;
	std::vector<fluidTank> tankList;
	std::vector<fluidPump> pumpList;
	std::vector<fluidExchanger> exchangerList;
	std::vector<fluidRadiator> radiatorList;

	nodalSolver<double> flowSolver;
	bool flowFactorised = false;
	std::vector<bool> flowFactorKey;       //which tanks were open (and which islands full) when it was factorised
	nodalSolver<double> heatSolver;
	std::vector<double> heatFactorKey;     //the flows and capacities it was factorised with
	bool heatFactorised = false;
	double heatFactorDt = 0.0;
};
