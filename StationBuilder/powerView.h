#pragma once
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <string>

class electricalNetwork;
class station;
class isometricLevel;
struct ImFont;
struct ImDrawList;
struct ImVec2;

//everything the player sees of the electrical simulation: loads that dim, the power view (P), the hover
//inspector and the generator warnings. (the debug wiring view is in networkOverlay.h.)
//the draw functions use ImGui draw lists, so call them between ImGui::SFML::Update and Render
namespace powerView
{
	// ---- colour rules (no drawing, so they can be tested on their own) ----

	//how bright a load looks: 1 at its rated power, less as its supply sags, 0 with no power
	double loadBrightness(double _watts, double _ratedWatts);
	//the sprite colour for a load at that brightness: white when fully powered, darker (with the square of the
	//brightness, as lamps look) as it dims, dark grey when off
	sf::Color loadColour(double _brightness);
	//tile colour for a voltage as a fraction of nominal (230 V): green when healthy, through yellow and orange to red
	sf::Color voltageColour(double _fractionOfNominal);
	//cable colour for a current as a fraction of the cable's rating: blue when light, cyan, yellow, orange at 100%, red over
	sf::Color currentColour(double _fractionOfRating);
	//amps the link between two network nodes is rated for: the weaker end, with parallel cables on a tile adding up
	double linkRating(const electricalNetwork& _power, int _edge);
	//how many of the electrical networks are live (have a working generator)
	int countLiveNetworks(const electricalNetwork& _power);

	// ---- in the world ----

	//colours the electrical sprites on _level: every load (light, heater...) by its brightness, and generators
	//dark while switched off or tripped. call after station::highlightAllTiles (which resets sprite colours) and
	//before the level is drawn
	void applyDeviceShading(const electricalNetwork& _power, station& _station, int _level);
	//the colour a generator is drawn while switched off or tripped
	const sf::Color generatorOffColour(110, 110, 120);
	//"59.99 W", "1.23 kW", "2.50 MW"; the same with "var" or "VA" as the unit
	std::string formatPower(double _value, const char* _unit = "W");
	//the power view: the scene dimmed, wired tiles tinted by voltage (grey = no power, red = faulted), cables
	//coloured by current with dots flowing the way the power goes, and a load bar under each generator.
	//_animationSeconds moves the dots; stop advancing it (when paused) to freeze them
	void drawOverlay(const electricalNetwork& _power, int _level, isometricLevel* _isoLevel, const sf::RenderWindow& _window, double _animationSeconds);
	//warnings over generators, shown all the time: amber while overloaded (with the time left before it trips),
	//red once tripped, red over every generator of a network whose generators disagree on frequency
	void drawAlerts(const electricalNetwork& _power, int _level, isometricLevel* _isoLevel, const sf::RenderWindow& _window, ImFont* _font);

	// ---- panels ----

	//the power view's colour key and the station's totals, pinned to the right edge with its top at _top.
	//returns the screen y of its bottom edge
	float drawLegend(const electricalNetwork& _power, float _top, float _scale, ImFont* _font);
	//details of a tile on the electrical network (nothing if it isn't): voltage, each cable link's current,
	//each device's power, its network's totals. shown as a tooltip by the mouse
	void drawInspector(const electricalNetwork& _power, int _level, sf::Vector2i _tile, ImFont* _font);

	//a small triangle above (_up) or below a tile's centre (screen pixels) for a riser's link to the next level:
	//filled with _colour (an ImU32) when linked, an outline in it when there is nothing to link to on that side.
	//the liquid and heat views use it for pipe and air duct risers too
	void levelLinkMarker(ImDrawList* _drawList, ImVec2 _centre, float _size, bool _up, unsigned int _colour, bool _linked);
}
