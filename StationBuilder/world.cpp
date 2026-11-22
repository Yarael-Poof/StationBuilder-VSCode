#include "world.h"
#include "simulation.h"
#include "gameManager.h"
#include "decks.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

vessel::vessel(textureManager& _textures, const std::string& _name, bool _ship)
	: name(_name), ship(_ship), layout(&_textures),
	power(layout), liquid(layout, &power), heat(layout, &power), items(layout),
	machines(layout, &power, &liquid, &heat), flight(layout, &power, &liquid, _ship)
{
	//as main.cpp wired the station's: heat exchangers and machines warm the air; the liquid, the thermostats, hot
	//generators and cables read the air's temperature
	heat.addHeatSource(&liquid);
	heat.addHeatSource(&machines);
	liquid.setTemperatureSource(&heat);
	power.setTemperatureSource(&heat);
	layout.setStationName(_name);
}

world::world(textureManager& _textures, simulationManager& _simulation)
	: textures(&_textures), sim(&_simulation)
{
	//docking goes first each tick: docked ships held in place before they (don't) fly
	ticker.owner = this;
	sim->addSystem(&ticker);
}

world::~world()
{
	sim->removeSystem(&ticker);
	for (const std::unique_ptr<vessel>& v : vessels)
	{
		for (simulationSystem* s : std::initializer_list<simulationSystem*>{ &v->power, &v->liquid, &v->heat, &v->items, &v->machines, &v->flight })
			sim->removeSystem(s);
	}
}

vessel& world::add(const std::string& _name, bool _ship)
{
	vessels.push_back(std::make_unique<vessel>(*textures, _name, _ship));
	vessel& v = *vessels.back();
	//each tick: its power, then liquids, heat, item tubes and machines (as before), then its flight, which uses that
	//tick's power and fuel
	for (simulationSystem* s : std::initializer_list<simulationSystem*>{ &v.power, &v.liquid, &v.heat, &v.items, &v.machines, &v.flight })
		sim->addSystem(s);
	return v;
}

std::string world::nextShipName() const
{
	for (int n = 1;; n++)
	{
		const std::string name = "Ship " + std::to_string(n);
		const bool taken = std::any_of(vessels.begin(), vessels.end(), [&](const std::unique_ptr<vessel>& _v) { return _v->name == name; });
		if (!taken)
			return name;
	}
}

vessel& world::newShip()
{
	home().flight.update();
	const double homeRadius = home().flight.radius();
	int ships = 0;
	for (const std::unique_ptr<vessel>& v : vessels)
		ships += v->ship;
	vessel& ship = add(nextShipName(), true);
	shipLayout::starter(ship.layout);
	ship.flight.update();
	//below the station on the map, each new one further along, facing up the map
	const double r = ship.flight.radius();
	ship.flight.position = vec2(-60.0 + 70.0 * (ships % 3), homeRadius + 30.0 + r + (ships / 3) * (2.0 * r + 30.0));
	const vec2 forwards = ship.flight.forwards();
	ship.flight.heading = (forwards.x != 0.0 || forwards.y != 0.0) ? std::atan2(-1.0, 0.0) - std::atan2(forwards.y, forwards.x) : 0.0;
	ship.flight.holdHere();
	return ship;
}

int world::numberOf(const vessel& _vessel) const
{
	for (int i = 0; i < count(); i++)
		if (vessels[size_t(i)].get() == &_vessel)
			return i;
	return -1;
}

void world::clearShips()
{
	dockLinks.clear();
	for (size_t i = 1; i < vessels.size(); i++)
	{
		vessel& v = *vessels[i];
		for (simulationSystem* s : std::initializer_list<simulationSystem*>{ &v.power, &v.liquid, &v.heat, &v.items, &v.machines, &v.flight })
			sim->removeSystem(s);
	}
	vessels.resize(1);
	viewedIndex = 0;
	wantedView = -1;
}

std::vector<station*> world::stations()
{
	std::vector<station*> list;
	for (const std::unique_ptr<vessel>& v : vessels)
		list.push_back(&v->layout);
	return list;
}

void world::setViewed(int _index)
{
	viewedIndex = std::clamp(_index, 0, count() - 1);
}

namespace shipLayout
{
	void starter(station& _ship)
	{
		_ship.appendNewIsoLevel(float(gridSize), float(gridSize), 64.f);
		isometricLevel& deck = *_ship.getIsoLevel(0.f);
		decks::setArea(deck, decks::shapeTiles(decks::shape::rectangle, sf::Vector2i(16, 10), sf::Vector2i(13, 11)));
		auto put = [&](int _x, int _y, const char* _key, const std::string& _settings = "") {
			if (!deck.addObjectToTile(sf::Vector2f(float(_x), float(_y)), _key, _settings))
				std::cout << "[SHIP] the starter ship couldn't place " << _key << " at " << _x << "," << _y << std::endl;
		};
		//the cabin: x 8-13, y 9-13, walled
		for (int y = 9; y <= 13; y++)
			for (int x = 8; x <= 13; x++)
				put(x, y, "floor");
		for (int x = 8; x <= 13; x++)
		{
			put(x, 9, "wallUR");
			put(x, 13, "wallLL");
		}
		for (int y = 9; y <= 13; y++)
		{
			put(8, y, "wallUL");
			put(13, y, "wallLR");
		}
		//a cable down its middle: the flight console, the generator, the gyroscope and a light on it
		for (int x = 9; x <= 13; x++)
			put(x, 11, "cable");
		put(9, 11, "flightConsole");
		put(10, 11, "generator");
		put(11, 11, "gyroscope");
		put(12, 11, "light");
		//frames behind the cabin (x 14-18): the cable on to the engine, and the fuel pipes
		for (int y = 9; y <= 13; y++)
			for (int x = 14; x <= 18; x++)
				put(x, y, "frame");
		for (int x = 14; x <= 16; x++)
			put(x, 11, "cable");
		for (const sf::Vector2i at : { sf::Vector2i(14, 12), sf::Vector2i(14, 11), sf::Vector2i(14, 10), sf::Vector2i(15, 10),
			sf::Vector2i(16, 10), sf::Vector2i(16, 9), sf::Vector2i(17, 9) })
			put(at.x, at.y, "pipe");
		//on the frames (a 2 x 2 machine is placed by its upper tile): the tank, its port facing the pipe at 14,12; the
		//engine, its fuel port facing 16,10 and its power port 16,11; the thrusters, their port facing 17,9
		put(15, 12, "propellantTank");
		put(17, 10, "mainEngine");
		put(18, 9, "thrusters");
		//the nose: a docking collar facing forwards (turned twice: its ring to the upper left), its ports facing 8,11,
		//where a tube joins it to a crate and a pipe runs along the cabin to the fuel line
		put(8, 11, "tube");
		put(8, 12, "crate");
		for (int x = 8; x <= 13; x++)
			put(x, 10, "pipe");
		put(8, 11, "pipe");
		put(7, 11, "dockingCollar", "r=2");
		_ship.setCurrentLevel(0);
	}
}

namespace
{
	std::string folderFor(const std::string& _name)
	{
		std::string folder = "ships/";
		for (char c : _name)
			folder += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
		return folder;
	}
	std::string num(double _value)
	{
		if (!std::isfinite(_value))
			_value = 0.0;
		std::ostringstream text;
		text.precision(12);
		text << _value;
		std::string s = text.str();
		if (s.find_first_of(".eE") == std::string::npos)
			s += ".0";
		return s;
	}
	std::string quoted(const std::string& _text)
	{
		std::string q = "\"";
		for (char c : _text)
		{
			if (c == '"' || c == '\\')
				q += '\\';
			q += c;
		}
		return q + "\"";
	}
	const char* orderName(flightOrder _order)
	{
		switch (_order)
		{
		case flightOrder::goTo: return "go_to";
		case flightOrder::stop: return "stop";
		case flightOrder::hold: return "hold";
		default: return "none";
		}
	}
	flightOrder orderFrom(const std::string& _name)
	{
		if (_name == "go_to") return flightOrder::goTo;
		if (_name == "stop") return flightOrder::stop;
		if (_name == "hold") return flightOrder::hold;
		return flightOrder::none;
	}
	vec2 pair(const toml::table& _table, const char* _field)
	{
		vec2 v(0.0, 0.0);
		if (const toml::array* a = _table[_field].as_array(); a && a->size() == 2)
		{
			v.x = (*a)[0].value<double>().value_or(0.0);
			v.y = (*a)[1].value<double>().value_or(0.0);
		}
		return v;
	}
}

namespace worldFiles
{
	void save(world& _world, gameManager& _files)
	{
		vessel& home = _world.home();
		const std::string stationName = home.layout.getStationName();
		_files.saveStationToDisc(&home.layout, stationName);
		std::ofstream out(stationName + ".world");
		out << "# The ships round " << stationName << ": where they are, how they move, and what they were told to do.\n";
		out << "# Their decks are saved in the folder each one names. Positions are in metres from the station, headings in radians.\n";
		for (int i = 1; i < _world.count(); i++)
		{
			vessel& v = _world.at(i);
			const std::string folder = folderFor(v.name);
			boost::system::error_code ignored;
			boost::filesystem::create_directories(folder, ignored);
			_files.saveStationToDisc(&v.layout, folder);
			const flightSystem& f = v.flight;
			out << "\n[[ship]]\n";
			out << "name = " << quoted(v.name) << "\n";
			out << "folder = " << quoted(folder) << "\n";
			out << "position = [" << num(f.position.x) << ", " << num(f.position.y) << "]\n";
			out << "velocity = [" << num(f.velocity.x) << ", " << num(f.velocity.y) << "]\n";
			out << "heading = " << num(f.heading) << "\n";
			out << "spin = " << num(f.spin) << "\n";
			out << "manual = " << (f.manual ? "true" : "false") << "\n";
			out << "order = " << quoted(orderName(f.order)) << "\n";
			out << "target = [" << num(f.target.x) << ", " << num(f.target.y) << "]\n";
			out << "max_speed = " << num(f.maxSpeed) << "\n";
			if (const dockLink* link = _world.linkOf(v); link && link->docked)
			{
				out << "docked_to = " << quoted(link->host->name) << "\n";
				out << "host_dock = [" << link->hostLevel << ", " << link->hostTile.x << ", " << link->hostTile.y << "]\n";
				out << "ship_dock = [" << link->shipLevel << ", " << link->shipTile.x << ", " << link->shipTile.y << "]\n";
				out << "cargo = " << link->cargo << "\n";
				out << "liquid = " << link->liquid << "\n";
			}
		}
		std::cout << "[INFO] saved " << (_world.count() - 1) << " ship(s) in " << stationName << ".world" << std::endl;
	}

	bool load(world& _world, gameManager& _files, textureManager& _textures)
	{
		vessel& home = _world.home();
		const std::string stationName = home.layout.getStationName();
		if (!_files.loadStationFromDisc(&home.layout, stationName, &_textures))
			return false;
		_world.clearShips();
		const std::string file = stationName + ".world";
		if (!boost::filesystem::exists(file))
			return true;   //an older save: just the station
		toml::table table;
		try
		{
			table = toml::parse_file(file);
		}
		catch (const toml::parse_error& _error)
		{
			std::cout << "[ERROR] " << file << " can't be read: " << _error.description() << " (the ships are left out)" << std::endl;
			return true;
		}
		//docked ships go back on their docks once every ship is in
		struct dockedShip { vessel* ship; std::string host; int hostDock[3]; int shipDock[3]; int cargo; int liquid; };
		std::vector<dockedShip> docked;
		auto triple = [](const toml::table& _t, const char* _field, int _out[3]) {
			const toml::array* a = _t[_field].as_array();
			if (!a || a->size() != 3)
				return false;
			for (int i = 0; i < 3; i++)
				_out[i] = int((*a)[size_t(i)].value<int64_t>().value_or(0));
			return true;
		};
		if (const toml::array* ships = table["ship"].as_array())
		{
			for (const toml::node& node : *ships)
			{
				const toml::table* t = node.as_table();
				if (!t)
					continue;
				const std::string name = (*t)["name"].value<std::string>().value_or(_world.nextShipName());
				const std::string folder = (*t)["folder"].value<std::string>().value_or(folderFor(name));
				if (!boost::filesystem::exists(folder + "/0.level"))
				{
					std::cout << "[ERROR] " << name << "'s decks aren't in " << folder << " (left out)" << std::endl;
					continue;
				}
				vessel& v = _world.add(name, true);
				_files.loadStationFromDisc(&v.layout, folder, &_textures);
				v.layout.setStationName(name);
				flightSystem& f = v.flight;
				f.update();
				f.position = pair(*t, "position");
				f.velocity = pair(*t, "velocity");
				f.heading = (*t)["heading"].value<double>().value_or(0.0);
				f.spin = (*t)["spin"].value<double>().value_or(0.0);
				f.maxSpeed = (*t)["max_speed"].value<double>().value_or(shipTuning::defaultMaxSpeed);
				f.order = orderFrom((*t)["order"].value<std::string>().value_or("none"));
				f.target = pair(*t, "target");
				f.manual = (*t)["manual"].value<bool>().value_or(false);
				dockedShip d;
				d.ship = &v;
				d.host = (*t)["docked_to"].value<std::string>().value_or("");
				d.cargo = int((*t)["cargo"].value<int64_t>().value_or(0));
				d.liquid = int((*t)["liquid"].value<int64_t>().value_or(0));
				if (!d.host.empty() && triple(*t, "host_dock", d.hostDock) && triple(*t, "ship_dock", d.shipDock))
					docked.push_back(d);
			}
		}
		for (const dockedShip& d : docked)
		{
			vessel* host = nullptr;
			for (int i = 0; i < _world.count(); i++)
				if (_world.at(i).name == d.host)
					host = &_world.at(i);
			if (!host || !_world.relink(*d.ship, d.shipDock[0], sf::Vector2i(d.shipDock[1], d.shipDock[2]), *host, d.hostDock[0],
				sf::Vector2i(d.hostDock[1], d.hostDock[2]), d.cargo, d.liquid))
				std::cout << "[ERROR] " << d.ship->name << " couldn't go back on its dock at " << d.host << " (left floating)" << std::endl;
		}
		std::cout << "[INFO] loaded " << (_world.count() - 1) << " ship(s) from " << file << std::endl;
		return true;
	}
}
