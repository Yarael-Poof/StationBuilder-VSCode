#pragma once
#include "flight.h"
#include <SFML/Window/Event.hpp>
#include <imgui.h>
#include <string>

class world;
struct ImFont;

//the nav map (N): the local area seen from above, as a terminal's vector display. every vessel is drawn as its decks'
//outline (floors filled, frames open, equipment outside shaded), turned to its heading, with its name and speed; the
//selected ship's velocity, trail and target show too. it is drawn into ImGui's background (so it glows like the rest
//of the GUI) in place of the deck view.
//mouse: the wheel zooms, a right drag moves the map, a left click picks a vessel, a right click (without dragging)
//sends the picked ship there on the autopilot. its panel lists the vessels, makes new ships, and shows and flies the
//picked one (autopilot orders, or manual: W thrust, S back, A and D turn, Q and E slide)
class navMap
{
public:
	//the map, filling the screen. call between ImGui::SFML::Update and Render
	void draw(world& _world, float _scale, ImFont* _font);
	//its panel, pinned to the right with its top at _top. returns its bottom
	float drawPanel(world& _world, float _top, float _scale, ImFont* _font);
	//a mouse event on the map; true if the map used it (so the deck view mustn't)
	bool handleEvent(const sf::Event& _event, world& _world, bool _guiWantsMouse);
	//each frame: the flying keys, for the picked ship in manual (_keysFree: not typing into a text box)
	void readControls(world& _world, bool _keysFree);
	//true if the key flies the picked ship (so it isn't used for anything else while the map is open)
	bool isFlyingKey(sf::Keyboard::Key _key) const;
	//true once after "Go inside" was pressed: main switches the screen to that vessel and closes the map
	bool takeGoInside() { const bool g = goInside; goInside = false; return g; }

	int picked = 0;    //the vessel picked (its number in the world)

private:
	int dockPick = 0;               //the dock picked in the panel's list
	std::string dockMessage;        //why the last docking order didn't take
	ImVec2 toScreen(vec2 _area) const;
	vec2 toArea(ImVec2 _screen) const;
	void fitTo(world& _world);

	vec2 centre;                    //the area point at the middle of the screen
	double pixelsPerMetre = 1.0;
	bool fitted = false;
	bool dragging = false;
	bool dragMoved = false;
	sf::Vector2i dragStart;
	sf::Vector2i dragLast;
	bool goInside = false;
};
