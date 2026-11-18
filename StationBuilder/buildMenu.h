#pragma once
#include <SFML/Graphics.hpp>
#include <string>
#include "textureManager.h"
#include "isometricLevel.h"
#include "tileCatalogue.h"

struct ImFont;

//the build panel along the bottom of the screen: a tab per category, an icon for each item, and a
//preview of the selected item with its title. the items come from tileCatalogue(), so adding an item
//or a category there makes it appear here. also shows the red warning when a placement is refused.
//
//R turns the selected item before it is placed: a wall to another edge, a pump to push another way, a cable or pipe
//to another rotation of its shape. Shift+R changes a cable's or pipe's shape: Auto (joins everything next to it),
//straight, corner, T-junction
class buildMenu
{
public:
	//draws the panel. call every frame in build mode, between ImGui::SFML::Update and Render
	void draw(textureManager* _textures);
	//texture key of the selected menu entry, "" when nothing is selected
	std::string selectedKey() const;
	//selects a menu entry as a click on its icon would (a new item starts unturned, a cable or pipe on Auto)
	void select(const std::string& _key);
	//what a click places: the selected item turned by R (wallUL -> wallLR...), "" when nothing is selected
	std::string placementKey() const;
	//the settings it is placed with, as saved: a cable's or pipe's shape ("c=9"), "" for none or Auto
	std::string placementSettings() const;
	//R: a quarter turn clockwise
	void rotate();
	//Shift+R: a cable's or pipe's next shape (Auto -> straight -> corner -> T -> Auto)
	void nextShape();
	//what a riser is placed as: 0 cable, 1 pipe, 2 air duct (its kind family's order). remembered between picks,
	//and picked with the buttons by the preview
	void setKind(int _kind);
	int kind() const { return kindChoice; }
	//a see-through copy of what a click would place, on the tile under the cursor (a dark silhouette if it would be
	//refused). call every frame in build mode, after the level has been drawn
	void drawGhost(sf::RenderWindow& _window, isometricLevel* _level, sf::Vector2i _tile, textureManager* _textures) const;
	//shows a red warning next to a tile for a few seconds
	void warnAt(sf::Vector2f _tileCoords, const std::string& _message);
	//draws the warning, if one is active. call every frame, after the level has been drawn
	void drawWarning(sf::RenderWindow& _window, isometricLevel* _level);
	//size of the panel and its warning: 1 = the original 1080p layout, 2 = twice as big (4K).
	//_font is the GUI font loaded at that scale (nullptr = the normal font)
	void setScale(float _scale, ImFont* _font);

private:
	//the selected item as its chosen kind (a riser: the cable, pipe or air duct one), before any turn
	std::string baseKey() const;
	//true when the selected item is a cable, riser or pipe (it has a shape)
	bool selectedIsLine() const;
	//true when the selected item is a machine (it turns with R, kept in its "r" setting)
	bool selectedIsMachine() const;
	//the mask of sides the placed cable or pipe will connect on
	int placementMask() const;
	//the texture that shows what a click places (a wall on its edge, a cable with just its shape's arms)
	std::string placementTexture(textureManager* _textures) const;
	//the texture for an item's menu icon: devices that sit on pipe stubs show the stubs they connect on
	static std::string iconTexture(const std::string& _key, textureManager* _textures);

	float scale = 1.f;
	ImFont* font = nullptr;
	std::string selected;
	int rotation = 0;                                              //quarter turns clockwise, from R
	int kindChoice = 0;                                            //a riser's kind, see setKind
	connections::shape lineShape = connections::shape::automatic;  //from Shift+R
	bool warningActive = false;
	sf::Vector2f warningTile;
	std::string warningText;
	double warningStart = 0.0;
};
