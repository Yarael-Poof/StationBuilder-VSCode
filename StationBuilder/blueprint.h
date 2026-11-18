#pragma once
#include <SFML/System/Vector2.hpp>
#include <string>
#include <vector>

class isometricLevel;

//one thing in a blueprint
struct blueprintObject
{
	std::string key;          //the item
	std::string settings;     //its settings as saves write them ("c=10;r=1"), without what it holds
	sf::Vector2i at;          //its tile, counted from the blueprint's upper tile (0, 0). a machine: its upper tile
};

//a set of tiles' floors, walls and fixtures, kept to place again: copied from the station, or read from a file in
//blueprints/ (one TOML file each). what containers and tanks hold isn't part of it: they are placed as new ones are
//(a tank full of its liquid; "@empty" in its settings asks for an empty one). settings are: shapes, turns, recipes,
//switches, filters...
struct blueprint
{
	std::string name;
	std::string description;
	sf::Vector2i size = sf::Vector2i(0, 0);   //tiles along x (towards the lower right) and y (towards the lower left)
	std::vector<blueprintObject> objects;     //floors, then walls, then fixtures in the order they were on their tiles
	std::string file;                         //the file it was read from or saved to, "" if it was only copied
	bool example = false;                     //one of the game's examples (blueprints/examples/)

	bool empty() const { return objects.empty(); }
	//the same blueprint a quarter turn clockwise on screen, as R turns a machine: its tiles turned, each machine turned,
	//line pieces' shapes and walls' edges turned with it
	blueprint turned() const;
	//where its upper tile goes when it is placed with the tile _pointed at its middle
	sf::Vector2i originFor(sf::Vector2i _pointed) const;
};

namespace blueprints
{
	//where the game keeps them (in the game folder): the player's, and the game's examples in examples/
	const char* const folderName = "blueprints";
	//what is on the tiles _from.._to of a level (corners either way round), trimmed to the tiles that have something.
	//a machine reaching outside them is left out; _leftOut (if given) counts those
	blueprint copy(isometricLevel& _level, sf::Vector2i _from, sf::Vector2i _to, int* _leftOut = nullptr);

	//one object of a blueprint placed with its upper tile at _origin: the tile it would go on (a machine: every tile it
	//would cover), whether it would go, and whether the same thing is there already (left as it is)
	struct objectCheck
	{
		std::vector<sf::Vector2i> tiles;
		bool fits = true;
		bool already = false;
	};
	std::vector<objectCheck> check(isometricLevel& _level, const blueprint& _blueprint, sf::Vector2i _origin);

	//what placing it did
	struct pasteResult
	{
		int placed = 0;
		int already = 0;           //the same thing was there: left as it was
		int refused = 0;           //a tile wouldn't take it (something in the way, or off the level)
		std::string firstRefused;  //"Cable at 5,6"
	};
	//places it with its upper tile at _origin: floors, then walls, then fixtures, each as the build menu would.
	//whatever a tile refuses is left out and counted, the rest is placed
	pasteResult paste(isometricLevel& _level, const blueprint& _blueprint, sf::Vector2i _origin);

	//its file text (TOML: name, description, size, objects = [ { key, at, settings } ])
	std::string toText(const blueprint& _blueprint);
	//reads a file's text. false (and _error says why) if it isn't a blueprint
	bool fromText(const std::string& _text, blueprint& _out, std::string& _error);
	//writes it to _path (making the folder if needed), and remembers that as its file
	bool save(blueprint& _blueprint, const std::string& _path, std::string& _error);
	bool load(const std::string& _path, blueprint& _out, std::string& _error);
	//every blueprint in a folder and its subfolders, the examples (in examples/) first, then by name. a file that won't
	//read is left out, with a line in _problems
	std::vector<blueprint> loadAll(const std::string& _folder, std::vector<std::string>& _problems);
	//a file name from a blueprint's name: "Iron line 2" -> "iron_line_2.toml"
	std::string fileName(const std::string& _name);
}
