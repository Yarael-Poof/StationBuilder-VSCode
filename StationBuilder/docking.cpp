//docking (world.h): ships latching onto docks on the station, held there while docked, and cargo and liquids moved
//through the dock
#include "world.h"
#include "tileCatalogue.h"
#include "tileNetwork.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
	const double pi = 3.14159265358979323846;
	const char* const sideNames[4] = { "UL", "UR", "LL", "LR" };

	double length(vec2 _v) { return std::sqrt(_v.x * _v.x + _v.y * _v.y); }
	vec2 rotated(vec2 _v, double _angle)
	{
		const double c = std::cos(_angle), s = std::sin(_angle);
		return vec2(_v.x * c - _v.y * s, _v.x * s + _v.y * c);
	}
	double wrapAngle(double _a)
	{
		_a = std::fmod(_a + pi, 2.0 * pi);
		if (_a < 0.0)
			_a += 2.0 * pi;
		return _a - pi;
	}

	//the node of a dock's port of one kind in a network, -1 if it has none (or the network hasn't got it)
	int portNode(const dockPoint& _dock, portInfo::kind _kind, const tileNetwork& _net)
	{
		const tileObject& machine = _dock.object->machine();
		if (!machine.type)
			return -1;
		for (const portInfo& port : footprint::turnedPorts(*machine.type, machine.turns()))
		{
			if (port.carries != _kind)
				continue;
			const int node = _net.nodeAt(_dock.level, machine.origin + port.cell);
			if (node >= 0)
				return node;
		}
		return -1;
	}
}

const std::vector<dockPoint>& world::docksOf(vessel& _vessel)
{
	if (_vessel.dockRevision == _vessel.layout.getRevision())
		return _vessel.dockCache;
	_vessel.dockRevision = _vessel.layout.getRevision();
	_vessel.dockCache.clear();
	for (int lv = 0; lv < _vessel.layout.getLayerListSize(); lv++)
	{
		isometricLevel& level = *_vessel.layout.getIsoLevel(float(lv));
		const int w = int(level.getSizeW()), h = int(level.getSizeH());
		for (int y = 1; y <= h; y++)
		{
			for (int x = 1; x <= w; x++)
			{
				tile& t = level.levelTiles[level.coords2TileIndex(x, y)];
				if (!t.hasAnything())
					continue;
				t.forEachObject([&](tileObject* _o) {
					if (_o->isPart() || !_o->type || !_o->type->dock.isDock)
						return;
					dockPoint d;
					d.owner = &_vessel;
					d.level = lv;
					d.object = _o;
					if (shipParts::placedDock(*_o, sf::Vector2i(x, y), d.tile, d.face))
						_vessel.dockCache.push_back(d);
				});
			}
		}
	}
	return _vessel.dockCache;
}

bool world::findDock(vessel& _vessel, int _level, sf::Vector2i _tile, dockPoint& _found)
{
	for (const dockPoint& d : docksOf(_vessel))
	{
		if (d.level == _level && d.tile == _tile)
		{
			_found = d;
			return true;
		}
	}
	return false;
}

bool world::fittingDock(vessel& _ship, const dockPoint& _dock, dockPoint& _own, std::string* _whyNot)
{
	const std::vector<dockPoint>& own = docksOf(_ship);
	if (own.empty())
	{
		if (_whyNot)
			*_whyNot = "it has no docking collar";
		return false;
	}
	const int wanted = connections::opposite(_dock.face);
	for (const dockPoint& d : own)
	{
		if (d.face == wanted)
		{
			_own = d;
			return true;
		}
	}
	if (_whyNot)
		*_whyNot = std::string("its docking collar would have to face ") + sideNames[wanted] + " (R turns it)";
	return false;
}

bool world::berth(vessel& _ship, const dockPoint& _own, const dockPoint& _dock, vec2& _position, double& _heading, vec2& _outwards,
	sf::Vector2i& _offset, int& _deckOffset)
{
	if (_own.face != connections::opposite(_dock.face))
		return false;
	vessel& host = *_dock.owner;
	host.flight.update();
	_ship.flight.update();
	//the ship's docking point goes on the tile across the dock's face, the two grids square
	const sf::Vector2i step = footprint::step(_dock.face);
	const sf::Vector2i across = _dock.tile + step;
	_offset = across - _own.tile;
	_deckOffset = _dock.level - _own.level;
	_heading = host.flight.heading;
	const vec2 meeting = host.flight.toArea(host.flight.localOfTile(across));
	_position = meeting - rotated(_ship.flight.localOfTile(_own.tile), _heading);
	_outwards = host.flight.directionToArea(vec2(step.x, step.y));
	return true;
}

std::vector<world::dockChoice> world::dockChoices(vessel& _ship)
{
	std::vector<dockChoice> choices;
	for (const std::unique_ptr<vessel>& v : vessels)
	{
		if (v.get() == &_ship || v->flight.canMove())
			continue;   //docks on vessels that keep still (the station)
		for (const dockPoint& d : docksOf(*v))
		{
			dockChoice c;
			c.dock = d;
			char label[160];
			std::snprintf(label, sizeof(label), "%s: %s at %d,%d", v->name.c_str(), d.object->machine().type->name.c_str(), d.tile.x, d.tile.y);
			c.label = label;
			dockPoint own;
			for (const dockLink& l : dockLinks)
			{
				if (l.host == v.get() && l.hostLevel == d.level && l.hostTile == d.tile && l.ship != &_ship)
					c.whyNot = "taken by " + l.ship->name;
			}
			if (c.whyNot.empty())
				fittingDock(_ship, d, own, &c.whyNot);
			c.usable = c.whyNot.empty();
			choices.push_back(c);
		}
	}
	return choices;
}

dockLink* world::linkOf(const vessel& _ship)
{
	for (dockLink& l : dockLinks)
		if (l.ship == &_ship)
			return &l;
	return nullptr;
}

void world::dropLinks(const vessel& _vessel)
{
	dockLinks.erase(std::remove_if(dockLinks.begin(), dockLinks.end(), [&](const dockLink& _l) { return _l.ship == &_vessel || _l.host == &_vessel; }), dockLinks.end());
}

bool world::startDocking(vessel& _ship, const dockPoint& _dock, std::string* _whyNot)
{
	dockPoint own;
	if (!_ship.ship || !fittingDock(_ship, _dock, own, _whyNot))
		return false;
	for (const dockLink& l : dockLinks)
	{
		if (l.host == _dock.owner && l.hostLevel == _dock.level && l.hostTile == _dock.tile && l.ship != &_ship)
		{
			if (_whyNot)
				*_whyNot = "taken by " + l.ship->name;
			return false;
		}
	}
	if (dockLink* existing = linkOf(_ship); existing && existing->docked)
	{
		if (_whyNot)
			*_whyNot = "it is docked already";
		return false;
	}
	dropLinks(_ship);
	dockLink link;
	link.ship = &_ship;
	link.host = _dock.owner;
	link.shipLevel = own.level;
	link.shipTile = own.tile;
	link.hostLevel = _dock.level;
	link.hostTile = _dock.tile;
	vec2 position, outwards;
	double heading = 0.0;
	berth(_ship, own, _dock, position, heading, outwards, link.offset, link.deckOffset);
	_ship.flight.dockAt(position, heading, outwards);
	dockLinks.push_back(link);
	return true;
}

bool world::dockNow(vessel& _ship, const dockPoint& _dock, std::string* _whyNot)
{
	dockPoint own;
	if (!_ship.ship || !fittingDock(_ship, _dock, own, _whyNot))
		return false;
	vec2 position, outwards;
	double heading = 0.0;
	sf::Vector2i offset;
	int deckOffset = 0;
	berth(_ship, own, _dock, position, heading, outwards, offset, deckOffset);
	flightSystem& f = _ship.flight;
	const double distance = length(f.position - position), speed = length(f.velocity), angle = std::abs(wrapAngle(heading - f.heading));
	if (distance > shipTuning::captureDistance || speed > shipTuning::captureSpeed || angle > shipTuning::captureAngle)
	{
		if (_whyNot)
		{
			char text[160];
			std::snprintf(text, sizeof(text), "not lined up yet: %.1f m off (2), %.2f m/s (0.5), %.1f degrees (3)", distance, speed, angle * 180.0 / pi);
			*_whyNot = text;
		}
		return false;
	}
	dropLinks(_ship);
	dockLink link;
	link.ship = &_ship;
	link.host = _dock.owner;
	link.shipLevel = own.level;
	link.shipTile = own.tile;
	link.hostLevel = _dock.level;
	link.hostTile = _dock.tile;
	link.offset = offset;
	link.deckOffset = deckOffset;
	link.docked = true;
	f.docked = true;
	f.order = flightOrder::none;
	dockLinks.push_back(link);
	return true;
}

bool world::relink(vessel& _ship, int _shipLevel, sf::Vector2i _shipTile, vessel& _host, int _hostLevel, sf::Vector2i _hostTile, int _cargo, int _liquid)
{
	dockPoint own, dock;
	if (!findDock(_ship, _shipLevel, _shipTile, own) || !findDock(_host, _hostLevel, _hostTile, dock))
		return false;
	vec2 position, outwards;
	double heading = 0.0;
	dockLink link;
	if (!berth(_ship, own, dock, position, heading, outwards, link.offset, link.deckOffset))
		return false;
	link.ship = &_ship;
	link.host = &_host;
	link.shipLevel = _shipLevel;
	link.shipTile = _shipTile;
	link.hostLevel = _hostLevel;
	link.hostTile = _hostTile;
	link.docked = true;
	link.cargo = _cargo;
	link.liquid = _liquid;
	_ship.flight.docked = true;
	_ship.flight.position = position;
	_ship.flight.heading = heading;
	_ship.flight.velocity = vec2(0.0, 0.0);
	_ship.flight.spin = 0.0;
	dropLinks(_ship);
	dockLinks.push_back(link);
	return true;
}

void world::undock(vessel& _ship)
{
	dockLink* link = linkOf(_ship);
	if (!link)
		return;
	flightSystem& f = _ship.flight;
	if (link->docked)
	{
		//pushed off along the dock's face, then it backs out to the stand-off point and holds there
		vec2 outwards = f.berthOutwards;
		dockPoint dock;
		if (findDock(*link->host, link->hostLevel, link->hostTile, dock))
		{
			const sf::Vector2i step = footprint::step(dock.face);
			outwards = link->host->flight.directionToArea(vec2(step.x, step.y));
		}
		f.docked = false;
		f.velocity = outwards * dockTuning::undockSpeed;
		f.goTo(f.position + outwards * shipTuning::dockStandoff);
	}
	else if (f.order == flightOrder::dock)
		f.holdHere();
	dropLinks(_ship);
}

std::vector<besideVessel> world::beside(vessel& _vessel)
{
	std::vector<besideVessel> list;
	for (const dockLink& l : dockLinks)
	{
		if (!l.docked)
			continue;
		besideVessel b;
		if (l.host == &_vessel)
		{
			b.other = l.ship;
			b.offset = l.offset;
			b.deckOffset = l.deckOffset;
			b.dockTile = l.hostTile;
		}
		else if (l.ship == &_vessel)
		{
			b.other = l.host;
			b.offset = -l.offset;
			b.deckOffset = -l.deckOffset;
			b.dockTile = l.shipTile;
		}
		else
			continue;
		list.push_back(b);
	}
	return list;
}

void world::moveCargo(dockLink& _link, const dockPoint& _shipDock, const dockPoint& _hostDock, double _dt)
{
	_link.cargoRate = 0.0;
	_link.cargoNote.clear();
	if (_link.cargo == 0)
		return;
	vessel& giverVessel = _link.cargo > 0 ? *_link.host : *_link.ship;
	vessel& takerVessel = _link.cargo > 0 ? *_link.ship : *_link.host;
	const dockPoint& giverDock = _link.cargo > 0 ? _hostDock : _shipDock;
	const dockPoint& takerDock = _link.cargo > 0 ? _shipDock : _hostDock;
	//the stores on each side's item network at its dock (the dock itself, if it is a store: the jetty's hold)
	auto storesAt = [](vessel& _v, const dockPoint& _d) {
		std::vector<tileObject*> stores;
		tileObject& dockObject = _d.object->machine();
		if (dockObject.type && dockObject.type->storage.isStore())
			stores.push_back(&dockObject);
		_v.items.update();
		const tileNetwork& net = _v.items.network();
		const int node = portNode(_d, portInfo::kind::items, net);
		if (node < 0)
			return stores;
		const int island = net.nodes()[size_t(node)].island;
		for (const logisticsEndpoint& e : _v.items.endpoints())
		{
			if (!e.store || e.processor || e.node < 0 || net.nodes()[size_t(e.node)].island != island)
				continue;
			if (std::find(stores.begin(), stores.end(), e.store) == stores.end())
				stores.push_back(e.store);
		}
		return stores;
	};
	const std::vector<tileObject*> givers = storesAt(giverVessel, giverDock);
	const std::vector<tileObject*> takers = storesAt(takerVessel, takerDock);
	if (givers.empty() || takers.empty())
	{
		_link.cargoNote = (givers.empty() ? giverVessel.name : takerVessel.name) + " has no containers on the item tubes at its dock";
		return;
	}
	double budget = dockTuning::cargoRate * _dt;
	double moved = 0.0;
	std::string what;
	for (tileObject* giver : givers)
	{
		const std::vector<tileObject::heldMaterial> held = giver->contents();
		for (const tileObject::heldMaterial& h : held)
		{
			const materialInfo* material = findMaterial(h.material);
			if (!material || material->state != materialInfo::phase::solid || h.amount <= 0.0)
				continue;
			double left = h.amount;
			for (tileObject* taker : takers)
			{
				if (budget <= 1e-9 || left <= 1e-9)
					break;
				const double room = storage::room(*taker, *material);
				const double want = std::min({ left, room, budget });
				if (want <= 1e-9)
					continue;
				const double got = storage::take(*giver, h.material, want);
				const double put = storage::put(*taker, *material, got);
				if (put < got)
					storage::put(*giver, *material, got - put);
				left -= put;
				budget -= put;
				moved += put;
				if (put > 0.0 && what.empty())
					what = material->name;
			}
		}
	}
	if (moved < 1e-6)
		moved = 0.0;   //crumbs: nothing
	_link.cargoRate = (_link.cargo > 0 ? 1.0 : -1.0) * moved / _dt;
	if (moved <= 0.0)
	{
		bool anything = false;
		for (tileObject* giver : givers)
			for (const tileObject::heldMaterial& h : giver->contents())
				anything = anything || (h.amount > 1e-9 && findMaterial(h.material) && findMaterial(h.material)->state == materialInfo::phase::solid);
		_link.cargoNote = anything ? takerVessel.name + "'s containers there are full (or their filters refuse it)" : giverVessel.name + "'s containers there are empty";
	}
	else
		_link.cargoNote = what;
}

void world::moveLiquid(dockLink& _link, const dockPoint& _shipDock, const dockPoint& _hostDock, double _dt)
{
	_link.liquidRate = 0.0;
	_link.liquidNote.clear();
	if (_link.liquid == 0)
		return;
	vessel& giverVessel = _link.liquid > 0 ? *_link.host : *_link.ship;
	vessel& takerVessel = _link.liquid > 0 ? *_link.ship : *_link.host;
	const dockPoint& giverDock = _link.liquid > 0 ? _hostDock : _shipDock;
	const dockPoint& takerDock = _link.liquid > 0 ? _shipDock : _hostDock;
	giverVessel.liquid.update();
	takerVessel.liquid.update();
	const int giverNode = portNode(giverDock, portInfo::kind::liquid, giverVessel.liquid.network());
	const int takerNode = portNode(takerDock, portInfo::kind::liquid, takerVessel.liquid.network());
	if (giverNode < 0 || takerNode < 0)
	{
		_link.liquidNote = (giverNode < 0 ? giverVessel.name : takerVessel.name) + " has no pipe at its dock";
		return;
	}
	const materialInfo* liquid = giverVessel.liquid.islandLiquidType(giverVessel.liquid.network().nodes()[size_t(giverNode)].island);
	if (!liquid)
	{
		_link.liquidNote = giverVessel.name + "'s pipes at the dock have nothing to send";
		return;
	}
	const materialInfo* theirs = takerVessel.liquid.islandLiquidType(takerVessel.liquid.network().nodes()[size_t(takerNode)].island);
	if (theirs && theirs != liquid)
	{
		_link.liquidNote = giverVessel.name + " has " + liquid->name + ", " + takerVessel.name + " " + theirs->name + ": they don't mix";
		return;
	}
	const double got = giverVessel.liquid.takeLiquid(giverNode, *liquid, dockTuning::liquidRate * _dt);
	const double put = got > 0.0 ? takerVessel.liquid.putLiquid(takerNode, *liquid, got) : 0.0;
	if (put < got)
		giverVessel.liquid.putLiquid(giverNode, *liquid, got - put);
	const double moved = put < 1e-6 ? 0.0 : put;   //a few drops into a tank that is all but full: nothing
	_link.liquidRate = (_link.liquid > 0 ? 1.0 : -1.0) * moved / _dt;
	if (moved <= 0.0)
		_link.liquidNote = got < 1e-6 ? giverVessel.name + "'s tanks there are empty" : takerVessel.name + "'s tanks there are full";
	else
		_link.liquidNote = liquid->name;
}

void world::tickDocking(double _dt)
{
	for (size_t i = 0; i < dockLinks.size();)
	{
		dockLink& l = dockLinks[i];
		flightSystem& f = l.ship->flight;
		dockPoint own, dock;
		const bool both = findDock(*l.ship, l.shipLevel, l.shipTile, own) && findDock(*l.host, l.hostLevel, l.hostTile, dock);
		//on its way: given another order, or a dock gone, it is off
		if (!l.docked && (f.order != flightOrder::dock || !both))
		{
			dockLinks.erase(dockLinks.begin() + std::ptrdiff_t(i));
			continue;
		}
		//docked but a dock has gone (bulldozed): it floats free
		if (l.docked && !both)
		{
			f.docked = false;
			dockLinks.erase(dockLinks.begin() + std::ptrdiff_t(i));
			continue;
		}
		vec2 position, outwards;
		double heading = 0.0;
		if (!berth(*l.ship, own, dock, position, heading, outwards, l.offset, l.deckOffset))
		{
			f.docked = false;
			dockLinks.erase(dockLinks.begin() + std::ptrdiff_t(i));
			continue;
		}
		if (!l.docked)
		{
			//flying in: the berth may move (the ship's centre of mass changes as it is built on)
			f.berth = position;
			f.berthHeading = heading;
			f.berthOutwards = outwards;
			if (f.readyToLatch())
			{
				l.docked = true;
				f.docked = true;
				f.order = flightOrder::none;
			}
		}
		if (l.docked)
		{
			//held in place, square to the host
			f.docked = true;
			f.position = position;
			f.heading = heading;
			f.velocity = l.host->flight.velocity;
			f.spin = 0.0;
			moveCargo(l, own, dock, _dt);
			moveLiquid(l, own, dock, _dt);
		}
		i++;
	}
}
