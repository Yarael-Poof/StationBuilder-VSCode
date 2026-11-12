#include "isometricLevel.h"
#include <imgui.h>

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
}

void isometricLevel::addObjectToTile(sf::Vector2f _tileCoords, std::string _textureKey)
{
	sf::Vector2f currentTileScreenPosition = levelTiles[coords2TileIndex(_tileCoords)].getRootObjectCoords();
	levelTiles[coords2TileIndex(_tileCoords)].addTileObject(new tileObject(textureMaster, _textureKey, currentTileScreenPosition));
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
		levelTiles[i].drawAllObjects(_window);
		
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
		for (std::list<tileObject*>::iterator iter = levelTiles[i].tileObjectsList.begin(); iter != levelTiles[i].tileObjectsList.end(); ++iter)

		{
			(*iter)->setColor(sf::Color(255, 255, 255, _alphaAmount));
		}
	}
}

void isometricLevel::setZOffset(float _zOffset)
{
	for (int i = 0; i < levelTiles.size(); i++)
	{
		for (std::list<tileObject*>::iterator iter = levelTiles[i].tileObjectsList.begin(); iter != levelTiles[i].tileObjectsList.end(); ++iter)

		{
			(*iter)->SetWorldZ(_zOffset);
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