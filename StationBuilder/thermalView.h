#pragma once
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/RenderWindow.hpp>

class thermalNetwork;
class isometricLevel;
struct ImFont;

//what the player sees of the heat simulation: the heat view (T), its key, and a hover inspector.
//the draw functions use ImGui draw lists, so call them between ImGui::SFML::Update and Render
namespace thermalView
{
	//tile colour for a temperature: purple when frozen, blue, cyan, green around 20 C, yellow, orange, red when hot
	sf::Color temperatureColour(double _celsius);

	//the heat view: the scene dimmed, every floored tile tinted by its temperature, and the edges where a tile
	//is open to space (no wall) picked out, since that is where heat pours out
	void drawOverlay(const thermalNetwork& _heat, int _level, isometricLevel* _isoLevel, const sf::RenderWindow& _window);
	//the colour key and the station's temperatures, pinned to the right edge with its top at _top. returns its bottom
	float drawLegend(const thermalNetwork& _heat, float _top, float _scale, ImFont* _font);
	//a floored tile's temperature, what it holds, the heat coming in from devices and across each side
	void drawInspector(const thermalNetwork& _heat, int _level, sf::Vector2i _tile, ImFont* _font);
}
