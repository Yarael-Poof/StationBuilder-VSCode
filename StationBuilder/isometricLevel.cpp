#include "isometricLevel.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include "tileCatalogue.h"
#include <imgui.h>
#include <cmath>

isometricLevel::isometricLevel()
{
	levelSizeH = 0;
	levelSizeW = 0;
	tileSize = 0;
	textureMaster = nullptr;
}
isometricLevel::isometricLevel(textureManager* _textureMaster, float _levelSizeW, float _levelSizeH, float _tileSize)
{	
	levelSizeW = _levelSizeW;
	levelSizeH = _levelSizeH;
	tileSize = _tileSize;
	textureMaster = _textureMaster;
	//std::cout << "Levelsize calc" << levelSizeH * levelSizeW << std::endl;
	//levelTiles.resize(levelSizeH * levelSizeW);
}

isometricLevel::~isometricLevel()
{
	levelTiles.clear();
}

int isometricLevel::coords2TileIndex(sf::Vector2f _tileCoords)
{
	return (int(_tileCoords.x) - 1) + (levelSizeW * (int(_tileCoords.y) - 1));
}
int isometricLevel::coords2TileIndex(int _tileX, int _tileY)
{
	return (_tileX - 1) + (levelSizeW * (_tileY - 1));
}
void isometricLevel::addFloor(sf::Vector2f _atWorldPosition)
{
	levelTiles.push_back(tile(_atWorldPosition,EMPTY));

}
void isometricLevel::clearAll()
{
	levelTiles.clear();
	markChanged();
}

std::uint64_t isometricLevel::newRevision()
{
	static std::uint64_t lastRevision = 0;
	return ++lastRevision;
}

std::uint64_t isometricLevel::getRevision() const
{
	return revision;
}

void isometricLevel::markChanged()
{
	revision = newRevision();
}

bool isometricLevel::changeObjectSetting(tileObject* _object, const std::string& _key, double _value)
{
	if (!_object)
		return false;
	const double before = _object->getSetting(_key);
	if (!_object->setSetting(_key, _value) || _object->getSetting(_key) == before)
		return false;
	markChanged();
	return true;
}
void isometricLevel::addFloor(float _worldX, float _worldY){}
void isometricLevel::addWall(sf::Vector2f _atWorldPosition, char _direction){}
void isometricLevel::addWall(float _worldX, float _worldY, char _direction){}

sf::Vector2f isometricLevel::isoToCart(sf::Vector2f _cart)
{
	sf::Vector2f iso(0.f, 0.f);
	iso.x = (2 * _cart.y + _cart.x) / 2;
	iso.y = (2 * _cart.y - _cart.x) / 2;
	return iso;
}

sf::Vector2f isometricLevel::cartToIso(sf::Vector2f _iso)
{
	sf::Vector2f cart(0.f, 0.f);
	cart.x = _iso.x - _iso.y;
	cart.y = (_iso.x + _iso.y) / 2;
	return cart;
}
void isometricLevel::fillFloors()
{
	for (int y = 0; y < levelSizeH; y++)
	{
		//std::cout<<std::endl;
		//std::cout << "At Y:" << y << std::endl;
		for(int x = 0; x <levelSizeW;x++)
		{
			//std::cout << "At X:" << tileSize/2 * x;
			
			//levelTiles.push_back(tile(sf::Vector2f(tileSize *x,tileSize*y), textureMaster, EMPTY));
			levelTiles.push_back(tile(cartToIso(sf::Vector2f(tileSize/2 * x, tileSize/2 * y)), textureMaster, "empty"));
		}

	}
	markChanged();
}

namespace
{
	//the turn ("r=N") in a settings text, 0 if it says none
	int turnsIn(const std::string& _settings)
	{
		const std::string key = std::string(footprint::rotationKey) + "=";
		size_t at = 0;
		while ((at = _settings.find(key, at)) != std::string::npos)
		{
			if (at == 0 || _settings[at - 1] == ';')
				return ((std::atoi(_settings.c_str() + at + key.size()) % 4) + 4) % 4;
			at += key.size();
		}
		return 0;
	}
}

bool isometricLevel::canAddToTile(sf::Vector2f _tileCoords, const std::string& _textureKey, const std::string& _settings)
{
	std::string aliasSettings;
	const tileTypeInfo* type = findTileType(resolveAlias(_textureKey, aliasSettings));
	if (!type)
		return false;
	if (!footprint::isMachine(*type))
	{
		const sf::Vector2i at(int(_tileCoords.x), int(_tileCoords.y));
		return isInLevel(at) && inArea(at) && levelTiles[coords2TileIndex(_tileCoords)].canPlace(*type) && (!type->outside || mountedOutside({ at }));
	}
	//a machine: every tile of its footprint, turned as the settings say
	const sf::Vector2i size = footprint::turnedSize(*type, _settings.empty() ? turnsIn(aliasSettings) : turnsIn(_settings));
	const sf::Vector2i origin = footprint::originFor(sf::Vector2i(int(_tileCoords.x), int(_tileCoords.y)), size);
	std::vector<sf::Vector2i> covers;
	for (int y = 0; y < size.y; y++)
	{
		for (int x = 0; x < size.x; x++)
		{
			const sf::Vector2i at = origin + sf::Vector2i(x, y);
			if (!isInLevel(at) || !inArea(at) || !levelTiles[coords2TileIndex(at.x, at.y)].canPlace(*type))
				return false;
			covers.push_back(at);
		}
	}
	return !type->outside || mountedOutside(covers);
}

bool isometricLevel::canReplaceOnTile(sf::Vector2f _tileCoords, const tileObject* _old, const std::string& _textureKey)
{
	const tileTypeInfo* type = findTileType(_textureKey);
	return type && type->layer == tileLayer::fixture && levelTiles[coords2TileIndex(_tileCoords)].canPlaceInstead(*type, _old);
}

bool isometricLevel::replaceObjectOnTile(sf::Vector2f _tileCoords, tileObject* _old, const std::string& _textureKey, const std::string& _settings)
{
	tile& target = levelTiles[coords2TileIndex(_tileCoords)];
	tileObject* newObject = new tileObject(textureMaster, _textureKey, target.getRootObjectCoords());
	if (!_settings.empty())
		newObject->applySettingsText(_settings);
	if (!target.replaceFixture(_old, newObject))
	{
		delete newObject;
		return false;
	}
	markChanged();
	return true;
}

bool isometricLevel::addObjectToTile(sf::Vector2f _tileCoords, std::string _textureKey, const std::string& _settings)
{
	tile& target = levelTiles[coords2TileIndex(_tileCoords)];
	sf::Vector2f currentTileScreenPosition = target.getRootObjectCoords();
	tileObject* newObject = new tileObject(textureMaster, _textureKey, currentTileScreenPosition);
	if (!_settings.empty())
		newObject->applySettingsText(_settings);
	if (newObject->type && footprint::isMachine(*newObject->type))
	{
		//a machine: over every tile of its footprint (turned), with the pointed tile at its middle
		const tileTypeInfo& type = *newObject->type;
		const sf::Vector2i size = footprint::turnedSize(type, newObject->turns());
		const sf::Vector2i origin = footprint::originFor(sf::Vector2i(int(_tileCoords.x), int(_tileCoords.y)), size);
		const sf::Vector2i front = origin + size - sf::Vector2i(1, 1);
		bool fits = true;
		std::vector<sf::Vector2i> covers;
		for (int y = 0; y < size.y && fits; y++)
		{
			for (int x = 0; x < size.x && fits; x++)
			{
				const sf::Vector2i at = origin + sf::Vector2i(x, y);
				fits = isInLevel(at) && inArea(at) && levelTiles[coords2TileIndex(at.x, at.y)].canPlace(type);
				covers.push_back(at);
			}
		}
		fits = fits && (!type.outside || mountedOutside(covers));
		newObject->origin = origin;
		newObject->cell = size - sf::Vector2i(1, 1);
		tile& frontTile = fits ? levelTiles[coords2TileIndex(front.x, front.y)] : target;
		if (!fits || !frontTile.placeObject(newObject))
		{
			delete newObject;
			return false;
		}
		for (int y = 0; y < size.y; y++)
		{
			for (int x = 0; x < size.x; x++)
			{
				if (sf::Vector2i(x, y) != newObject->cell)
					levelTiles[coords2TileIndex(origin.x + x, origin.y + y)].placeObject(tileObject::makePart(newObject, sf::Vector2i(x, y)));
			}
		}
		newObject->fitSprite(textureMaster, frontTile.getRootObjectCoords());
		markChanged();
		return true;
	}
	//one tile: in the deck's area, and something outside held on by the hull
	const sf::Vector2i at(int(_tileCoords.x), int(_tileCoords.y));
	if (!inArea(at) || (newObject->type && newObject->type->outside && !mountedOutside({ at })) || !target.placeObject(newObject))
	{
		delete newObject;
		return false;
	}
	markChanged();
	return true;
}

std::vector<sf::Vector2i> isometricLevel::machineTiles(const tileObject& _machine) const
{
	const tileObject& machine = _machine.machine();
	if (!machine.type || !footprint::isMachine(*machine.type))
		return { machine.origin };
	std::vector<sf::Vector2i> tiles;
	const sf::Vector2i size = footprint::turnedSize(*machine.type, machine.turns());
	for (int y = 0; y < size.y; y++)
		for (int x = 0; x < size.x; x++)
			tiles.push_back(machine.origin + sf::Vector2i(x, y));
	return tiles;
}

void isometricLevel::removeMachine(tileObject* _machine)
{
	tileObject* machine = &_machine->machine();
	for (const sf::Vector2i& at : machineTiles(*machine))
	{
		if (!isInLevel(at))
			continue;
		tile& t = levelTiles[coords2TileIndex(at.x, at.y)];
		for (tileObject* object : t.allObjects())
		{
			if (object->owner == machine)
			{
				t.removeTileObject(object);
				delete object;
			}
			else if (object == machine)
				t.removeTileObject(object);
		}
	}
	delete machine;
	markChanged();
}

void isometricLevel::attachMachines()
{
	const int width = int(levelSizeW);
	for (int i = 0; width > 0 && i < int(levelTiles.size()); i++)
	{
		const sf::Vector2i front(i % width + 1, i / width + 1);
		for (tileObject* object : levelTiles[i].allObjects())
		{
			if (object->isPart() || !object->type || !footprint::isMachine(*object->type))
				continue;
			const sf::Vector2i size = footprint::turnedSize(*object->type, object->turns());
			object->origin = front - size + sf::Vector2i(1, 1);
			object->cell = size - sf::Vector2i(1, 1);
			//its other tiles: each has to be in the level and free for it
			bool fits = true;
			for (int y = 0; y < size.y && fits; y++)
			{
				for (int x = 0; x < size.x && fits; x++)
				{
					const sf::Vector2i at = object->origin + sf::Vector2i(x, y);
					fits = at == front || (isInLevel(at) && levelTiles[coords2TileIndex(at.x, at.y)].canPlace(*object->type));
				}
			}
			if (!fits)
			{
				std::cout << "[WARN] " << object->tileTextureKey << " at " << front.x << "," << front.y << ": its footprint no longer fits, removed\n";
				levelTiles[i].removeTileObject(object);
				delete object;
				continue;
			}
			for (int y = 0; y < size.y; y++)
			{
				for (int x = 0; x < size.x; x++)
				{
					const sf::Vector2i at = object->origin + sf::Vector2i(x, y);
					if (at != front)
						levelTiles[coords2TileIndex(at.x, at.y)].placeObject(tileObject::makePart(object, sf::Vector2i(x, y)));
				}
			}
			object->fitSprite(textureMaster, levelTiles[i].getRootObjectCoords());
		}
	}
}

void isometricLevel::refreshSprites()
{
	for (tile& t : levelTiles)
	{
		for (tileObject* object : t.allObjects())
		{
			if (object->isPart())
				continue;
			if (object->type && footprint::isMachine(*object->type))
				object->fitSprite(textureMaster, t.getRootObjectCoords());
			else if (textureMaster->hasTexture(object->tileTextureKey))
				object->setTexture(*textureMaster->getTexture(object->tileTextureKey), true);
		}
	}
}

bool isometricLevel::removeTopObjectFromTile(sf::Vector2f _tileCoords)
{
	//a machine on top: the whole of it goes, from every tile it covers
	tile& target = levelTiles[coords2TileIndex(_tileCoords)];
	const std::vector<tileObject*> objects = target.allObjects();
	if (!objects.empty() && objects.back()->layer == tileLayer::fixture && objects.back()->type && footprint::isMultiTile(*objects.back()->type))
	{
		removeMachine(objects.back());
		return true;
	}
	if (!target.removeTopObject())
		return false;
	markChanged();
	return true;
}

sf::Vector2f isometricLevel::tileWorldPosition(sf::Vector2f _tileCoords)
{
	return levelTiles[coords2TileIndex(_tileCoords)].getRootObjectCoords();
}

namespace
{
	//a tile's diamond is tileSize wide and half that tall, so neighbouring tiles are half a width across
	//and a quarter of a width down from each other (tile x + 1 is down-right, tile y + 1 is down-left)
	float halfDiamondWidth(float _tileSize) { return (_tileSize > 0.f ? _tileSize : 64.f) / 2.f; }
	float halfDiamondHeight(float _tileSize) { return (_tileSize > 0.f ? _tileSize : 64.f) / 4.f; }
}

sf::Vector2f isometricLevel::tileCentreWorld(sf::Vector2f _tileCoords) const
{
	//the same layout every level is built with: tile x,y (0-based) has its sprite at cartToIso(32x, 32y)
	const float hw = halfDiamondWidth(tileSize), hh = halfDiamondHeight(tileSize);
	const float x = _tileCoords.x - 1.f, y = _tileCoords.y - 1.f;
	return sf::Vector2f(hw * (x - y) + diamondCentreX, hh * (x + y) + diamondCentreY);
}

sf::Vector2i isometricLevel::tileAtWorld(sf::Vector2f _world) const
{
	//undo tileCentreWorld: relative to tile 1,1's centre, d = (hw (x - y), hh (x + y)), so
	//x = (d.x/hw + d.y/hh) / 2 and y = (d.y/hh - d.x/hw) / 2. a point inside a diamond is within half a
	//tile of its centre in both x and y, so rounding to the nearest whole tile picks exactly that diamond
	const float hw = halfDiamondWidth(tileSize), hh = halfDiamondHeight(tileSize);
	const sf::Vector2f d = _world - sf::Vector2f(diamondCentreX, diamondCentreY);
	const float x = (d.x / hw + d.y / hh) / 2.f;
	const float y = (d.y / hh - d.x / hw) / 2.f;
	return sf::Vector2i(int(std::floor(x + 0.5f)) + 1, int(std::floor(y + 0.5f)) + 1);
}

bool isometricLevel::isInLevel(sf::Vector2i _tileCoords) const
{
	return _tileCoords.x >= 1 && _tileCoords.y >= 1 && _tileCoords.x <= int(levelSizeW) && _tileCoords.y <= int(levelSizeH);
}

bool isometricLevel::inArea(sf::Vector2i _tile) const
{
	if (!isInLevel(_tile))
		return false;
	const size_t index = size_t(_tile.x - 1) + size_t(levelSizeW) * size_t(_tile.y - 1);
	return area.empty() || (index < area.size() && area[index] != 0);
}

void isometricLevel::setArea(sf::Vector2i _tile, bool _buildable)
{
	if (!isInLevel(_tile))
		return;
	const size_t count = size_t(levelSizeW) * size_t(levelSizeH);
	if (area.size() != count)
		area.assign(count, 1);
	area[size_t(_tile.x - 1) + size_t(levelSizeW) * size_t(_tile.y - 1)] = _buildable ? 1 : 0;
}

int isometricLevel::areaCount() const
{
	if (area.empty())
		return int(levelSizeW) * int(levelSizeH);
	return int(std::count(area.begin(), area.end(), char(1)));
}

std::string isometricLevel::areaText() const
{
	std::string text;
	for (int y = 1; y <= int(levelSizeH); y++)
	{
		for (int x = 1; x <= int(levelSizeW); x++)
			text += inArea(sf::Vector2i(x, y)) ? '#' : '.';
		text += '\n';
	}
	return text;
}

bool isometricLevel::applyAreaText(const std::string& _text)
{
	std::vector<char> read;
	int rowLength = -1, rows = 0, length = 0;
	for (char c : _text)
	{
		if (c == '\r')
			continue;
		if (c == '\n')
		{
			if (length == 0)
				continue;
			if (rowLength >= 0 && length != rowLength)
				return false;
			rowLength = length;
			length = 0;
			rows++;
			continue;
		}
		if (c != '#' && c != '.')
			return false;
		read.push_back(c == '#' ? 1 : 0);
		length++;
	}
	if (length > 0)
	{
		if (rowLength >= 0 && length != rowLength)
			return false;
		rowLength = length;
		rows++;
	}
	if (rowLength != int(levelSizeW) || rows != int(levelSizeH))
		return false;
	area = std::count(read.begin(), read.end(), char(0)) == 0 ? std::vector<char>() : read;
	return true;
}

bool isometricLevel::hasFloor(sf::Vector2i _tile) const
{
	return isInLevel(_tile) && levelTiles[size_t(_tile.x - 1) + size_t(levelSizeW) * size_t(_tile.y - 1)].hasFloor();
}

bool isometricLevel::mountedOutside(const std::vector<sf::Vector2i>& _tiles) const
{
	for (const sf::Vector2i& at : _tiles)
	{
		if (below && below->hasFloor(at))
			return true;   //on the roof of the deck below
		if (isInLevel(at) && levelTiles[size_t(at.x - 1) + size_t(levelSizeW) * size_t(at.y - 1)].hasFrame())
			return true;   //on a frame
		for (int side = 0; side < 4; side++)
		{
			const sf::Vector2i next = at + footprint::step(side);
			if (std::find(_tiles.begin(), _tiles.end(), next) == _tiles.end() && hasFloor(next))
				return true;   //beside the hull
		}
	}
	return false;
}

bool isometricLevel::addEdgeWall(char _direction) { return false; }//only for square levels return 0 if called on nonsquare level
void isometricLevel::draw(sf::RenderWindow& _window)
{
	if (levelTiles.empty())
	{
		std::cout << "[ERROR] level has 0 tiles";
		exit(EXIT_FAILURE);
	}
	//std::cout << "levelTiles.size()" << levelTiles.size();
	for (int i = 0; i < levelTiles.size(); i++)
	{
		//tiles outside the deck's area are space: nothing to draw
		if (!area.empty() && (size_t(i) >= area.size() || !area[i]))
			continue;
		levelTiles[i].drawAllObjects(_window);
		
	}
}

void isometricLevel::drawBuilt(sf::RenderWindow& _window, const sf::RenderStates& _states)
{
	for (int i = 0; i < int(levelTiles.size()); i++)
	{
		if (!area.empty() && (size_t(i) >= area.size() || !area[i]))
			continue;
		if (levelTiles[i].hasAnything())
			levelTiles[i].drawBuiltObjects(_window, _states);
	}
}

void isometricLevel::printTileInfos()
{
	std::cout << "\nThis level has: " << levelTiles.size()<<" "<< std::endl;
	for (int i = 0; i < levelTiles.size(); i++)
	{	
		std::cout << "tile #:" << i;
		levelTiles[i].printAllObjectsInTile();
	}
}
void isometricLevel::printTileInfo(sf::Vector2f _tileCoords)
{
	
	std::cout << "Tile #:" << coords2TileIndex(_tileCoords) << std::endl;
		levelTiles[coords2TileIndex(_tileCoords)].printAllObjectsInTile();
}

void isometricLevel::drawTilePositionDebug(const sf::RenderWindow& _window)
{
	//draws each tile's index and world position as text over the level, using ImGui's
	//background draw list so it sits under the toolbar but over the SFML scene
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	const float lineHeight = ImGui::GetTextLineHeight();
	for (int i = 0; i < levelTiles.size(); i++)
	{
		sf::Vector2f tilePosition = levelTiles[i].getRootObjectCoords();
		sf::Vector2i screenPos = _window.mapCoordsToPixel(tilePosition);
		std::string tileNumberString = std::to_string(i);
		std::string tileCoordsString = std::to_string((int)tilePosition.x) + "," + std::to_string((int)tilePosition.y);
		ImVec2 pos((float)screenPos.x, (float)screenPos.y);
		drawList->AddText(pos, IM_COL32(255, 255, 255, 255), tileNumberString.c_str());
		drawList->AddText(ImVec2(pos.x, pos.y + lineHeight), IM_COL32(255, 255, 0, 255), tileCoordsString.c_str());
	}
}


void isometricLevel::setTextureManager(textureManager* _textureMaster)
{
	textureMaster = _textureMaster;
}

float isometricLevel::getSizeW()
{
	return levelSizeW;
}
float isometricLevel::getSizeH()
{
	return levelSizeH;
}
float isometricLevel::getTileSize()
{
	return tileSize;
}


void isometricLevel::setSizeW(float _sizeW)
{
	levelSizeW = _sizeW;
}
void isometricLevel::setSizeH(float _sizeH)
{
	levelSizeH = _sizeH;
}
void isometricLevel::setTileSize(float _tileSize) {

	tileSize = _tileSize;
}

void isometricLevel::setLevelOpacity(int _alphaAmount)
{
	for (int i = 0; i < levelTiles.size(); i++)
	{
		for (tileObject* object : levelTiles[i].allObjects())
		{
			object->setColor(sf::Color(255, 255, 255, _alphaAmount));
		}
	}
}

void isometricLevel::setZOffset(float _zOffset)
{
	for (int i = 0; i < levelTiles.size(); i++)
	{
		for (tileObject* object : levelTiles[i].allObjects())
		{
			object->SetWorldZ(_zOffset);
		}
	}
}

void isometricLevel::setTileColour(sf::Vector2f _tileCoords, sf::Color _colour)
{
	int currentTile = int(_tileCoords.x - 1) + (levelSizeW*(int(_tileCoords.y - 1)));
	levelTiles[currentTile].setTileObjectColour(_colour);
		
}

void isometricLevel::clearTileColour()
{
	for (int i = 0; i < levelTiles.size(); i++)
	{
		levelTiles[i].setTileObjectColour(sf::Color::White);
	}
	return;
}