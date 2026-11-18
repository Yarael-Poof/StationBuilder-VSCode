#include "lineArt.h"
#include "station.h"
#include "tileCatalogue.h"
#include "tileNetwork.h"
#include <SFML/Graphics/Image.hpp>
#include <cmath>

namespace
{
	//the neighbour across each side: UL x - 1, UR y - 1, LL y + 1, LR x + 1
	sf::Vector2i across(int _side)
	{
		static const sf::Vector2i steps[4] = { sf::Vector2i(-1, 0), sf::Vector2i(0, -1), sf::Vector2i(0, 1), sf::Vector2i(1, 0) };
		return steps[_side];
	}

	bool isPlainPipe(const tileTypeInfo& _type)
	{
		return _type.fluid.carriesLiquid && !_type.fluid.pump && !_type.fluid.tank && _type.fluid.exchange == 0.0;
	}

	//the medium a piece carries: power for cables and risers, liquid for pipes, pumps, tanks and exchangers. nullptr
	//for anything else
	const networkMedium* mediumOf(const tileTypeInfo& _type)
	{
		if (_type.electrical.conductor)
			return &electricalMedium;
		if (_type.items.tube)
			return &itemsMedium;
		if (_type.fluid.carriesLiquid)
			return &fluidMedium;
		return nullptr;
	}

	int connectsOn(const networkMedium& _medium, const tileObject& _object)
	{
		if (!_object.type || !_medium.conducts(_object))
			return 0;
		return _medium.connectsOn ? _medium.connectsOn(_object) : connections::all;
	}

	std::vector<double> defaultSettings(const tileTypeInfo& _type)
	{
		std::vector<double> values;
		for (const itemSetting& setting : _type.settings)
			values.push_back(setting.defaultValue);
		return values;
	}

	//the four-way art with the arms not in _mask cleared: everything outside the middle (and a riser's upright
	//conduit) belongs to the arm of its quarter
	sf::Image cutArms(const sf::Image& _cross, int _mask, bool _keepConduit)
	{
		sf::Image cut = _cross;
		const sf::Vector2u size = _cross.getSize();
		for (unsigned y = 0; y < size.y; y++)
		{
			for (unsigned x = 0; x < size.x; x++)
			{
				//relative to the middle of the floor diamond (32, 46.5); the arms run out to the four edge midpoints
				const float dx = float(x) + 0.5f - 32.f;
				const float dy = float(y) + 0.5f - 46.5f;
				const bool middle = std::abs(dx) / (_keepConduit ? 13.f : 10.f) + std::abs(dy) / (_keepConduit ? 7.f : 5.5f) <= 1.f;
				const bool conduit = _keepConduit && std::abs(dx) <= 6.f && dy < 0.f;
				if (middle || conduit)
					continue;
				const int side = dy < 0.f ? (dx < 0.f ? 0 : 1) : (dx < 0.f ? 2 : 3);
				if (!(_mask & (1 << side)))
					cut.setPixel(x, y, sf::Color::Transparent);
			}
		}
		return cut;
	}
}

namespace lineArt
{
	std::string variantKey(const std::string& _key, int _mask)
	{
		return _key + "#" + std::to_string(_mask & connections::all);
	}

	bool isLinePiece(const tileObject& _object)
	{
		return _object.type && (_object.type->electrical.conductor || _object.type->items.tube || isPlainPipe(*_object.type));
	}

	void makeVariants(textureManager& _textures, const std::string& _key, const std::string& _file, bool _keepConduit)
	{
		sf::Image cross;
		if (!cross.loadFromFile(_file))
			return;
		for (int mask = 0; mask <= connections::all; mask++)
			_textures.addTexture(variantKey(_key, mask), cutArms(cross, mask, _keepConduit));
	}

	void makeDeviceVariants(textureManager& _textures, const std::string& _key, const std::string& _file, const std::string& _pipeFile)
	{
		sf::Image device, cross;
		if (!device.loadFromFile(_file) || !cross.loadFromFile(_pipeFile))
			return;
		for (int mask = 0; mask <= connections::all; mask++)
		{
			sf::Image combined = cutArms(cross, mask, false);
			combined.copy(device, 0, 0, sf::IntRect(), true);   //the device drawn over its pipe stubs
			_textures.addTexture(variantKey(_key, mask), combined);
		}
	}

	int ownSides(const tileTypeInfo& _type)
	{
		const networkMedium* medium = mediumOf(_type);
		if (!medium)
			return 0;
		//unturned, as the menu shows it: a machine's ports of its kind on its first tile; anything else as placed
		if (!_type.ports.empty())
		{
			int sides = 0;
			const portInfo::kind kind = medium == &electricalMedium ? portInfo::kind::power : medium == &itemsMedium ? portInfo::kind::items : portInfo::kind::liquid;
			for (const portInfo& port : _type.ports)
			{
				if (port.carries == kind && port.cell == sf::Vector2i(0, 0))
					sides |= 1 << port.side;
			}
			return sides;
		}
		if (_type.fluid.pump)
			return _type.fluid.pumpSide >= 0 ? (1 << _type.fluid.pumpSide) | (1 << connections::opposite(_type.fluid.pumpSide)) : 0;
		if (_type.fluid.tank || _type.fluid.exchange > 0.0)
			return connections::all;
		return connections::sides(_type, defaultSettings(_type));
	}

	int linkedSides(station& _station, int _level, sf::Vector2i _tile, const tileObject& _object)
	{
		const networkMedium* kind = _object.type ? mediumOf(*_object.type) : nullptr;
		if (!kind || _level < 0 || _level >= _station.getLayerListSize())
			return 0;
		const networkMedium& medium = *kind;
		const int own = connectsOn(medium, _object);
		isometricLevel* level = _station.getIsoLevel(float(_level));
		int linked = 0;
		for (int side = 0; side < 4; side++)
		{
			if (!(own & (1 << side)))
				continue;
			const sf::Vector2i next = _tile + across(side);
			if (!level->isInLevel(next))
				continue;
			int back = 0;
			level->levelTiles[level->coords2TileIndex(next.x, next.y)].forEachObject([&](tileObject* _other) {
				back |= connectsOn(medium, *_other);
			});
			if (back & (1 << connections::opposite(side)))
				linked |= 1 << side;
		}
		return linked;
	}

	void update(station& _station, textureManager& _textures)
	{
		for (int lv = 0; lv < _station.getLayerListSize(); lv++)
		{
			isometricLevel* level = _station.getIsoLevel(float(lv));
			const int width = int(level->getSizeW());
			for (int i = 0; width > 0 && i < int(level->levelTiles.size()); i++)
			{
				const sf::Vector2i tile(i % width + 1, i / width + 1);
				level->levelTiles[i].forEachObject([&](tileObject* _object) {
					//cables, risers and pipes, and the pumps and exchangers drawn over pipe stubs (pieces with no
					//variants, like tanks, keep their art; parts of big machines draw nothing)
					if (!_object->type || _object->isPart() || !mediumOf(*_object->type) || !_textures.hasTexture(variantKey(_object->artKey(), 0)))
						return;
					const std::string key = variantKey(_object->artKey(), linkedSides(_station, lv, tile, *_object));
					if (_textures.hasTexture(key))
						_object->setTexture(*_textures.getTexture(key));
				});
			}
		}
	}
}
