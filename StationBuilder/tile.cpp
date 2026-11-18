#include "tile.h"
#include "tileCatalogue.h"
#include <algorithm>

namespace
{
	bool isPlainPipe(const tileTypeInfo& _type)
	{
		const fluidProperties& f = _type.fluid;
		return f.carriesLiquid && !f.pump && !f.tank && f.exchange == 0.0;
	}

	//cables and pipes lie on the floor, under the other fixtures on the tile
	bool isUtilityLine(const tileObject* _object)
	{
		return _object->type && (_object->type->electrical.conductor || _object->type->items.tube || isPlainPipe(*_object->type));
	}
}

tile::tile(sf::Vector2f _worldPosition, textureManager* _textureMaster, std::string _tileObjectCSVList)
{	//every tile gets the blank background sprite, then each comma separated key in _tileObjectCSVList is
	//placed in its slot in the order it was saved (background, floor, walls, fixtures), using the normal rules.
	backgroundSlot = new tileObject(_textureMaster, "empty", _worldPosition);
	//keys are normally comma separated, but saves made before the save fix put the commas in the wrong
	//places (e.g. "emptywallUR,floor,"). so rather than splitting on commas, read the string as a run of
	//registered texture keys, always taking the longest key that matches at the current position.
	//a key can be followed by the object's own settings in braces: "generator{on=0;v=240}"
	size_t pos = 0;
	while (pos < _tileObjectCSVList.size())
	{
		if (_tileObjectCSVList[pos] == ',' || _tileObjectCSVList[pos] == ' ')
		{
			pos++;
			continue;
		}
		std::string key;
		for (int i = 1; i <= _textureMaster->getTextureMapSize(); i++)
		{
			std::string candidate = _textureMaster->getTextureKey(i);
			if (candidate.size() > key.size() && _tileObjectCSVList.compare(pos, candidate.size(), candidate) == 0)
				key = candidate;
		}
		if (key.empty())
		{
			std::cout << "[ERROR] saved tile has unknown texture data '" << _tileObjectCSVList.substr(pos) << "', rest of tile skipped\n";
			break;
		}
		pos += key.size();
		std::string settingsText;
		bool malformed = false;
		if (pos < _tileObjectCSVList.size() && _tileObjectCSVList[pos] == '{')
		{
			const size_t close = _tileObjectCSVList.find('}', pos);
			if (close == std::string::npos)
			{
				std::cout << "[ERROR] saved " << key << " has settings with no closing '}', defaults used and rest of tile skipped\n";
				malformed = true;
				pos = _tileObjectCSVList.size();
			}
			else
			{
				settingsText = _tileObjectCSVList.substr(pos + 1, close - pos - 1);
				pos = close + 1;
			}
		}
		if (key == "empty")
			continue;
		tileObject* object = new tileObject(_textureMaster, key, _worldPosition);
		if (!malformed && !settingsText.empty())
			object->applySettingsText(settingsText);
		if (!placeObject(object))
		{
			std::cout << "[ERROR] saved tile stack refused " << key << ", skipped\n";
			delete object;
		}
	}
}
tile::tile()
{

}
tile::~tile() {}

bool tile::hasAnyWall() const
{
	for (tileObject* wall : wallSlots)
	{
		if (wall)
			return true;
	}
	return false;
}

bool tile::canPlace(tileLayer _layer, int _wallEdge) const
{
	switch (_layer)
	{
	case tileLayer::floor:
		return floorSlot == nullptr;

	case tileLayer::wall:
		return _wallEdge >= 0 && _wallEdge <= 3 && wallSlots[_wallEdge] == nullptr;

	case tileLayer::fixture:
		//fixtures need something to sit on, and any number of them can share it
		return floorSlot != nullptr || hasAnyWall();

	default:
		//the background and unknown keys can't be placed by the player
		return false;
	}
}

bool tile::canPlace(const tileTypeInfo& _type) const
{
	return canPlaceInstead(_type, nullptr);
}

bool tile::canPlaceInstead(const tileTypeInfo& _type, const tileObject* _replacing) const
{
	//an item whose definition has gone from the content files can't be placed again, or loaded from a save (as an
	//unknown key isn't); the ones already placed stay
	if (_type.removed)
		return false;
	//outside the hull: a bare tile (no floor). inside: a floor or wall to sit on
	if (_type.outside ? floorSlot != nullptr || _type.layer != tileLayer::fixture : !canPlace(_type.layer, _type.wallEdge))
		return false;
	//nor a floor under something mounted outside (it would be inside then)
	if (_type.layer == tileLayer::floor)
	{
		for (const tileObject* fixture : fixtureSlots)
			if (fixture->type && fixture->type->outside)
				return false;
	}
	//a machine bigger than one tile has its tiles to itself: it needs them free of fixtures, and nothing joins it
	if (_type.layer == tileLayer::fixture)
	{
		for (const tileObject* fixture : fixtureSlots)
		{
			if (fixture == _replacing || !fixture->type)
				continue;
			if (footprint::isMultiTile(_type) || footprint::isMultiTile(*fixture->type))
				return false;
		}
	}
	//one cable (or cable riser), one plain pipe (or pipe riser), one item tube (or tube riser) and one air duct riser per tile
	const bool cable = _type.electrical.conductor;
	const bool pipe = isPlainPipe(_type);
	const bool duct = _type.thermal.ductConductance > 0.0;
	const bool tube = _type.items.tube;
	if (!cable && !pipe && !duct && !tube)
		return true;
	for (const tileObject* fixture : fixtureSlots)
	{
		if (!fixture->type || fixture == _replacing)
			continue;
		if (cable && fixture->type->electrical.conductor)
			return false;
		if (pipe && isPlainPipe(*fixture->type))
			return false;
		if (duct && fixture->type->thermal.ductConductance > 0.0)
			return false;
		if (tube && fixture->type->items.tube)
			return false;
	}
	return true;
}

bool tile::replaceFixture(tileObject* _old, tileObject* _replacement)
{
	const auto at = std::find(fixtureSlots.begin(), fixtureSlots.end(), _old);
	if (at == fixtureSlots.end() || _replacement->layer != tileLayer::fixture)
		return false;
	if (_replacement->type ? !canPlaceInstead(*_replacement->type, _old) : false)
		return false;
	*at = _replacement;
	delete _old;
	return true;
}

bool tile::placeObject(tileObject* _object)
{
	if (_object->type ? !canPlace(*_object->type) : !canPlace(_object->layer, _object->wallEdge))
		return false;

	switch (_object->layer)
	{
	case tileLayer::floor:
		floorSlot = _object;
		return true;
	case tileLayer::wall:
		wallSlots[_object->wallEdge] = _object;
		return true;
	case tileLayer::fixture:
		//fixtures draw in list order, so lines go in before the first fixture that isn't a line
		if (isUtilityLine(_object))
			fixtureSlots.insert(std::find_if(fixtureSlots.begin(), fixtureSlots.end(),
				[](const tileObject* _other) { return !isUtilityLine(_other); }), _object);
		else
			fixtureSlots.push_back(_object);
		return true;
	default:
		return false;
	}
}

void tile::removeTileObject(tileObject* _object)
{
	if (floorSlot == _object)
		floorSlot = nullptr;
	for (tileObject*& wall : wallSlots)
	{
		if (wall == _object)
			wall = nullptr;
	}
	fixtureSlots.remove(_object);
}

bool tile::removeTopObject()
{
	//fixtures always go first, so nothing is left floating when a wall or floor under it is removed
	if (!fixtureSlots.empty())
	{
		delete fixtureSlots.back();
		fixtureSlots.pop_back();
		return true;
	}
	for (int edge = 3; edge >= 0; edge--)
	{
		if (wallSlots[edge])
		{
			delete wallSlots[edge];
			wallSlots[edge] = nullptr;
			return true;
		}
	}
	if (floorSlot)
	{
		delete floorSlot;
		floorSlot = nullptr;
		return true;
	}
	return false;
}

void tile::clearTile()
{
	floorSlot = nullptr;
	for (tileObject*& wall : wallSlots)
		wall = nullptr;
	fixtureSlots.clear();
}

std::vector<tileObject*> tile::allObjects() const
{
	std::vector<tileObject*> objects;
	if (backgroundSlot)
		objects.push_back(backgroundSlot);
	if (floorSlot)
		objects.push_back(floorSlot);
	for (tileObject* wall : wallSlots)
	{
		if (wall)
			objects.push_back(wall);
	}
	for (tileObject* fixture : fixtureSlots)
		objects.push_back(fixture);
	return objects;
}

void tile::drawAllObjects(sf::RenderWindow& _window)
{	//draw every object on this tile in slot order
	for (tileObject* object : allObjects())
	{
		_window.draw(*object);
	}
}
void tile::printAllObjectsInTile()
{
	std::vector<tileObject*> objects = allObjects();
	for (tileObject* object : objects)
	{
		std::cout << " has " << objects.size() << " objects at: " << std::endl;
		std::cout << object->GetWorldPosition().x << "," << object->GetWorldPosition().y << std::endl;
	}

}

sf::Vector2f tile::getRootObjectCoords()
{
	if (!backgroundSlot)
		return sf::Vector2f(0.f, 0.f);
	return backgroundSlot->getPosition();
}

void tile::setTileObjectColour(sf::Color _colour)
{
	for (tileObject* object : allObjects())
	{
		object->setColor(_colour);
	}
}
