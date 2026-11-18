#pragma once
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Vector2.hpp>
#include <string>
#include <vector>
#include "blueprint.h"

class station;
class textureManager;
struct ImFont;
struct ImDrawList;
struct ImVec2;

//copy and paste, and blueprints. C picks the copy tool: drag over tiles to select them, then Copy (Ctrl+C) takes what is
//on them and starts placing it. placing shows a see-through copy under the mouse (dark where something is in the way):
//click to place it (again as often as wanted), R to turn it, Esc to stop; Ctrl+V places the last copy again. B opens the
//Blueprints menu: the copy can be saved there as a blueprint (a file in blueprints/), and any blueprint, the game's
//examples included, picked to place
class blueprintTool
{
public:
	enum class mode { none, selecting, placing };
	explicit blueprintTool(const std::string& _folder = blueprints::folderName);

	mode current() const { return state; }
	bool active() const { return state != mode::none; }
	void startSelecting();
	//places this blueprint (the copy, or one from the menu) until stopped
	void startPlacing(const blueprint& _blueprint);
	void stop();
	//Ctrl+C: what is on the selected tiles becomes the copy, and placing it starts. false if nothing is selected
	bool copySelection(station& _station);
	//Ctrl+V: places the last copy again. false if there is none
	bool pasteCopy();
	//R while placing: a quarter turn clockwise
	void turn();
	//Esc: the selection goes, then the tool stops. false if it had nothing to step back from
	bool escape();
	void togglePanel() { panelOpen = !panelOpen; }
	bool isPanelOpen() const { return panelOpen; }

	//every frame, once the GUI is built: the mouse on the level being shown. a drag selects (copy tool), a click places
	void update(station& _station, sf::Vector2i _hovered, bool _inLevel, bool _leftPressed, bool _leftDown, bool _guiWantsMouse);
	//tints the selected tiles (after the level's tile colours are reset, before it is drawn)
	void highlight(station& _station, sf::Vector2i _hovered, bool _inLevel);
	//what a click would place, over the drawn level
	void drawGhost(sf::RenderWindow& _window, station& _station, textureManager& _textures, sf::Vector2i _hovered, bool _inLevel);
	//the Blueprints menu, and the tool's notes (the selection's size and buttons, what placing does)
	void drawPanels(station& _station, textureManager& _textures, float _scale, ImFont* _font);

	//a small picture of a blueprint in a box (_size pixels) at _corner, drawn from the items' art
	static void drawPreview(ImDrawList* _drawList, textureManager& _textures, const blueprint& _blueprint, const ImVec2& _corner, const ImVec2& _size);
	//the texture key one object of a blueprint is drawn with (a line piece's shape, a machine's turn)
	static std::string artFor(const blueprintObject& _object, textureManager& _textures);

private:
	void reloadLibrary();
	void say(const std::string& _text, bool _problem);

	std::string folder;
	mode state = mode::none;
	//the copy tool: the drag, and the tiles it has selected (on one level)
	bool dragging = false;
	bool selected = false;
	sf::Vector2i dragFrom, dragTo;
	int selectionLevel = 0;
	//the last copy, and what is being placed (turned as R has set it)
	blueprint clipboard;
	blueprint placing;
	int turns = 0;
	//the menu
	bool panelOpen = false;
	std::vector<blueprint> library;
	std::vector<std::string> libraryProblems;
	bool libraryRead = false;
	std::string saveName;
	std::string saveDescription;
	std::string confirmDelete;    //the file asked about
	std::string message;
	bool messageProblem = false;
	double messageTime = -100.0;
};
