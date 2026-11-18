#pragma once
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Vector2.hpp>
#include <string>
#include <vector>

class station;
class isometricLevel;
struct ImFont;

//decks: each level of the station is a deck. every deck uses the station's whole grid, so they line up (a riser shaft
//is the same tile on each), and each has an area, any shape, where it can be built (isometricLevel::inArea). the floors
//built in it are the station's actual shape: the hull the exterior view draws
namespace decks
{
	const int gridSize = 48;    //a new station's grid, every deck the same

	enum class shape { rectangle, octagon, cross, ellipse };
	const int shapeCount = 4;
	const char* shapeName(shape _shape);
	//the tiles of a shape _size tiles big (x along the lower right, y along the lower left), centred on _centre
	std::vector<sf::Vector2i> shapeTiles(shape _shape, sf::Vector2i _size, sf::Vector2i _centre);

	//gives a deck exactly this area, except that a tile with something built on it stays in it. returns how many stayed
	//for that reason
	int setArea(isometricLevel& _level, const std::vector<sf::Vector2i>& _tiles);
	//adds the tiles _from.._to (corners either way round) to a deck's area
	void addArea(isometricLevel& _level, sf::Vector2i _from, sf::Vector2i _to);
	//takes them out of it, except a tile with something built on it. returns how many stayed for that reason
	int removeArea(isometricLevel& _level, sf::Vector2i _from, sf::Vector2i _to);
	//the middle of a deck's area (of the whole grid when all of it is area)
	sf::Vector2i areaCentre(const isometricLevel& _level);
	//tiles with a floor: the deck's part of the hull
	int hullCount(const isometricLevel& _level);
	//nothing at all built on it
	bool isEmpty(const isometricLevel& _level);

	//a new game's station: a gridSize x gridSize grid. the lowest deck's area a 24 x 18 rectangle with a walled 12 x 8 room
	//in its middle; the deck above an octagon, ready to build on. the lowest deck is the one shown
	void startingStation(station& _station);

	//the deck view's guides: the edge of the deck's area, and the outline of the hull on the deck below (to build over it).
	//with _wholeGrid, the grid outside the area too (while growing it). ImGui's background draw list: call between
	//ImGui::SFML::Update and Render
	void drawGuides(const sf::RenderWindow& _window, station& _station, bool _wholeGrid);
}

//the Decks panel (D): every deck from the top down, how big its area and its hull are, and a click to go to it; adding
//a deck above or below the shown one (its area a shape you pick), taking an empty deck away; and the shown deck's area
//changed by dragging over tiles (grow it, or shrink it: never under something built) or set to a shape
class deckPanel
{
public:
	enum class tool { none, grow, shrink };
	void toggle() { open = !open; }
	bool isOpen() const { return open; }
	tool current() const { return state; }
	bool active() const { return state != tool::none; }
	void stopTool();
	//Esc: the drag, then the tool. false if there was nothing to step back from
	bool escape();

	//every frame, once the GUI is built: a drag on the level being shown grows or shrinks its area
	void update(station& _station, sf::Vector2i _hovered, bool _inLevel, bool _leftPressed, bool _leftDown, bool _guiWantsMouse);
	//tints the tiles being dragged over (after the level's colours are reset, before it is drawn)
	void highlight(station& _station, sf::Vector2i _hovered, bool _inLevel);
	//the panel. true if the list of decks changed (a deck added or taken away: deck numbers have moved)
	bool draw(station& _station, float _scale, ImFont* _font);

private:
	void say(const std::string& _text, bool _problem);

	bool open = false;
	tool state = tool::none;
	bool dragging = false;
	sf::Vector2i dragFrom, dragTo;
	int shapeIndex = 1;                     //the shape picked for a new deck, or to set this one to
	sf::Vector2i shapeSize = sf::Vector2i(18, 14);
	bool confirmRemove = false;
	std::string message;
	bool messageProblem = false;
	double messageTime = -100.0;
};
