#include "thermalView.h"
#include "thermalNetwork.h"
#include "isometricLevel.h"
#include "powerView.h"
#include <imgui.h>
#include "uiTheme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
	const char* degree = "\xC2\xB0";   //the degree sign, in UTF-8

	struct colourStop { double at; sf::Color colour; };
	const colourStop temperatureStops[] = {
		{ -150.0, sf::Color(120, 60, 200) },   //purple: frozen
		{ -40.0, sf::Color(40, 80, 210) },     //blue
		{ 0.0, sf::Color(60, 190, 240) },      //cyan
		{ 20.0, sf::Color(80, 210, 110) },     //green: comfortable
		{ 30.0, sf::Color(240, 215, 60) },     //yellow
		{ 45.0, sf::Color(255, 140, 30) },     //orange
		{ 70.0, sf::Color(230, 40, 40) },      //red: hot
	};
	const ImU32 openToSpaceColour = IM_COL32(200, 240, 255, 255);

	ImU32 toImGui(sf::Color _c, int _alpha = 255) { return IM_COL32(_c.r, _c.g, _c.b, _alpha); }
	ImVec4 toImGuiVec(sf::Color _c) { return ImVec4(_c.r / 255.f, _c.g / 255.f, _c.b / 255.f, 1.f); }
	ImVec2 toPixel(const sf::RenderWindow& _window, sf::Vector2f _world)
	{
		const sf::Vector2i p = _window.mapCoordsToPixel(_world);
		return ImVec2(float(p.x), float(p.y));
	}

	//the two corners of a tile's side, relative to its centre (world units): the diamond is 64 x 32
	void sideCorners(int _side, sf::Vector2f& _from, sf::Vector2f& _to)
	{
		const sf::Vector2f left(-32.f, 0.f), top(0.f, -16.f), right(32.f, 0.f), bottom(0.f, 16.f);
		switch (_side)
		{
		case 0: _from = left; _to = top; break;      //upper left
		case 1: _from = top; _to = right; break;     //upper right
		case 2: _from = left; _to = bottom; break;   //lower left
		default: _from = bottom; _to = right; break; //lower right
		}
	}

	void swatch(sf::Color _colour, const char* _label)
	{
		const float size = ImGui::GetTextLineHeight();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + size, p.y + size), toImGui(_colour));
		ImGui::Dummy(ImVec2(size, size));
		ImGui::SameLine();
		ImGui::TextUnformatted(_label);
		ImGui::SameLine();
	}
}

namespace thermalView
{
	sf::Color temperatureColour(double _celsius)
	{
		const int count = int(sizeof(temperatureStops) / sizeof(temperatureStops[0]));
		if (_celsius <= temperatureStops[0].at)
			return temperatureStops[0].colour;
		for (int i = 1; i < count; i++)
		{
			if (_celsius <= temperatureStops[i].at)
			{
				const double t = (_celsius - temperatureStops[i - 1].at) / (temperatureStops[i].at - temperatureStops[i - 1].at);
				const sf::Color& a = temperatureStops[i - 1].colour;
				const sf::Color& b = temperatureStops[i].colour;
				return sf::Color(sf::Uint8(a.r + (b.r - a.r) * t), sf::Uint8(a.g + (b.g - a.g) * t), sf::Uint8(a.b + (b.b - a.b) * t));
			}
		}
		return temperatureStops[count - 1].colour;
	}

	void drawOverlay(const thermalNetwork& _heat, int _level, isometricLevel* _isoLevel, const sf::RenderWindow& _window)
	{
		const std::vector<networkNode>& nodes = _heat.network().nodes();
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		const sf::Vector2i left = _window.mapCoordsToPixel(sf::Vector2f(0.f, 0.f));
		const sf::Vector2i right = _window.mapCoordsToPixel(sf::Vector2f(64.f, 0.f));
		const float tilePixels = std::hypot(float(right.x - left.x), float(right.y - left.y));
		const float edgeWidth = std::clamp(tilePixels * 0.05f, 2.f, 8.f);

		drawList->AddRectFilled(ImVec2(0.f, 0.f), display, IM_COL32(0, 0, 10, 90));
		for (int n = 0; n < int(nodes.size()); n++)
		{
			const networkNode& node = nodes[n];
			if (node.level != _level)
				continue;
			const sf::Vector2f c = _isoLevel->tileCentreWorld(sf::Vector2f(node.tile));
			const ImVec2 centre = toPixel(_window, c);
			if (centre.x < -tilePixels || centre.y < -tilePixels || centre.x > display.x + tilePixels || centre.y > display.y + tilePixels)
				continue;
			drawList->AddQuadFilled(toPixel(_window, c + sf::Vector2f(0.f, -16.f)), toPixel(_window, c + sf::Vector2f(32.f, 0.f)),
				toPixel(_window, c + sf::Vector2f(0.f, 16.f)), toPixel(_window, c + sf::Vector2f(-32.f, 0.f)),
				toImGui(temperatureColour(_heat.temperature(n)), 165));
			//edges open to space: where the heat pours out
			for (int s = 0; s < 4; s++)
			{
				const thermalSide& side = _heat.side(n, s);
				if (side.neighbour >= 0 || side.walls > 0)
					continue;
				sf::Vector2f from, to;
				sideCorners(s, from, to);
				drawList->AddLine(toPixel(_window, c + from), toPixel(_window, c + to), IM_COL32(0, 0, 0, 200), edgeWidth + 2.f);
				drawList->AddLine(toPixel(_window, c + from), toPixel(_window, c + to), openToSpaceColour, edgeWidth);
			}
			//an air duct riser: an arrow up and/or down for its link to the next level, coloured by the air at the
			//other end (an outline where a level is there but has no floored air duct riser on this tile)
			if (_heat.ductConductance(n) > 0.0)
			{
				const float markerSize = std::clamp(tilePixels * 0.11f, 4.f, 16.f);
				for (const bool up : { true, false })
				{
					const int edge = up ? node.edgeUp : node.edgeDown;
					if (edge >= 0 && _heat.ductLink(edge))
					{
						const networkEdge& link = _heat.network().edges()[edge];
						const int other = link.a == n ? link.b : link.a;
						powerView::levelLinkMarker(drawList, centre, markerSize, up, toImGui(temperatureColour(_heat.temperature(other))), true);
					}
					else if (up ? _level + 1 < _heat.network().levelCount() : _level > 0)
						powerView::levelLinkMarker(drawList, centre, markerSize, up, IM_COL32(230, 230, 240, 255), false);
				}
			}
		}
	}

	float drawLegend(const thermalNetwork& _heat, float _top, float _scale, ImFont* _font)
	{
		const float s = _scale;
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		if (_font)
			ImGui::PushFont(_font);
		ImGui::SetNextWindowPos(ImVec2(display.x - 8.f * s, _top), ImGuiCond_Always, ImVec2(1.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, uiTheme::panel());
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f * s, 8.f * s));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f * s, 4.f * s));
		ImGui::Begin("heatLegend", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);

		ImGui::TextUnformatted("HEAT VIEW");
		ImGui::SameLine();
		ImGui::TextDisabled("(T to close)");
		char label[32];
		for (double t : { -150.0, -40.0, 0.0, 20.0, 30.0, 45.0, 70.0 })
		{
			snprintf(label, sizeof(label), "%.0f%s", t, degree);
			swatch(temperatureColour(t), label);
		}
		ImGui::NewLine();
		{
			const float size = ImGui::GetTextLineHeight();
			const ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + size * 0.5f), ImVec2(p.x + size * 1.5f, p.y + size * 0.5f), openToSpaceColour, 4.f);
			ImGui::Dummy(ImVec2(size * 1.5f, size));
			ImGui::SameLine();
			ImGui::TextUnformatted("edge open to space (build a wall to stop the leak)");
		}
		ImGui::Separator();
		if (_heat.network().nodes().empty())
			ImGui::TextDisabled("no floors: nothing holds air yet");
		else
		{
			ImGui::Text("floored tiles: %d", int(_heat.network().nodes().size()));
			ImGui::Text("coldest %.1f%s, average %.1f%s, hottest %.1f%s", _heat.minimumTemperature(), degree, _heat.averageTemperature(), degree,
				_heat.maximumTemperature(), degree);
			ImGui::Text("heat in %s, lost to space %s", powerView::formatPower(_heat.totalHeatInput()).c_str(),
				powerView::formatPower(_heat.totalLossToSpace()).c_str());
		}

		const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
		if (_font)
			ImGui::PopFont();
		return bottom;
	}

	void drawInspector(const thermalNetwork& _heat, int _level, sf::Vector2i _tile, ImFont* _font)
	{
		const int node = _heat.network().nodeAt(_level, _tile);
		if (node < 0)
			return;
		const double t = _heat.temperature(node);
		if (_font)
			ImGui::PushFont(_font);
		ImGui::BeginTooltip();

		ImGui::Text("Tile %d,%d", _tile.x, _tile.y);
		ImGui::SameLine(0.f, ImGui::CalcTextSize(" ").x);   //a space before what follows (the GUI's style has no gap)
		ImGui::TextColored(toImGuiVec(temperatureColour(t)), "%.1f%sC", t, degree);
		const double rate = _heat.netHeatFlow(node) / _heat.heatCapacity(node) * 60.0;   //C per minute
		if (std::abs(rate) >= 0.05)
			ImGui::TextDisabled("%s %.1f%sC a minute, holds %.1f kJ/%sC", rate > 0 ? "warming" : "cooling", std::abs(rate), degree,
				_heat.heatCapacity(node) / 1000.0, degree);
		else
			ImGui::TextDisabled("steady, holds %.1f kJ/%sC", _heat.heatCapacity(node) / 1000.0, degree);
		if (_heat.heatInput(node) > 0.0)
			ImGui::Text("from its devices: +%s", powerView::formatPower(_heat.heatInput(node)).c_str());

		//each side: where it leads, whether walled, and the heat crossing it (+ in, - out)
		for (int s = 0; s < 4; s++)
		{
			const thermalSide& side = _heat.side(node, s);
			char where[64];
			if (side.neighbour >= 0)
			{
				const sf::Vector2i across = _heat.network().nodes()[side.neighbour].tile;
				snprintf(where, sizeof(where), "to %d,%d", across.x, across.y);
			}
			else
				snprintf(where, sizeof(where), "to space");
			const char* wall = side.walls == 0 ? "open" : (side.walls == 1 ? "wall" : "2 walls");
			double inward;
			if (side.neighbour >= 0)
				inward = side.conductance * (_heat.temperature(side.neighbour) - t);
			else
				inward = side.conductance * (thermalTuning::outsideTemperature - t);
			const ImVec4 colour = side.neighbour < 0 && side.walls == 0 ? ImVec4(0.8f, 0.95f, 1.f, 1.f) : ImVec4(1.f, 1.f, 1.f, 1.f);
			ImGui::TextColored(colour, "  %s, %s %s: %s%s", tileSides::name(s), where, wall, inward >= 0 ? "+" : "-",
				powerView::formatPower(std::abs(inward)).c_str());
		}
		const networkNode& n = _heat.network().nodes()[node];
		for (const bool up : { true, false })
		{
			const int edge = up ? n.edgeUp : n.edgeDown;
			if (edge < 0)
				continue;
			const networkEdge& e = _heat.network().edges()[edge];
			const double inward = (e.a == node ? -1.0 : 1.0) * _heat.heatFlow(edge);
			ImGui::Text("  %s, through %s to level %d: %s%s", up ? "up" : "down", _heat.ductLink(edge) ? "the air duct" : "the deck",
				_heat.network().nodes()[e.a == node ? e.b : e.a].level, inward >= 0 ? "+" : "-", powerView::formatPower(std::abs(inward)).c_str());
		}

		ImGui::EndTooltip();
		if (_font)
			ImGui::PopFont();
	}
}
