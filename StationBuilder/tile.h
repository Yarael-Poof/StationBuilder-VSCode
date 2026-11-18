#pragma once
#include <SFML/Graphics.hpp>
#include <list>
#include <vector>
#include <iostream>
#include "tileObject.h"
#include "engineMacros.h"
#include "textureManager.h"

//this a tile, one cell of the map. it has a position in the grid system of the map and holds
//tileObjects (sprites) in fixed slots:
//  background: the blank grid sprite, always present, never stacks
//  floor:      at most 1
//  walls:      at most 1 per edge, 4 slots indexed by wallEdge (0 UL, 1 UR, 2 LL, 3 LR)
//  fixtures:   any number (lights, cables...), but only on a floor or a wall. cables are kept ahead of
//              the other fixtures in the list, so they draw underneath them
//objects are drawn in that order, so fixtures always sit on top of walls and floors
class tile
{
public:
	tile();
	tile(sf::Vector2f _worldPosition, textureManager* _textureMaster,std::string _tileObjectCSVList = "empty");
	~tile();
	float tileSize = 64;

	//would an object in this slot be accepted right now? nothing is changed
	bool canPlace(tileLayer _layer, int _wallEdge) const;
	//would an object of this type be accepted right now: its slot is free, and it isn't a second cable (or cable
	//riser), a second pipe (or pipe riser) or a second air duct riser on the tile (one of each: the networks have one
	//node per tile, so two couldn't stay apart)
	bool canPlace(const tileTypeInfo& _type) const;
	//the same, as if _replacing (a fixture on this tile) were gone: could it be swapped for one of this type
	bool canPlaceInstead(const tileTypeInfo& _type, const tileObject* _replacing) const;
	//puts the object in its slot if the placement rules allow it. returns false otherwise,
	//and the caller keeps ownership of the object
	bool placeObject(tileObject* _object);
	//swaps the fixture _old for _replacement in the same place in the draw order, and deletes _old. false (nothing
	//changed, the caller keeps _replacement) if _old isn't a fixture on this tile or the rules refuse _replacement
	bool replaceFixture(tileObject* _old, tileObject* _replacement);
	void removeTileObject(tileObject* _object);
	bool removeTopObject();//deletes the topmost object (fixtures, then walls, then the floor). false if the tile is empty
	void clearTile();
	void drawAllObjects(sf::RenderWindow& _window);
	void printAllObjectsInTile();
	void setTileObjectColour(sf::Color _colour);
	sf::Vector2f getRootObjectCoords();//the world position of the background sprite
	//every object on this tile, in draw order (background, floor, walls, fixtures)
	std::vector<tileObject*> allObjects() const;
	//a floor on it: it is inside the hull. no floor: a bare tile, open to space (where outside items mount)
	bool hasFloor() const { return floorSlot != nullptr; }
	//anything on it but its blank background
	bool hasAnything() const { return floorSlot || hasAnyWall() || !fixtureSlots.empty(); }
	//calls _visit(tileObject*) for every object, in the same order as allObjects(), without building a list
	//(the simulations scan every tile of the station when it changes, so this matters on big stations)
	template <typename F>
	void forEachObject(F&& _visit) const
	{
		if (backgroundSlot)
			_visit(backgroundSlot);
		if (floorSlot)
			_visit(floorSlot);
		for (tileObject* wall : wallSlots)
		{
			if (wall)
				_visit(wall);
		}
		for (tileObject* fixture : fixtureSlots)
			_visit(fixture);
	}

protected:
	bool hasAnyWall() const;
	tileObject* backgroundSlot = nullptr;
	tileObject* floorSlot = nullptr;
	tileObject* wallSlots[4] = { nullptr, nullptr, nullptr, nullptr };
	std::list<tileObject*> fixtureSlots;
	//the actual objects are stored in our main level class
	//implement alpha blending between each tileObject at some point
};
