#include "fluidView.h"
#include "machineView.h"
#include "fluidNetwork.h"
#include "thermalNetwork.h"
#include "thermalView.h"
#include "powerView.h"
#include "isometricLevel.h"
#include "tileCatalogue.h"
#include <imgui.h>
#include "uiTheme.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace
{
	const char* degree = "\xC2\xB0";   //the degree sign, in UTF-8
	const ImU32 pipeOutline = IM_COL32(0, 0, 0, 200);
	const ImU32 noLiquidColour = IM_COL32(110, 110, 120, 255);

	ImU32 toImGui(sf::Color _c, int _alpha = 255) { return IM_COL32(_c.r, _c.g, _c.b, _alpha); }
	ImVec4 toImGuiVec(sf::Color _c) { return ImVec4(_c.r / 255.f, _c.g / 255.f, _c.b / 255.f, 1.f); }
	ImVec2 toPixel(const sf::RenderWindow& _window, sf::Vector2f _world)
	{
		const sf::Vector2i p = _window.mapCoordsToPixel(_world);
		return ImVec2(float(p.x), float(p.y));
	}
	float tilePixelWidth(const sf::RenderWindow& _window)
	{
		const sf::Vector2i left = _window.mapCoordsToPixel(sf::Vector2f(0.f, 0.f));
		const sf::Vector2i right = _window.mapCoordsToPixel(sf::Vector2f(64.f, 0.f));
		return std::hypot(float(right.x - left.x), float(right.y - left.y));
	}
	//"0.25 L/s", "1.30 L/s"
	std::string flowText(double _litresPerSecond)
	{
		char text[32];
		snprintf(text, sizeof(text), "%.2f L/s", _litresPerSecond);
		return text;
	}
	const ImU32 mixedColour = IM_COL32(230, 40, 40, 255);
	//"Water", "Water + Coolant" (mixed), "no liquid"
	std::string liquidNames(const fluidNetwork& _fluid, int _island)
	{
		std::string names;
		for (const materialInfo* liquid : _fluid.islandLiquidTypes(_island))
			names += (names.empty() ? "" : " + ") + liquid->name;
		return names.empty() ? "no liquid" : names;
	}
	//a coloured square followed by text, in a panel
	void swatch(sf::Color _colour, const char* _text)
	{
		const float size = ImGui::GetTextLineHeight();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + size, p.y + size), toImGui(_colour));
		ImGui::Dummy(ImVec2(size, size));
		ImGui::SameLine();
		ImGui::TextUnformatted(_text);
	}
}

namespace fluidView
{
	void drawOverlay(const fluidNetwork& _fluid, int _level, isometricLevel* _isoLevel, const sf::RenderWindow& _window,
		double _animationSeconds, ImFont* _font)
	{
		const std::vector<networkNode>& nodes = _fluid.network().nodes();
		const std::vector<networkEdge>& edges = _fluid.network().edges();
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		const float tilePixels = tilePixelWidth(_window);
		const float lineWidth = std::clamp(tilePixels * 0.09f, 2.f, 14.f);
		auto onScreen = [&](ImVec2 _p) {
			return _p.x > -tilePixels && _p.y > -tilePixels && _p.x < display.x + tilePixels && _p.y < display.y + tilePixels;
		};
		auto filled = [&](int _node) { return _fluid.islandState(nodes[_node].island) == fluidState::filled; };
		auto centreOf = [&](int _node) { return toPixel(_window, _isoLevel->tileCentreWorld(sf::Vector2f(nodes[_node].tile))); };

		drawList->AddRectFilled(ImVec2(0.f, 0.f), display, IM_COL32(0, 0, 10, 90));

		//tiles holding liquid, tinted by which liquid it is (faint when there isn't enough of it, red when mixed); the
		//pipes over them show its temperature
		for (int n = 0; n < int(nodes.size()); n++)
		{
			if (nodes[n].level != _level)
				continue;
			const sf::Vector2f c = _isoLevel->tileCentreWorld(sf::Vector2f(nodes[n].tile));
			if (!onScreen(toPixel(_window, c)))
				continue;
			const fluidState state = _fluid.islandState(nodes[n].island);
			const materialInfo* liquid = _fluid.islandLiquidType(nodes[n].island);
			ImU32 colour = IM_COL32(110, 110, 120, 120);
			if (state == fluidState::mixed)
				colour = (mixedColour & 0x00FFFFFF) | (170u << IM_COL32_A_SHIFT);
			else if (liquid)
				colour = toImGui(liquid->colour, state == fluidState::filled ? 160 : 70);
			const float k = 0.6f;   //smaller than the tile: the pipe, not the room
			drawList->AddQuadFilled(toPixel(_window, c + sf::Vector2f(0.f, -16.f * k)), toPixel(_window, c + sf::Vector2f(32.f * k, 0.f)),
				toPixel(_window, c + sf::Vector2f(0.f, 16.f * k)), toPixel(_window, c + sf::Vector2f(-32.f * k, 0.f)), colour);
		}

		//pipes, coloured by temperature, with dots moving the way the liquid flows
		for (int e = 0; e < int(edges.size()); e++)
		{
			if (edges[e].vertical || nodes[edges[e].a].level != _level)
				continue;
			const ImVec2 from = centreOf(edges[e].a), to = centreOf(edges[e].b);
			if (!onScreen(from) && !onScreen(to))
				continue;
			const bool full = filled(edges[e].a);
			const double t = 0.5 * (_fluid.liquidTemperature(edges[e].a) + _fluid.liquidTemperature(edges[e].b));
			drawList->AddLine(from, to, pipeOutline, lineWidth + 2.f);
			drawList->AddLine(from, to, full ? toImGui(thermalView::temperatureColour(t)) : noLiquidColour, lineWidth);
			const double q = _fluid.flow(e);
			if (!full || std::abs(q) < 1e-4)
				continue;
			const ImVec2 start = q > 0.0 ? from : to;
			const ImVec2 end = q > 0.0 ? to : from;
			const double speed = 0.4 + 1.2 * std::min(std::abs(q), 2.0);   //tile lengths per second
			for (double offset : { 0.0, 0.5 })
			{
				const float s = float(std::fmod(_animationSeconds * speed + offset, 1.0));
				drawList->AddCircleFilled(ImVec2(start.x + (end.x - start.x) * s, start.y + (end.y - start.y) * s), lineWidth * 0.45f, IM_COL32(235, 250, 255, 235));
			}
		}

		//pipe risers: an arrow up and/or down for their link to the next level, coloured by the liquid's temperature
		//like the pipes (an outline where a level is there but has no pipe riser on this tile to link to)
		const float markerSize = std::clamp(tilePixels * 0.11f, 4.f, 16.f);
		for (int n = 0; n < int(nodes.size()); n++)
		{
			if (nodes[n].level != _level || nodes[n].verticalConductors == 0)
				continue;
			const ImVec2 centre = centreOf(n);
			if (!onScreen(centre))
				continue;
			for (const bool up : { true, false })
			{
				const int edge = up ? nodes[n].edgeUp : nodes[n].edgeDown;
				const bool levelThere = up ? _level + 1 < _fluid.network().levelCount() : _level > 0;
				if (edge < 0)
				{
					if (levelThere)
						powerView::levelLinkMarker(drawList, centre, markerSize, up, noLiquidColour, false);
					continue;
				}
				const double t = 0.5 * (_fluid.liquidTemperature(edges[edge].a) + _fluid.liquidTemperature(edges[edge].b));
				powerView::levelLinkMarker(drawList, centre, markerSize, up, filled(n) ? toImGui(thermalView::temperatureColour(t)) : noLiquidColour, true);
			}
		}

		//tanks: a fill bar beside the tile, in the colour of their liquid
		for (const fluidTank& tank : _fluid.tanks())
		{
			if (nodes[tank.node].level != _level)
				continue;
			const ImVec2 centre = centreOf(tank.node);
			if (!onScreen(centre))
				continue;
			const float width = std::max(4.f, tilePixels * 0.08f), height = tilePixels * 0.45f;
			const ImVec2 topLeft(centre.x + tilePixels * 0.22f, centre.y - height * 0.8f);
			const ImVec2 bottomRight(topLeft.x + width, topLeft.y + height);
			const float fill = float(std::clamp(tank.contents / tank.capacity, 0.0, 1.0));
			drawList->AddRectFilled(ImVec2(topLeft.x - 1.f, topLeft.y - 1.f), ImVec2(bottomRight.x + 1.f, bottomRight.y + 1.f), IM_COL32(0, 0, 0, 230));
			drawList->AddRectFilled(ImVec2(topLeft.x, bottomRight.y - height * fill), bottomRight, tank.liquid ? toImGui(tank.liquid->colour) : IM_COL32(70, 160, 255, 255));
		}

		//pumps: an arrow from the tile towards the side it pushes, bright when running
		for (const fluidPump& pump : _fluid.pumps())
		{
			if (nodes[pump.node].level != _level)
				continue;
			const sf::Vector2f c = _isoLevel->tileCentreWorld(sf::Vector2f(nodes[pump.node].tile));
			const sf::Vector2i step = tileSides::neighbourStep(pump.side);
			//a neighbour's centre is half a tile across and a quarter down for each step in x, back and down for y
			const sf::Vector2f towards(32.f * float(step.x - step.y), 16.f * float(step.x + step.y));
			const ImVec2 base = toPixel(_window, c), tip = toPixel(_window, c + towards * 0.45f);
			const ImVec2 dir(tip.x - base.x, tip.y - base.y);
			const float length = std::max(1.f, std::hypot(dir.x, dir.y));
			const ImVec2 unit(dir.x / length, dir.y / length), across(-unit.y, unit.x);
			const float w = lineWidth * 0.9f;
			const ImVec2 left(tip.x - unit.x * w * 1.6f + across.x * w, tip.y - unit.y * w * 1.6f + across.y * w);
			const ImVec2 right(tip.x - unit.x * w * 1.6f - across.x * w, tip.y - unit.y * w * 1.6f - across.y * w);
			const bool running = pump.head > 0.0 && filled(pump.node);
			const ImU32 colour = running ? IM_COL32(255, 255, 255, 255) : IM_COL32(140, 140, 150, 255);
			drawList->AddTriangleFilled(tip, left, right, colour);
			drawList->AddTriangle(tip, left, right, IM_COL32(0, 0, 0, 220), 1.5f);
		}

		//machines' liquid ports, where they join the pipes
		machineView::drawLevelPorts(drawList, _window, *_isoLevel, machineView::kindBit(portInfo::kind::liquid));

		//heat exchangers: the heat they are moving, + into the air, - out of it
		ImFont* font = _font ? _font : ImGui::GetFont();
		//a mixed network: a red label on its first tank on this level, naming its liquids
		std::vector<bool> labelled(_fluid.network().islands().size(), false);
		for (const fluidTank& tank : _fluid.tanks())
		{
			const int island = nodes[tank.node].island;
			if (nodes[tank.node].level != _level || labelled[island] || _fluid.islandState(island) != fluidState::mixed)
				continue;
			labelled[island] = true;
			const ImVec2 centre = centreOf(tank.node);
			if (!onScreen(centre))
				continue;
			const std::string text = "mixed: " + liquidNames(_fluid, island);
			const ImVec2 size = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text.c_str());
			const ImVec2 topLeft(centre.x - size.x * 0.5f, centre.y - tilePixels * 0.75f - size.y);
			drawList->AddRectFilled(ImVec2(topLeft.x - 4.f, topLeft.y - 2.f), ImVec2(topLeft.x + size.x + 4.f, topLeft.y + size.y + 2.f), IM_COL32(60, 0, 0, 230));
			drawList->AddText(font, font->FontSize, topLeft, IM_COL32(255, 120, 120, 255), text.c_str());
		}
		for (const fluidExchanger& exchanger : _fluid.exchangers())
		{
			if (nodes[exchanger.node].level != _level || std::abs(exchanger.heatToAir) < 1.0)
				continue;
			const ImVec2 centre = centreOf(exchanger.node);
			if (!onScreen(centre))
				continue;
			char text[48];
			snprintf(text, sizeof(text), "%s%s", exchanger.heatToAir > 0 ? "+" : "-", powerView::formatPower(std::abs(exchanger.heatToAir)).c_str());
			const ImVec2 size = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text);
			const ImVec2 topLeft(centre.x - size.x * 0.5f, centre.y + tilePixels * 0.12f);
			drawList->AddRectFilled(ImVec2(topLeft.x - 3.f, topLeft.y - 2.f), ImVec2(topLeft.x + size.x + 3.f, topLeft.y + size.y + 2.f), IM_COL32(0, 0, 0, 200));
			drawList->AddText(font, font->FontSize, topLeft, exchanger.heatToAir > 0 ? IM_COL32(255, 160, 80, 255) : IM_COL32(120, 200, 255, 255), text);
		}
	}

	float drawLegend(const fluidNetwork& _fluid, float _top, float _scale, ImFont* _font)
	{
		const float s = _scale;
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		if (_font)
			ImGui::PushFont(_font);
		ImGui::SetNextWindowPos(ImVec2(display.x - 8.f * s, _top), ImGuiCond_Always, ImVec2(1.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, uiTheme::panel());
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f * s, 8.f * s));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f * s, 4.f * s));
		ImGui::Begin("liquidLegend", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);

		ImGui::TextUnformatted("LIQUID VIEW");
		ImGui::SameLine();
		ImGui::TextDisabled("(L to close)");
		ImGui::TextDisabled("pipes coloured by liquid temperature (as the heat view), dots show the flow");
		ImGui::TextDisabled("tiles tinted by the liquid in them (faint: not enough of it, red: mixed liquids)");
		ImGui::TextDisabled("bars: tank levels. arrows: pumps");
		ImGui::Separator();

		int full = 0, short_ = 0, dry = 0, mixed = 0;
		for (int i = 0; i < int(_fluid.network().islands().size()); i++)
		{
			const fluidState state = _fluid.islandState(i);
			if (state == fluidState::filled) full++;
			else if (state == fluidState::notEnough) short_++;
			else if (state == fluidState::mixed) mixed++;
			else dry++;
		}
		//what is in the tanks, liquid by liquid
		std::vector<std::pair<const materialInfo*, double>> held;
		for (const fluidTank& tank : _fluid.tanks())
		{
			if (!tank.liquid || tank.contents <= 0.0)
				continue;
			auto found = std::find_if(held.begin(), held.end(), [&](const auto& _h) { return _h.first == tank.liquid; });
			if (found == held.end())
				held.push_back({ tank.liquid, tank.contents });
			else
				found->second += tank.contents;
		}
		double capacity = 0.0;
		for (const fluidTank& tank : _fluid.tanks())
			capacity += tank.capacity;
		int running = 0;
		for (const fluidPump& pump : _fluid.pumps())
			running += pump.head > 0.0;
		double toAir = 0.0, fromAir = 0.0;
		for (const fluidExchanger& exchanger : _fluid.exchangers())
		{
			if (exchanger.heatToAir > 0) toAir += exchanger.heatToAir;
			else fromAir -= exchanger.heatToAir;
		}
		ImGui::Text("networks: %d full, %d short of liquid, %d with none", full, short_, dry);
		if (mixed > 0)
			ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f), "%d with mixed liquids: nothing flows (separate them, or drain a tank)", mixed);
		ImGui::Text("liquid in tanks: %.0f of %.0f L", _fluid.totalLiquid(), capacity);
		for (const auto& [liquid, litres] : held)
		{
			char text[96];
			snprintf(text, sizeof(text), "%s: %.0f L", liquid->name.c_str(), litres);
			ImGui::SameLine();
			swatch(liquid->colour, text);
		}
		ImGui::Text("pumps: %d running of %d", running, int(_fluid.pumps().size()));
		ImGui::Text("heat exchangers: taking %s from the air, giving %s", powerView::formatPower(fromAir).c_str(), powerView::formatPower(toAir).c_str());

		const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
		if (_font)
			ImGui::PopFont();
		return bottom;
	}

	void drawInspector(const fluidNetwork& _fluid, int _level, sf::Vector2i _tile, ImFont* _font)
	{
		const int node = _fluid.network().nodeAt(_level, _tile);
		if (node < 0)
			return;
		const networkNode& n = _fluid.network().nodes()[node];
		const fluidState state = _fluid.islandState(n.island);
		const bool full = state == fluidState::filled;
		if (_font)
			ImGui::PushFont(_font);
		ImGui::BeginTooltip();

		ImGui::Text("Tile %d,%d", _tile.x, _tile.y);
		ImGui::SameLine(0.f, ImGui::CalcTextSize(" ").x);   //a space before what follows (the GUI's style has no gap)
		const std::string names = liquidNames(_fluid, n.island);
		if (full)
			ImGui::TextDisabled("liquid network %d: %s, full", n.island + 1, names.c_str());
		else if (state == fluidState::mixed)
			ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f), "liquid network %d: mixed liquids, %s: nothing flows", n.island + 1, names.c_str());
		else
			ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f), "liquid network %d: %s, %s (has %.0f L, needs %.0f L)", n.island + 1, names.c_str(),
				fluidStateName(state), _fluid.islandLiquid(n.island), _fluid.islandNeeds(n.island));
		if (full)
		{
			const double t = _fluid.liquidTemperature(node);
			ImGui::TextColored(toImGuiVec(thermalView::temperatureColour(t)), "liquid %.1f%sC, %.1f kPa, %.0f L here", t, degree, _fluid.pressure(node),
				_fluid.liquidOnNode(node));
			for (int e = 0; e < int(_fluid.network().edges().size()); e++)
			{
				const networkEdge& edge = _fluid.network().edges()[e];
				if (edge.a != node && edge.b != node)
					continue;
				const int other = edge.a == node ? edge.b : edge.a;
				const double outward = edge.a == node ? _fluid.flow(e) : -_fluid.flow(e);
				const networkNode& far = _fluid.network().nodes()[other];
				const char* direction = std::abs(outward) < 1e-4 ? "(still)" : (outward > 0 ? "out" : "in");
				if (edge.vertical)
					ImGui::Text("  %s to level %d: %s %s", far.level > _level ? "up" : "down", far.level, flowText(std::abs(outward)).c_str(), direction);
				else
					ImGui::Text("  to %d,%d: %s %s", far.tile.x, far.tile.y, flowText(std::abs(outward)).c_str(), direction);
			}
		}
		for (const fluidTank& tank : _fluid.tanks())
		{
			if (tank.node == node)
				ImGui::Text("Tank: %s %.0f of %.0f L%s", tank.liquid ? tank.liquid->name.c_str() : "empty,", tank.contents, tank.capacity,
					std::abs(tank.outflow) > 1e-4 ? (tank.outflow > 0 ? ", emptying" : ", filling") : "");
		}
		for (const fluidPump& pump : _fluid.pumps())
		{
			if (pump.node != node)
				continue;
			if (!pump.switchedOn)
				ImGui::TextDisabled("Pump (%s): switched off", tileSides::name(pump.side));
			else if (pump.powerFraction <= 0.0)
				ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f), "Pump (%s): no power", tileSides::name(pump.side));
			else if (pump.edge < 0)
				ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f), "Pump (%s): nothing to push into on that side", tileSides::name(pump.side));
			else
				ImGui::Text("Pump (%s): %.0f kPa, %s", tileSides::name(pump.side), pump.head, flowText(pump.flow).c_str());
		}
		for (const fluidExchanger& exchanger : _fluid.exchangers())
		{
			if (exchanger.node != node)
				continue;
			if (std::abs(exchanger.heatToAir) < 0.5)
				ImGui::TextDisabled("Heat exchanger: idle");
			else if (exchanger.heatToAir > 0)
				ImGui::Text("Heat exchanger: warming the air by %s", powerView::formatPower(exchanger.heatToAir).c_str());
			else
				ImGui::Text("Heat exchanger: cooling the air by %s", powerView::formatPower(-exchanger.heatToAir).c_str());
		}

		ImGui::EndTooltip();
		if (_font)
			ImGui::PopFont();
	}
}
