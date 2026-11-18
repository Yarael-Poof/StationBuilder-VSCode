#pragma once
#include <SFML/Graphics/RenderWindow.hpp>

class fluidNetwork;
class isometricLevel;
struct ImFont;

//what the player sees of the liquid simulation: the liquid view (L), its key, a hover inspector, and warnings.
//the draw functions use ImGui draw lists, so call them between ImGui::SFML::Update and Render
namespace fluidView
{
	//the liquid view: the scene dimmed, every tile that holds liquid tinted by the liquid's temperature (grey for
	//networks without enough liquid), dots moving along the pipes with the flow, a fill bar on each tank, an arrow
	//on each pump for the way it pushes, and each heat exchanger's heat into (+) or out of (-) the air.
	//_animationSeconds moves the dots; stop advancing it (when paused) to freeze them
	void drawOverlay(const fluidNetwork& _fluid, int _level, isometricLevel* _isoLevel, const sf::RenderWindow& _window,
		double _animationSeconds, ImFont* _font);
	//the colour key and the station's liquid totals, pinned to the right edge with its top at _top. returns its bottom
	float drawLegend(const fluidNetwork& _fluid, float _top, float _scale, ImFont* _font);
	//a tile's liquid: its network, temperature, pressure, the flow on each side, its tanks, pumps and exchangers
	void drawInspector(const fluidNetwork& _fluid, int _level, sf::Vector2i _tile, ImFont* _font);
}
