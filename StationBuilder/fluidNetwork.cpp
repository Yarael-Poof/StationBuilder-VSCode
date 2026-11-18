#include "fluidNetwork.h"
#include "station.h"
#include "tileCatalogue.h"
#include "thermalNetwork.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>

const char* fluidStateName(fluidState _state)
{
	switch (_state)
	{
	case fluidState::noLiquid: return "no liquid";
	case fluidState::notEnough: return "not enough liquid";
	case fluidState::filled: return "full";
	case fluidState::mixed: return "mixed liquids";
	}
	return "?";
}

namespace
{
	//a tank's liquid and contents, from its placed object (only a liquid it can hold counts)
	void readTank(fluidTank& _tank)
	{
		_tank.liquid = nullptr;
		_tank.contents = 0.0;
		if (!_tank.object)
			return;
		for (const tileObject::heldMaterial& held : _tank.object->contents())
		{
			const materialInfo* material = findMaterial(held.material);
			if (material && material->state == materialInfo::phase::liquid && held.amount > 0.0)
			{
				_tank.liquid = material;
				_tank.contents = std::min(held.amount, _tank.capacity);
				return;
			}
		}
	}
}

fluidNetwork::fluidNetwork(station& _station, const electricalNetwork* _power)
	: stationRef(&_station), power(_power), net(fluidMedium)
{
}

void fluidNetwork::update()
{
	refresh();
}

void fluidNetwork::refresh()
{
	if (built && stationRef->getRevision() == seenRevision)
		return;
	seenRevision = stationRef->getRevision();
	//the network may be rebuilt: remember liquid temperatures by tile, to carry them over. (what the tanks hold is
	//kept on the tanks themselves)
	std::map<std::pair<int, int>, double> oldTemps;
	for (int n = 0; n < int(net.nodes().size()) && n < int(temps.size()); n++)
		oldTemps[{ net.nodes()[n].level, net.nodes()[n].tileIndex }] = temps[n];
	if (!net.update(*stationRef) && built)
	{
		//the same network, but a tank may have been swapped for another, or filled or drained from its panel
		bindTanks();
		findLiquids();
		return;
	}
	built = true;
	rebuild();

	const std::vector<networkNode>& nodes = net.nodes();
	for (int n = 0; n < int(nodes.size()); n++)
	{
		const auto found = oldTemps.find({ nodes[n].level, nodes[n].tileIndex });
		if (found != oldTemps.end())
			temps[n] = found->second;
	}
	findLiquids();
}

void fluidNetwork::bindTanks()
{
	const std::vector<networkNode>& nodes = net.nodes();
	for (fluidTank& tank : tankList)
	{
		tank.object = nullptr;
		const networkNode& node = nodes[tank.node];
		int seen = 0;
		stationRef->getIsoLevel(float(node.level))->levelTiles[node.tileIndex].forEachObject([&](tileObject* _object) {
			if (_object->type && _object->type->fluid.tank && fluidMedium.hasNode(*_object) &&
				footprint::isMainTile(*_object, portInfo::kind::liquid) && seen++ == tank.key.ordinal)
				tank.object = &_object->machine();   //a big tank keeps what it holds on the machine itself
		});
		readTank(tank);
	}
}

void fluidNetwork::findLiquids()
{
	const std::vector<networkNode>& nodes = net.nodes();
	liquidTypes.assign(net.islands().size(), {});
	for (const fluidTank& tank : tankList)
	{
		std::vector<const materialInfo*>& found = liquidTypes[nodes[tank.node].island];
		if (tank.liquid && tank.contents > 0.0 && std::find(found.begin(), found.end(), tank.liquid) == found.end())
			found.push_back(tank.liquid);
	}
}

void fluidNetwork::storeTanks()
{
	for (fluidTank& tank : tankList)
	{
		if (tank.contents <= 1e-9)
			tank.liquid = nullptr;   //an empty tank takes whatever liquid fills it next
		if (!tank.object)
			continue;
		if (tank.liquid)
			tank.object->setContents(tank.liquid->key, tank.contents);
		else
			tank.object->clearContents();
	}
}

void fluidNetwork::rebuild()
{
	const std::vector<networkNode>& nodes = net.nodes();
	const std::vector<networkEdge>& edges = net.edges();
	const int nodeCount = int(nodes.size());
	tileConductance.assign(nodeCount, 0.0);
	nodeVolume.assign(nodeCount, 0.0);
	nodeExchange.assign(nodeCount, 0.0);
	temps.assign(nodeCount, fluidTuning::startTemperature);
	pressures.assign(nodeCount, 0.0);
	flows.assign(edges.size(), 0.0);
	edgeHead.assign(edges.size(), 0.0);
	tankList.clear();
	pumpList.clear();
	exchangerList.clear();
	radiatorList.clear();

	//what is on each tile, read straight from the station (for the pumps' settings)
	std::vector<double> riserConductance(nodeCount, 0.0);   //just its pipe risers: what a link between levels runs through
	for (int n = 0; n < nodeCount; n++)
	{
		const networkNode& node = nodes[n];
		isometricLevel* level = stationRef->getIsoLevel(float(node.level));
		int fluidIndex = 0, pumpsOnTile = 0, tanksOnTile = 0;
		level->levelTiles[node.tileIndex].forEachObject([&](tileObject* _object) {
			if (!_object->type || !fluidMedium.hasNode(*_object))
				return;
			const tileTypeInfo& type = *_object->type;
			const fluidProperties& f = type.fluid;
			//a machine's liquid port lets liquid through even if the machine has no fluid behaviour of its own
			const double g = f.conductance > 0.0 ? f.conductance : fluidTuning::portConductance;
			tileConductance[n] += g;
			if (f.vertical)
				riserConductance[n] += g;
			//a big machine's tank, volume, exchanger or pump is on one of its tiles: its first liquid port's
			if (!f.carriesLiquid || !footprint::isMainTile(*_object, portInfo::kind::liquid))
			{
				fluidIndex++;
				return;
			}
			tileObject& machine = _object->machine();
			if (f.tank)
			{
				fluidTank tank;
				tank.node = n;
				tank.key = generatorKey{ node.level, node.tileIndex, tanksOnTile++ };
				tank.capacity = f.capacity;
				tank.object = &machine;
				readTank(tank);   //what it holds is kept on the tank itself (placed full of its liquid)
				tankList.push_back(tank);
			}
			else
				nodeVolume[n] += f.volume;
			if (f.radiatingArea > 0.0)
			{
				fluidRadiator radiator;
				radiator.node = n;
				radiator.properties = f;
				radiatorList.push_back(radiator);
			}
			if (f.exchange > 0.0)
			{
				fluidExchanger exchanger;
				exchanger.node = n;
				exchanger.exchange = f.exchange;
				exchangerList.push_back(exchanger);
				nodeExchange[n] += f.exchange;
			}
			if (f.pump)
			{
				fluidPump pump;
				pump.node = n;
				pump.objectOnNode = fluidIndex;
				pump.side = f.pumpSide;
				pump.powerLevel = node.level;
				pump.powerTile = node.tile;
				pump.pumpOnTile = pumpsOnTile++;
				//a pump with ports pushes from its "in" port to its "out" port, turned as it is placed: a one-tile one
				//through the out port's face, a bigger one through itself (the link between its two port tiles)
				for (const portInfo& port : footprint::turnedPorts(type, machine.turns()))
				{
					if (port.carries != portInfo::kind::liquid || port.flow == portInfo::direction::both)
						continue;
					const int at = net.nodeAt(node.level, machine.origin + port.cell);
					if (port.flow == portInfo::direction::out)
					{
						pump.side = port.side;
						pump.outNode = at;
					}
					else
						pump.inNode = at;
				}
				if (footprint::isMultiTile(type))
				{
					//its electricity comes in at its power port's tile, where it is the only pump
					pump.powerTile = machine.origin + footprint::mainCell(type, machine.turns(), portInfo::kind::power);
					pump.pumpOnTile = 0;
				}
				pump.ratedHead = f.pumpHead;
				pump.ratedFlow = f.pumpFlow;
				pump.switchedOn = !findSetting(type, "on") || machine.getSetting("on") != 0.0;
				pump.speed = findSetting(type, "s") ? machine.getSetting("s") / 100.0 : 1.0;
				pumpList.push_back(pump);
			}
			fluidIndex++;
		});
		tileConductance[n] = std::max(tileConductance[n], 1e-6);
	}

	//each link: half of each end's resistance in series (for a link between levels, half of each pipe riser's), plus
	//the own resistance of any pump pushing along it
	std::vector<double> resistance(edges.size(), 0.0);
	for (int e = 0; e < int(edges.size()); e++)
	{
		const std::vector<double>& ends = edges[e].vertical ? riserConductance : tileConductance;
		resistance[e] = 0.5 * (1.0 / std::max(ends[edges[e].a], 1e-6) + 1.0 / std::max(ends[edges[e].b], 1e-6));
	}
	for (fluidPump& pump : pumpList)
	{
		//the link it pushes along: from its tile to the neighbour on its out side, or for a big pump the link inside it
		//from its in port's tile to its out port's
		const networkNode& node = nodes[pump.node];
		const bool inside = pump.inNode >= 0 && pump.outNode >= 0 && pump.inNode != pump.outNode;
		const int from = inside ? pump.inNode : pump.node;
		const int across = inside ? pump.outNode : (pump.side >= 0 ? net.nodeAt(node.level, node.tile + tileSides::neighbourStep(pump.side)) : -1);
		for (int e = 0; across >= 0 && e < int(edges.size()); e++)
		{
			if ((edges[e].a == from && edges[e].b == across) || (edges[e].b == from && edges[e].a == across))
			{
				pump.edge = e;
				pump.sign = edges[e].a == from ? 1.0 : -1.0;
				if (pump.ratedFlow > 0.0)
					resistance[e] += pump.ratedHead / pump.ratedFlow;   //a straight pump curve: head falls to 0 at its rated flow
				break;
			}
		}
	}
	edgeConductance.assign(edges.size(), 0.0);
	for (int e = 0; e < int(edges.size()); e++)
		edgeConductance[e] = 1.0 / std::max(resistance[e], 1e-9);

	states.assign(net.islands().size(), fluidState::noLiquid);
	needs.assign(net.islands().size(), 0.0);
	liquid.assign(net.islands().size(), 0.0);
	liquidTypes.assign(net.islands().size(), {});
	flowFactorised = false;
	heatFactorised = false;
}

void fluidNetwork::drivePumps()
{
	const std::vector<networkNode>& nodes = net.nodes();
	edgeHead.assign(net.edges().size(), 0.0);
	for (fluidPump& pump : pumpList)
	{
		pump.powerFraction = 1.0;
		if (power)
		{
			//its electricity: how much of its rated power its electrical network is giving it. its electrical device is
			//the same-numbered pump among the pumps on its tile (both networks list a tile's objects in draw order)
			pump.powerFraction = 0.0;
			const int electricalNode = power->network().nodeAt(pump.powerLevel, pump.powerTile);
			int pumpsSeen = 0;
			for (int d : electricalNode >= 0 ? power->devicesOnNode(electricalNode) : std::vector<int>())
			{
				const electricalDevice& device = power->devices()[d];
				if (device.isSource || !device.type->fluid.pump || pumpsSeen++ != pump.pumpOnTile)
					continue;
				const double rated = electrical::ratedLoadWatts(device.type->electrical);
				pump.powerFraction = rated > 0.0 ? std::clamp(device.power.real() / rated, 0.0, 1.0) : 0.0;
				break;
			}
		}
		pump.head = pump.switchedOn ? pump.ratedHead * pump.speed * pump.powerFraction : 0.0;
		if (pump.edge >= 0)
			edgeHead[pump.edge] += pump.sign * pump.head;
	}
}

void fluidNetwork::solveFlow(double _dt)
{
	const std::vector<networkNode>& nodes = net.nodes();
	const std::vector<networkEdge>& edges = net.edges();
	const std::vector<networkIsland>& islands = net.islands();
	const int nodeCount = int(nodes.size());

	//each network: what its pipes need, what its tanks hold, so whether it is full; each tank's share of the
	//pipes, and so how much it has free (that sets its pressure)
	std::vector<double> capacity(islands.size(), 0.0);
	std::fill(needs.begin(), needs.end(), 0.0);
	std::fill(liquid.begin(), liquid.end(), 0.0);
	for (int n = 0; n < nodeCount; n++)
		needs[nodes[n].island] += nodeVolume[n];
	for (const fluidTank& tank : tankList)
	{
		liquid[nodes[tank.node].island] += tank.contents;
		capacity[nodes[tank.node].island] += tank.capacity;
	}
	findLiquids();
	for (int i = 0; i < int(islands.size()); i++)
	{
		if (liquidTypes[i].size() > 1)
			states[i] = fluidState::mixed;   //nothing flows, and no tank's liquid moves, until it is sorted out
		else if (capacity[i] <= 0.0 || liquid[i] <= 1e-9)
			states[i] = fluidState::noLiquid;
		else if (liquid[i] + 1e-9 < needs[i])
			states[i] = fluidState::notEnough;
		else
			states[i] = fluidState::filled;
	}
	auto filled = [&](int _node) { return states[nodes[_node].island] == fluidState::filled; };
	for (fluidTank& tank : tankList)
	{
		const int island = nodes[tank.node].island;
		tank.free = tank.contents - needs[island] * tank.capacity / capacity[island];
		tank.blocked = false;
		tank.outflow = 0.0;
	}
	std::fill(flows.begin(), flows.end(), 0.0);
	std::fill(pressures.begin(), pressures.end(), 0.0);

	//solve; then an empty tank asked to give, or a full one asked to take, is shut and it is solved again
	for (int attempt = 0; attempt <= int(tankList.size()); attempt++)
	{
		std::vector<bool> key(tankList.size() + islands.size());
		for (size_t t = 0; t < tankList.size(); t++)
			key[t] = !tankList[t].blocked;
		for (size_t i = 0; i < islands.size(); i++)
			key[tankList.size() + i] = states[i] == fluidState::filled;
		if (!flowFactorised || key != flowFactorKey)
		{
			flowSolver.reset(nodeCount);
			for (int n = 0; n < nodeCount; n++)
				flowSolver.addShunt(n, fluidTuning::referenceLeak);
			for (int e = 0; e < int(edges.size()); e++)
				flowSolver.addBranch(edges[e].a, edges[e].b, edgeConductance[e]);
			for (const fluidTank& tank : tankList)
			{
				if (!tank.blocked && filled(tank.node))
					flowSolver.addShunt(tank.node, tileConductance[tank.node]);
			}
			flowFactorised = nodeCount > 0 && flowSolver.factorise();
			flowFactorKey = key;
		}
		if (!flowFactorised)
			return;

		std::vector<double> rhs(nodeCount, 0.0);
		for (const fluidTank& tank : tankList)
		{
			if (!tank.blocked && filled(tank.node))
				rhs[tank.node] += tileConductance[tank.node] * fluidTuning::tankFullPressure * std::clamp(tank.free / tank.capacity, 0.0, 1.0);
		}
		for (int e = 0; e < int(edges.size()); e++)
		{
			if (!filled(edges[e].a) || edgeHead[e] == 0.0)
				continue;
			//a pump's push along a->b: as if G h were taken from a and put into b
			rhs[edges[e].a] -= edgeConductance[e] * edgeHead[e];
			rhs[edges[e].b] += edgeConductance[e] * edgeHead[e];
		}
		if (!flowSolver.solve(rhs, pressures))
			return;
		for (int e = 0; e < int(edges.size()); e++)
			flows[e] = filled(edges[e].a) ? edgeConductance[e] * (pressures[edges[e].a] - pressures[edges[e].b] + edgeHead[e]) : 0.0;

		bool again = false;
		for (fluidTank& tank : tankList)
		{
			tank.outflow = 0.0;
			if (tank.blocked || !filled(tank.node))
				continue;
			const double tankPressure = fluidTuning::tankFullPressure * std::clamp(tank.free / tank.capacity, 0.0, 1.0);
			tank.outflow = tileConductance[tank.node] * (tankPressure - pressures[tank.node]);
			const bool empty = tank.free <= 1e-9 && tank.outflow > 1e-12;
			const bool full = tank.contents >= tank.capacity - 1e-9 && tank.outflow < -1e-12;
			if (empty || full)
			{
				tank.blocked = true;
				tank.outflow = 0.0;
				again = true;
			}
		}
		if (!again)
			break;
	}

	for (fluidTank& tank : tankList)
	{
		tank.contents = std::clamp(tank.contents - tank.outflow * _dt, 0.0, tank.capacity);
		//an empty tank filling from the network takes the network's liquid
		const std::vector<const materialInfo*>& network = liquidTypes[nodes[tank.node].island];
		if (!tank.liquid && tank.contents > 1e-9 && network.size() == 1)
			tank.liquid = network.front();
	}
	storeTanks();
	for (fluidPump& pump : pumpList)
		pump.flow = pump.edge >= 0 ? pump.sign * flows[pump.edge] : 0.0;
}

double fluidNetwork::airTemperature(int _node) const
{
	if (!airTemperatures)
		return 20.0;
	const networkNode& node = net.nodes()[_node];
	return airTemperatures->temperatureAt(node.level, node.tile);
}

void fluidNetwork::solveTemperatures(double _dt)
{
	const std::vector<networkNode>& nodes = net.nodes();
	const std::vector<networkEdge>& edges = net.edges();
	const int nodeCount = int(nodes.size());
	auto filled = [&](int _node) { return states[nodes[_node].island] == fluidState::filled; };
	//J/K per litre of each network's liquid (its material's heat capacity x density)
	std::vector<double> perLitre(net.islands().size());
	for (int i = 0; i < int(perLitre.size()); i++)
		perLitre[i] = islandHeatPerLitre(i);
	auto c = [&](int _node) { return perLitre[nodes[_node].island]; };

	//each tile's liquid holds C = c x litres; heat moves downstream with the flow (each tile takes in its upstream
	//neighbours' liquid at their temperature) and between the liquid and the air at exchangers
	std::vector<double> heldHeat(nodeCount), exchange(nodeCount), air(nodeCount);
	for (int n = 0; n < nodeCount; n++)
	{
		heldHeat[n] = c(n) * std::max(liquidOnNode(n), 1.0);
		air[n] = airTemperature(n);
		exchange[n] = filled(n) && !std::isnan(air[n]) ? nodeExchange[n] : 0.0;
	}
	//radiators: e sigma A (T^4 - Ts^4) as a W/K to space through this tick's temperature (the line through Ts and T, so
	//what it sheds is right at T and it can never overshoot below space)
	std::vector<double> radiate(nodeCount, 0.0);
	const double sink = fluid::spaceSinkCelsius;
	for (const fluidRadiator& radiator : radiatorList)
	{
		const double t = temps[radiator.node];
		if (filled(radiator.node) && t - sink > 1e-6)
			radiate[radiator.node] += fluid::radiatedWatts(radiator.properties, t) / (t - sink);
	}
	std::vector<double> key;
	key.reserve(nodeCount * 2 + edges.size() + nodeCount);
	for (int n = 0; n < nodeCount; n++)
	{
		key.push_back(filled(n) ? heldHeat[n] : -1.0);
		key.push_back(exchange[n]);
		key.push_back(radiate[n]);
	}
	for (int e = 0; e < int(edges.size()); e++)
		key.push_back(flows[e]);
	if (!heatFactorised || key != heatFactorKey || _dt != heatFactorDt)
	{
		heatSolver.reset(nodeCount);
		for (int n = 0; n < nodeCount; n++)
			heatSolver.addEntry(n, n, filled(n) ? heldHeat[n] / _dt + exchange[n] + radiate[n] : 1.0);
		for (int e = 0; e < int(edges.size()); e++)
		{
			const double q = flows[e];
			if (q == 0.0 || !filled(edges[e].a))
				continue;
			const int from = q > 0.0 ? edges[e].a : edges[e].b;
			const int to = q > 0.0 ? edges[e].b : edges[e].a;
			heatSolver.addEntry(to, to, std::abs(q) * c(to));
			heatSolver.addEntry(to, from, -std::abs(q) * c(to));
		}
		heatFactorised = nodeCount > 0 && heatSolver.factorise();
		heatFactorKey = key;
		heatFactorDt = _dt;
		if (!heatFactorised)
			return;
	}
	std::vector<double> rhs(nodeCount);
	for (int n = 0; n < nodeCount; n++)
		rhs[n] = filled(n) ? heldHeat[n] / _dt * temps[n] + exchange[n] * (std::isnan(air[n]) ? 0.0 : air[n]) + radiate[n] * sink : temps[n];
	std::vector<double> next;
	if (heatSolver.solve(rhs, next))
		temps.swap(next);

	for (fluidExchanger& exchanger : exchangerList)
	{
		const double t = air[exchanger.node];
		exchanger.heatToAir = filled(exchanger.node) && !std::isnan(t) ? exchanger.exchange * (temps[exchanger.node] - t) : 0.0;
	}
	for (fluidRadiator& radiator : radiatorList)
		radiator.heatToSpace = filled(radiator.node) ? fluid::radiatedWatts(radiator.properties, temps[radiator.node]) : 0.0;
}

void fluidNetwork::tick(double _dt)
{
	refresh();
	if (net.nodes().empty() || _dt <= 0.0)
		return;
	drivePumps();
	solveFlow(_dt);
	solveTemperatures(_dt);
}

fluidState fluidNetwork::islandState(int _island) const
{
	return _island >= 0 && _island < int(states.size()) ? states[_island] : fluidState::noLiquid;
}

const materialInfo* fluidNetwork::islandLiquidType(int _island) const
{
	return _island >= 0 && _island < int(liquidTypes.size()) && liquidTypes[_island].size() == 1 ? liquidTypes[_island].front() : nullptr;
}

std::vector<const materialInfo*> fluidNetwork::islandLiquidTypes(int _island) const
{
	return _island >= 0 && _island < int(liquidTypes.size()) ? liquidTypes[_island] : std::vector<const materialInfo*>();
}

double fluidNetwork::islandHeatPerLitre(int _island) const
{
	const materialInfo* liquidType = islandLiquidType(_island);
	return liquidType ? liquidType->heatPerLitre() : fluidTuning::heatPerLitre;
}

double fluidNetwork::islandLiquid(int _island) const
{
	return _island >= 0 && _island < int(liquid.size()) ? liquid[_island] : 0.0;
}

double fluidNetwork::islandNeeds(int _island) const
{
	return _island >= 0 && _island < int(needs.size()) ? needs[_island] : 0.0;
}

double fluidNetwork::pressure(int _node) const
{
	return _node >= 0 && _node < int(pressures.size()) ? pressures[_node] : 0.0;
}

double fluidNetwork::liquidTemperature(int _node) const
{
	return _node >= 0 && _node < int(temps.size()) ? temps[_node] : std::numeric_limits<double>::quiet_NaN();
}

void fluidNetwork::setLiquidTemperature(int _node, double _celsius)
{
	if (_node >= 0 && _node < int(temps.size()))
		temps[_node] = _celsius;
}

void fluidNetwork::setTankContents(int _tank, double _litres)
{
	if (_tank < 0 || _tank >= int(tankList.size()))
		return;
	fluidTank& tank = tankList[_tank];
	tank.contents = std::clamp(_litres, 0.0, tank.capacity);
	if (!tank.liquid && tank.contents > 0.0 && tank.object && tank.object->type)
		tank.liquid = findMaterial(tank.object->type->fluid.liquid);
	storeTanks();
	findLiquids();
}

double fluidNetwork::takeLiquid(int _node, const materialInfo& _liquid, double _litres)
{
	refresh();
	if (_node < 0 || _node >= int(net.nodes().size()) || _litres <= 0.0)
		return 0.0;
	const std::vector<networkNode>& nodes = net.nodes();
	const int island = nodes[_node].island;
	if (islandState(island) != fluidState::filled || islandLiquidType(island) != &_liquid)
		return 0.0;
	//each tank lends its share of what the pipes hold (by its size): only what it has beyond that can go
	double capacity = 0.0, needed = 0.0;
	for (const fluidTank& tank : tankList)
		if (nodes[tank.node].island == island)
			capacity += tank.capacity;
	for (int n : net.islands()[island].nodes)
		needed += nodeVolume[n];
	double taken = 0.0;
	for (fluidTank& tank : tankList)
	{
		if (nodes[tank.node].island != island || tank.liquid != &_liquid || capacity <= 0.0)
			continue;
		const double spare = tank.contents - needed * tank.capacity / capacity;
		const double amount = std::clamp(std::min(spare, _litres - taken), 0.0, tank.contents);
		tank.contents -= amount;
		taken += amount;
		if (taken >= _litres - 1e-12)
			break;
	}
	storeTanks();
	findLiquids();
	return taken;
}

double fluidNetwork::putLiquid(int _node, const materialInfo& _liquid, double _litres)
{
	refresh();
	if (_node < 0 || _node >= int(net.nodes().size()) || _litres <= 0.0)
		return 0.0;
	const std::vector<networkNode>& nodes = net.nodes();
	const int island = nodes[_node].island;
	const std::vector<const materialInfo*> held = islandLiquidTypes(island);
	if (held.size() > 1 || (held.size() == 1 && held.front() != &_liquid))
		return 0.0;
	double put = 0.0;
	for (fluidTank& tank : tankList)
	{
		if (nodes[tank.node].island != island || (tank.liquid && tank.contents > 1e-9 && tank.liquid != &_liquid))
			continue;
		const double amount = std::clamp(std::min(tank.capacity - tank.contents, _litres - put), 0.0, tank.capacity);
		if (amount <= 0.0)
			continue;
		tank.contents += amount;
		tank.liquid = &_liquid;
		put += amount;
		if (put >= _litres - 1e-12)
			break;
	}
	storeTanks();
	findLiquids();
	return put;
}

double fluidNetwork::flow(int _edge) const
{
	return _edge >= 0 && _edge < int(flows.size()) ? flows[_edge] : 0.0;
}

double fluidNetwork::liquidOnNode(int _node) const
{
	if (_node < 0 || _node >= int(nodeVolume.size()))
		return 0.0;
	double litres = nodeVolume[_node];
	for (const fluidTank& tank : tankList)
	{
		if (tank.node == _node)
			litres += tank.contents;
	}
	return litres;
}

double fluidNetwork::heatToAir(int _node) const
{
	double watts = 0.0;
	for (const fluidExchanger& exchanger : exchangerList)
	{
		if (exchanger.node == _node)
			watts += exchanger.heatToAir;
	}
	return watts;
}

double fluidNetwork::totalRadiated() const
{
	double total = 0.0;
	for (const fluidRadiator& radiator : radiatorList)
		total += radiator.heatToSpace;
	return total;
}

double fluidNetwork::totalLiquid() const
{
	double litres = 0.0;
	for (const fluidTank& tank : tankList)
		litres += tank.contents;
	return litres;
}

void fluidNetwork::forEachTileHeat(const std::function<void(int, sf::Vector2i, double)>& _add) const
{
	for (const fluidExchanger& exchanger : exchangerList)
	{
		if (exchanger.heatToAir != 0.0)
			_add(net.nodes()[exchanger.node].level, net.nodes()[exchanger.node].tile, exchanger.heatToAir);
	}
}
