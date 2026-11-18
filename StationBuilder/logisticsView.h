#pragma once
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Vector2.hpp>

class logisticsNetwork;
class processingSystem;
class isometricLevel;
struct ImFont;

//what the player sees of the item tubes: the logistics view (I). ImGui draw lists and windows: call between
//ImGui::SFML::Update and Render
namespace logisticsView
{
	//the scene dimmed, tubes drawn lit while their network is moving something (pulsing) and dark while idle, item
	//ports marked, and each container's port labelled with the kg/s going in (+) or out (-); an output that nothing
	//on its network will take from is marked blocked. each machine that makes things (_machines, may be nullptr) is
	//labelled with what it is doing and a bar for its cycle. _animationSeconds pulses the moving tubes
	void drawOverlay(const logisticsNetwork& _items, int _level, isometricLevel* _isoLevel, const sf::RenderWindow& _window,
		double _animationSeconds, ImFont* _font, const processingSystem* _machines = nullptr);
	//the view's key and the station's totals, pinned to the right edge with its top at _top. returns its bottom
	float drawLegend(const logisticsNetwork& _items, float _top, float _scale, ImFont* _font, const processingSystem* _machines = nullptr);
	//a tile's items network: its rate, what it is moving, and the containers on the tile. shown by the mouse
	void drawInspector(const logisticsNetwork& _items, int _level, sf::Vector2i _tile, ImFont* _font);
}
