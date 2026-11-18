#pragma once
#include <complex>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include "simulation.h"
#include "tileNetwork.h"
#include "nodalSolver.h"
#include "tileHeatSource.h"

class station;
struct tileTypeInfo;

using complexd = std::complex<double>;

//AC electrical simulation. every quantity is an rms phasor (magnitude and phase at the network's frequency),
//solved as a steady state each tick. power S = V conj(I): real part P in watts, imaginary part Q in var.
//every circuit returns through the station ground, so a source or load sits between its node and ground.

//an island's condition after a solve
enum class islandState
{
	dead,               //no generator connected: everything at 0 V
	live,               //solved
	frequencyMismatch,  //connected generators disagree on frequency: faulted, everything at 0 V
	solveFailed         //the matrix couldn't be solved (shouldn't happen with resistance floors): 0 V
};
const char* islandStateName(islandState _state);

//one island as plain numbers, built from the tile network by electricalNetwork (or by hand in tests).
//node indices are local to the circuit: 0 .. nodeConductors.size() - 1
struct acCircuit
{
	struct seriesRL { double resistance = 0.0; double inductance = 0.0; };
	struct source
	{
		int node = 0;
		double emf = 0.0;           //V rms, open circuit
		double frequency = 0.0;     //Hz
		double ratedPower = 0.0;    //VA
		double resistance = 0.0;    //internal impedance R + jwL
		double inductance = 0.0;
		double emfScale = 1.0;      //EMF actually produced, as a fraction of emf (sags when overloaded)
		bool connected = true;      //false = tripped/off: no EMF and no internal impedance to ground
	};
	struct load
	{
		int node = 0;
		double resistance = 0.0;    //R + jwL + 1/(jwC) to ground
		double inductance = 0.0;
		double capacitance = 0.0;   //0 = none
		bool connected = true;      //false = switched off (by hand or by its thermostat): draws nothing
	};
	struct branch { int a = 0; int b = 0; bool vertical = false; };

	//per node: the conductors (cables, risers) on that tile, acting in parallel. its size is the node count
	std::vector<std::vector<seriesRL>> nodeConductors;
	//per node: just its risers (vertical conductors). may be shorter than nodeConductors (missing = none)
	std::vector<std::vector<seriesRL>> nodeRisers;
	//nodes joined through their conductors: half of each end's impedance, in series. a neighbour on the same
	//level uses all of each tile's conductors; a vertical branch (the same tile one level up) only the risers
	std::vector<branch> branches;
	std::vector<source> sources;
	std::vector<load> loads;
};

//the result of solving an acCircuit (all vectors sized to match it, zeros unless live)
struct acSolution
{
	islandState state = islandState::dead;
	double frequency = 0.0;              //Hz, 0 unless live
	std::vector<complexd> nodeVoltage;
	std::vector<complexd> branchCurrent; //from a to b
	std::vector<double> branchLoss;      //W turned into heat in the cables
	std::vector<complexd> sourceCurrent; //out of each source into its node
	std::vector<complexd> sourcePower;   //S each source delivers to the network
	std::vector<complexd> loadCurrent;   //into each load
	std::vector<complexd> loadPower;     //S each load takes
	double generatedWatts = 0.0;         //sum of source P
	double consumedWatts = 0.0;          //sum of load P
	double lossWatts = 0.0;              //sum of cable losses (generated = consumed + losses)
};

//impedance helpers. _w is the angular frequency, 2 pi f (rad/s)
namespace ac
{
	const double minimumResistance = 1e-6;   //ohms: floors that keep every matrix solvable
	complexd seriesImpedance(double _resistance, double _inductance, double _w);
	complexd loadImpedance(double _resistance, double _inductance, double _capacitance, double _w);
	//conductors on one tile in parallel
	complexd tileImpedance(const std::vector<acCircuit::seriesRL>& _conductors, double _w);
}

//solves one island, keeping the factorised matrix between solves. the matrix depends only on the frequency
//and on which generators are connected, so changing a generator's emfScale only redoes the cheap part
class acSolver
{
public:
	void setCircuit(const acCircuit& _circuit);
	//change sources' emfScale or connected here between solves
	acCircuit& circuit() { return circ; }
	const acCircuit& circuit() const { return circ; }
	const acSolution& solve();
	const acSolution& solution() const { return sol; }
	//how many times the matrix has been factorised (for tests: should only grow when the matrix changes)
	int factorisations() const { return factorCount; }

private:
	bool factorise(double _w);
	acCircuit circ;
	acSolution sol;
	nodalSolver<complexd> nodal;
	bool haveFactors = false;
	double factorFrequency = 0.0;
	std::vector<bool> factorConnected;
	std::vector<bool> factorLoadConnected;
	std::vector<complexd> branchZ, sourceZ, loadZ;   //impedances at the factorised frequency
	int factorCount = 0;
};

//generator protection. a generator asked for more than its rating lowers its EMF until it supplies just
//its rating (everything on its network dims). if the overload lasts, it trips and stays off until reset
namespace generatorProtection
{
	const double sagTimeConstant = 0.3;   //s: how quickly the EMF follows the load (settles in about 1 s)
	const double overloadMargin = 1.05;   //asking for more than 105% of the rating counts as an overload
	const double tripSeconds = 3.0;       //s of overload before it trips. the count drains again while not overloaded
}

//how temperature (from the thermal simulation) acts on electrical things
namespace thermalEffects
{
	const double deratingStart = 40.0;        //C: a generator gives its full rating up to this tile temperature
	const double deratingPerDegree = 0.014;   //fraction of its rating lost for each C hotter than that
	const double deratingFloor = 0.3;         //it never drops below 30% of its rating
	const double thermostatBand = 0.5;        //C: a heater turns on below its setting minus this, off above it plus this
	const double cableRiseAtRating = 45.0;    //C a cable runs above its tile at its rated current (goes with current squared)
	const double cableTimeConstant = 20.0;    //s: how quickly a cable's temperature follows its current
	const double cableHot = 70.0;             //C: past this a cable's insulation is getting hot
	const double cableOverheating = 90.0;     //C: past this it is beyond its insulation's rating
	//the fraction of its rating a generator can give with its tile at _celsius (1 when cool; NaN counts as cool)
	double deratingFactor(double _celsius);
}

//one generator's protection state. kept across network rebuilds (not saved to disc yet)
struct generatorState
{
	double emfScale = 1.0;          //EMF it is producing, as a fraction of its rated EMF
	double demand = 0.0;            //what its network would take from it at full EMF, as a fraction of its rating
	double overloadSeconds = 0.0;   //fills while overloaded, drains while not; it trips when this reaches tripSeconds
	bool tripped = false;
	double effectiveRating = 0.0;   //VA it can give right now: its rating, less when its tile is hot. 0 until first ticked
	double tileTemperature = 20.0;  //C, its tile's temperature as of the last tick (NaN: no floor, counts as cool)
};

//a heater's thermostat: whether it is calling for heat. kept across network rebuilds
struct thermostatState
{
	bool heating = true;
};

//which generator: the tile it is on and which of that tile's generators it is
struct generatorKey
{
	int level = 0;
	int tileIndex = 0;
	int ordinal = 0;
	bool operator<(const generatorKey& _other) const
	{
		if (level != _other.level) return level < _other.level;
		if (tileIndex != _other.tileIndex) return tileIndex < _other.tileIndex;
		return ordinal < _other.ordinal;
	}
};

//energy since the game started, in joules of game time (1 kWh = 3.6 MJ)
struct energyTotals
{
	double generatedJoules = 0.0;   //delivered by generators to their networks
	double consumedJoules = 0.0;    //taken by loads
	double lostJoules = 0.0;        //turned into heat in cables
};

//a generator or load somewhere on the network, with its latest result
struct electricalDevice
{
	int node = -1;                      //network node it sits on
	int island = -1;
	const tileTypeInfo* type = nullptr;
	bool isSource = false;
	int local = 0;                      //its index in the island circuit's sources or loads
	int objectOnNode = 0;               //its index in its node's objects (the tile's electrical objects, in draw order)
	generatorKey key;                   //which device (tile and which of that tile's generators, or of its loads)
	bool switchedOn = true;             //its "on" setting where it has one. off = disconnected on purpose
	double thermostat = std::numeric_limits<double>::quiet_NaN();   //loads with a thermostat: its setting in C, else NaN
	int turns = 0;                      //a machine's quarter turns (where its tiles are, for a big one's heat)
	complexd current;                   //source: out of it. load: into it
	complexd power;                     //source: delivered. load: taken
};

class thermalNetwork;

//the station's electrical network as a simulation system: rebuilds the circuits whenever the wiring
//changes, and each tick solves every island, runs generator protection, and adds up the energy
class electricalNetwork : public simulationSystem, public tileHeatSource
{
public:
	explicit electricalNetwork(station& _station);
	//where tile temperatures come from: heaters' thermostats, hot generators losing capacity, and cable temperatures
	//use it. nullptr (the default) = everything at 20 C
	void setTemperatureSource(const thermalNetwork* _heat) { temperatures = _heat; }

	void tick(double _dt) override;
	//rebuild and solve now if the station has changed (for use while paused, e.g. by the debug view)
	void update();

	const tileNetwork& network() const { return net; }
	complexd nodeVoltage(int _node) const;
	complexd edgeCurrent(int _edge) const;
	const acSolution& islandSolution(int _island) const { return solvers[_island]->solution(); }
	const acCircuit& islandCircuit(int _island) const { return solvers[_island]->circuit(); }
	const std::vector<electricalDevice>& devices() const { return deviceList; }
	//devices on one node, as indices into devices()
	std::vector<int> devicesOnNode(int _node) const;

	//a source device's protection state, nullptr for a load
	const generatorState* generatorStatus(int _device) const;
	//clears a tripped generator's trip. it comes back on line if it is switched on. false if it wasn't tripped
	bool resetGenerator(int _device);
	//resets every tripped generator on a tile. returns how many were reset
	int resetGenerators(int _level, sf::Vector2i _tile);
	//the device for the _objectOnNode-th electrical object on a tile, or -1
	int deviceAt(int _level, sf::Vector2i _tile, int _objectOnNode) const;

	//a load device's thermostat: is it calling for heat? true for loads without one
	bool thermostatHeating(int _device) const;
	//a machine that makes things switches its power off while it has nothing to do (waiting for inputs, its output
	//full...): an idle load is disconnected from the next tick on. kept by the load's key, so it survives rebuilds
	void setLoadIdle(const generatorKey& _load, bool _idle);
	bool loadIdle(int _device) const;
	//the temperature of the cables on a network node's tile, C. NaN if the tile has no cables
	double cableTemperature(int _node) const;
	//the tile temperature the electrical side is using for a node (20 C with no temperature source or no floor)
	double tileTemperature(int _node) const;

	const energyTotals& energy() const { return totals; }
	//per network node: watts of heat made on that tile right now: its loads, its generators' waste heat and
	//internal losses, and half of each cable link touching it. the thermal simulation adds this to the tiles
	const std::vector<double>& nodeHeatWatts() const { return nodeHeat; }
	//the same heat, tile by tile, for the thermal simulation
	void forEachTileHeat(const std::function<void(int, sf::Vector2i, double)>& _add) const override;

private:
	void rebuild();
	void solveAll();
	//sag, overload count and trip for every generator, against its rating at its tile's temperature.
	//true if any EMF or connection changed
	bool protectGenerators(double _dt);
	//heaters' thermostats switch them on and off. true if any switched
	bool applyThermostats();
	void updateCableTemperatures(double _dt);
	void computeHeat();

	station* stationRef;
	const thermalNetwork* temperatures = nullptr;
	std::map<generatorKey, thermostatState> thermostatStates;   //keyed by tile and which of its loads
	std::set<generatorKey> idleLoads;                           //machines' loads switched off while they have nothing to do
	std::vector<double> cableTemps;                             //per network node, NaN without cables
	std::vector<std::pair<int, int>> cableTileKeys;             //per network node: level and tile index, to carry them over
	tileNetwork net;
	std::vector<std::unique_ptr<acSolver>> solvers;   //one per island
	std::vector<int> nodeLocal;                       //network node -> its index in its island's circuit
	std::vector<int> edgeLocal;                       //network edge -> its branch index in its island's circuit
	std::vector<electricalDevice> deviceList;
	std::map<generatorKey, generatorState> generatorStates;
	energyTotals totals;
	std::vector<double> nodeHeat;
	//big machines' heat, to share over every tile they cover (it is in nodeHeat too, on the power port's tile)
	struct machineHeat { int node; sf::Vector2i origin; sf::Vector2i size; double watts; };
	std::vector<machineHeat> spreadHeat;
};
