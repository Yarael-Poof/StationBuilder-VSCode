#pragma once
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Vector2.hpp>
#include <functional>
#include <string>

class station;
class isometricLevel;
class tileObject;
class electricalNetwork;
class thermalNetwork;
class fluidNetwork;
class logisticsNetwork;
class processingSystem;
struct ImFont;

//the panel for a selected tile: every object on it, live readings for its electrical ones (generators, loads,
//cables), controls for any settings they have (a generator's switch and output voltage), what a riser carries
//(cable, pipe or air duct: switching it switches its whole shaft), and Reset for a tripped generator. a tile is
//selected with a left click when not building or bulldozing; Esc or the panel's close button clears it
class tilePanel
{
public:
	void select(int _level, sf::Vector2i _tile);
	void clear();
	bool hasSelection() const { return selected; }
	int level() const { return selectedLevel; }
	sf::Vector2i tile() const { return selectedTile; }

	//draws the panel pinned to the right edge of the screen with its top at _top. clears the selection if its
	//tile has gone (a level deleted, another station loaded). _heat and _fluid may be nullptr (no temperatures or
	//liquid readings shown). call between ImGui::SFML::Update and Render
	void draw(station& _station, electricalNetwork& _power, const thermalNetwork* _heat, const fluidNetwork* _fluid, float _top,
		float _scale, ImFont* _font, const logisticsNetwork* _items = nullptr, const processingSystem* _machines = nullptr);
	//an outline around the selected tile, when it is on the level being shown
	void drawOutline(station& _station, int _currentLevel, const sf::RenderWindow& _window) const;
	//set by the game: opens the content editor on an item's definition (the "edit" button by each object's name)
	std::function<void(const std::string&)> onEditDefinition;

private:
	bool selected = false;
	int selectedLevel = 0;
	sf::Vector2i selectedTile;
	std::string kindMessage;      //why the last riser switch was refused ("level 2 already has a pipe on 3,3"), "" if it wasn't
	std::string kindMessageKey;   //the kind of riser it was about

	//a tank's Fill (with a liquid picked from a list) and Drain buttons. for testing, until liquids can be made
	void tankControls(tileObject* _tank, isometricLevel* _level, float _scale);
	std::string fillLiquid;       //the liquid picked to fill with, "" for the tank's own
	std::string tankMessage;      //why the last Fill was refused

	//a container's fill bar and what it holds, and buttons to put a solid in, take it out or empty it (for testing,
	//until tubes and machines bring materials)
	void storageControls(tileObject* _store, float _scale, const logisticsNetwork* _items);
	std::string storeMaterial;    //the solid picked for the buttons
	std::string storeMessage;     //"only 40 kg fitted"

	//a machine that makes things: its recipe (picked from a list), what it is doing, how far through its cycle, and its
	//input and output buffers. for testing: buttons to fill its inputs and empty its outputs
	void processorControls(tileObject* _machine, isometricLevel* _level, float _scale, const processingSystem* _machines,
		const logisticsNetwork* _items);
};
