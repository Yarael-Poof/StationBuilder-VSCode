#include "networkOverlay.h"
#include "tileNetwork.h"
#include "isometricLevel.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace
{
	//one colour per network, reused in order. no red (warnings) or grey (unconnected), and no orange (the cable art)
	const ImU32 islandColours[] = {
		IM_COL32(0, 200, 255, 255),     //cyan
		IM_COL32(255, 80, 200, 255),    //magenta
		IM_COL32(120, 230, 60, 255),    //lime
		IM_COL32(170, 120, 255, 255),   //violet
		IM_COL32(255, 230, 60, 255),    //yellow
		IM_COL32(40, 220, 170, 255),    //teal
		IM_COL32(80, 120, 255, 255),    //blue
		IM_COL32(255, 140, 170, 255),   //pink
	};
	const int islandColourCount = int(sizeof(islandColours) / sizeof(islandColours[0]));
	const ImU32 outline = IM_COL32(0, 0, 0, 220);
	const ImU32 unconnected = IM_COL32(150, 150, 150, 255);

	//screen position of the middle of a tile's floor diamond
	ImVec2 tileCentre(isometricLevel* _isoLevel, const sf::RenderWindow& _window, sf::Vector2i _tile)
	{
		const sf::Vector2i pixel = _window.mapCoordsToPixel(_isoLevel->tileCentreWorld(sf::Vector2f(_tile)));
		return ImVec2(float(pixel.x), float(pixel.y));
	}

	//a small triangle above (or below) a tile's centre for a link to the next level: filled when linked,
	//an outline when the riser has nothing to link to on that side
	void levelLinkMarker(ImDrawList* _drawList, ImVec2 _centre, float _size, bool _up, ImU32 _colour, bool _linked)
	{
		const float dir = _up ? -1.f : 1.f;
		const ImVec2 apex(_centre.x, _centre.y + dir * _size * 2.4f);
		const ImVec2 left(_centre.x - _size, _centre.y + dir * _size * 1.1f);
		const ImVec2 right(_centre.x + _size, _centre.y + dir * _size * 1.1f);
		if (_linked)
		{
			_drawList->AddTriangleFilled(apex, left, right, _colour);
			_drawList->AddTriangle(apex, left, right, IM_COL32(0, 0, 0, 220), 1.5f);
		}
		else
			_drawList->AddTriangle(apex, left, right, _colour, 2.f);
	}
}

void drawNetworkDebug(const tileNetwork& _network, int _level, isometricLevel* _isoLevel, const sf::RenderWindow& _window)
{
	const std::vector<networkNode>& nodes = _network.nodes();
	const std::vector<networkIsland>& islands = _network.islands();
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();

	//sizes follow the zoom: how many pixels wide is one tile right now
	const sf::Vector2i tileLeft = _window.mapCoordsToPixel(sf::Vector2f(0.f, 0.f));
	const sf::Vector2i tileRight = _window.mapCoordsToPixel(sf::Vector2f(64.f, 0.f));
	const float tilePixels = std::hypot(float(tileRight.x - tileLeft.x), float(tileRight.y - tileLeft.y));
	const float lineWidth = std::clamp(tilePixels * 0.06f, 2.f, 10.f);
	const float dotRadius = std::clamp(tilePixels * 0.07f, 2.5f, 12.f);

	const ImVec2 display = ImGui::GetIO().DisplaySize;
	auto onScreen = [&](ImVec2 _p) {
		return _p.x > -tilePixels && _p.y > -tilePixels && _p.x < display.x + tilePixels && _p.y < display.y + tilePixels;
	};

	for (const networkEdge& edge : _network.edges())
	{
		const networkNode& a = nodes[edge.a];
		if (a.level != _level || edge.vertical)
			continue;   //links between levels are shown as markers on the tile, below
		const ImVec2 from = tileCentre(_isoLevel, _window, a.tile);
		const ImVec2 to = tileCentre(_isoLevel, _window, nodes[edge.b].tile);
		if (!onScreen(from) && !onScreen(to))
			continue;
		drawList->AddLine(from, to, outline, lineWidth + 2.f);
		drawList->AddLine(from, to, islandColours[a.island % islandColourCount], lineWidth);
	}

	for (const networkNode& node : nodes)
	{
		if (node.level != _level)
			continue;
		const ImVec2 centre = tileCentre(_isoLevel, _window, node.tile);
		if (!onScreen(centre))
			continue;
		const bool hasDevice = int(node.objects.size()) > node.conductors;
		const float radius = hasDevice ? dotRadius * 1.6f : dotRadius;
		if (node.conductors == 0 && islands[node.island].nodes.size() == 1)
		{
			//a device with no cable under it: nothing can reach it
			drawList->AddCircle(centre, radius, outline, 0, lineWidth + 2.f);
			drawList->AddCircle(centre, radius, unconnected, 0, lineWidth);
			continue;
		}
		drawList->AddCircleFilled(centre, radius + 1.5f, outline);
		drawList->AddCircleFilled(centre, radius, islandColours[node.island % islandColourCount]);

		//a riser: markers for its links up and down (outlines where it has no partner on a level that exists)
		if (node.verticalConductors > 0)
		{
			const ImU32 colour = islandColours[node.island % islandColourCount];
			const float markerSize = dotRadius * 1.6f;
			if (node.edgeUp >= 0 || node.level + 1 < _network.levelCount())
				levelLinkMarker(drawList, centre, markerSize, true, node.edgeUp >= 0 ? colour : unconnected, node.edgeUp >= 0);
			if (node.edgeDown >= 0 || node.level > 0)
				levelLinkMarker(drawList, centre, markerSize, false, node.edgeDown >= 0 ? colour : unconnected, node.edgeDown >= 0);
		}
	}
}
