#include "gameManager.h"
#include <sstream>

void gameManager::saveStationToDisc(station* _stationToSave, std::string _stationName)
{
	std::cout << "\n[INFO] Saving " << _stationName << " please wait...\n";
	boost::filesystem::path dir(_stationName);
	if (!(boost::filesystem::exists(dir))) {
		std::cout << "Doesn't Exists" << std::endl;

		if (boost::filesystem::create_directory(dir))
			std::cout << "....Successfully Created !" << std::endl;
	}
	for (int currentLevel = 0; currentLevel < _stationToSave->getLayerListSize(); currentLevel++)
	{
		saveLevelToDisc(_stationToSave->getIsoLevel(currentLevel), _stationName + "/" + std::to_string(currentLevel));
	}
	//a deck taken away since the last save: its old files would load as a deck again
	for (int extra = _stationToSave->getLayerListSize(); boost::filesystem::exists(_stationName + "/" + std::to_string(extra) + ".level"); extra++)
	{
		boost::system::error_code ignored;
		boost::filesystem::remove(_stationName + "/" + std::to_string(extra) + ".level", ignored);
		boost::filesystem::remove(_stationName + "/" + std::to_string(extra) + ".area", ignored);
	}
}

bool gameManager::loadStationFromDisc(station* _stationToRestoreInto, std::string _stationName, textureManager* _textureManager)
{
	std::cout << "\n[INFO] Loading " << _stationName << " please wait...\n";
	//levels were saved as <name>/0.level, <name>/1.level, ... so keep loading until the next one is missing
	std::vector<isometricLevel> loadedLevels;
	for (int currentLevel = 0; boost::filesystem::exists(_stationName + "/" + std::to_string(currentLevel) + ".level"); currentLevel++)
	{
		isometricLevel level;
		if (!loadLevelFromDisc(level, _stationName + "/" + std::to_string(currentLevel), _textureManager))
		{
			std::cout << "[ERROR] could not load level " << currentLevel << ", the station is unchanged\n";
			return false;
		}
		loadedLevels.push_back(level);
	}
	if (loadedLevels.empty())
	{
		std::cout << "[ERROR] no saved levels found in " << _stationName << ", the station is unchanged\n";
		return false;
	}
	//only replace the current station once every level has loaded successfully
	_stationToRestoreInto->deleteAllLevels();
	for (int i = 0; i < int(loadedLevels.size()); i++)
	{
		_stationToRestoreInto->addIsoLevel(loadedLevels[i], i);
	}
	_stationToRestoreInto->setStationName(_stationName);
	_stationToRestoreInto->setCurrentLevel(0);
	std::cout << "[INFO] " << _stationName << " loaded, " << loadedLevels.size() << " levels\n";
	return true;
}


void gameManager::saveLevelToDisc(isometricLevel* _levelToSave, std::string _levelName)
{

	std::cout << "\n[INFO] Saving " << _levelName << ".level please wait...\n";
	levelDiscStructure toSave;
	toSave.sizeW = _levelToSave->getSizeW();
	toSave.sizeH = _levelToSave->getSizeH();
	toSave.tileSize = _levelToSave->getTileSize();
	for (int i = 0; i < _levelToSave->levelTiles.size(); i++)
	{
		//one comma separated list of texture keys per tile, in slot order (background, floor, walls, fixtures).
		//an object whose settings differ from the defaults has them after its key: "generator{on=0;v=240}"
		std::string textureKeys = "";
		std::vector<tileObject*> objects = _levelToSave->levelTiles[i].allObjects();
		for (int n = 0; n < int(objects.size()); n++)
		{
			if (objects[n]->isPart())
				continue;   //a big machine is saved once, on its front tile; its parts are made again when it loads
			if (!textureKeys.empty())
				textureKeys += ",";
			textureKeys += objects[n]->tileTextureKey;
			const std::string settings = objects[n]->settingsText();
			if (!settings.empty())
				textureKeys += "{" + settings + "}";
		}
		toSave.tileTypes.emplace(i, textureKeys);
	}
	std::ofstream saveFile(_levelName + ".level");
	boost::archive::text_oarchive saveArchive(saveFile);
	saveArchive << toSave.sizeW;
	saveArchive << toSave.sizeH;
	saveArchive << toSave.tileSize;
	saveArchive << toSave.tileTypes;
	//its area (where it can be built), as rows of # and . beside it. a deck that is all area has none (as old saves)
	boost::system::error_code ignored;
	if (_levelToSave->hasWholeArea())
		boost::filesystem::remove(_levelName + ".area", ignored);
	else
		std::ofstream(_levelName + ".area") << _levelToSave->areaText();
	std::cout << "[INFO] " << _levelName << ".level saved successfully\n";
}

bool gameManager::loadLevelFromDisc(isometricLevel& _levelToRestoreInto, std::string _levelName, textureManager* _textureManager)
{
	std::cout << "\n[INFO] Loading " << _levelName << ".level please wait...\n";
	levelDiscStructure toLoad;
	std::ifstream loadFile(_levelName + ".level");
	if (!loadFile)
	{
		std::cout << "[ERROR] failed to find the level file\n";
		return false;
	}
	try {
		boost::archive::text_iarchive loadArchive(loadFile);
		loadArchive >> toLoad.sizeW;
		loadArchive >> toLoad.sizeH;
		loadArchive >> toLoad.tileSize;
		loadArchive >> toLoad.tileTypes;
	}
	catch (...)
	{
		std::cout << "[ERROR] failed to read .level file\n";
		return false;
	}

	std::cout << "[INFO] Parsing level please wait...\n";
	_levelToRestoreInto.setSizeW(toLoad.sizeW);
	_levelToRestoreInto.setSizeH(toLoad.sizeH);
	_levelToRestoreInto.setTileSize(toLoad.tileSize);
	_levelToRestoreInto.setTextureManager(_textureManager);
	_levelToRestoreInto.levelTiles.clear();

	for (int y = 0; y < toLoad.sizeH; y++)
	{
		for (int x = 0; x < toLoad.sizeW; x++)
		{
			//tiles were saved row by row, so tile x,y is at index x + y * width (same order as fillFloors and appendNewIsoLevel)
			_levelToRestoreInto.levelTiles.push_back(tile(cartToIso(sf::Vector2f(toLoad.tileSize / 2 * x, toLoad.tileSize / 2 * y)), _textureManager, toLoad.tileTypes[x + y * toLoad.sizeW]));
		}
	}
	//its area, if it was saved with one (an older save has none: all of it)
	std::ifstream areaFile(_levelName + ".area");
	if (areaFile)
	{
		std::stringstream areaText;
		areaText << areaFile.rdbuf();
		if (!_levelToRestoreInto.applyAreaText(areaText.str()))
			std::cout << "[WARN] " << _levelName << ".area doesn't fit the level: all of it can be built\n";
	}
	_levelToRestoreInto.attachMachines();   //big machines get their parts on their other tiles, machines their pictures
	_levelToRestoreInto.markChanged();
	std::cout << "[INFO] " << _levelName << " load successful\n";
	return true;
}

sf::Vector2f gameManager::isoToCart(sf::Vector2f _cart)
{
	sf::Vector2f iso(0.f, 0.f);
	iso.x = (2 * _cart.y + _cart.x) / 2;
	iso.y = (2 * _cart.y - _cart.x) / 2;
	return iso;
}

sf::Vector2f gameManager::cartToIso(sf::Vector2f _iso)
{
	sf::Vector2f cart(0.f, 0.f);
	cart.x = _iso.x - _iso.y;
	cart.y = (_iso.x + _iso.y) / 2;
	return cart;
}
