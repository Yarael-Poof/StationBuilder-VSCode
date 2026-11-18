#pragma once
#include <SFML/Graphics/RenderWindow.hpp>
#include "tileCatalogue.h"

class isometricLevel;
struct ImDrawList;
struct ImVec2;

//machines drawn by the game rather than in their art: an outline round a machine's footprint, and a tag on each of its
//ports labelled In, Out or Any, pointing into the face, out of it, or both ways, and coloured by what the port carries:
//power yellow, liquid blue, items orange. drawn on the placement ghost, round a selected machine,
//and in the power and liquid views. ImGui draw lists: call between ImGui::SFML::Update and Render
namespace machineView
{
	//the colour ports carrying _kind are marked in (an ImU32)
	unsigned int portColour(portInfo::kind _kind);
	//one port marker in screen pixels: _middle the middle of its face, _out the unit direction out of the face on screen,
	//_length how long the marker is (11 at the game's 1:1 zoom)
	void drawPortMarker(ImDrawList* _drawList, const ImVec2& _middle, const ImVec2& _out, float _length, portInfo::kind _kind,
		portInfo::direction _flow, int _alpha = 255);
	//one machine's ports (_type turned _turns, with its upper tile at _origin), for the kinds in _kinds (bit (1 << kind))
	void drawPorts(ImDrawList* _drawList, const sf::RenderWindow& _window, const isometricLevel& _level, const tileTypeInfo& _type, int _turns,
		sf::Vector2i _origin, int _kinds = 7, int _alpha = 255);
	//every placed machine's ports on a level, for the kinds in _kinds
	void drawLevelPorts(ImDrawList* _drawList, const sf::RenderWindow& _window, isometricLevel& _level, int _kinds);
	//an outline round a footprint of _size tiles with its upper tile at _origin
	void drawFootprint(ImDrawList* _drawList, const sf::RenderWindow& _window, const isometricLevel& _level, sf::Vector2i _origin, sf::Vector2i _size,
		unsigned int _colour, float _width);
	//the bit for a kind, for _kinds
	inline int kindBit(portInfo::kind _kind) { return 1 << int(_kind); }
}
