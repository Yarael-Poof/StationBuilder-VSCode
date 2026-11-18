#include "thermalNetwork.h"
#include "station.h"
#include "tileCatalogue.h"
#include "electricalNetwork.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>

namespace tileSides
{
	sf::Vector2i neighbourStep(int _side)
	{
		switch (_side)
		{
		case 0: return sf::Vector2i(-1, 0);   //upper left
		case 1: return sf::Vector2i(0, -1);   //upper right
		case 2: return sf::Vector2i(0, 1);    //lower left
		default: return sf::Vector2i(1, 0);   //lower right
		}
	}

	int opposite(int _side)
	{
		return 3 - _side;
	}

	const char* name(int _side)
	{
		static const char* names[4] = { "upper left", "upper right", "lower left", "lower right" };
		return _side >= 0 && _side < 4 ? names[_side] : "?";
	}
}

namespace
{
	//W/K through the walls standing on one tile edge: walls in series (two back to back pass half as much)
	double wallsConductance(const std::vector<double>& _walls)
	{
		double resistance = 0.0;
		for (double g : _walls)
			resistance += 1.0 / std::max(g, 1e-9);
		return 1.0 / resistance;
	}
}

thermalNetwork::thermalNetwork(station& _station, const tileHeatSource* _heatSource)
	: stationRef(&_station), net(thermalMedium)
{
	addHeatSource(_heatSource);
}

void thermalNetwork::addHeatSource(const tileHeatSource* _heatSource)
{
	if (_heatSource && std::find(heatSources.begin(), heatSources.end(), _heatSource) == heatSources.end())
		heatSources.push_back(_heatSource);
}

void thermalNetwork::refresh()
{
	if (built && stationRef->getRevision() == seenRevision)
		return;
	seenRevision = stationRef->getRevision();

	//remember every tile's temperature, so they carry over whatever the network does
	std::map<std::pair<int, int>, double> before;
	for (int n = 0; n < int(net.nodes().size()) && n < int(temps.size()); n++)
		before[{ net.nodes()[n].level, net.nodes()[n].tileIndex }] = temps[n];
	const bool networkChanged = net.update(*stationRef) || !built;
	built = true;

	const std::vector<networkNode>& nodes = net.nodes();
	const std::vector<networkEdge>& edges = net.edges();
	if (networkChanged)
	{
		temps.assign(nodes.size(), thermalTuning::startTemperature);
		for (int n = 0; n < int(nodes.size()); n++)
		{
			const auto found = before.find({ nodes[n].level, nodes[n].tileIndex });
			if (found != before.end())
				temps[n] = found->second;
		}
	}

	//what each tile holds, and what is on each of its sides. walls can change without the network changing,
	//so this is worked out again on any station change, and the matrix only redone if something differs.
	//first one pass over every tile: its heat capacity and the wall (if any) on each of its edges
	const int levels = stationRef->getLayerListSize();
	std::vector<std::vector<double>> tileCapacity(levels);
	std::vector<std::vector<std::array<double, 4>>> tileWalls(levels);   //W/K of the wall on each edge, 0 = none
	std::vector<std::vector<double>> tileDuct(levels);                   //W/K of its air duct riser, 0 = none
	for (int lv = 0; lv < levels; lv++)
	{
		const std::vector<tile>& tiles = stationRef->getIsoLevel(float(lv))->levelTiles;
		tileCapacity[lv].assign(tiles.size(), 0.0);
		tileWalls[lv].assign(tiles.size(), { 0.0, 0.0, 0.0, 0.0 });
		tileDuct[lv].assign(tiles.size(), 0.0);
		for (int i = 0; i < int(tiles.size()); i++)
		{
			tiles[i].forEachObject([&](tileObject* _object) {
				if (!_object->type)
					return;
				tileCapacity[lv][i] += _object->type->thermal.heatCapacity / footprint::cellCount(*_object->type);   //a big machine's, shared over its tiles
				tileDuct[lv][i] = std::max(tileDuct[lv][i], _object->type->thermal.ductConductance);
				if (_object->type->layer == tileLayer::wall && _object->wallEdge >= 0 && _object->wallEdge < 4)
					tileWalls[lv][i][_object->wallEdge] = std::max(_object->type->thermal.wallConductance, 1e-9);
			});
		}
	}

	std::vector<double> newCapacity(nodes.size(), 0.0);
	std::vector<double> newSpaceG(nodes.size(), 0.0);
	std::vector<std::array<thermalSide, 4>> newSides(nodes.size());
	for (int n = 0; n < int(nodes.size()); n++)
	{
		const networkNode& node = nodes[n];
		isometricLevel* level = stationRef->getIsoLevel(float(node.level));
		newCapacity[n] = std::max(tileCapacity[node.level][node.tileIndex], 1.0);   //never zero, so the matrix stays solvable
		for (int s = 0; s < 4; s++)
		{
			//the walls on this edge: this tile's own, and the neighbouring tile's facing one (floored or not)
			std::vector<double> walls;
			if (tileWalls[node.level][node.tileIndex][s] > 0.0)
				walls.push_back(tileWalls[node.level][node.tileIndex][s]);
			const sf::Vector2i across = node.tile + tileSides::neighbourStep(s);
			if (level->isInLevel(across))
			{
				const double facing = tileWalls[node.level][level->coords2TileIndex(across.x, across.y)][tileSides::opposite(s)];
				if (facing > 0.0)
					walls.push_back(facing);
			}
			thermalSide& side = newSides[n][s];
			side.neighbour = net.nodeAt(node.level, across);
			side.walls = int(walls.size());
			if (side.walls > 0)
				side.conductance = wallsConductance(walls);
			else
				side.conductance = side.neighbour >= 0 ? thermalTuning::openEdge : thermalTuning::openToSpace;
			if (side.neighbour < 0)
				newSpaceG[n] += side.conductance;
		}
	}
	std::vector<double> newEdgeG(edges.size(), thermalTuning::deck);
	nodeDuct.assign(nodes.size(), 0.0);
	for (int n = 0; n < int(nodes.size()); n++)
		nodeDuct[n] = tileDuct[nodes[n].level][nodes[n].tileIndex];
	for (int e = 0; e < int(edges.size()); e++)
	{
		//through the floor: the deck's slow conduction, or the air moving through a pair of air duct risers
		if (edges[e].vertical)
		{
			const double duct = std::min(nodeDuct[edges[e].a], nodeDuct[edges[e].b]);
			if (duct > 0.0)
				newEdgeG[e] = duct;
			continue;
		}
		//b is a's neighbour to the lower right (x + 1) or lower left (y + 1), since edges run towards higher tiles
		const sf::Vector2i step = nodes[edges[e].b].tile - nodes[edges[e].a].tile;
		const int sideOfA = step == sf::Vector2i(1, 0) ? 3 : 2;
		newEdgeG[e] = newSides[edges[e].a][sideOfA].conductance;
		newSides[edges[e].a][sideOfA].edge = e;
		newSides[edges[e].b][tileSides::opposite(sideOfA)].edge = e;
	}

	if (networkChanged || newCapacity != capacity || newSpaceG != spaceG || newEdgeG != edgeG)
		factorised = false;
	capacity.swap(newCapacity);
	spaceG.swap(newSpaceG);
	edgeG.swap(newEdgeG);
	sides.swap(newSides);
	heatIn.assign(nodes.size(), 0.0);
}

void thermalNetwork::gatherHeat()
{
	heatIn.assign(net.nodes().size(), 0.0);
	//each source's heat goes to the floored tile it is on (heat on a tile with no floor goes nowhere)
	for (const tileHeatSource* source : heatSources)
	{
		source->forEachTileHeat([&](int _level, sf::Vector2i _tile, double _watts) {
			const int n = net.nodeAt(_level, _tile);
			if (n >= 0)
				heatIn[n] += _watts;
		});
	}
}

void thermalNetwork::update()
{
	refresh();
	gatherHeat();   //so the inspector shows the heat coming in even while paused
}

void thermalNetwork::tick(double _dt)
{
	refresh();
	gatherHeat();
	const int count = int(net.nodes().size());
	if (count == 0 || _dt <= 0.0)
		return;

	if (!factorised || _dt != factorDt)
	{
		solver.reset(count);
		for (int n = 0; n < count; n++)
			solver.addShunt(n, capacity[n] / _dt + spaceG[n]);
		for (int e = 0; e < int(net.edges().size()); e++)
			solver.addBranch(net.edges()[e].a, net.edges()[e].b, edgeG[e]);
		factorised = solver.factorise();
		factorDt = _dt;
		factorCount++;
		if (!factorised)
		{
			std::cout << "[HEAT] could not factorise the heat matrix\n";
			return;
		}
	}

	std::vector<double> rhs(count);
	for (int n = 0; n < count; n++)
		rhs[n] = capacity[n] / _dt * temps[n] + heatIn[n] + spaceG[n] * thermalTuning::outsideTemperature;
	std::vector<double> next;
	if (solver.solve(rhs, next))
		temps.swap(next);
}

double thermalNetwork::temperature(int _node) const
{
	return _node >= 0 && _node < int(temps.size()) ? temps[_node] : std::numeric_limits<double>::quiet_NaN();
}

double thermalNetwork::temperatureAt(int _level, sf::Vector2i _tile) const
{
	return temperature(net.nodeAt(_level, _tile));
}

void thermalNetwork::setTemperature(int _node, double _celsius)
{
	if (_node >= 0 && _node < int(temps.size()))
		temps[_node] = _celsius;
}

double thermalNetwork::heatCapacity(int _node) const
{
	return _node >= 0 && _node < int(capacity.size()) ? capacity[_node] : 0.0;
}

double thermalNetwork::heatInput(int _node) const
{
	return _node >= 0 && _node < int(heatIn.size()) ? heatIn[_node] : 0.0;
}

double thermalNetwork::edgeConductance(int _edge) const
{
	return _edge >= 0 && _edge < int(edgeG.size()) ? edgeG[_edge] : 0.0;
}

double thermalNetwork::ductConductance(int _node) const
{
	return _node >= 0 && _node < int(nodeDuct.size()) ? nodeDuct[_node] : 0.0;
}

bool thermalNetwork::ductLink(int _edge) const
{
	if (_edge < 0 || _edge >= int(net.edges().size()) || !net.edges()[_edge].vertical)
		return false;
	return ductConductance(net.edges()[_edge].a) > 0.0 && ductConductance(net.edges()[_edge].b) > 0.0;
}

double thermalNetwork::heatFlow(int _edge) const
{
	if (_edge < 0 || _edge >= int(edgeG.size()))
		return 0.0;
	const networkEdge& edge = net.edges()[_edge];
	return edgeG[_edge] * (temps[edge.a] - temps[edge.b]);
}

const thermalSide& thermalNetwork::side(int _node, int _side) const
{
	return sides[_node][_side];
}

double thermalNetwork::spaceConductance(int _node) const
{
	return _node >= 0 && _node < int(spaceG.size()) ? spaceG[_node] : 0.0;
}

double thermalNetwork::netHeatFlow(int _node) const
{
	if (_node < 0 || _node >= int(temps.size()))
		return 0.0;
	double flow = heatIn[_node] - spaceG[_node] * (temps[_node] - thermalTuning::outsideTemperature);
	for (int e = 0; e < int(net.edges().size()); e++)
	{
		const networkEdge& edge = net.edges()[e];
		if (edge.a == _node)
			flow -= heatFlow(e);
		else if (edge.b == _node)
			flow += heatFlow(e);
	}
	return flow;
}

double thermalNetwork::totalHeatInput() const
{
	double total = 0.0;
	for (double q : heatIn)
		total += q;
	return total;
}

double thermalNetwork::totalLossToSpace() const
{
	double total = 0.0;
	for (int n = 0; n < int(temps.size()); n++)
		total += spaceG[n] * (temps[n] - thermalTuning::outsideTemperature);
	return total;
}

double thermalNetwork::minimumTemperature() const
{
	return temps.empty() ? std::numeric_limits<double>::quiet_NaN() : *std::min_element(temps.begin(), temps.end());
}

double thermalNetwork::maximumTemperature() const
{
	return temps.empty() ? std::numeric_limits<double>::quiet_NaN() : *std::max_element(temps.begin(), temps.end());
}

double thermalNetwork::averageTemperature() const
{
	if (temps.empty())
		return std::numeric_limits<double>::quiet_NaN();
	double total = 0.0;
	for (double t : temps)
		total += t;
	return total / double(temps.size());
}
