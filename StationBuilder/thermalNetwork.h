#pragma once
#include <SFML/System/Vector2.hpp>
#include <array>
#include <cstdint>
#include <vector>
#include "simulation.h"
#include "tileNetwork.h"
#include "nodalSolver.h"

#include "tileHeatSource.h"

class station;

//the game-tuned numbers of the heat simulation. what items add (a floor's heat capacity, a wall's conductance,
//a generator's mass) is in the catalogue, tileCatalogue.cpp; these are the ones that belong to the gaps between
//things. tuned so a 2 kW heater warms a walled 3x3 room by roughly 1.3 C a minute at 1x, an unheated walled room
//takes hours to go cold, and a tile open to space freezes in a few minutes
namespace thermalTuning
{
	const double startTemperature = 20.0;       //C: new floors (fresh air) start here
	//C: what heat leaking out of the station goes to: the hull's outside, averaged over sun and shadow. deep space
	//(-270 C) would make even walled rooms lose most of a heater's output, so walls wouldn't seem to matter
	const double outsideTemperature = -60.0;
	//W/K between neighbouring floored tiles with no wall between them: the air mixes, so a room evens out in a
	//minute or two (a tile's 5 kJ/K over 200 W/K is about 25 s)
	const double openEdge = 200.0;
	const double openToSpace = 20.0;         //W/K from a floored tile out through an edge with no wall
	const double deck = 4.0;                 //W/K through the floor between a tile and the one directly above
	                                         //(an air duct riser on both tiles replaces it with the duct's own, see thermal::duct)
}

//the four sides of a tile, numbered like the wall slots: 0 UL, 1 UR, 2 LL, 3 LR. the neighbour across each
//side is at tile x-1, y-1, y+1 and x+1 respectively
namespace tileSides
{
	sf::Vector2i neighbourStep(int _side);
	int opposite(int _side);
	const char* name(int _side);   //"upper left", "upper right", "lower left", "lower right"
}

//one side of a floored tile
struct thermalSide
{
	int neighbour = -1;          //the floored tile's node across this side, or -1: space
	int edge = -1;               //the network edge to it, or -1
	int walls = 0;               //walls standing on this side (this tile's own, and the neighbour's facing one)
	double conductance = 0.0;    //W/K across it, to the neighbour or out to space
};

//the heat simulation as a simulation system. every floored tile holds heat and has a temperature; heat moves to
//neighbouring tiles and to the tiles directly above and below, leaks out to space through the tile's outside
//edges (quickly if open, slowly through a wall), and comes in from the electrical network's heat on each tile.
//each tick solves (C/dt + G) T = C/dt T_before + Q + G_space T_space: an implicit step, stable at any tick
//length. temperatures carry over when the station is rebuilt; new floors start at startTemperature
class thermalNetwork : public simulationSystem
{
public:
	//_heatSource may be nullptr; otherwise its heat per tile warms the tiles (more can be added with addHeatSource).
	//add this system to the simulation after its heat sources, so it uses the same tick's heat
	thermalNetwork(station& _station, const tileHeatSource* _heatSource);
	void addHeatSource(const tileHeatSource* _heatSource);

	void tick(double _dt) override;
	//catch up with station changes now, without any time passing
	void update();

	const tileNetwork& network() const { return net; }
	double temperature(int _node) const;
	//the temperature of a tile, NaN if it has no floor
	double temperatureAt(int _level, sf::Vector2i _tile) const;
	void setTemperature(int _node, double _celsius);
	double heatCapacity(int _node) const;          //J/K
	double heatInput(int _node) const;             //W in from the electrical network, as of the last tick
	double edgeConductance(int _edge) const;       //W/K
	double heatFlow(int _edge) const;              //W from the edge's a to its b right now
	//W/K of the air duct riser on a node's tile, 0 if it has none
	double ductConductance(int _node) const;
	//true for a link between levels that runs through a pair of air duct risers (not just the deck)
	bool ductLink(int _edge) const;
	const thermalSide& side(int _node, int _side) const;
	double spaceConductance(int _node) const;      //W/K out to space, all sides together
	//W the tile is gaining (positive) or losing right now: its heat input less what flows out
	double netHeatFlow(int _node) const;

	//station totals
	double totalHeatInput() const;
	double totalLossToSpace() const;
	double minimumTemperature() const;
	double maximumTemperature() const;
	double averageTemperature() const;
	//how many times the matrix has been factorised (tests: should only grow when the layout changes)
	int factorisations() const { return factorCount; }

private:
	void refresh();
	void gatherHeat();

	station* stationRef;
	std::vector<const tileHeatSource*> heatSources;
	tileNetwork net;
	std::uint64_t seenRevision = 0;
	bool built = false;

	std::vector<double> temps;       //per node, C
	std::vector<double> capacity;    //per node, J/K
	std::vector<double> heatIn;      //per node, W
	std::vector<double> spaceG;      //per node, W/K to space
	std::vector<double> edgeG;       //per edge, W/K
	std::vector<double> nodeDuct;    //per node, W/K of its air duct riser (0 = none)
	std::vector<std::array<thermalSide, 4>> sides;

	nodalSolver<double> solver;
	bool factorised = false;
	double factorDt = 0.0;
	int factorCount = 0;
};
