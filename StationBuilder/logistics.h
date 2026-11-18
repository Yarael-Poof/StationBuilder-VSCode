#pragma once
#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include "simulation.h"
#include "tileNetwork.h"
#include "tileCatalogue.h"

class station;
class tileObject;

namespace logisticsTuning
{
	const double directRate = 1.0;   //kg/s between item ports that touch with no tube between them
}

//a place on an items network that solids go in or out of: a container's item port, or a one-tile container with no
//ports (it takes and gives both ways, on any side), or a machine's item port (it takes what its recipe uses into its
//input buffer, and gives what is in its output buffer)
struct logisticsEndpoint
{
	int node = -1;
	tileObject* store = nullptr;                         //the container or machine (a big one's machine object)
	bool processor = false;                              //a machine that makes things, rather than a container
	portInfo::direction flow = portInfo::direction::both;
	sf::Vector2i tile;                                   //the tile its port is on
	int side = -1;                                       //its port's side, -1 for a container joined on every side
	double rate = 0.0;                                   //kg/s through it this tick: + in, - out
	bool blocked = false;                                //an output holding something no receiver on its network will take
};

//one items network's state this tick
struct logisticsIsland
{
	double capacity = 0.0;                               //kg/s it can move in all: its slowest tube's rate
	double moving = 0.0;                                 //kg/s it moved this tick
	std::vector<std::pair<const materialInfo*, double>> materials;   //kg/s of each
	int blocked = 0;                                     //outputs holding something no receiver on the network will take (an
	                                                     //output with no receivers at all is just not connected, not blocked)
	std::string blockedWhat;                             //what one of them holds ("Iron ore")
};

//the item tubes as a simulation system. tubes join containers' and machines' item ports into networks; each tick a
//network moves solids at up to its slowest tube's rate (kg/s, in total), in this order:
//  1. from "out" ports to "in" ports
//  2. what is left over, from "out" ports to "both"-way containers (one-tile containers with no ports: buffers)
//  3. from buffers to "in" ports that still have room
//a machine's "both" port counts as an in and an out: it takes its inputs from buffers and gives its products to them
//buffers never pass things between themselves, and a container never sends to itself. a receiver takes only what it
//has room for, only solids, and only its filter's material if it has one. where several could send or take, the one
//that goes first changes each tick, so it evens out. the materials are kept on the containers (storage:: in
//tileCatalogue.h), so what moves is saved with them
class logisticsNetwork : public simulationSystem
{
public:
	explicit logisticsNetwork(station& _station);

	void tick(double _dt) override;
	//catch up with station changes now, without any time passing
	void update();

	const tileNetwork& network() const { return net; }
	const std::vector<logisticsEndpoint>& endpoints() const { return endpointList; }
	//a network's state as of the last tick
	const logisticsIsland& island(int _island) const;
	//kg/s into a container (+) or out of it (-) as of the last tick, all its ports together
	double rateOf(const tileObject& _store) const;
	double totalMoving() const;

private:
	void refresh();
	//finds the endpoints again (after any station change: containers may have been replaced)
	void bindEndpoints();

	station* stationRef;
	tileNetwork net;
	std::uint64_t seenRevision = 0;
	bool built = false;
	std::vector<logisticsEndpoint> endpointList;
	std::vector<logisticsIsland> islandList;
	std::uint64_t tickCount = 0;
};
