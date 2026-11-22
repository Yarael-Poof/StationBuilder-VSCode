//writes a scene for the raylib 3D prototype (prototypes/raylib3d): every vessel's decks as plain text, so the
//prototype can draw them in 3D without the game's code (it has its own copy of Dear ImGui, set up for raylib).
//
//the scene is the README's station (the example blueprints, the cooling loop, solar arrays, two more decks) with a
//jetty, a ship docked at it and a ship out in space; or, with "save", the station and ships saved in the game.
//
//build:  cmake --build build/debug --target SceneExport
//run it from the StationBuilder folder:
//        ..\build\debug\SceneExport.exe [demo|save] [output file]
//the output defaults to ../prototypes/raylib3d/scene.txt
//
//the file, one record a line (tiles are the grid's, 1-based; metres; radians):
//  vessel <name> <ship 0/1> <x> <y> <heading> <centre x> <centre y> <forwards x> <forwards y> <docked 0/1>
//      the centre is the centre of mass, in metres from the grid's corner; forwards is the way its engines push
//  deck <index>
//  floor <x> <y>              frame <x> <y>              wall <x> <y> <edge 0 UL, 1 UR, 2 LL, 3 LR>
//  line <x> <y> <kind 0 cable, 1 pipe, 2 tube, 3 duct> <sides mask> <riser 0/1>
//  box <x> <y> <w> <h> <height m> <r> <g> <b> <outside 0/1> <light 0/1> <key>
//      an item: its footprint as turned (upper tile, size), its height and colour from its art
//  dock <x> <y> <face>
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include "station.h"
#include "tileCatalogue.h"
#include "content.h"
#include "decks.h"
#include "simulation.h"
#include "blueprint.h"
#include "gameManager.h"
#include "world.h"

namespace
{
	//an item's look from its art: the mean colour of its solid pixels, and its height above the floor diamond
	struct look
	{
		sf::Color colour = sf::Color(160, 160, 170);
		double height = 1.0;
	};
	std::map<std::string, look> looks;

	const look& lookOf(const tileTypeInfo& _type, int _turns, sf::Vector2i _size)
	{
		std::string file = _type.art;
		if (_turns > 0 && int(_type.turnArt.size()) >= _turns)
			file = _type.turnArt[size_t(_turns - 1)];
		auto found = looks.find(file);
		if (found != looks.end())
			return found->second;
		look l;
		sf::Image image;
		if (image.loadFromFile(file))
		{
			const sf::Vector2u size = image.getSize();
			double r = 0, g = 0, b = 0, n = 0;
			int top = int(size.y);
			for (unsigned y = 0; y < size.y; y++)
			{
				for (unsigned x = 0; x < size.x; x++)
				{
					const sf::Color c = image.getPixel(x, y);
					if (c.a < 128)
						continue;
					r += c.r; g += c.g; b += c.b; n++;
					top = std::min(top, int(y));
				}
			}
			if (n > 0)
				l.colour = sf::Color(sf::Uint8(r / n), sf::Uint8(g / n), sf::Uint8(b / n));
			//the floor diamond is the art's bottom 16 (w + h) pixels; what rises above it is the item's height, at about
			//13 pixels a metre (a 40 pixel deck is 3 m)
			const int diamond = 16 * (_size.x + _size.y);
			const int abovePx = (int(size.y) - diamond) - top;
			l.height = std::clamp(abovePx / 13.3, 0.3, 6.0);
		}
		return looks[file] = l;
	}

	bool isLine(const tileTypeInfo& _t, int& _kind)
	{
		if (_t.electrical.conductor) { _kind = 0; return true; }
		if (_t.items.tube) { _kind = 2; return true; }
		if (_t.thermal.ductConductance > 0.0) { _kind = 3; return true; }
		const fluidProperties& f = _t.fluid;
		if (f.carriesLiquid && !f.pump && !f.tank && f.exchange == 0.0 && f.radiatingArea == 0.0) { _kind = 1; return true; }
		return false;
	}

	std::string noSpaces(std::string _s)
	{
		for (char& c : _s)
			if (c == ' ')
				c = '_';
		return _s;
	}

	void writeVessel(std::ofstream& _out, world& _world, vessel& _v)
	{
		flightSystem& f = _v.flight;
		f.update();
		const sf::Vector2<double> centre = f.centreOnGrid();
		const double T = shipTuning::tileMetres;
		const dockLink* link = _world.linkOf(_v);
		_out << "vessel " << noSpaces(_v.name) << " " << (_v.ship ? 1 : 0) << " " << f.position.x << " " << f.position.y << " " << f.heading << " "
			<< (centre.x - 0.5) * T << " " << (centre.y - 0.5) * T << " " << f.forwards().x << " " << f.forwards().y << " "
			<< (link && link->docked ? 1 : 0) << "\n";
		for (int lv = 0; lv < _v.layout.getLayerListSize(); lv++)
		{
			isometricLevel& level = *_v.layout.getIsoLevel(float(lv));
			_out << "deck " << lv << "\n";
			const int w = int(level.getSizeW()), h = int(level.getSizeH());
			for (int y = 1; y <= h; y++)
			{
				for (int x = 1; x <= w; x++)
				{
					tile& t = level.levelTiles[level.coords2TileIndex(x, y)];
					if (!t.hasAnything())
						continue;
					if (t.hasFloor())
						_out << "floor " << x << " " << y << "\n";
					else if (t.hasFrame())
						_out << "frame " << x << " " << y << "\n";
					t.forEachObject([&](tileObject* _o) {
						if (!_o->type || _o->isPart())
							return;
						const tileTypeInfo& type = *_o->type;
						if (type.layer == tileLayer::background || type.layer == tileLayer::floor)
							return;
						if (type.layer == tileLayer::wall)
						{
							_out << "wall " << x << " " << y << " " << type.wallEdge << "\n";
							return;
						}
						int kind = 0;
						if (isLine(type, kind))
						{
							const bool riser = type.electrical.vertical || type.fluid.vertical || type.items.vertical || type.thermal.ductConductance > 0.0;
							_out << "line " << x << " " << y << " " << kind << " " << connections::sides(type, _o->allSettingValues()) << " " << (riser ? 1 : 0) << "\n";
							return;
						}
						const bool machine = footprint::isMachine(type);
						const sf::Vector2i origin = machine ? _o->origin : sf::Vector2i(x, y);
						const sf::Vector2i size = machine ? footprint::turnedSize(type, _o->turns()) : sf::Vector2i(1, 1);
						const look& l = lookOf(type, machine ? _o->turns() : 0, size);
						_out << "box " << origin.x << " " << origin.y << " " << size.x << " " << size.y << " " << l.height << " "
							<< int(l.colour.r) << " " << int(l.colour.g) << " " << int(l.colour.b) << " " << (type.outside ? 1 : 0) << " "
							<< (type.category == "Lights" ? 1 : 0) << " " << type.key << "\n";
						sf::Vector2i dockTile;
						int face = 0;
						if (shipParts::placedDock(*_o, sf::Vector2i(x, y), dockTile, face))
							_out << "dock " << dockTile.x << " " << dockTile.y << " " << face << "\n";
					});
				}
			}
		}
	}

	//---- the README's station (a copy of tools/readmeShots.cpp's), with a jetty ----
	void put(isometricLevel& _level, int _x, int _y, const char* _key, const std::string& _settings = "")
	{
		if (!_level.addObjectToTile(sf::Vector2f(float(_x), float(_y)), _key, _settings))
			std::cout << "refused " << _key << " at " << _x << "," << _y << std::endl;
	}
	void placeBlueprint(isometricLevel& _level, const std::vector<blueprint>& _library, const char* _name, int _x, int _y)
	{
		for (const blueprint& b : _library)
			if (b.name == _name)
				blueprints::paste(_level, b, sf::Vector2i(_x, _y));
	}
	void buildStation(station& _station, const std::vector<blueprint>& _library)
	{
		decks::startingStation(_station);
		isometricLevel& lowest = *_station.getIsoLevel(0);
		isometricLevel& middle = *_station.getIsoLevel(1);
		placeBlueprint(lowest, _library, "Iron smelting line", 19, 15);
		placeBlueprint(lowest, _library, "Ice to water", 19, 29);
		placeBlueprint(lowest, _library, "Waste recycling", 28, 29);
		placeBlueprint(lowest, _library, "Powered room", 12, 21);
		for (int x = 24; x <= 28; x++)
			put(lowest, x, 22, "cable");
		put(lowest, 24, 22, "generator");
		put(lowest, 26, 22, "heater");
		put(lowest, 28, 22, "heater");
		put(lowest, 25, 21, "cable");
		put(lowest, 25, 21, "light");
		for (int y = 23; y <= 26; y++)
			put(lowest, 24, y, "cable");
		put(lowest, 24, 26, "light");
		for (int x = 22; x <= 23; x++)
			put(lowest, x, 24, "cable");
		put(lowest, 23, 27, "pipe");
		put(lowest, 23, 26, "exchanger");
		put(lowest, 23, 25, "pipe");
		put(lowest, 23, 24, "pipe");
		put(lowest, 22, 24, "pump");
		put(lowest, 21, 24, "pipe");
		put(lowest, 21, 25, "exchanger");
		put(lowest, 21, 26, "pipe");
		put(lowest, 21, 27, "pipe");
		put(lowest, 20, 24, "coolantTank");
		put(lowest, 22, 28, "radiatorPanel");
		put(lowest, 30, 21, "solarArray");
		put(lowest, 29, 21, "cable");
		put(lowest, 29, 22, "cable");
		put(lowest, 31, 25, "dockingJetty");    //30-33, 25: ships dock at 33,25
		put(lowest, 29, 25, "tube");
		put(lowest, 28, 25, "crate");
		for (int y = 21; y <= 26; y++)
			for (int x = 20; x <= 27; x++)
				put(middle, x, y, "floor");
		for (int x = 20; x <= 27; x++)
		{
			put(middle, x, 21, "wallUR");
			put(middle, x, 26, "wallLL");
		}
		for (int y = 21; y <= 26; y++)
		{
			put(middle, 20, y, "wallUL");
			put(middle, 27, y, "wallLR");
		}
		for (int x = 22; x <= 26; x++)
			put(middle, x, 23, "cable");
		put(middle, 23, 23, "light");
		put(middle, 25, 23, "light");
		put(middle, 28, 22, "solarArray");
		put(middle, 27, 22, "cable");
		put(middle, 27, 23, "cable");
		const int top = _station.insertNewIsoLevel(2, float(decks::gridSize), float(decks::gridSize));
		isometricLevel& upper = *_station.getIsoLevel(float(top));
		decks::setArea(upper, decks::shapeTiles(decks::shape::ellipse, sf::Vector2i(12, 10), sf::Vector2i(24, 24)));
		for (int y = 22; y <= 25; y++)
			for (int x = 22; x <= 25; x++)
				put(upper, x, y, "floor");
		for (int x = 22; x <= 25; x++)
		{
			put(upper, x, 22, "wallUR");
			put(upper, x, 25, "wallLL");
		}
		for (int y = 22; y <= 25; y++)
		{
			put(upper, 22, y, "wallUL");
			put(upper, 25, y, "wallLR");
		}
		put(upper, 27, 22, "largeRadiator");
		_station.setCurrentLevel(0);
	}
}

int main(int argc, char** argv)
{
	const std::string mode = argc > 1 ? argv[1] : "demo";
	const std::string outFile = argc > 2 ? argv[2] : "../prototypes/raylib3d/scene.txt";
	textureManager textures;
	content::load("content");
	content::loadArt(textures);
	simulationManager sim;
	world w(textures, sim);
	vessel& home = w.add("Haxel_Port", false);
	if (mode == "save")
	{
		gameManager files;
		if (!worldFiles::load(w, files, textures))
		{
			std::cout << "no saved station to load (Haxel_Port/0.level): run the game and save first" << std::endl;
			return 1;
		}
	}
	else
	{
		std::vector<std::string> problems;
		const std::vector<blueprint> library = blueprints::loadAll(blueprints::folderName, problems);
		buildStation(home.layout, library);
		//a ship docked at the jetty, and one out in space
		vessel& docked = w.newShip();
		w.newShip();
		for (int i = 0; i < 10; i++)
			sim.step();
		if (!w.docksOf(home).empty())
		{
			const dockPoint dock = w.docksOf(home)[0];
			std::string why;
			w.startDocking(docked, dock, &why);
			docked.flight.position = docked.flight.berth;
			docked.flight.heading = docked.flight.berthHeading;
			docked.flight.velocity = vec2(0.0, 0.0);
			if (!w.dockNow(docked, dock, &why))
				std::cout << "couldn't dock the first ship: " << why << std::endl;
		}
		for (int i = 0; i < 10; i++)
			sim.step();
	}
	std::ofstream out(outFile);
	if (!out)
	{
		std::cout << "can't write " << outFile << std::endl;
		return 1;
	}
	out << "# StationBuilder scene for the raylib 3D prototype, written by tools/sceneExport.cpp (" << mode << ")\n";
	for (int i = 0; i < w.count(); i++)
		writeVessel(out, w, w.at(i));
	std::cout << "wrote " << w.count() << " vessels to " << outFile << std::endl;
	return 0;
}
