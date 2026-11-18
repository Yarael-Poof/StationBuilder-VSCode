#include "blueprint.h"
#include "contentWriter.h"
#include "isometricLevel.h"
#include "tileCatalogue.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace
{
	namespace fs = std::filesystem;

	//one value from settings as saves write them ("c=10;r=1"), _fallback if it isn't there
	double settingIn(const std::string& _settings, const std::string& _key, double _fallback)
	{
		std::istringstream entries(_settings);
		std::string entry;
		while (std::getline(entries, entry, ';'))
		{
			if (entry.rfind(_key + "=", 0) != 0)
				continue;
			try
			{
				return std::stod(entry.substr(_key.size() + 1));
			}
			catch (...)
			{
				return _fallback;
			}
		}
		return _fallback;
	}
	//the same settings with one value set (added if it isn't there)
	std::string withSetting(const std::string& _settings, const std::string& _key, int _value)
	{
		std::string result;
		bool found = false;
		std::istringstream entries(_settings);
		std::string entry;
		while (std::getline(entries, entry, ';'))
		{
			if (entry.empty())
				continue;
			if (entry.rfind(_key + "=", 0) == 0)
			{
				entry = _key + "=" + std::to_string(_value);
				found = true;
			}
			result += (result.empty() ? "" : ";") + entry;
		}
		if (!found)
			result += (result.empty() ? "" : ";") + _key + "=" + std::to_string(_value);
		return result;
	}

	int turnsOf(const blueprintObject& _object)
	{
		return ((int(settingIn(_object.settings, footprint::rotationKey, 0.0)) % 4) + 4) % 4;
	}
	//the tiles a blueprint object covers, from its upper tile (a machine: its turned footprint)
	std::vector<sf::Vector2i> cellsOf(const blueprintObject& _object, const tileTypeInfo* _type, sf::Vector2i _upper)
	{
		if (!_type || !footprint::isMachine(*_type))
			return { _upper };
		const sf::Vector2i size = footprint::turnedSize(*_type, turnsOf(_object));
		std::vector<sf::Vector2i> cells;
		for (int y = 0; y < size.y; y++)
			for (int x = 0; x < size.x; x++)
				cells.push_back(_upper + sf::Vector2i(x, y));
		return cells;
	}
	//the tile to point at to place it there (a machine is placed with the pointed tile at its middle)
	sf::Vector2i pointFor(const blueprintObject& _object, const tileTypeInfo* _type, sf::Vector2i _upper)
	{
		if (!_type || !footprint::isMachine(*_type))
			return _upper;
		const sf::Vector2i size = footprint::turnedSize(*_type, turnsOf(_object));
		return _upper + sf::Vector2i((size.x - 1) / 2, (size.y - 1) / 2);
	}

	tile* tileAt(isometricLevel& _level, sf::Vector2i _at)
	{
		return _level.isInLevel(_at) ? &_level.levelTiles[_level.coords2TileIndex(_at.x, _at.y)] : nullptr;
	}
	bool hasLayer(const tile& _tile, tileLayer _layer)
	{
		bool found = false;
		_tile.forEachObject([&](tileObject* _o) { found = found || (_o->type && _o->type->layer == _layer); });
		return found;
	}
	//the same object (key, and settings for a fixture; a machine also its upper tile) is there already
	bool alreadyThere(isometricLevel& _level, const blueprintObject& _object, const tileTypeInfo& _type, sf::Vector2i _upper)
	{
		tile* t = tileAt(_level, _upper);
		if (!t)
			return false;
		if (_type.layer == tileLayer::floor)
			return hasLayer(*t, tileLayer::floor);
		bool found = false;
		t->forEachObject([&](tileObject* _o) {
			const tileObject& m = _o->machine();
			if (found || m.tileTextureKey != _object.key)
				return;
			if (_type.layer == tileLayer::wall)
				found = true;
			else if (footprint::isMachine(_type))
				found = m.origin == _upper && m.turns() == turnsOf(_object);
			else
				found = m.settingsText(false) == _object.settings;
		});
		return found;
	}
}

blueprint blueprint::turned() const
{
	//a quarter turn clockwise: (x, y) -> (h - 1 - y, x), in a w x h blueprint that becomes h x w (as footprint::turnCell)
	blueprint result = *this;
	result.size = sf::Vector2i(size.y, size.x);
	for (blueprintObject& object : result.objects)
	{
		const tileTypeInfo* type = findTileType(object.key);
		if (type && footprint::isMachine(*type))
		{
			//its footprint's tiles turn; its upper tile is the least of them. and it turns a quarter itself
			const sf::Vector2i s = footprint::turnedSize(*type, turnsOf(object));
			object.at = sf::Vector2i(size.y - object.at.y - s.y, object.at.x);
			object.settings = withSetting(object.settings, footprint::rotationKey, (turnsOf(object) + 1) % 4);
			continue;
		}
		object.at = sf::Vector2i(size.y - 1 - object.at.y, object.at.x);
		if (type && findSetting(*type, connections::settingKey))
			object.settings = withSetting(object.settings, connections::settingKey,
				connections::rotate(int(settingIn(object.settings, connections::settingKey, connections::all))));
		else if (familyOf(object.key))
			object.key = rotateKey(object.key, 1);   //a wall onto the turned edge
	}
	return result;
}

sf::Vector2i blueprint::originFor(sf::Vector2i _pointed) const
{
	return _pointed - sf::Vector2i((size.x - 1) / 2, (size.y - 1) / 2);
}

namespace blueprints
{
	blueprint copy(isometricLevel& _level, sf::Vector2i _from, sf::Vector2i _to, int* _leftOut)
	{
		blueprint result;
		if (_leftOut)
			*_leftOut = 0;
		const sf::Vector2i lo(std::min(_from.x, _to.x), std::min(_from.y, _to.y)), hi(std::max(_from.x, _to.x), std::max(_from.y, _to.y));
		auto inside = [&](sf::Vector2i _at) { return _at.x >= lo.x && _at.y >= lo.y && _at.x <= hi.x && _at.y <= hi.y; };
		std::vector<blueprintObject> floors, walls, fixtures;
		std::set<const tileObject*> machinesSeen;   //a big machine is met on each of its tiles: it is taken (or not) once
		sf::Vector2i least(1 << 30, 1 << 30), most(-(1 << 30), -(1 << 30));
		auto include = [&](sf::Vector2i _at) {
			least = sf::Vector2i(std::min(least.x, _at.x), std::min(least.y, _at.y));
			most = sf::Vector2i(std::max(most.x, _at.x), std::max(most.y, _at.y));
		};
		for (int y = lo.y; y <= hi.y; y++)
		{
			for (int x = lo.x; x <= hi.x; x++)
			{
				tile* t = tileAt(_level, sf::Vector2i(x, y));
				if (!t)
					continue;
				t->forEachObject([&](tileObject* _o) {
					if (!_o->type || _o->type->layer == tileLayer::background)
						return;
					tileObject* machine = &_o->machine();
					if (footprint::isMachine(*machine->type) && !machinesSeen.insert(machine).second)
						return;
					blueprintObject object{ machine->tileTextureKey, machine->settingsText(false), sf::Vector2i(x, y) };
					if (footprint::isMachine(*machine->type))
					{
						//a machine: all of it, or none of it (one reaching outside the selection is met on a tile inside it,
						//perhaps only by a part)
						const std::vector<sf::Vector2i> covers = _level.machineTiles(*machine);
						if (!std::all_of(covers.begin(), covers.end(), inside))
						{
							if (_leftOut)
								(*_leftOut)++;
							return;
						}
						object.at = machine->origin;
						for (const sf::Vector2i& at : covers)
							include(at);
					}
					else
						include(object.at);
					(_o->type->layer == tileLayer::floor ? floors : _o->type->layer == tileLayer::wall ? walls : fixtures).push_back(object);
				});
			}
		}
		if (floors.empty() && walls.empty() && fixtures.empty())
			return result;
		for (std::vector<blueprintObject>* list : { &floors, &walls, &fixtures })
		{
			for (blueprintObject& object : *list)
			{
				object.at -= least;
				result.objects.push_back(object);
			}
		}
		result.size = most - least + sf::Vector2i(1, 1);
		return result;
	}

	std::vector<objectCheck> check(isometricLevel& _level, const blueprint& _blueprint, sf::Vector2i _origin)
	{
		//the tiles the blueprint itself gives something to stand on
		std::set<std::pair<int, int>> standing;
		for (const blueprintObject& object : _blueprint.objects)
		{
			const tileTypeInfo* type = findTileType(object.key);
			if (type && (type->layer == tileLayer::floor || type->layer == tileLayer::wall))
				standing.insert({ object.at.x, object.at.y });
		}
		std::vector<objectCheck> checks;
		for (const blueprintObject& object : _blueprint.objects)
		{
			objectCheck c;
			const tileTypeInfo* type = findTileType(object.key);
			const sf::Vector2i upper = _origin + object.at;
			c.tiles = cellsOf(object, type, upper);
			for (const sf::Vector2i& at : c.tiles)
				c.fits = c.fits && _level.isInLevel(at);
			if (!type || type->removed || !c.fits)
			{
				c.fits = false;
				checks.push_back(c);
				continue;
			}
			c.already = alreadyThere(_level, object, *type, upper);
			if (c.already)
			{
				checks.push_back(c);
				continue;
			}
			if (type->layer == tileLayer::floor)
				c.fits = true;
			else if (type->layer == tileLayer::wall)
				c.fits = _level.canAddToTile(sf::Vector2f(upper), object.key, object.settings);
			else
			{
				//a fixture: each of its tiles takes it, or is bare and gets a floor (or wall) from the blueprint first
				for (const sf::Vector2i& at : c.tiles)
				{
					const tile& t = *tileAt(_level, at);
					if (hasLayer(t, tileLayer::floor) || hasLayer(t, tileLayer::wall))
						c.fits = c.fits && t.canPlace(*type);
					else
						c.fits = c.fits && standing.count({ at.x - _origin.x, at.y - _origin.y }) > 0;
				}
			}
			checks.push_back(c);
		}
		return checks;
	}

	pasteResult paste(isometricLevel& _level, const blueprint& _blueprint, sf::Vector2i _origin)
	{
		pasteResult result;
		for (const blueprintObject& object : _blueprint.objects)
		{
			const tileTypeInfo* type = findTileType(object.key);
			const sf::Vector2i upper = _origin + object.at;
			bool onLevel = true;
			for (const sf::Vector2i& at : cellsOf(object, type, upper))
				onLevel = onLevel && _level.isInLevel(at);
			if (type && onLevel && alreadyThere(_level, object, *type, upper))
			{
				result.already++;
				continue;
			}
			const sf::Vector2i pointed = pointFor(object, type, upper);
			if (type && onLevel && _level.addObjectToTile(sf::Vector2f(pointed), object.key, object.settings))
			{
				result.placed++;
				continue;
			}
			result.refused++;
			if (result.firstRefused.empty())
				result.firstRefused = (type ? type->name : "\"" + object.key + "\" (not an item)") + " at " + std::to_string(upper.x) + "," + std::to_string(upper.y);
		}
		return result;
	}

	std::string toText(const blueprint& _blueprint)
	{
		std::string text = "# a Station Builder blueprint: place it from the Blueprints menu (B)\n";
		text += "name = " + contentWriter::quoted(_blueprint.name) + "\n";
		if (!_blueprint.description.empty())
			text += "description = " + contentWriter::quoted(_blueprint.description) + "\n";
		text += "size = [" + std::to_string(_blueprint.size.x) + ", " + std::to_string(_blueprint.size.y) + "]\n";
		text += "# each thing: its item, its tile from the upper tile [0, 0] (a machine's upper tile), and its settings as saves write them\n";
		text += "objects = [\n";
		for (const blueprintObject& object : _blueprint.objects)
		{
			text += "    { key = " + contentWriter::quoted(object.key) + ", at = [" + std::to_string(object.at.x) + ", " + std::to_string(object.at.y) + "]";
			if (!object.settings.empty())
				text += ", settings = " + contentWriter::quoted(object.settings);
			text += " },\n";
		}
		text += "]\n";
		return text;
	}

	bool fromText(const std::string& _text, blueprint& _out, std::string& _error)
	{
		_out = blueprint();
		toml::table file;
		try
		{
			file = toml::parse(_text);
		}
		catch (const toml::parse_error& error)
		{
			_error = "line " + std::to_string(error.source().begin.line) + ": " + std::string(error.description());
			return false;
		}
		_out.name = file["name"].value_or(std::string());
		_out.description = file["description"].value_or(std::string());
		const toml::array* objects = file["objects"].as_array();
		if (!objects)
		{
			_error = "it has no objects = [ ... ] list";
			return false;
		}
		sf::Vector2i most(0, 0);
		int index = 0;
		for (const toml::node& node : *objects)
		{
			index++;
			const toml::table* entry = node.as_table();
			const toml::array* at = entry ? (*entry)["at"].as_array() : nullptr;
			if (!entry || !(*entry)["key"].is_string() || !at || at->size() != 2 || !(*at)[0].is_integer() || !(*at)[1].is_integer())
			{
				_error = "object " + std::to_string(index) + ": needs key = \"...\" and at = [x, y]";
				return false;
			}
			blueprintObject object;
			object.key = (*entry)["key"].value_or(std::string());
			object.at = sf::Vector2i(int((*at)[0].value_or(int64_t(0))), int((*at)[1].value_or(int64_t(0))));
			object.settings = (*entry)["settings"].value_or(std::string());
			if (object.at.x < 0 || object.at.y < 0)
			{
				_error = "object " + std::to_string(index) + ": at can't be below [0, 0]";
				return false;
			}
			const tileTypeInfo* type = findTileType(object.key);
			for (const sf::Vector2i& cell : cellsOf(object, type, object.at))
				most = sf::Vector2i(std::max(most.x, cell.x + 1), std::max(most.y, cell.y + 1));
			_out.objects.push_back(object);
		}
		_out.size = most;
		if (const toml::array* size = file["size"].as_array(); size && size->size() == 2 && (*size)[0].is_integer() && (*size)[1].is_integer())
			_out.size = sf::Vector2i(std::max(most.x, int((*size)[0].value_or(int64_t(0)))), std::max(most.y, int((*size)[1].value_or(int64_t(0)))));
		return true;
	}

	bool save(blueprint& _blueprint, const std::string& _path, std::string& _error)
	{
		std::error_code problem;
		const fs::path path(_path);
		if (path.has_parent_path())
			fs::create_directories(path.parent_path(), problem);
		std::ofstream out(_path, std::ios::binary | std::ios::trunc);
		out << toText(_blueprint);
		if (!out)
		{
			_error = "couldn't write " + _path;
			return false;
		}
		_blueprint.file = path.generic_string();
		return true;
	}

	bool load(const std::string& _path, blueprint& _out, std::string& _error)
	{
		std::ifstream in(_path, std::ios::binary);
		if (!in)
		{
			_error = "can't open it";
			return false;
		}
		std::stringstream text;
		text << in.rdbuf();
		if (!fromText(text.str(), _out, _error))
			return false;
		_out.file = fs::path(_path).generic_string();
		if (_out.name.empty())
			_out.name = fs::path(_path).stem().string();
		return true;
	}

	std::vector<blueprint> loadAll(const std::string& _folder, std::vector<std::string>& _problems)
	{
		std::vector<blueprint> found;
		std::error_code problem;
		std::vector<fs::path> paths;
		for (fs::recursive_directory_iterator it(_folder, problem), end; !problem && it != end; it.increment(problem))
			if (it->is_regular_file() && it->path().extension() == ".toml")
				paths.push_back(it->path());
		for (const fs::path& path : paths)
		{
			blueprint b;
			std::string error;
			if (!load(path.string(), b, error))
			{
				_problems.push_back(path.generic_string() + ": " + error);
				continue;
			}
			const std::string relative = fs::relative(path, _folder, problem).generic_string();
			b.example = relative.rfind("examples/", 0) == 0;
			found.push_back(b);
		}
		std::stable_sort(found.begin(), found.end(), [](const blueprint& _a, const blueprint& _b) {
			if (_a.example != _b.example)
				return _a.example;
			return _a.name < _b.name;
		});
		return found;
	}

	std::string fileName(const std::string& _name)
	{
		std::string name;
		for (char c : _name)
		{
			const unsigned char u = static_cast<unsigned char>(c);
			if (std::isalnum(u))
				name += char(std::tolower(u));
			else if (!name.empty() && name.back() != '_')
				name += '_';
		}
		while (!name.empty() && name.back() == '_')
			name.pop_back();
		return (name.empty() ? "blueprint" : name) + ".toml";
	}
}
