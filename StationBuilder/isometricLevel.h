#pragma once
#include <SFML/Graphics.hpp>
#include "textureManager.h"
#include "tile.h"
#include "engineMacros.h"
#include <stdlib.h>
#include <string>
#include <cstdint>

//direction chars: 0 = UL, 1 = UR, 2 = LL, 3 = LR
class isometricLevel
{	

public:
	isometricLevel();
	isometricLevel(textureManager* _textureMaster,float _levelSizeW = 1.f, float _levelSizeH = 1.f, float _tileSize = 10.f);
	~isometricLevel();
	void addFloor(sf::Vector2f _atWorldPosition);
	void addFloor(float _worldX, float _worldY);
	void addWall(sf::Vector2f _atWorldPosition, char _direction);
	void addWall(float _worldX, float _worldY, char _direction);
	void fillFloors();//fill entire level with floors
	//false if the tile's rules refuse the object. _settings (as saved, e.g. "c=9", "r=1") are applied before it is
	//placed. a machine bigger than one tile is placed with _tileCoords at its middle, over every tile of its footprint
	bool addObjectToTile(sf::Vector2f _tileCoords, std::string _textureKey, const std::string& _settings = "");
	//would the tile (every tile of a big machine's footprint, turned as _settings say) take an object of this key right
	//now? nothing is changed
	bool canAddToTile(sf::Vector2f _tileCoords, const std::string& _textureKey, const std::string& _settings = "");
	//the tiles a placed machine covers (just its own tile for anything one tile big)
	std::vector<sf::Vector2i> machineTiles(const tileObject& _machine) const;
	//removes a placed machine from every tile it covers, and deletes it and its parts
	void removeMachine(tileObject* _machine);
	//after a level is loaded: each machine learns its footprint, each big one gets its parts on its other tiles (one that
	//no longer fits is removed, with a message), and each gets its picture for its turn
	void attachMachines();
	//gives every placed object its picture again (after the content files are reloaded): machines fitted to their footprint
	void refreshSprites();
	//would the tile take an object of this key in place of _old (one of its fixtures)? nothing is changed
	bool canReplaceOnTile(sf::Vector2f _tileCoords, const tileObject* _old, const std::string& _textureKey);
	//swaps the fixture _old on the tile for a new object of this key (with _settings applied, as saved), in the same
	//place in the draw order. _old is deleted. false (nothing changed) if the tile's rules refuse it
	bool replaceObjectOnTile(sf::Vector2f _tileCoords, tileObject* _old, const std::string& _textureKey, const std::string& _settings = "");
	bool removeTopObjectFromTile(sf::Vector2f _tileCoords);//false if there was nothing to remove
	sf::Vector2f tileWorldPosition(sf::Vector2f _tileCoords);//world position of a tile sprite's top left corner

	//where the middle of the floor diamond sits inside a tile's 64x64 sprite. sprites are drawn from their
	//top left corner and the diamond fills rows 30-62 of the lower half (widest at rows 46-47)
	static constexpr float diamondCentreX = 32.f;
	static constexpr float diamondCentreY = 46.5f;
	//world position of the middle of a tile's floor diamond (1-based tile coords), for drawing things on a tile
	sf::Vector2f tileCentreWorld(sf::Vector2f _tileCoords) const;
	//the tile whose floor diamond contains a world point, as 1-based tile coords. this is the exact inverse of
	//how tiles are laid out, so anywhere inside a diamond picks that tile. it can be outside the level: check isInLevel
	sf::Vector2i tileAtWorld(sf::Vector2f _world) const;
	//true if 1-based tile coords are inside this level
	bool isInLevel(sf::Vector2i _tileCoords) const;
	void clearAll();
	bool addEdgeWall(char _direction);//only for square levels return 0 if called on nonsquare level
	void draw(sf::RenderWindow&  _window);//add all the level's tiles to the window.
	void printTileInfos();//print dump all the objects on every tile of the level (verbose)
	void printTileInfo(sf::Vector2f _tileCoords);//show the objets present on the specific tile
	void drawTilePositionDebug(const sf::RenderWindow& _window);//overlay tile index + world coords labels (call between ImGui::SFML::Update and Render)
	void setTextureManager(textureManager* _textureMaster);
	float getSizeW();
	float getSizeH();
	float getTileSize();
	void setSizeW(float _sizeW);
	void setSizeH(float _sizeH);
	void setTileSize(float _tileSize);

	sf::Vector2f isoToCart(sf::Vector2f _cart);
	sf::Vector2f cartToIso(sf::Vector2f _iso);
	std::vector<tile> levelTiles;//where our tiles are
	void setLevelOpacity(int _alphaAmount);
	void setTileColour(sf::Vector2f _tileCoords, sf::Color _colour);
	void clearTileColour();
	void setZOffset(float _zOffset);
	int	 coords2TileIndex(sf::Vector2f _tileCoords);
	int	 coords2TileIndex(int _tileX, int _tileY);

	//change tracking for the simulations: the revision changes whenever an object is added to or removed
	//from this level, so a network built from it knows when it must be rebuilt. code that edits levelTiles
	//directly should call markChanged() afterwards
	std::uint64_t getRevision() const;
	void markChanged();
	//changes one of an object's settings (an object on this level) and marks the level changed if the value
	//actually changed, so the simulations pick it up. returns true if it changed
	bool changeObjectSetting(tileObject* _object, const std::string& _key, double _value);
	//a new, never used before revision number (shared by every level and the station)
	static std::uint64_t newRevision();

	//the deck's area: where on it things can be built. every deck uses the station's whole grid, so they line up (a
	//riser on tile 5,6 meets the one on 5,6 of the deck above); the area is what makes one deck small, another large,
	//and either any shape. no area set (an old save): all of it. tiles outside it aren't drawn
	bool inArea(sf::Vector2i _tile) const;
	void setArea(sf::Vector2i _tile, bool _buildable);
	bool hasWholeArea() const { return area.empty(); }
	int areaCount() const;
	//the area as text: a line per row, '#' for a tile in it and '.' for one that isn't. read back with applyAreaText
	//(false, and nothing changed, if it doesn't fit this level)
	std::string areaText() const;
	bool applyAreaText(const std::string& _text);
	//a floor on the tile: it is inside the hull (the floors built are the station's shape)
	bool hasFloor(sf::Vector2i _tile) const;
	//the deck directly below (kept up to date by the station), whose roofs things can be mounted on. nullptr for the lowest
	void setLevelBelow(const isometricLevel* _below) { below = _below; }
	const isometricLevel* levelBelow() const { return below; }
	//would something mounted outside, covering these tiles, be held on: beside the hull (a floored tile next to one of
	//them, on this deck) or on a roof (a floor directly below one of them, on the deck below)
	bool mountedOutside(const std::vector<sf::Vector2i>& _tiles) const;

protected:
	std::uint64_t revision = newRevision();
	std::vector<char> area;                  //see inArea: one per tile (1 = in it), or empty for all of it
	const isometricLevel* below = nullptr;

	float levelSizeW;
	float levelSizeH;
	float tileSize;
	textureManager* textureMaster;
};

