#include "flight.h"
#include "station.h"
#include "electricalNetwork.h"
#include "fluidNetwork.h"
#include "tileCatalogue.h"
#include "tileNetwork.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

namespace
{
	const double pi = 3.14159265358979323846;

	double wrapAngle(double _a)
	{
		_a = std::fmod(_a + pi, 2.0 * pi);
		if (_a < 0.0)
			_a += 2.0 * pi;
		return _a - pi;
	}
	double length(vec2 _v) { return std::sqrt(_v.x * _v.x + _v.y * _v.y); }
	double dot(vec2 _a, vec2 _b) { return _a.x * _b.x + _a.y * _b.y; }
	double cross(vec2 _a, vec2 _b) { return _a.x * _b.y - _a.y * _b.x; }
	vec2 rotated(vec2 _v, double _angle)
	{
		const double c = std::cos(_angle), s = std::sin(_angle);
		return vec2(_v.x * c - _v.y * s, _v.x * s + _v.y * c);
	}
	double sign(double _x) { return _x < 0.0 ? -1.0 : 1.0; }

	//what an object holds, in kg (liquids are held in litres)
	double heldKg(const tileObject& _object, const std::string& _only = "")
	{
		double kg = 0.0;
		for (const tileObject::heldMaterial& held : _object.contents())
		{
			if (!_only.empty() && held.material != _only)
				continue;
			const materialInfo* material = findMaterial(held.material);
			if (!material)
				continue;
			kg += material->state == materialInfo::phase::liquid ? held.amount * material->kilogramsPerLitre() : held.amount;
		}
		return kg;
	}
}

flightSystem::flightSystem(station& _station, electricalNetwork* _power, fluidNetwork* _liquid, bool _canMove)
	: stationRef(&_station), power(_power), liquids(_liquid), movable(_canMove)
{
}

void flightSystem::update()
{
	if (!built || stationRef->getRevision() != seenRevision)
		rebuild();
}

int flightSystem::findDevice(const tileObject& _object, int _level, sf::Vector2i _tile) const
{
	if (!power)
		return -1;
	const tileTypeInfo& type = *_object.type;
	//its power comes in at its power port's tile (a one-tile item: its own tile; only machines know their origin)
	sf::Vector2i at = footprint::isMachine(type) ? _object.origin : _tile;
	if (footprint::isMultiTile(type))
		at = _object.origin + footprint::mainCell(type, _object.turns(), portInfo::kind::power);
	const int node = power->network().nodeAt(_level, at);
	if (node < 0)
		return -1;
	//its load is the same-numbered one among the loads of its type on that tile
	isometricLevel* level = stationRef->getIsoLevel(float(_level));
	if (!level->isInLevel(at))
		return -1;
	int before = 0;
	bool seen = false;
	level->levelTiles[level->coords2TileIndex(at.x, at.y)].forEachObject([&](tileObject* _o) {
		if (seen || _o->type != &type)
			return;
		if (&_o->machine() == &_object)
			seen = true;
		else
			before++;
	});
	for (int d : power->devicesOnNode(node))
	{
		const electricalDevice& device = power->devices()[d];
		if (!device.isSource && device.type == &type && before-- == 0)
			return d;
	}
	return -1;
}

double flightSystem::powerShare(const part& _part) const
{
	if (_part.watts <= 0.0)
		return 1.0;
	if (!power)
		return 1.0;   //no electrical simulation: as if powered
	if (_part.device < 0 || _part.device >= int(power->devices().size()))
		return 0.0;
	return std::clamp(power->devices()[_part.device].power.real() / _part.watts, 0.0, 1.0);
}

void flightSystem::setIdle(const part& _part, bool _idle)
{
	if (power && _part.device >= 0 && _part.device < int(power->devices().size()))
		power->setLoadIdle(power->devices()[_part.device].key, _idle);
}

void flightSystem::rebuild()
{
	built = true;
	seenRevision = stationRef->getRevision();
	engines.clear();
	thrusterParts.clear();
	gyros.clear();
	helms.clear();
	holders.clear();
	cells.clear();

	struct massPoint { vec2 at; double kg; tileObject* holder; };
	std::vector<massPoint> points;
	std::map<std::pair<int, int>, outlineCell::kind> seenFromAbove;
	auto mark = [&](int _x, int _y, outlineCell::kind _kind) {
		auto found = seenFromAbove.find({ _x, _y });
		//a floor shows over anything; equipment over a frame
		if (found == seenFromAbove.end() || int(_kind) < int(found->second))
			seenFromAbove[{ _x, _y }] = _kind;
	};
	const double T = shipTuning::tileMetres;
	for (int lv = 0; lv < stationRef->getLayerListSize(); lv++)
	{
		isometricLevel& level = *stationRef->getIsoLevel(float(lv));
		const int w = int(level.getSizeW()), h = int(level.getSizeH());
		for (int y = 1; y <= h; y++)
		{
			for (int x = 1; x <= w; x++)
			{
				tile& t = level.levelTiles[level.coords2TileIndex(x, y)];
				if (!t.hasAnything())
					continue;
				if (t.hasFloor())
					mark(x, y, outlineCell::kind::floor);
				else if (t.hasFrame())
					mark(x, y, outlineCell::kind::frame);
				t.forEachObject([&](tileObject* _o) {
					if (!_o->type || _o->type->layer == tileLayer::background || _o->isPart())
						return;
					const tileTypeInfo& type = *_o->type;
					const bool machine = footprint::isMachine(type);
					const sf::Vector2i origin = machine ? _o->origin : sf::Vector2i(x, y);
					const sf::Vector2i size = machine ? footprint::turnedSize(type, _o->turns()) : sf::Vector2i(1, 1);
					//its middle, in metres from the grid's corner
					const vec2 at((origin.x - 1 + size.x * 0.5) * T, (origin.y - 1 + size.y * 0.5) * T);
					const bool holds = type.fluid.tank || type.storage.isStore() || !type.recipes.empty();
					points.push_back({ at, shipParts::kg(type), holds ? _o : nullptr });
					if (holds)
						holders.push_back(_o);
					if (type.outside)
					{
						for (int cy = 0; cy < size.y; cy++)
							for (int cx = 0; cx < size.x; cx++)
								mark(origin.x + cx, origin.y + cy, outlineCell::kind::equipment);
					}

					part p;
					p.object = _o;
					p.level = lv;
					p.local = at;
					p.tile = sf::Vector2i(x, y);
					p.watts = electrical::ratedLoadWatts(type.electrical);
					if (type.engine.isEngine() || type.thrusters.isThruster())
					{
						for (const portInfo& port : footprint::turnedPorts(type, _o->turns()))
						{
							if (port.carries == portInfo::kind::liquid && port.flow != portInfo::direction::out)
							{
								p.fuelTile = origin + port.cell;
								break;
							}
						}
					}
					if (type.engine.isEngine())
					{
						p.newtons = type.engine.newtons;
						p.exhaustVelocity = type.engine.exhaustVelocity;
						p.electric = type.engine.electric();
						p.fuel = p.electric ? nullptr : findMaterial(type.engine.fuel);
						const sf::Vector2i step = footprint::step(footprint::turnSide(type.engine.pushes, _o->turns()));
						p.push = vec2(step.x, step.y);
						p.device = findDevice(*_o, lv, sf::Vector2i(x, y));
						engines.push_back(p);
					}
					else if (type.thrusters.isThruster())
					{
						p.newtons = type.thrusters.newtons;
						p.exhaustVelocity = type.thrusters.exhaustVelocity;
						p.electric = type.thrusters.electric();
						p.fuel = p.electric ? nullptr : findMaterial(type.thrusters.fuel);
						p.device = p.watts > 0.0 ? findDevice(*_o, lv, sf::Vector2i(x, y)) : -1;
						thrusterParts.push_back(p);
					}
					if (type.gyroTorque > 0.0)
					{
						p.torque = type.gyroTorque;
						p.device = findDevice(*_o, lv, sf::Vector2i(x, y));
						gyros.push_back(p);
					}
					if (type.helm)
					{
						p.device = findDevice(*_o, lv, sf::Vector2i(x, y));
						helms.push_back(p);
					}
				});
			}
		}
	}

	//its centre of mass and its moment of inertia about it
	double total = 0.0;
	vec2 moment(0.0, 0.0);
	for (const massPoint& p : points)
	{
		const double kg = p.kg + (p.holder ? heldKg(*p.holder) : 0.0);
		total += kg;
		moment += p.at * kg;
	}
	structureKg = 0.0;
	for (const massPoint& p : points)
		structureKg += p.kg;
	gridCentre = total > 0.0 ? moment / total : vec2(0.0, 0.0);
	inertia = 0.0;
	for (const massPoint& p : points)
	{
		const double kg = p.kg + (p.holder ? heldKg(*p.holder) : 0.0);
		const vec2 r = p.at - gridCentre;
		inertia += kg * (dot(r, r) + T * T / 6.0);   //plus each piece's own, as a tile-sized block
	}
	inertia = std::max(inertia, 1.0);

	extent = 0.0;
	for (const auto& [at, kind] : seenFromAbove)
	{
		outlineCell c;
		c.local = vec2((at.first - 0.5) * T, (at.second - 0.5) * T) - gridCentre;
		c.what = kind;
		cells.push_back(c);
		extent = std::max(extent, length(c.local) + T);
	}
	for (std::vector<part>* list : { &engines, &thrusterParts, &gyros, &helms })
		for (part& p : *list)
			p.local -= gridCentre;

	//the main engines: those pushing the way most of the thrust goes
	vec2 sum(0.0, 0.0);
	for (const part& e : engines)
		sum += e.push * e.newtons;
	mainDirection = length(sum) > 0.0 ? sum / length(sum) : vec2(0.0, 0.0);
	stat = flightStatus();
	double veSum = 0.0, fuelledThrust = 0.0;
	for (const part& e : engines)
	{
		stat.needsFuel = stat.needsFuel || !e.electric;
		if (dot(e.push, mainDirection) < 0.9)
			continue;
		stat.mainThrust += e.newtons;
		stat.engineWatts += e.watts;
		if (e.electric)
			stat.electricThrust += e.newtons;
		else
		{
			veSum += e.newtons * e.exhaustVelocity;
			fuelledThrust += e.newtons;
		}
		stat.engineTorque += cross(e.local, e.push * e.newtons);
	}
	stat.exhaustVelocity = fuelledThrust > 0.0 ? veSum / fuelledThrust : 0.0;
	for (const part& t : thrusterParts)
		stat.needsFuel = stat.needsFuel || !t.electric;
	for (const part& t : thrusterParts)
		stat.thrusterThrust += t.newtons;
	for (const part& g : gyros)
		stat.gyroTorque += g.torque;
	stat.engines = int(engines.size());
	stat.thrusters = int(thrusterParts.size());
	stat.gyroscopes = int(gyros.size());
	stat.helms = int(helms.size());
	stat.inertia = inertia;
}

void flightSystem::weigh()
{
	double held = 0.0, fuel = 0.0;
	std::vector<std::string> fuels;
	for (const std::vector<part>* list : { &engines, &thrusterParts })
		for (const part& p : *list)
			if (p.fuel && std::find(fuels.begin(), fuels.end(), p.fuel->key) == fuels.end())
				fuels.push_back(p.fuel->key);
	for (const tileObject* holder : holders)
	{
		held += heldKg(*holder);
		if (holder->type && holder->type->fluid.tank)
			for (const std::string& key : fuels)
				fuel += heldKg(*holder, key);
	}
	stat.mass = structureKg + held;
	stat.dryMass = stat.mass - fuel;
	stat.fuelKg = fuel;
	//electric main engines need no fuel: unlimited. otherwise the rocket equation for the fuelled ones
	if (stat.mainThrust > 0.0 && stat.electricThrust >= stat.mainThrust)
		stat.deltaV = std::numeric_limits<double>::infinity();
	else
		stat.deltaV = stat.exhaustVelocity > 0.0 && stat.dryMass > 0.0 ? stat.exhaustVelocity * std::log(stat.mass / stat.dryMass) : 0.0;
}

vec2 flightSystem::toArea(vec2 _local) const
{
	return position + rotated(_local, heading);
}

vec2 flightSystem::directionToArea(vec2 _local) const
{
	return rotated(_local, heading);
}

sf::Vector2<double> flightSystem::centreOnGrid() const
{
	return sf::Vector2<double>(gridCentre.x / shipTuning::tileMetres + 0.5, gridCentre.y / shipTuning::tileMetres + 0.5);
}

void flightSystem::goTo(vec2 _target)
{
	manual = false;
	order = flightOrder::goTo;
	target = _target;
	headingHeld = false;
}

void flightSystem::stopHere()
{
	manual = false;
	order = flightOrder::stop;
	headingHeld = false;
}

void flightSystem::holdHere()
{
	manual = false;
	order = flightOrder::hold;
	target = position;
	headingHeld = false;
}

void flightSystem::dockAt(vec2 _berth, double _heading, vec2 _outwards)
{
	manual = false;
	order = flightOrder::dock;
	berth = _berth;
	berthHeading = _heading;
	berthOutwards = _outwards;
	dockPhase = 0;
	headingHeld = false;
}

bool flightSystem::readyToLatch() const
{
	return order == flightOrder::dock && dockPhase == 2 && length(position - berth) < shipTuning::latchDistance &&
		length(velocity) < shipTuning::latchSpeed && std::abs(wrapAngle(berthHeading - heading)) < shipTuning::latchAngle && std::abs(spin) < 0.01;
}

vec2 flightSystem::localOfTile(sf::Vector2i _tile) const
{
	return vec2((_tile.x - 0.5) * shipTuning::tileMetres, (_tile.y - 0.5) * shipTuning::tileMetres) - gridCentre;
}

void flightSystem::setManual(bool _manual)
{
	controls = manualControls();
	headingHeld = false;
	if (_manual)
	{
		manual = true;
		return;
	}
	holdHere();   //back on the autopilot: it holds where it is until told otherwise
}

void flightSystem::autopilot(vec2& _wanted, double& _heading, bool& _turnOnly, bool& _mainAllowed)
{
	_wanted = vec2(0.0, 0.0);
	_turnOnly = false;
	_mainAllowed = true;
	stat.arrived = false;
	const double m = std::max(stat.mass, 1.0);
	const double aMain = stat.mainThrust / m;
	const double aThrusters = stat.thrusterThrust / m;
	//docking: to the stand-off point first (an ordinary trip), then lining up square to the dock there and closing in, on
	//the thrusters alone, heading held
	vec2 goal = target;
	double limit = maxSpeed;
	double slack = shipTuning::arriveRadius * 0.5;
	bool forcedHeading = false;
	flightOrder kind = order;
	if (order == flightOrder::dock)
	{
		const vec2 standoff = berth + berthOutwards * shipTuning::dockStandoff;
		const bool atStandoff = length(standoff - position) < shipTuning::arriveRadius && length(velocity) < shipTuning::arriveSpeed;
		if (dockPhase == 0 && atStandoff)
			dockPhase = 1;
		if (dockPhase == 1 && atStandoff && std::abs(wrapAngle(berthHeading - heading)) < shipTuning::latchAngle * 2.0 && std::abs(spin) < 0.01)
			dockPhase = 2;
		goal = dockPhase == 2 ? berth : standoff;
		if (dockPhase > 0)
		{
			limit = dockPhase == 2 ? shipTuning::dockSpeed : 1.0;
			slack = 0.0;
			_mainAllowed = false;
			forcedHeading = true;
		}
		kind = flightOrder::goTo;
	}
	if (kind == flightOrder::goTo || kind == flightOrder::hold)
	{
		const vec2 toTarget = goal - position;
		const double distance = length(toTarget);
		//the fastest it can go here and still stop at the target: with the main engines (after turning round to brake),
		//or with the thrusters (no turning)
		const double alpha = stat.gyroTorque / inertia;
		const double turnTime = alpha > 0.0 ? 2.0 * std::sqrt(pi / alpha) : 30.0;
		const double brake = aMain * shipTuning::brakeShare;
		const double room = std::max(distance - slack, 0.0);
		double speed = 0.0;
		if (brake > 0.0 && _mainAllowed)
			speed = -brake * turnTime + std::sqrt(brake * brake * turnTime * turnTime + 2.0 * brake * room);
		if (aThrusters > 0.0)
			speed = std::max(speed, std::sqrt(2.0 * aThrusters * shipTuning::brakeShare * room));
		speed = std::min(speed, limit);
		const vec2 wantedVelocity = distance > 1e-6 ? toTarget / distance * speed : vec2(0.0, 0.0);
		_wanted = (wantedVelocity - velocity) / shipTuning::velocityTime;
		stat.arrived = distance < shipTuning::arriveRadius && length(velocity) < shipTuning::arriveSpeed;
	}
	else if (order == flightOrder::stop)
	{
		_wanted = -velocity / shipTuning::velocityTime;
		stat.arrived = length(velocity) < shipTuning::arriveSpeed;
	}
	//the heading: square to the dock when docking; the main engines along the acceleration it wants, when the thrusters
	//alone can't give it; otherwise it keeps the heading it has
	const double need = length(_wanted);
	if (forcedHeading)
	{
		_heading = berthHeading;
		headingHeld = false;
	}
	else if (aMain > 0.0 && need > aThrusters * 0.9)
	{
		_heading = std::atan2(_wanted.y, _wanted.x) - std::atan2(mainDirection.y, mainDirection.x);
		headingHeld = false;
		_turnOnly = std::abs(wrapAngle(_heading - heading)) >= shipTuning::turnMargin;
	}
	else
	{
		if (!headingHeld)
		{
			heldHeading = heading;
			headingHeld = true;
		}
		_heading = heldHeading;
	}
}

double flightSystem::steerTo(double _heading) const
{
	const double error = wrapAngle(_heading - heading);
	const double alpha = stat.gyroTorque / inertia;
	const double wantedSpin = alpha > 0.0 ? sign(error) * std::min({ std::sqrt(2.0 * alpha * 0.8 * std::abs(error)), 3.0 * std::abs(error), 1.0 }) : 0.0;
	return inertia * (wantedSpin - spin) / 0.3;
}

void flightSystem::fly(double _dt)
{
	const double m = std::max(stat.mass, 1.0);
	const double aMain = stat.mainThrust / m;
	const vec2 forwardsLocal = length(mainDirection) > 0.0 ? mainDirection : vec2(0.0, -1.0);
	double mainThrottle = 0.0;
	vec2 thrusterForce(0.0, 0.0);   //wanted, in its own frame (N)
	double gyroCommand = 0.0;       //wanted torque (N m)
	stat.arrived = false;

	if (!stat.helmPowered)
	{
		stat.state = helms.empty() ? "no flight console" : "the flight console has no power";
	}
	else if (manual)
	{
		mainThrottle = std::clamp(controls.thrust, 0.0, 1.0);
		const vec2 right(-forwardsLocal.y, forwardsLocal.x);   //a quarter turn clockwise on the nav map
		thrusterForce = (-forwardsLocal * std::clamp(controls.back, 0.0, 1.0) + right * std::clamp(controls.slide, -1.0, 1.0)) * stat.thrusterThrust;
		if (controls.turn != 0.0)
		{
			gyroCommand = std::clamp(controls.turn, -1.0, 1.0) * stat.gyroTorque;
			headingHeld = false;
		}
		else if (!headingHeld && std::abs(spin) > 0.01)
			gyroCommand = -spin * inertia / 0.5;   //let go of the turn: the gyroscopes stop it spinning...
		else
		{
			//...then keep it pointing that way (against the engines' push, if it is off its centre of mass)
			if (!headingHeld)
			{
				heldHeading = heading;
				headingHeld = true;
			}
			gyroCommand = steerTo(heldHeading);
		}
		stat.state = "manual";
	}
	else if (order == flightOrder::none)
	{
		stat.state = "drifting";
	}
	else
	{
		vec2 wanted;
		double wantedHeading = heading;
		bool turnOnly = false, mainAllowed = true;
		autopilot(wanted, wantedHeading, turnOnly, mainAllowed);
		gyroCommand = steerTo(wantedHeading);
		//the main engines, once pointing the right way
		const double need = length(wanted);
		if (mainAllowed && aMain > 0.0 && !turnOnly && need > (stat.thrusterThrust / m) * 0.9)
			mainThrottle = std::clamp(need / aMain, 0.0, 1.0);
		//the thrusters: whatever the main engines don't give
		const vec2 rest = wanted - directionToArea(forwardsLocal) * (mainThrottle * aMain);
		thrusterForce = rotated(rest, -heading) * m;
		if (order == flightOrder::dock && dockPhase > 0 && stat.thrusterThrust <= 0.0)
			stat.state = "docking needs manoeuvring thrusters";
		else if (order == flightOrder::dock && dockPhase == 1)
			stat.state = "docking: lining up";
		else if (order == flightOrder::dock && dockPhase == 2)
		{
			char text[64];
			snprintf(text, sizeof(text), "docking: closing in, %.1f m", length(berth - position));
			stat.state = text;
		}
		else if (order == flightOrder::dock && mainThrottle > 0.0)
			stat.state = "docking: on the way (burning)";
		else if (order == flightOrder::dock)
			stat.state = "docking: on the way";
		else if (stat.arrived)
			stat.state = order == flightOrder::stop ? "stopped" : "holding at the target";
		else if (mainThrottle > 0.0)
			stat.state = "burning";
		else if (turnOnly)
			stat.state = "turning to burn";
		else if (length(thrusterForce) > 1.0)
			stat.state = "manoeuvring";
		else
			stat.state = "coasting";
	}

	//main engines: fuel through their liquid ports, power through their loads (switched off while they don't fire)
	vec2 force(0.0, 0.0);
	double torque = 0.0;
	double delivered = 0.0;
	for (part& e : engines)
	{
		const double throttle = dot(e.push, mainDirection) >= 0.9 ? mainThrottle : 0.0;
		setIdle(e, throttle <= 0.0);
		if (throttle <= 0.0)
			continue;
		const double share = throttle * powerShare(e);
		double newtons = 0.0;
		if (e.electric)
			newtons = share * e.newtons;   //on its power alone
		else
		{
			const double kgWanted = share * e.newtons / e.exhaustVelocity * _dt;
			double kgGot = 0.0;
			if (kgWanted > 0.0 && e.fuel && e.fuelNode >= 0 && liquids)
				kgGot = liquids->takeLiquid(e.fuelNode, *e.fuel, kgWanted / e.fuel->kilogramsPerLitre()) * e.fuel->kilogramsPerLitre();
			newtons = kgWanted > 0.0 ? share * e.newtons * (kgGot / kgWanted) : 0.0;
		}
		const vec2 f = e.push * newtons;
		force += f;
		torque += cross(e.local, f);
		delivered += newtons;
	}
	stat.throttle = stat.mainThrust > 0.0 ? delivered / stat.mainThrust : 0.0;

	//thrusters: up to their thrust each way, through the centre of mass; each burns its share of the fuel
	const double each = stat.thrusterThrust;
	const vec2 asked(std::clamp(thrusterForce.x, -each, each), std::clamp(thrusterForce.y, -each, each));
	const double askedNewtons = std::abs(asked.x) + std::abs(asked.y);
	stat.thrusterUse = 0.0;
	const bool thrusting = each > 0.0 && askedNewtons > 1e-6;
	for (part& t : thrusterParts)
		setIdle(t, !thrusting);   //thrusters with a load draw it only while they fire
	if (thrusting)
	{
		double got = 0.0;
		for (part& t : thrusterParts)
		{
			const double portion = t.newtons / each;
			if (t.electric)
			{
				got += portion * powerShare(t);
				continue;
			}
			const double kgWanted = askedNewtons * portion / t.exhaustVelocity * _dt;
			double kgGot = 0.0;
			if (t.fuel && t.fuelNode >= 0 && liquids)
				kgGot = liquids->takeLiquid(t.fuelNode, *t.fuel, kgWanted / t.fuel->kilogramsPerLitre()) * t.fuel->kilogramsPerLitre();
			got += portion * (kgWanted > 0.0 ? kgGot / kgWanted : 0.0) * powerShare(t);
		}
		force += asked * got;
		stat.thrusterUse = std::clamp(askedNewtons * got / (2.0 * each), 0.0, 1.0);
	}

	//gyroscopes: as much of the wanted torque as they have, on the power they get
	double available = 0.0;
	const bool turning = std::abs(gyroCommand) > stat.gyroTorque * 0.002;
	for (part& g : gyros)
	{
		setIdle(g, !turning);
		available += g.torque * powerShare(g);
	}
	const double gyroTorque = turning ? std::clamp(gyroCommand, -available, available) : 0.0;
	torque += gyroTorque;
	stat.turning = stat.gyroTorque > 0.0 ? gyroTorque / stat.gyroTorque : 0.0;

	//Newton: the forces in its own frame turned into the area's
	const vec2 acceleration = directionToArea(force) / m;
	velocity += acceleration * _dt;
	position += velocity * _dt;
	spin += torque / inertia * _dt;
	heading = wrapAngle(heading + spin * _dt);
}

void flightSystem::findLinks()
{
	for (std::vector<part>* list : { &engines, &thrusterParts, &gyros, &helms })
	{
		for (part& p : *list)
		{
			p.device = p.watts > 0.0 ? findDevice(*p.object, p.level, p.tile) : -1;
			p.fuelNode = liquids && p.fuelTile.x >= 0 ? liquids->network().nodeAt(p.level, p.fuelTile) : -1;
		}
	}
}

void flightSystem::tick(double _dt)
{
	update();
	findLinks();
	weigh();
	if (!movable)
	{
		stat.state = "anchored";
		return;
	}
	stat.helmPowered = false;
	for (const part& h : helms)
		stat.helmPowered = stat.helmPowered || powerShare(h) > 0.5;
	if (docked)
	{
		//held at its dock by the world: nothing fires
		for (std::vector<part>* list : { &engines, &thrusterParts, &gyros })
			for (part& p : *list)
				setIdle(p, true);
		stat.throttle = stat.thrusterUse = stat.turning = 0.0;
		stat.arrived = true;
		stat.state = "docked";
		return;
	}
	fly(_dt);

	sinceTrail += _dt;
	if (sinceTrail >= shipTuning::trailSeconds)
	{
		sinceTrail = 0.0;
		trailPoints.push_back(position);
		while (int(trailPoints.size()) > shipTuning::trailPoints)
			trailPoints.pop_front();
	}
}
