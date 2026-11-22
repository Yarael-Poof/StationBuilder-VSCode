#include "tileNetwork.h"
#include "station.h"
#include "tileCatalogue.h"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace
{
	bool isMultiTileMachine(const tileObject& _object) { return _object.type && footprint::isMultiTile(*_object.type); }

	//a one-tile device (light, heater, generator) joins the node of its own tile, so it needs a cable there; a machine
	//bigger than one tile is on the network only on the tile of its power port
	bool electricalHasNode(const tileObject& _object)
	{
		const tileTypeInfo& type = *_object.type;
		if (isMultiTileMachine(_object))
			return footprint::portSidesOnTile(_object, portInfo::kind::power) != 0;
		return type.electrical.isElectrical();
	}
	//cables conduct, and so does a machine's power port (on its face)
	bool electricalConducts(const tileObject& _object)
	{
		return _object.type->electrical.conductor || footprint::portSidesOnTile(_object, portInfo::kind::power) != 0;
	}
	bool electricalLinksVertically(const tileObject& _object) { return _object.type->electrical.conductor && _object.type->electrical.vertical; }
	int electricalConnectsOn(const tileObject& _object)
	{
		if (_object.type->electrical.conductor)
			return connections::sides(*_object.type, _object.allSettingValues());
		return footprint::portSidesOnTile(_object, portInfo::kind::power);
	}
}

const networkMedium electricalMedium = { "electrical", electricalHasNode, electricalConducts, electricalLinksVertically, electricalConnectsOn };

namespace
{
	//a floor holds air; a frame (open structure) doesn't
	bool isFloor(const tileObject& _object) { return _object.type->layer == tileLayer::floor && !_object.type->frame; }
}

const networkMedium thermalMedium = { "thermal", isFloor, isFloor, isFloor };

namespace
{
	//liquid items, and a machine's tiles that have liquid ports (a machine that only processes liquid has ports but no
	//fluid behaviour of its own)
	bool carriesLiquid(const tileObject& _object)
	{
		if (footprint::isMachine(*_object.type) && !_object.type->ports.empty())
			return footprint::portSidesOnTile(_object, portInfo::kind::liquid) != 0;
		return _object.type->fluid.carriesLiquid;
	}
	bool liquidLinksVertically(const tileObject& _object) { return carriesLiquid(_object) && _object.type->fluid.vertical; }
	//a machine through its liquid ports; pipes by their shape; an old-style pump only in line (front and back); tanks and
	//exchangers on every side
	int liquidConnectsOn(const tileObject& _object)
	{
		const tileTypeInfo& type = *_object.type;
		if (!type.ports.empty())
			return footprint::portSidesOnTile(_object, portInfo::kind::liquid);
		if (type.fluid.pump)
			return type.fluid.pumpSide >= 0 ? (1 << type.fluid.pumpSide) | (1 << connections::opposite(type.fluid.pumpSide)) : 0;
		if (type.fluid.tank || type.fluid.exchange > 0.0)
			return connections::all;
		return connections::sides(type, _object.allSettingValues());
	}
	//inside a big machine: a pump pushes from its "in" port's tile to its "out" port's; a tank, exchanger or plain body
	//joins all its liquid port tiles; a machine that only processes liquid keeps them apart (it moves the liquid itself)
	void liquidInternalLinks(const tileObject& _machine, std::vector<std::pair<sf::Vector2i, sf::Vector2i>>& _links)
	{
		const tileTypeInfo& type = *_machine.type;
		if (!type.fluid.carriesLiquid)
			return;
		const std::vector<portInfo> ports = footprint::turnedPorts(type, _machine.turns());
		if (type.fluid.pump)
		{
			sf::Vector2i in(-1, -1), out(-1, -1);
			for (const portInfo& port : ports)
			{
				if (port.carries == portInfo::kind::liquid && port.flow == portInfo::direction::in)
					in = port.cell;
				if (port.carries == portInfo::kind::liquid && port.flow == portInfo::direction::out)
					out = port.cell;
			}
			if (in.x >= 0 && out.x >= 0 && in != out)
				_links.push_back({ in, out });
			return;
		}
		const sf::Vector2i main = footprint::mainCell(type, _machine.turns(), portInfo::kind::liquid);
		for (const portInfo& port : ports)
		{
			if (port.carries == portInfo::kind::liquid && port.cell != main &&
				std::find_if(_links.begin(), _links.end(), [&](const auto& _l) { return _l.second == port.cell; }) == _links.end())
				_links.push_back({ main, port.cell });
		}
	}
}

const networkMedium fluidMedium = { "fluid", carriesLiquid, carriesLiquid, liquidLinksVertically, liquidConnectsOn, liquidInternalLinks };

namespace
{
	//tubes; a machine's tiles with item ports; a one-tile container with no ports (it joins tubes on any side)
	bool carriesItems(const tileObject& _object)
	{
		const tileTypeInfo& type = *_object.type;
		if (type.items.tube)
			return true;
		if (!type.ports.empty())
			return footprint::portSidesOnTile(_object, portInfo::kind::items) != 0;
		return type.storage.isStore() && !footprint::isMultiTile(type);
	}
	bool itemsLinkVertically(const tileObject& _object) { return _object.type->items.tube && _object.type->items.vertical; }
	int itemsConnectOn(const tileObject& _object)
	{
		const tileTypeInfo& type = *_object.type;
		if (type.items.tube)
			return connections::sides(type, _object.allSettingValues());
		if (!type.ports.empty())
			return footprint::portSidesOnTile(_object, portInfo::kind::items);
		return connections::all;
	}
}

const networkMedium itemsMedium = { "items", carriesItems, carriesItems, itemsLinkVertically, itemsConnectOn };

tileNetwork::tileNetwork(const networkMedium& _medium)
	: mediumRules(&_medium)
{
}

namespace
{
	bool sameNode(const networkNode& _a, const networkNode& _b)
	{
		//settings count too: switching a generator off changes the network as much as removing it
		return _a.level == _b.level && _a.tileIndex == _b.tileIndex && _a.conductors == _b.conductors &&
			_a.verticalConductors == _b.verticalConductors && _a.linkSides == _b.linkSides && _a.objects == _b.objects &&
			_a.settings == _b.settings;
	}
}

bool tileNetwork::update(station& _station)
{
	if (built && _station.getRevision() == revision)
		return false;
	const bool first = !built;
	const std::vector<networkNode> oldNodes = std::move(nodeList);
	const std::vector<networkEdge> oldEdges = std::move(edgeList);
	//the content files reloaded: every definition may have new numbers, though the objects point at the same ones
	const bool newDefinitions = builtGeneration != catalogueGeneration();
	builtGeneration = catalogueGeneration();
	build(_station);

	//most edits (floors, walls, other media) leave this network exactly as it was: say so, so the
	//simulation can keep what it has instead of starting again
	bool same = !first && !newDefinitions && oldNodes.size() == nodeList.size() && oldEdges.size() == edgeList.size();
	for (size_t n = 0; same && n < nodeList.size(); n++)
		same = sameNode(oldNodes[n], nodeList[n]);
	for (size_t e = 0; same && e < edgeList.size(); e++)
		same = oldEdges[e].a == edgeList[e].a && oldEdges[e].b == edgeList[e].b && oldEdges[e].vertical == edgeList[e].vertical && oldEdges[e].internal == edgeList[e].internal;
	if (!same)
		std::cout << "[NET] " << mediumRules->name << ": " << nodeList.size() << " nodes, " << edgeList.size() << " links, "
			<< islandList.size() << " networks (" << lastBuildMs << " ms)\n";
	return !same;
}

int tileNetwork::nodeAt(int _level, sf::Vector2i _tile) const
{
	if (_level < 0 || _level >= int(nodeOfTile.size()))
		return -1;
	const sf::Vector2i size = levelSize[_level];
	if (_tile.x < 1 || _tile.y < 1 || _tile.x > size.x || _tile.y > size.y)
		return -1;
	const int index = (_tile.x - 1) + size.x * (_tile.y - 1);
	if (index >= int(nodeOfTile[_level].size()))
		return -1;
	return nodeOfTile[_level][index];
}

void tileNetwork::build(station& _station)
{
	const auto start = std::chrono::steady_clock::now();
	nodeList.clear();
	edgeList.clear();
	islandList.clear();
	nodeOfTile.clear();
	levelSize.clear();
	revision = _station.getRevision();
	built = true;

	const int levels = _station.getLayerListSize();
	nodeOfTile.resize(levels);
	levelSize.resize(levels);

	//nodes: every tile holding at least one object of this medium, in level then tile order
	std::vector<std::pair<int, const tileObject*>> machines;   //big machines (level, the machine), for their insides
	for (int level = 0; level < levels; level++)
	{
		isometricLevel* isoLevel = _station.getIsoLevel(float(level));
		const int width = int(isoLevel->getSizeW());
		const int height = int(isoLevel->getSizeH());
		const int tileCount = int(isoLevel->levelTiles.size());
		levelSize[level] = sf::Vector2i(width, height);
		nodeOfTile[level].assign(tileCount, -1);
		if (width <= 0)
			continue;

		for (int i = 0; i < tileCount; i++)
		{
			//most tiles have nothing of this medium: check before building a node
			bool any = false;
			isoLevel->levelTiles[i].forEachObject([&](tileObject* _object) {
				any = any || (_object->type && mediumRules->hasNode(*_object));
				//a big machine (kept on its front tile): its insides may join its tiles, once the nodes are all made
				if (mediumRules->internalLinks && _object->type && !_object->isPart() && footprint::isMultiTile(*_object->type))
					machines.push_back({ level, _object });
			});
			if (!any)
				continue;
			networkNode node;
			isoLevel->levelTiles[i].forEachObject([&](tileObject* _object) {
				if (!_object->type || !mediumRules->hasNode(*_object))
					return;
				node.objects.push_back(_object->type);
				node.settings.push_back(_object->allSettingValues());
				if (mediumRules->conducts(*_object))
				{
					node.conductors++;
					node.linkSides |= mediumRules->connectsOn ? mediumRules->connectsOn(*_object) : 15;
				}
				if (mediumRules->linksVertically && mediumRules->linksVertically(*_object))
					node.verticalConductors++;
			});
			node.level = level;
			node.tileIndex = i;
			node.tile = sf::Vector2i(i % width + 1, i / width + 1);
			nodeOfTile[level][i] = int(nodeList.size());
			nodeList.push_back(node);
		}
	}

	//edges: each conducting node looks at its +x and +y neighbours, so every pair is found once.
	//neighbours come later in tile order, so a < b
	for (int n = 0; n < int(nodeList.size()); n++)
	{
		const networkNode& node = nodeList[n];
		if (node.conductors == 0)
			continue;
		//x + 1 is across this tile's lower right side (3), which is the neighbour's upper left (0); y + 1 is across
		//its lower left (2), the neighbour's upper right (1). both have to connect on that side
		const int sideTowards[2] = { 3, 2 };
		const sf::Vector2i steps[2] = { sf::Vector2i(1, 0), sf::Vector2i(0, 1) };
		for (int k = 0; k < 2; k++)
		{
			const int other = nodeAt(node.level, node.tile + steps[k]);
			if (other < 0 || nodeList[other].conductors == 0)
				continue;
			const int side = sideTowards[k];
			if ((node.linkSides & (1 << side)) && (nodeList[other].linkSides & (1 << (3 - side))))
				edgeList.push_back(networkEdge{ n, other, false });
		}
	}

	//inside big machines: the tiles its insides join (a big tank's ports, a pump's in and out)
	for (const auto& [level, machine] : machines)
	{
		std::vector<std::pair<sf::Vector2i, sf::Vector2i>> links;
		mediumRules->internalLinks(*machine, links);
		for (const auto& [from, to] : links)
		{
			const int a = nodeAt(level, machine->origin + from), b = nodeAt(level, machine->origin + to);
			if (a >= 0 && b >= 0 && a != b)
			{
				networkEdge edge{ std::min(a, b), std::max(a, b), false };
				edge.internal = true;
				edgeList.push_back(edge);
			}
		}
	}

	//vertical edges: up from each node that links vertically to the same tile on the next level, if that tile
	//links vertically too. nodes are in level order, so the upper node always has the higher index
	for (int n = 0; n < int(nodeList.size()); n++)
	{
		if (nodeList[n].verticalConductors == 0)
			continue;
		const int above = nodeAt(nodeList[n].level + 1, nodeList[n].tile);
		if (above < 0 || nodeList[above].verticalConductors == 0)
			continue;
		nodeList[n].edgeUp = int(edgeList.size());
		nodeList[above].edgeDown = int(edgeList.size());
		edgeList.push_back(networkEdge{ n, above, true });
	}

	//islands: flood fill along the edges
	std::vector<std::vector<int>> linked(nodeList.size());
	for (const networkEdge& edge : edgeList)
	{
		linked[edge.a].push_back(edge.b);
		linked[edge.b].push_back(edge.a);
	}
	for (int n = 0; n < int(nodeList.size()); n++)
	{
		if (nodeList[n].island >= 0)
			continue;
		const int islandIndex = int(islandList.size());
		islandList.emplace_back();
		std::vector<int> toVisit{ n };
		nodeList[n].island = islandIndex;
		while (!toVisit.empty())
		{
			const int current = toVisit.back();
			toVisit.pop_back();
			islandList[islandIndex].nodes.push_back(current);
			for (int next : linked[current])
			{
				if (nodeList[next].island < 0)
				{
					nodeList[next].island = islandIndex;
					toVisit.push_back(next);
				}
			}
		}
	}
	for (int e = 0; e < int(edgeList.size()); e++)
		islandList[nodeList[edgeList[e].a].island].edges.push_back(e);

	lastBuildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
