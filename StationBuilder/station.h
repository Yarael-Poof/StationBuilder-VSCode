#pragma once
#include "isometricLevel.h"
#include "textureManager.h"
#include <SFML/graphics.hpp>
#include <iostream>
#include <string>
class station
{
public:
	station();//we should really just use the constructor with the texturemanager in it
	station(textureManager* _textureManager);
	~station();
	void setTextureManager(textureManager* _textureManager);//better to use the constructor instead of this, guess is useful if we want to change to a different set of textures easily
	int appendNewIsoLevel(float _levelSizeW = 1.f, float _levelSizeH = 1.f, float _tileSize = 64.f, std::string _tileBaseTextureKey = "empty");//adds and creates an empty level in the level list, returns index of where it just created it. Also sets currentLevel to the same.
	//the same, put in at _at (0 = a new lowest deck): the decks from _at up move up one. returns where it went
	int insertNewIsoLevel(int _at, float _levelSizeW, float _levelSizeH, float _tileSize = 64.f, std::string _tileBaseTextureKey = "empty");
	bool deleteIsoLevel(float _atWhichStackLayer);
	void deleteAllLevels();
	void addIsoLevel(isometricLevel _LevelToAdd, float _atWhichStackLayer);
	void drawLayer(sf::RenderWindow& _win, float _whichStackLayer);
	isometricLevel* getIsoLevel(float _atWhichStackLayer);
	isometricLevel* getCurrentIsoLevel(); //returns an isometricalLevel pointer to the current level's data
	void highlightTile(sf::Vector2f _tileCoords, sf::Color _colour);
	sf::Vector2f isoToCart(sf::Vector2f _cart);
	sf::Vector2f cartToIso(sf::Vector2f _iso);
	int getLayerListSize();
	int getCurrentLevel();//returns an integer showing where the current level is in in the station stack
	void setCurrentLevel(int _levelNumber);
	void incCurrentLevel();
	void decCurrentLevel();
	void fadeAndSlideAwayUp();
	void fadeAndSlideAwayDown();
	void setBuildMode(bool _toggle);
	void setDebugMode(bool _toggle);
	void toggleBuildMode();
	void toggleDebugMode();
	bool getBuildMode();
	bool getDebugMode();
	void highlightAllTiles(sf::Color _colour);
	void setStationName(const std::string& _name);//the name used for saving and loading this station's files
	std::string getStationName();
	//changes whenever anything in the station changes: an object placed or removed on any level, or levels
	//added, removed or loaded. simulations compare it with the value they last saw to know when to rebuild
	std::uint64_t getRevision();
	//changes what a riser on a tile carries (a cable, a pipe or an air duct: _newKey is the riser of that kind), and
	//with it the risers stacked directly above and below that it links to (the same kind on the same tile of each
	//next level), so a riser shaft stays joined. a cable or pipe riser keeps its shape. all or nothing: if any of
	//those tiles would refuse the new kind (it already has a pipe, say) none change, and _whyNot (if given) says why.
	//returns how many risers changed, 0 if none did
	int changeRiserKind(int _level, sf::Vector2i _tile, tileObject* _riser, const std::string& _newKey, std::string* _whyNot = nullptr);

private:
	//tells each deck which deck is below it (after any change to the list of decks)
	void relinkLevels();
	std::uint64_t revision = isometricLevel::newRevision();//bumped when the list of levels changes

	bool buildMode = false;
	bool debugMode = false;
	std::string stationName = "Untitled";

	int animationDurationms = 255;
	int currentLevel = 0;
	textureManager* textureMaster;
	std::vector<isometricLevel> isoLayerList;
	std::string debugErrorString = "[ERROR] from [Station].";
	std::string debuginfoString = "[INFO] from [Station].";
};

