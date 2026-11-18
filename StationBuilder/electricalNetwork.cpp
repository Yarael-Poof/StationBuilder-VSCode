#include "electricalNetwork.h"
#include "station.h"
#include "tileCatalogue.h"
#include "thermalNetwork.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
	const double twoPi = 6.283185307179586;
	const double frequencyTolerance = 1e-9;   //Hz: generators closer than this count as the same frequency
}

const char* islandStateName(islandState _state)
{
	switch (_state)
	{
	case islandState::dead: return "dead";
	case islandState::live: return "live";
	case islandState::frequencyMismatch: return "frequency mismatch";
	case islandState::solveFailed: return "solve failed";
	}
	return "?";
}

namespace ac
{
	complexd seriesImpedance(double _resistance, double _inductance, double _w)
	{
		return complexd(std::max(_resistance, minimumResistance), _w * _inductance);
	}

	complexd loadImpedance(double _resistance, double _inductance, double _capacitance, double _w)
	{
		double reactance = _w * _inductance;
		if (_capacitance > 0.0)
			reactance -= 1.0 / (_w * _capacitance);
		//a load can be purely reactive (R = 0), but never exactly zero: that would be an infinite admittance
		const complexd z(std::max(_resistance, 0.0), reactance);
		return std::abs(z) < minimumResistance ? complexd(minimumResistance, 0.0) : z;
	}

	complexd tileImpedance(const std::vector<acCircuit::seriesRL>& _conductors, double _w)
	{
		complexd admittance = 0.0;
		for (const acCircuit::seriesRL& c : _conductors)
			admittance += 1.0 / seriesImpedance(c.resistance, c.inductance, _w);
		return 1.0 / admittance;
	}
}

// ---------------------------------------------------------------------------------------------------------
// acSolver
// ---------------------------------------------------------------------------------------------------------

void acSolver::setCircuit(const acCircuit& _circuit)
{
	circ = _circuit;
	haveFactors = false;
	sol = acSolution();
}

bool acSolver::factorise(double _w)
{
	const int nodeCount = int(circ.nodeConductors.size());
	nodal.reset(nodeCount);

	//tile impedances first (all conductors, and risers on their own), then each branch is half of each end in series
	std::vector<complexd> tileZ(nodeCount, complexd(0.0, 0.0));
	std::vector<complexd> riserZ(nodeCount, complexd(0.0, 0.0));
	for (int n = 0; n < nodeCount; n++)
	{
		if (!circ.nodeConductors[n].empty())
			tileZ[n] = ac::tileImpedance(circ.nodeConductors[n], _w);
		if (n < int(circ.nodeRisers.size()) && !circ.nodeRisers[n].empty())
			riserZ[n] = ac::tileImpedance(circ.nodeRisers[n], _w);
	}
	branchZ.assign(circ.branches.size(), complexd(0.0, 0.0));
	for (size_t b = 0; b < circ.branches.size(); b++)
	{
		const acCircuit::branch& branch = circ.branches[b];
		const std::vector<complexd>& endZ = branch.vertical ? riserZ : tileZ;
		branchZ[b] = 0.5 * (endZ[branch.a] + endZ[branch.b]);
		if (std::abs(branchZ[b]) < ac::minimumResistance)
			branchZ[b] = complexd(ac::minimumResistance, 0.0);   //a branch with nothing in it (shouldn't happen): keep the matrix solvable
		nodal.addBranch(branch.a, branch.b, 1.0 / branchZ[b]);
	}

	sourceZ.assign(circ.sources.size(), complexd(0.0, 0.0));
	factorConnected.assign(circ.sources.size(), false);
	for (size_t s = 0; s < circ.sources.size(); s++)
	{
		const acCircuit::source& source = circ.sources[s];
		sourceZ[s] = ac::seriesImpedance(source.resistance, source.inductance, _w);
		factorConnected[s] = source.connected;
		if (source.connected)
			nodal.addShunt(source.node, 1.0 / sourceZ[s]);   //Norton equivalent: internal admittance to ground
	}

	loadZ.assign(circ.loads.size(), complexd(0.0, 0.0));
	factorLoadConnected.assign(circ.loads.size(), false);
	for (size_t l = 0; l < circ.loads.size(); l++)
	{
		const acCircuit::load& load = circ.loads[l];
		loadZ[l] = ac::loadImpedance(load.resistance, load.inductance, load.capacitance, _w);
		factorLoadConnected[l] = load.connected;
		if (load.connected)
			nodal.addShunt(load.node, 1.0 / loadZ[l]);
	}

	factorCount++;
	haveFactors = nodal.factorise();
	factorFrequency = _w / twoPi;
	return haveFactors;
}

const acSolution& acSolver::solve()
{
	const int nodeCount = int(circ.nodeConductors.size());
	sol = acSolution();
	sol.nodeVoltage.assign(nodeCount, complexd(0.0, 0.0));
	sol.branchCurrent.assign(circ.branches.size(), complexd(0.0, 0.0));
	sol.branchLoss.assign(circ.branches.size(), 0.0);
	sol.sourceCurrent.assign(circ.sources.size(), complexd(0.0, 0.0));
	sol.sourcePower.assign(circ.sources.size(), complexd(0.0, 0.0));
	sol.loadCurrent.assign(circ.loads.size(), complexd(0.0, 0.0));
	sol.loadPower.assign(circ.loads.size(), complexd(0.0, 0.0));

	//the island's frequency comes from its connected generators, and they must agree
	bool anySource = false;
	double frequency = 0.0;
	for (const acCircuit::source& source : circ.sources)
	{
		if (!source.connected)
			continue;
		if (!anySource)
		{
			frequency = source.frequency;
			anySource = true;
		}
		else if (std::abs(source.frequency - frequency) > frequencyTolerance)
		{
			sol.state = islandState::frequencyMismatch;
			return sol;
		}
	}
	if (!anySource || frequency <= 0.0)
	{
		sol.state = islandState::dead;
		return sol;
	}

	//the matrix only changes with the frequency or the set of connected generators and loads
	bool sameConnections = factorConnected.size() == circ.sources.size() && factorLoadConnected.size() == circ.loads.size();
	for (size_t s = 0; sameConnections && s < circ.sources.size(); s++)
		sameConnections = factorConnected[s] == circ.sources[s].connected;
	for (size_t l = 0; sameConnections && l < circ.loads.size(); l++)
		sameConnections = factorLoadConnected[l] == circ.loads[l].connected;
	if (!haveFactors || !sameConnections || std::abs(frequency - factorFrequency) > frequencyTolerance)
	{
		if (!factorise(twoPi * frequency))
		{
			sol.state = islandState::solveFailed;
			return sol;
		}
	}

	//each generator injects E / Zs into its node (Norton equivalent)
	std::vector<complexd> injection(nodeCount, complexd(0.0, 0.0));
	for (size_t s = 0; s < circ.sources.size(); s++)
	{
		const acCircuit::source& source = circ.sources[s];
		if (source.connected)
			injection[source.node] += source.emf * source.emfScale / sourceZ[s];
	}
	if (!nodal.solve(injection, sol.nodeVoltage))
	{
		sol.nodeVoltage.assign(nodeCount, complexd(0.0, 0.0));
		sol.state = islandState::solveFailed;
		return sol;
	}
	sol.state = islandState::live;
	sol.frequency = frequency;

	for (size_t b = 0; b < circ.branches.size(); b++)
	{
		const acCircuit::branch& branch = circ.branches[b];
		const complexd current = (sol.nodeVoltage[branch.a] - sol.nodeVoltage[branch.b]) / branchZ[b];
		sol.branchCurrent[b] = current;
		sol.branchLoss[b] = std::norm(current) * branchZ[b].real();   //|I|^2 R
		sol.lossWatts += sol.branchLoss[b];
	}
	for (size_t s = 0; s < circ.sources.size(); s++)
	{
		const acCircuit::source& source = circ.sources[s];
		if (!source.connected)
			continue;
		const complexd v = sol.nodeVoltage[source.node];
		const complexd current = (source.emf * source.emfScale - v) / sourceZ[s];
		sol.sourceCurrent[s] = current;
		sol.sourcePower[s] = v * std::conj(current);
		sol.generatedWatts += sol.sourcePower[s].real();
	}
	for (size_t l = 0; l < circ.loads.size(); l++)
	{
		if (!circ.loads[l].connected)
			continue;
		const complexd v = sol.nodeVoltage[circ.loads[l].node];
		const complexd current = v / loadZ[l];
		sol.loadCurrent[l] = current;
		sol.loadPower[l] = v * std::conj(current);
		sol.consumedWatts += sol.loadPower[l].real();
	}
	return sol;
}

// ---------------------------------------------------------------------------------------------------------
// electricalNetwork
// ---------------------------------------------------------------------------------------------------------

electricalNetwork::electricalNetwork(station& _station)
	: stationRef(&_station), net(electricalMedium)
{
}

void electricalNetwork::tick(double _dt)
{
	if (net.update(*stationRef))
		rebuild();
	applyThermostats();   //heaters switch by last tick's temperatures; the solve below picks that up
	solveAll();
	//protection reacts to that solve; if it changed any generator, solve again so what is shown and
	//counted this tick matches the generators' new state
	if (protectGenerators(_dt))
		solveAll();
	updateCableTemperatures(_dt);

	for (const std::unique_ptr<acSolver>& solver : solvers)
	{
		const acSolution& sol = solver->solution();
		if (sol.state != islandState::live)
			continue;
		totals.generatedJoules += sol.generatedWatts * _dt;
		totals.consumedJoules += sol.consumedWatts * _dt;
		totals.lostJoules += sol.lossWatts * _dt;
	}
}

bool electricalNetwork::protectGenerators(double _dt)
{
	using namespace generatorProtection;
	const double ease = 1.0 - std::exp(-_dt / sagTimeConstant);
	bool changed = false;
	for (const electricalDevice& device : deviceList)
	{
		if (!device.isSource)
			continue;
		generatorState& state = generatorStates[device.key];
		acSolver& solver = *solvers[device.island];
		acCircuit::source& source = solver.circuit().sources[device.local];
		const acSolution& sol = solver.solution();
		//a hot generator can give less: protection works to its rating at its tile's temperature
		state.tileTemperature = tileTemperature(device.node);
		state.effectiveRating = source.ratedPower * thermalEffects::deratingFactor(state.tileTemperature);
		if (state.tripped || state.effectiveRating <= 0.0)
			continue;
		if (!device.switchedOn || sol.state != islandState::live)
		{
			//switched off, or faulted (frequency mismatch): not producing, so not overloaded
			state.demand = 0.0;
			state.overloadSeconds = std::max(0.0, state.overloadSeconds - _dt);
			continue;
		}

		//power goes with the EMF squared, so from what it supplies now at emfScale we can tell what the network
		//would take at full EMF, and the EMF that would make it supply exactly its rating
		const double loading = std::abs(sol.sourcePower[device.local]) / state.effectiveRating;
		state.demand = state.emfScale > 1e-6 ? loading / (state.emfScale * state.emfScale) : loading;
		const double target = loading > 1e-12 ? std::min(1.0, state.emfScale / std::sqrt(loading)) : 1.0;
		state.emfScale += (target - state.emfScale) * ease;

		if (state.demand > overloadMargin)
			state.overloadSeconds += _dt;
		else
			state.overloadSeconds = std::max(0.0, state.overloadSeconds - _dt);

		if (state.overloadSeconds >= tripSeconds - 1e-9)
		{
			state.tripped = true;
			state.overloadSeconds = 0.0;
			state.emfScale = 1.0;
			source.connected = false;
			changed = true;
			const networkNode& node = net.nodes()[device.node];
			std::cout << "[POWER] " << device.type->name << " at " << node.tile.x << "," << node.tile.y << " (level " << node.level
				<< ") tripped: overloaded for " << tripSeconds << " s\n";
			continue;
		}
		if (source.emfScale != state.emfScale)
		{
			source.emfScale = state.emfScale;
			changed = true;
		}
	}
	return changed;
}

namespace thermalEffects
{
	double deratingFactor(double _celsius)
	{
		if (std::isnan(_celsius) || _celsius <= deratingStart)
			return 1.0;
		return std::max(deratingFloor, 1.0 - deratingPerDegree * (_celsius - deratingStart));
	}
}

double electricalNetwork::tileTemperature(int _node) const
{
	if (!temperatures || _node < 0 || _node >= int(net.nodes().size()))
		return 20.0;
	const networkNode& node = net.nodes()[_node];
	const double t = temperatures->temperatureAt(node.level, node.tile);
	return std::isnan(t) ? 20.0 : t;
}

bool electricalNetwork::applyThermostats()
{
	bool switched = false;
	for (const electricalDevice& device : deviceList)
	{
		if (device.isSource)
			continue;
		bool heating = true;
		if (!std::isnan(device.thermostat))
		{
			//only a tile with air has a temperature to sense; without one the thermostat just keeps calling
			thermostatState& state = thermostatStates[device.key];
			const networkNode& node = net.nodes()[device.node];
			const double t = temperatures ? temperatures->temperatureAt(node.level, node.tile) : std::numeric_limits<double>::quiet_NaN();
			if (!std::isnan(t))
			{
				if (t < device.thermostat - thermalEffects::thermostatBand)
					state.heating = true;
				else if (t > device.thermostat + thermalEffects::thermostatBand)
					state.heating = false;
			}
			heating = state.heating;
		}
		acCircuit::load& load = solvers[device.island]->circuit().loads[device.local];
		const bool connected = device.switchedOn && heating && !idleLoads.count(device.key);
		if (load.connected != connected)
		{
			load.connected = connected;
			switched = true;
		}
	}
	return switched;
}

void electricalNetwork::setLoadIdle(const generatorKey& _load, bool _idle)
{
	if (_idle)
		idleLoads.insert(_load);
	else
		idleLoads.erase(_load);
}

bool electricalNetwork::loadIdle(int _device) const
{
	return _device >= 0 && _device < int(deviceList.size()) && !deviceList[_device].isSource && idleLoads.count(deviceList[_device].key) > 0;
}

bool electricalNetwork::thermostatHeating(int _device) const
{
	if (_device < 0 || _device >= int(deviceList.size()) || deviceList[_device].isSource || std::isnan(deviceList[_device].thermostat))
		return true;
	const auto found = thermostatStates.find(deviceList[_device].key);
	return found == thermostatStates.end() || found->second.heating;
}

void electricalNetwork::updateCableTemperatures(double _dt)
{
	//each wired tile's cables run above the tile's temperature by the rise at their rating times (current / rating)^2,
	//using the busiest link through the tile; they follow that with a lag
	const std::vector<networkNode>& nodes = net.nodes();
	const std::vector<networkEdge>& edges = net.edges();
	std::vector<double> busiest(nodes.size(), 0.0);
	for (int e = 0; e < int(edges.size()); e++)
	{
		const double amps = std::abs(edgeCurrent(e));
		busiest[edges[e].a] = std::max(busiest[edges[e].a], amps);
		busiest[edges[e].b] = std::max(busiest[edges[e].b], amps);
	}
	const double ease = 1.0 - std::exp(-_dt / thermalEffects::cableTimeConstant);
	for (int n = 0; n < int(nodes.size()); n++)
	{
		if (nodes[n].conductors == 0)
			continue;
		double rating = 0.0;
		for (const tileTypeInfo* type : nodes[n].objects)
		{
			if (type->electrical.conductor)
				rating += type->electrical.ratedCurrent;
		}
		const double ratio = rating > 0.0 ? busiest[n] / rating : 0.0;
		const double target = tileTemperature(n) + thermalEffects::cableRiseAtRating * ratio * ratio;
		if (std::isnan(cableTemps[n]))
			cableTemps[n] = tileTemperature(n);
		cableTemps[n] += (target - cableTemps[n]) * ease;
	}
}

double electricalNetwork::cableTemperature(int _node) const
{
	return _node >= 0 && _node < int(cableTemps.size()) ? cableTemps[_node] : std::numeric_limits<double>::quiet_NaN();
}

const generatorState* electricalNetwork::generatorStatus(int _device) const
{
	if (_device < 0 || _device >= int(deviceList.size()) || !deviceList[_device].isSource)
		return nullptr;
	const auto found = generatorStates.find(deviceList[_device].key);
	return found == generatorStates.end() ? nullptr : &found->second;
}

bool electricalNetwork::resetGenerator(int _device)
{
	if (_device < 0 || _device >= int(deviceList.size()) || !deviceList[_device].isSource)
		return false;
	const electricalDevice& device = deviceList[_device];
	generatorState& state = generatorStates[device.key];
	if (!state.tripped)
		return false;
	state = generatorState();   //back at full EMF; if the overload is still there it will sag and trip again
	acCircuit::source& source = solvers[device.island]->circuit().sources[device.local];
	source.connected = device.switchedOn;
	source.emfScale = 1.0;
	const networkNode& node = net.nodes()[device.node];
	std::cout << "[POWER] reset " << device.type->name << " at " << node.tile.x << "," << node.tile.y << "\n";
	solveAll();
	return true;
}

int electricalNetwork::resetGenerators(int _level, sf::Vector2i _tile)
{
	update();
	const int node = net.nodeAt(_level, _tile);
	if (node < 0)
		return 0;
	int reset = 0;
	for (int d : devicesOnNode(node))
	{
		if (resetGenerator(d))
			reset++;
	}
	return reset;
}

int electricalNetwork::deviceAt(int _level, sf::Vector2i _tile, int _objectOnNode) const
{
	const int node = net.nodeAt(_level, _tile);
	if (node < 0)
		return -1;
	for (int d = 0; d < int(deviceList.size()); d++)
	{
		if (deviceList[d].node == node && deviceList[d].objectOnNode == _objectOnNode)
			return d;
	}
	return -1;
}

void electricalNetwork::computeHeat()
{
	nodeHeat.assign(net.nodes().size(), 0.0);
	spreadHeat.clear();
	for (const electricalDevice& device : deviceList)
	{
		const acSolver& solver = *solvers[device.island];
		double watts = 0.0;
		if (device.isSource)
		{
			const acCircuit::source& source = solver.circuit().sources[device.local];
			watts += std::norm(device.current) * std::max(source.resistance, ac::minimumResistance);
			//waste heat: whatever it burns that doesn't become electricity
			const double efficiency = device.type->electrical.efficiency;
			if (efficiency > 0.0 && efficiency < 1.0 && device.power.real() > 0.0)
				watts += device.power.real() * (1.0 / efficiency - 1.0);
		}
		else
			watts += device.power.real();
		nodeHeat[device.node] += watts;
		//a machine bigger than one tile warms every tile it covers, not just the one with its power port
		if (watts != 0.0 && footprint::isMultiTile(*device.type))
		{
			const sf::Vector2i size = footprint::turnedSize(*device.type, device.turns);
			const sf::Vector2i origin = net.nodes()[device.node].tile - footprint::mainCell(*device.type, device.turns, portInfo::kind::power);
			spreadHeat.push_back({ device.node, origin, size, watts });
		}
	}
	const std::vector<networkEdge>& edges = net.edges();
	for (int e = 0; e < int(edges.size()); e++)
	{
		const acSolution& sol = solvers[net.nodes()[edges[e].a].island]->solution();
		const int local = edgeLocal[e];
		if (local >= int(sol.branchLoss.size()))
			continue;
		nodeHeat[edges[e].a] += 0.5 * sol.branchLoss[local];
		nodeHeat[edges[e].b] += 0.5 * sol.branchLoss[local];
	}
}

void electricalNetwork::forEachTileHeat(const std::function<void(int, sf::Vector2i, double)>& _add) const
{
	const std::vector<networkNode>& nodes = net.nodes();
	std::vector<double> atNode = nodeHeat;
	for (const machineHeat& machine : spreadHeat)
	{
		atNode[machine.node] -= machine.watts;
		const double share = machine.watts / double(machine.size.x * machine.size.y);
		for (int y = 0; y < machine.size.y; y++)
			for (int x = 0; x < machine.size.x; x++)
				_add(nodes[machine.node].level, machine.origin + sf::Vector2i(x, y), share);
	}
	for (int n = 0; n < int(nodes.size()) && n < int(atNode.size()); n++)
	{
		if (std::abs(atNode[n]) > 1e-12)
			_add(nodes[n].level, nodes[n].tile, atNode[n]);
	}
}

void electricalNetwork::update()
{
	if (net.update(*stationRef))
	{
		rebuild();
		solveAll();
	}
}

void electricalNetwork::rebuild()
{
	const std::vector<networkNode>& nodes = net.nodes();
	const std::vector<networkEdge>& edges = net.edges();
	const std::vector<networkIsland>& islands = net.islands();

	solvers.clear();
	deviceList.clear();
	nodeLocal.assign(nodes.size(), -1);
	edgeLocal.assign(edges.size(), -1);
	//generators keep their protection state, heaters their thermostat's call for heat, and cables their temperature
	//through the rebuild; ones that were removed are forgotten
	std::map<generatorKey, generatorState> previousStates;
	previousStates.swap(generatorStates);
	std::map<generatorKey, thermostatState> previousThermostats;
	previousThermostats.swap(thermostatStates);
	std::map<std::pair<int, int>, double> previousCableTemps;
	for (size_t n = 0; n < cableTileKeys.size() && n < cableTemps.size(); n++)
		previousCableTemps[cableTileKeys[n]] = cableTemps[n];
	cableTemps.assign(nodes.size(), std::numeric_limits<double>::quiet_NaN());
	cableTileKeys.assign(nodes.size(), { 0, 0 });
	for (int n = 0; n < int(nodes.size()); n++)
	{
		cableTileKeys[n] = { nodes[n].level, nodes[n].tileIndex };
		const auto found = previousCableTemps.find(cableTileKeys[n]);
		if (nodes[n].conductors > 0 && found != previousCableTemps.end())
			cableTemps[n] = found->second;
	}

	for (int i = 0; i < int(islands.size()); i++)
	{
		const networkIsland& island = islands[i];
		acCircuit circuit;
		circuit.nodeConductors.resize(island.nodes.size());
		circuit.nodeRisers.resize(island.nodes.size());
		for (int local = 0; local < int(island.nodes.size()); local++)
		{
			const int n = island.nodes[local];
			nodeLocal[n] = local;
			int sourcesOnTile = 0;
			int loadsOnTile = 0;
			for (int objectOnNode = 0; objectOnNode < int(nodes[n].objects.size()); objectOnNode++)
			{
				const tileTypeInfo* type = nodes[n].objects[objectOnNode];
				const electricalProperties& e = type->electrical;
				//the object's own settings, where its type has them: "on" (a switch), "v" (output voltage), "t" (thermostat)
				const std::vector<double>& values = nodes[n].settings[objectOnNode];
				auto setting = [&](const char* _key, double _fallback) {
					for (size_t s = 0; s < type->settings.size() && s < values.size(); s++)
						if (type->settings[s].key == _key)
							return values[s];
					return _fallback;
				};
				if (e.conductor)
					circuit.nodeConductors[local].push_back({ e.resistance, e.inductance });
				if (e.conductor && e.vertical)
					circuit.nodeRisers[local].push_back({ e.resistance, e.inductance });
				if (e.source)
				{
					acCircuit::source source;
					source.node = local;
					source.emf = e.emf;
					source.frequency = e.frequency;
					source.ratedPower = e.ratedPower;
					source.resistance = e.sourceResistance;
					source.inductance = e.sourceInductance;
					electricalDevice device;
					device.node = n;
					device.island = i;
					device.type = type;
					device.isSource = true;
					device.local = int(circuit.sources.size());
					device.objectOnNode = objectOnNode;
					device.key = generatorKey{ nodes[n].level, nodes[n].tileIndex, sourcesOnTile++ };
					const auto previous = previousStates.find(device.key);
					const generatorState state = previous != previousStates.end() ? previous->second : generatorState();
					generatorStates[device.key] = state;
					device.switchedOn = setting("on", 1.0) != 0.0;
					device.turns = int(setting(footprint::rotationKey, 0.0));
					source.emf = setting("v", e.emf);
					source.emfScale = state.emfScale;
					source.connected = device.switchedOn && !state.tripped;
					circuit.sources.push_back(source);
					deviceList.push_back(device);
				}
				if (e.load)
				{
					acCircuit::load load;
					load.node = local;
					load.resistance = e.loadResistance;
					load.inductance = e.loadInductance;
					load.capacitance = e.loadCapacitance;
					electricalDevice device;
					device.node = n;
					device.island = i;
					device.type = type;
					device.isSource = false;
					device.local = int(circuit.loads.size());
					device.objectOnNode = objectOnNode;
					device.key = generatorKey{ nodes[n].level, nodes[n].tileIndex, loadsOnTile++ };
					device.switchedOn = setting("on", 1.0) != 0.0;
					device.turns = int(setting(footprint::rotationKey, 0.0));
					device.thermostat = setting("t", std::numeric_limits<double>::quiet_NaN());
					bool heating = true;
					if (!std::isnan(device.thermostat))
					{
						const auto previous = previousThermostats.find(device.key);
						const thermostatState state = previous != previousThermostats.end() ? previous->second : thermostatState();
						thermostatStates[device.key] = state;
						heating = state.heating;
					}
					load.connected = device.switchedOn && heating && !idleLoads.count(device.key);
					circuit.loads.push_back(load);
					deviceList.push_back(device);
				}
			}
		}
		for (int e : island.edges)
		{
			edgeLocal[e] = int(circuit.branches.size());
			circuit.branches.push_back({ nodeLocal[edges[e].a], nodeLocal[edges[e].b], edges[e].vertical });
		}
		solvers.push_back(std::make_unique<acSolver>());
		solvers.back()->setCircuit(circuit);
	}
}

void electricalNetwork::solveAll()
{
	for (std::unique_ptr<acSolver>& solver : solvers)
	{
		//an island with no generator at all can't be anything but dead: skip the work
		if (solver->circuit().sources.empty())
		{
			if (solver->solution().nodeVoltage.size() != solver->circuit().nodeConductors.size())
				solver->solve();   //once, so its result vectors are sized
			continue;
		}
		solver->solve();
	}
	for (electricalDevice& device : deviceList)
	{
		const acSolution& sol = solvers[device.island]->solution();
		device.current = device.isSource ? sol.sourceCurrent[device.local] : sol.loadCurrent[device.local];
		device.power = device.isSource ? sol.sourcePower[device.local] : sol.loadPower[device.local];
	}
	computeHeat();
}

complexd electricalNetwork::nodeVoltage(int _node) const
{
	if (_node < 0 || _node >= int(nodeLocal.size()))
		return complexd(0.0, 0.0);
	const acSolution& sol = solvers[net.nodes()[_node].island]->solution();
	const int local = nodeLocal[_node];
	return local < int(sol.nodeVoltage.size()) ? sol.nodeVoltage[local] : complexd(0.0, 0.0);
}

complexd electricalNetwork::edgeCurrent(int _edge) const
{
	if (_edge < 0 || _edge >= int(edgeLocal.size()))
		return complexd(0.0, 0.0);
	const acSolution& sol = solvers[net.nodes()[net.edges()[_edge].a].island]->solution();
	const int local = edgeLocal[_edge];
	return local < int(sol.branchCurrent.size()) ? sol.branchCurrent[local] : complexd(0.0, 0.0);
}

std::vector<int> electricalNetwork::devicesOnNode(int _node) const
{
	std::vector<int> found;
	for (int d = 0; d < int(deviceList.size()); d++)
	{
		if (deviceList[d].node == _node)
			found.push_back(d);
	}
	return found;
}
