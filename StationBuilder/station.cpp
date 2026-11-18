#include "station.h"
#include "tileCatalogue.h"
#include <algorithm>

void station::setStationName(const std::string& _name)
{
	stationName = _name;
}
std::string station::getStationName()
{
	return stationName;
}
std::uint64_t station::getRevision()
{
	//every revision number is new and larger than all before it, so the largest one changes on any edit
	std::uint64_t latest = revision;
	for (const isometricLevel& level : isoLayerList)
		latest = std::max(latest, level.getRevision());
	return latest;
}

namespace
{
	//the first object with this key on a tile, or nullptr (also for a tile off the level)
	tileObject* objectOn(isometricLevel* _level, sf::Vector2i _tile, const std::string& _key)
	{
		if (!_level->isInLevel(_tile))
			return nullptr;
		tileObject* found = nullptr;
		_level->levelTiles[_level->coords2TileIndex(_tile.x, _tile.y)].forEachObject([&](tileObject* _object) {
			if (!found && _object->tileTextureKey == _key)
				found = _object;
		});
		return found;
	}

	//what stops an item of this type going on a tile that refused it: "a cable", "a pipe", "an air duct riser", "an item tube"
	std::string whatBlocks(const tileTypeInfo& _type)
	{
		if (_type.electrical.conductor)
			return "a cable";
		if (_type.thermal.ductConductance > 0.0)
			return "an air duct riser";
		if (_type.items.tube)
			return "an item tube";
		return "a pipe";
	}
}

int station::changeRiserKind(int _level, sf::Vector2i _tile, tileObject* _riser, const std::string& _newKey, std::string* _whyNot)
{
	auto refuse = [&](const std::string& _why) {
		if (_whyNot)
			*_whyNot = _why;
		return 0;
	};
	const kindFamily* family = _riser ? kindFamilyOf(_riser->tileTextureKey) : nullptr;
	const tileTypeInfo* newType = findTileType(_newKey);
	if (!family || !newType || kindFamilyOf(_newKey) != family || _level < 0 || _level >= getLayerListSize())
		return refuse("that isn't a riser");
	const std::string oldKey = _riser->tileTextureKey;
	if (oldKey == _newKey)
		return refuse("");
	bool onTile = false;
	if (getIsoLevel(float(_level))->isInLevel(_tile))
		getIsoLevel(float(_level))->levelTiles[getIsoLevel(float(_level))->coords2TileIndex(_tile.x, _tile.y)].forEachObject([&](tileObject* _object) {
			onTile = onTile || _object == _riser;
		});
	if (!onTile)
		return refuse("that riser isn't on this tile");

	//the shaft: this riser, and the same kind of riser straight up and straight down until the run stops
	std::vector<std::pair<int, tileObject*>> shaft{ { _level, _riser } };
	for (const int step : { 1, -1 })
	{
		for (int lv = _level + step; lv >= 0 && lv < getLayerListSize(); lv += step)
		{
			tileObject* next = objectOn(getIsoLevel(float(lv)), _tile, oldKey);
			if (!next)
				break;
			shaft.push_back({ lv, next });
		}
	}

	//all or nothing: check every tile before changing any
	for (const auto& [lv, riser] : shaft)
	{
		if (!getIsoLevel(float(lv))->canReplaceOnTile(sf::Vector2f(_tile), riser, _newKey))
			return refuse("level " + std::to_string(lv) + " already has " + whatBlocks(*newType) + " on " + std::to_string(_tile.x) + "," +
				std::to_string(_tile.y));
	}
	for (const auto& [lv, riser] : shaft)
	{
		//a cable riser's shape carries over to a pipe riser and back (an air duct has none: it comes back on Auto)
		std::string settingsText;
		if (riser->type && findSetting(*riser->type, connections::settingKey) && findSetting(*newType, connections::settingKey))
		{
			const int mask = int(riser->getSetting(connections::settingKey));
			if (mask != connections::all)
				settingsText = std::string(connections::settingKey) + "=" + std::to_string(mask);
		}
		getIsoLevel(float(lv))->replaceObjectOnTile(sf::Vector2f(_tile), riser, _newKey, settingsText);
	}
	if (_whyNot)
		_whyNot->clear();
	return int(shaft.size());
}

station::station()
{
	textureMaster = nullptr;
}
station::~station()
{

}
station::station(textureManager* _textureManager)
{
	textureMaster = _textureManager;
}
sf::Vector2f station::isoToCart(sf::Vector2f _cart)
{
	sf::Vector2f iso(0.f, 0.f);
	iso.x = (2 * _cart.y + _cart.x) / 2;
	iso.y = (2 * _cart.y - _cart.x) / 2;
	return iso;
}

sf::Vector2f station::cartToIso(sf::Vector2f _iso)
{
	sf::Vector2f cart(0.f, 0.f);
	cart.x = _iso.x - _iso.y;
	cart.y = (_iso.x + _iso.y) / 2;
	return cart;
}
int station::appendNewIsoLevel(float _levelSizeW, float _levelSizeH, float _tileSize, std::string _tileBaseTextureKey)
{	
	isoLayerList.push_back(isometricLevel(textureMaster, _levelSizeW, _levelSizeH, _tileSize));
	currentLevel = (isoLayerList.size() - 1);
	//fill the newly made level with empty tile textures
	for (int y = 0; y < isoLayerList[currentLevel].getSizeH(); y++)
	{
		
		for (int x = 0; x < isoLayerList[currentLevel].getSizeW(); x++)
		{
			
			isoLayerList[currentLevel].levelTiles.push_back(tile(cartToIso(sf::Vector2f(isoLayerList[currentLevel].getTileSize() / 2 * x, isoLayerList[currentLevel].getTileSize() / 2 * y)), textureMaster, _tileBaseTextureKey));
		}

	}
	revision = isometricLevel::newRevision();
	relinkLevels();
	return (isoLayerList.size() - 1);
}

int station::insertNewIsoLevel(int _at, float _levelSizeW, float _levelSizeH, float _tileSize, std::string _tileBaseTextureKey)
{
	appendNewIsoLevel(_levelSizeW, _levelSizeH, _tileSize, _tileBaseTextureKey);
	const int at = std::clamp(_at, 0, int(isoLayerList.size()) - 1);
	std::rotate(isoLayerList.begin() + at, isoLayerList.end() - 1, isoLayerList.end());
	currentLevel = at;
	revision = isometricLevel::newRevision();
	relinkLevels();
	return at;
}

void station::relinkLevels()
{
	for (int i = 0; i < int(isoLayerList.size()); i++)
		isoLayerList[i].setLevelBelow(i > 0 ? &isoLayerList[i - 1] : nullptr);
}
void station::setTextureManager(textureManager* _textureManager)
{
	textureMaster = _textureManager;
}
void station::addIsoLevel(isometricLevel _LevelToAdd, float _atWhichStackLayer)
{	
	try {
		isoLayerList.at(int(_atWhichStackLayer));
		isoLayerList[int(_atWhichStackLayer)] = _LevelToAdd;
	}
	catch (...)
	{
		//add checks here to see if we are adding sequentially. i.e do not allow a level to be added to layer 2 if we dont have 0 and 1 already.
		std::cout << this->debugErrorString + std::string(__func__) << " tried to add a level at a location that does not exist, attempting resize of isoLevelList"<<std::endl;
		isoLayerList.resize(int(_atWhichStackLayer) + 1);
		
		try {
			isoLayerList[int(_atWhichStackLayer)] = _LevelToAdd;
		}
		catch (...)
		{
			std::cout << this->debugErrorString + std::string(__func__) << "fatal! giving up, cannot add level" << std::endl;
		}
	}

	std::cout << this->debuginfoString + std::string(__func__) << " resize succesful, level added" << std::endl;
	currentLevel = _atWhichStackLayer;
	revision = isometricLevel::newRevision();
	relinkLevels();
}

bool station::deleteIsoLevel(float _atWhichStackLayer)
{
	if (int(_atWhichStackLayer) < 0 || int(_atWhichStackLayer) >= int(isoLayerList.size()))
		return false;
	try
	{
		isoLayerList.erase(isoLayerList.begin() + int(_atWhichStackLayer));
		revision = isometricLevel::newRevision();
		currentLevel = std::clamp(currentLevel, 0, std::max(int(isoLayerList.size()) - 1, 0));
		relinkLevels();
		return true;
	}
	catch (...)
	{
		std::cout << this->debugErrorString + std::string(__func__) << "attempted to delete a level from a location that does not exit" << std::endl;
		return false;
	}

}

void station::deleteAllLevels()
{
	isoLayerList.clear();
	revision = isometricLevel::newRevision();
	std::cout << this->debuginfoString << "cleared all levels" << std::endl;
}

void station::drawLayer(sf::RenderWindow& _win, float _whichStackLayer)
{
	try {
		isoLayerList.at(int(_whichStackLayer));
	}
	catch(...){
		std::cout << this->debugErrorString + std::string(__func__) << " attempted to draw a level that does not exist" << std::endl;
		exit(EXIT_FAILURE);
	}
	isoLayerList[int(_whichStackLayer)].draw(_win);
}

int station::getLayerListSize()
{
	return int(isoLayerList.size());
}
///<summary>
///returns an integer showing where the current level is in in the station stack
///</summary>
int station::getCurrentLevel()
{
	return currentLevel;
}

void station::setCurrentLevel(int _levelNumber)
{
	currentLevel = _levelNumber;
}
void station::incCurrentLevel()
{
	if ((currentLevel + 1) >= int(isoLayerList.size()))
	{
		std::cout << this->debuginfoString + __func__ << " No higher levels to select" << std::endl;
		return;
	}
	else
	{
		
		currentLevel++;
		return;
	}
	
}

void station::decCurrentLevel()
{
	if (currentLevel - 1 < 0)
	{
		std::cout << this->debuginfoString + __func__<< " No lower levels to select" << std::endl;
		return;
	}
	else
	{
		currentLevel--;
		return;
	}
}
void station::setBuildMode(bool _toggle)
{
	buildMode = _toggle;
	

}
bool station::getBuildMode() { return buildMode; }
void station::toggleBuildMode()
{
	buildMode = !buildMode;
	if (buildMode == false)//if we exit build mode, clear all highlighting on all levels.
	{
		for (int i = 0; i < isoLayerList.size(); i++)

			isoLayerList[i].clearTileColour();
	}
}
void station::setDebugMode(bool _toggle)
{
	debugMode = _toggle;


}
bool station::getDebugMode() { return debugMode; }
void station::toggleDebugMode()
{
	debugMode = !debugMode;
}
void station::fadeAndSlideAwayDown()
{	//animations for switching levls, not finished, needs hooking in somehow to the main render loop.
	sf::Clock timer;
	sf::Time startTime = timer.getElapsedTime();
	int alpha = 255;
	float zOffsetInc = 0.1;
	while((timer.getElapsedTime().asMilliseconds() - startTime.asMilliseconds()) < animationDurationms)
	{
		isoLayerList[currentLevel].setZOffset(zOffsetInc);
		isoLayerList[currentLevel].setLevelOpacity(alpha);
		alpha--;
		zOffsetInc + 0.1;
	}
	return;
}
void station::fadeAndSlideAwayUp()
{

}

///<summary>
///returns an isometricalLevel pointer to the current level's data
///</summary>

isometricLevel* station::getCurrentIsoLevel()
{
	return &isoLayerList[currentLevel];
}
isometricLevel* station::getIsoLevel(float _atWhichStackLayer)
{
	return &isoLayerList[_atWhichStackLayer];
}
void station::highlightTile(sf::Vector2f _tileCoords, sf::Color _colour)
{	
	
		isoLayerList[currentLevel].setTileColour(_tileCoords, _colour);
}

void station::highlightAllTiles(sf::Color _colour)
{
	for (int y = 1; y < isoLayerList[currentLevel].getSizeH() + 1; y++)
	{
		for (int x = 1; x < isoLayerList[currentLevel].getSizeW() + 1; x++)
		{ 
			isoLayerList[currentLevel].setTileColour(sf::Vector2f(x, y), _colour);
		}
	}
}