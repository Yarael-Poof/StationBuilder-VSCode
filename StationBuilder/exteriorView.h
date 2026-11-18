#pragma once
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Vector2.hpp>

class station;
struct ImFont;

//the station from outside (O): every deck stacked, the lowest first, each raised a deck's height above the one below.
//its hull is built from the floors: each floored tile is a block whose roof shows where nothing is built on the deck
//above, and whose two near sides show where the tile beside it is open (no floor). a walled side is hull plating with a
//window; a side with no wall is open to space (the same gaps the heat model loses heat through), drawn dark with a red
//edge. what is mounted outside (radiators, solar arrays, jetties: anything on a tile with no floor) is drawn on its
//deck. the insides are hidden. stars behind
namespace exteriorView
{
	const float deckHeight = 40.f;   //world units a deck rises above the one below (a tile's floor diamond is 64 x 32)

	//draws it all (in place of the shown deck). _hoveredDeck is lit
	void draw(sf::RenderWindow& _window, station& _station, int _hoveredDeck);
	//the deck under the mouse (its roof or near sides), -1 if none
	int deckAt(station& _station, sf::Vector2f _mouseWorld);
	//the view's key: decks, hull, what is outside, and the deck under the mouse. pinned to the right edge at _top.
	//returns its bottom
	float drawLegend(station& _station, int _hoveredDeck, float _top, float _scale, ImFont* _font);
}
