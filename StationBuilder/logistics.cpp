#include "logistics.h"
#include "station.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	//what an endpoint can give: a container's contents, a machine's output buffer (its solids: liquids leave by pipe)
	std::vector<std::pair<std::string, double>> giveable(const logisticsEndpoint& _from)
	{
		std::vector<std::pair<std::string, double>> list;
		for (const tileObject::heldMaterial& held : _from.processor ? _from.store->outputs() : _from.store->contents())
		{
			const materialInfo* material = findMaterial(held.material);
			if (held.amount > 1e-9 && (!_from.processor || (material && material->state == materialInfo::phase::solid)))
				list.push_back({ held.material, held.amount });
		}
		return list;
	}
	//kg of a solid an endpoint would take: a container's room (and its filter), a machine's room for an input of its recipe
	double roomFor(const logisticsEndpoint& _to, const materialInfo& _material)
	{
		if (_material.state != materialInfo::phase::solid)
			return 0.0;
		if (_to.processor)
			return recipes::inputRoom(*_to.store, _material);
		if (!_to.store->accepts().empty() && _to.store->accepts() != _material.key)
			return 0.0;
		return storage::room(*_to.store, _material);
	}
	double give(const logisticsEndpoint& _from, const std::string& _material, double _kg)
	{
		return _from.processor ? recipes::takeOutput(*_from.store, _material, _kg) : storage::take(*_from.store, _material, _kg);
	}
	double receive(const logisticsEndpoint& _to, const materialInfo& _material, double _kg)
	{
		return _to.processor ? recipes::putInput(*_to.store, _material, _kg) : storage::put(*_to.store, _material, _kg);
	}
}

logisticsNetwork::logisticsNetwork(station& _station)
	: stationRef(&_station), net(itemsMedium)
{
}

void logisticsNetwork::update()
{
	refresh();
}

void logisticsNetwork::refresh()
{
	if (built && stationRef->getRevision() == seenRevision)
		return;
	seenRevision = stationRef->getRevision();
	net.update(*stationRef);
	built = true;
	bindEndpoints();
}

void logisticsNetwork::bindEndpoints()
{
	endpointList.clear();
	const std::vector<networkNode>& nodes = net.nodes();
	for (int n = 0; n < int(nodes.size()); n++)
	{
		const networkNode& node = nodes[n];
		stationRef->getIsoLevel(float(node.level))->levelTiles[node.tileIndex].forEachObject([&](tileObject* _object) {
			if (!_object->type || _object->type->items.tube || !itemsMedium.hasNode(*_object))
				return;
			tileObject& machine = _object->machine();
			const bool processor = recipes::isProcessor(*machine.type);
			if (!storage::isStore(machine) && !processor)
				return;
			if (_object->type->ports.empty())
			{
				//a one-tile container with no ports: both ways, on every side
				logisticsEndpoint endpoint;
				endpoint.node = n;
				endpoint.store = &machine;
				endpoint.tile = node.tile;
				endpointList.push_back(endpoint);
				return;
			}
			for (const portInfo& port : footprint::turnedPorts(*_object->type, machine.turns()))
			{
				if (port.carries != portInfo::kind::items || port.cell != _object->cell)
					continue;
				logisticsEndpoint endpoint;
				endpoint.node = n;
				endpoint.store = &machine;
				endpoint.processor = processor;
				endpoint.flow = port.flow;
				endpoint.tile = node.tile;
				endpoint.side = port.side;
				//a machine's both-ways port: an in (its inputs) and an out (its products), so it works with buffers too
				if (processor && port.flow == portInfo::direction::both)
				{
					endpoint.flow = portInfo::direction::in;
					endpointList.push_back(endpoint);
					endpoint.flow = portInfo::direction::out;
				}
				endpointList.push_back(endpoint);
			}
		});
	}
}

void logisticsNetwork::tick(double _dt)
{
	refresh();
	const std::vector<networkNode>& nodes = net.nodes();
	const std::vector<networkIsland>& islands = net.islands();
	islandList.assign(islands.size(), logisticsIsland());
	for (logisticsEndpoint& endpoint : endpointList)
	{
		endpoint.rate = 0.0;
		endpoint.blocked = false;
	}
	if (_dt <= 0.0)
		return;

	//each network's rate: its slowest tube (two ports touching with no tube: the direct rate)
	for (int i = 0; i < int(islands.size()); i++)
	{
		double slowest = std::numeric_limits<double>::infinity();
		for (int n : islands[i].nodes)
			for (const tileTypeInfo* type : nodes[n].objects)
				if (type->items.tube)
					slowest = std::min(slowest, type->items.rate);
		islandList[i].capacity = std::isfinite(slowest) ? slowest : logisticsTuning::directRate;
	}

	//the endpoints of each network, by which way they face
	std::vector<std::vector<int>> outs(islands.size()), ins(islands.size()), boths(islands.size());
	for (int e = 0; e < int(endpointList.size()); e++)
	{
		const int island = nodes[endpointList[e].node].island;
		switch (endpointList[e].flow)
		{
		case portInfo::direction::out: outs[island].push_back(e); break;
		case portInfo::direction::in: ins[island].push_back(e); break;
		default: boths[island].push_back(e); break;
		}
	}
	for (int i = 0; i < int(islands.size()); i++)
	{
		logisticsIsland& state = islandList[i];
		double budget = state.capacity * _dt;   //kg it can still move this tick
		//moves what it can from one endpoint's container to another's; true once the network's rate is used up
		auto move = [&](int _from, int _to) {
			const logisticsEndpoint& source = endpointList[_from];
			const logisticsEndpoint& target = endpointList[_to];
			if (source.store == target.store)
				return false;
			for (const auto& [key, held] : giveable(source))
			{
				const materialInfo* material = findMaterial(key);
				if (!material)
					continue;
				const double amount = std::min({ budget, held, roomFor(target, *material) });
				if (amount <= 1e-9)
					continue;
				const double taken = give(source, key, amount);
				const double put = receive(target, *material, taken);
				if (put < taken - 1e-12)
				{
					//what didn't fit goes back (it always should fit)
					if (source.processor)
						source.store->outputsToChange().push_back({ key, taken - put });
					else
						storage::put(*source.store, *material, taken - put);
				}
				budget -= put;
				endpointList[_from].rate -= put / _dt;
				endpointList[_to].rate += put / _dt;
				state.moving += put / _dt;
				auto found = std::find_if(state.materials.begin(), state.materials.end(), [&](const auto& _m) { return _m.first == material; });
				if (found == state.materials.end())
					state.materials.push_back({ material, put / _dt });
				else
					found->second += put / _dt;
				if (budget <= 1e-9)
					return true;
			}
			return false;
		};
		//each phase: every sender to every receiver, the first of each changing every tick so it evens out
		const std::vector<std::pair<const std::vector<int>*, const std::vector<int>*>> phases = {
			{ &outs[i], &ins[i] }, { &outs[i], &boths[i] }, { &boths[i], &ins[i] } };
		bool spent = budget <= 1e-9;
		for (const auto& [senders, receivers] : phases)
		{
			for (size_t s = 0; s < senders->size() && !spent; s++)
			{
				const int from = (*senders)[(s + tickCount) % senders->size()];
				for (size_t r = 0; r < receivers->size() && !spent; r++)
					spent = move(from, (*receivers)[(r + tickCount) % receivers->size()]);
			}
		}

		//outputs holding something none of the receivers here will take (no room, or a filter that keeps it out). an
		//output with no receivers on its network at all isn't blocked, just not connected to anything yet
		for (int from : outs[i])
		{
			const logisticsEndpoint& source = endpointList[from];
			const std::vector<std::pair<std::string, double>> held = giveable(source);
			if (held.empty() || source.rate < 0.0)
				continue;
			bool receivers = false;
			for (const std::vector<int>* list : { &ins[i], &boths[i] })
				for (int to : *list)
					receivers = receivers || endpointList[to].store != source.store;
			if (!receivers)
				continue;
			bool somewhere = false;
			for (const auto& [key, amount] : held)
			{
				const materialInfo* material = findMaterial(key);
				for (const std::vector<int>* list : { &ins[i], &boths[i] })
					for (int to : *list)
						somewhere = somewhere || (material && endpointList[to].store != source.store && roomFor(endpointList[to], *material) > 1e-9);
			}
			if (!somewhere)
			{
				endpointList[from].blocked = true;
				state.blocked++;
				if (state.blockedWhat.empty())
				{
					const materialInfo* first = findMaterial(held.front().first);
					state.blockedWhat = first ? first->name : held.front().first;
				}
			}
		}
	}
	tickCount++;
}

const logisticsIsland& logisticsNetwork::island(int _island) const
{
	static const logisticsIsland none;
	return _island >= 0 && _island < int(islandList.size()) ? islandList[_island] : none;
}

double logisticsNetwork::rateOf(const tileObject& _store) const
{
	double rate = 0.0;
	for (const logisticsEndpoint& endpoint : endpointList)
		if (endpoint.store == &_store.machine())
			rate += endpoint.rate;
	return rate;
}

double logisticsNetwork::totalMoving() const
{
	double total = 0.0;
	for (const logisticsIsland& island : islandList)
		total += island.moving;
	return total;
}
