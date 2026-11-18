#include "logisticsView.h"
#include "logistics.h"
#include "processing.h"
#include "tileObject.h"
#include "machineView.h"
#include "isometricLevel.h"
#include "powerView.h"
#include <imgui.h>
#include "uiTheme.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace
{
	const ImU32 movingColour = IM_COL32(240, 190, 110, 255);
	const ImU32 idleColour = IM_COL32(120, 105, 90, 255);
	const ImVec4 red = uiTheme::danger;
	const ImVec4 amber = uiTheme::warning;

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
	ImU32 scaled(ImU32 _colour, float _brightness)
	{
		const auto channel = [&](int _shift) { return ImU32(std::clamp(float((_colour >> _shift) & 0xFF) * _brightness, 0.f, 255.f)) << _shift; };
		return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT) | (_colour & IM_COL32_A_MASK);
	}
	std::string rateText(double _kgPerSecond)
	{
		char text[32];
		snprintf(text, sizeof(text), "%+.2f kg/s", _kgPerSecond);
		return text;
	}
	void swatch(sf::Color _colour, const char* _text)
	{
		const float size = ImGui::GetTextLineHeight();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + size, p.y + size), IM_COL32(_colour.r, _colour.g, _colour.b, 255));
		ImGui::Dummy(ImVec2(size, size));
		ImGui::SameLine();
		ImGui::TextUnformatted(_text);
	}
}

namespace logisticsView
{
	void drawOverlay(const logisticsNetwork& _items, int _level, isometricLevel* _isoLevel, const sf::RenderWindow& _window,
		double _animationSeconds, ImFont* _font, const processingSystem* _machines)
	{
		const std::vector<networkNode>& nodes = _items.network().nodes();
		const std::vector<networkEdge>& edges = _items.network().edges();
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		const float tilePixels = tilePixelWidth(_window);
		const float width = std::clamp(tilePixels * 0.08f, 2.f, 12.f);
		auto centreOf = [&](int _node) { return toPixel(_window, _isoLevel->tileCentreWorld(sf::Vector2f(nodes[_node].tile))); };
		const float pulse = 0.75f + 0.25f * float(std::sin(_animationSeconds * 5.0));

		drawList->AddRectFilled(ImVec2(0.f, 0.f), display, IM_COL32(0, 0, 10, 90));
		//tubes: lit (pulsing) while their network moves something
		for (const networkEdge& edge : edges)
		{
			if (edge.vertical || nodes[edge.a].level != _level)
				continue;
			const bool moving = _items.island(nodes[edge.a].island).moving > 1e-9;
			const ImVec2 from = centreOf(edge.a), to = centreOf(edge.b);
			drawList->AddLine(from, to, IM_COL32(0, 0, 0, 200), width + 2.f);
			drawList->AddLine(from, to, moving ? scaled(movingColour, pulse) : idleColour, width);
		}
		for (int n = 0; n < int(nodes.size()); n++)
		{
			if (nodes[n].level != _level)
				continue;
			const bool moving = _items.island(nodes[n].island).moving > 1e-9;
			drawList->AddCircleFilled(centreOf(n), width * 0.75f, moving ? scaled(movingColour, pulse) : idleColour);
		}
		machineView::drawLevelPorts(drawList, _window, *_isoLevel, machineView::kindBit(portInfo::kind::items));

		//each container's endpoint: kg/s in or out, or blocked
		ImFont* font = _font ? _font : ImGui::GetFont();
		for (const logisticsEndpoint& endpoint : _items.endpoints())
		{
			if (nodes[endpoint.node].level != _level)
				continue;
			const logisticsIsland& island = _items.island(nodes[endpoint.node].island);
			std::string text;
			ImU32 colour = IM_COL32(220, 220, 220, 255);
			if (std::abs(endpoint.rate) > 1e-6)
			{
				text = rateText(endpoint.rate);
				colour = endpoint.rate > 0.0 ? IM_COL32(120, 230, 140, 255) : IM_COL32(255, 190, 90, 255);
			}
			else if (endpoint.blocked)
			{
				text = "blocked";
				colour = IM_COL32(255, 110, 110, 255);
			}
			if (text.empty())
				continue;
			const ImVec2 at = toPixel(_window, _isoLevel->tileCentreWorld(sf::Vector2f(endpoint.tile)));
			const ImVec2 size = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text.c_str());
			const ImVec2 topLeft(at.x - size.x * 0.5f, at.y - tilePixels * 0.45f - size.y);
			drawList->AddRectFilled(ImVec2(topLeft.x - 3.f, topLeft.y - 2.f), ImVec2(topLeft.x + size.x + 3.f, topLeft.y + size.y + 2.f), IM_COL32(0, 0, 0, 210));
			drawList->AddText(font, font->FontSize, topLeft, colour, text.c_str());
		}

		//each machine that makes things: what it is doing, and how far through its cycle, under its middle
		for (const processorState& state : _machines ? _machines->machines() : std::vector<processorState>())
		{
			if (state.level != _level || state.tiles.empty() || !state.machine)
				continue;
			ImVec2 middle(0.f, 0.f);
			for (const sf::Vector2i& at : state.tiles)
			{
				const ImVec2 p = toPixel(_window, _isoLevel->tileCentreWorld(sf::Vector2f(at)));
				middle.x += p.x / float(state.tiles.size());
				middle.y += p.y / float(state.tiles.size());
			}
			const std::string text = machineStatusName(state.status);
			const sf::Color c = machineStatusColour(state.status);
			const ImVec2 size = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text.c_str());
			const float barWidth = std::max(size.x, tilePixels * 0.5f);
			const ImVec2 topLeft(middle.x - size.x * 0.5f, middle.y + tilePixels * 0.08f);
			const ImVec2 boxMin(middle.x - barWidth * 0.5f - 3.f, topLeft.y - 2.f);
			const ImVec2 boxMax(middle.x + barWidth * 0.5f + 3.f, topLeft.y + size.y + 7.f);
			drawList->AddRectFilled(boxMin, boxMax, IM_COL32(0, 0, 0, 210));
			drawList->AddText(font, font->FontSize, topLeft, IM_COL32(c.r, c.g, c.b, 255), text.c_str());
			const float barTop = topLeft.y + size.y + 2.f;
			const float filled = barWidth * float(std::clamp(state.machine->progress, 0.0, 1.0));
			drawList->AddRectFilled(ImVec2(middle.x - barWidth * 0.5f, barTop), ImVec2(middle.x + barWidth * 0.5f, barTop + 3.f), IM_COL32(60, 60, 60, 255));
			drawList->AddRectFilled(ImVec2(middle.x - barWidth * 0.5f, barTop), ImVec2(middle.x - barWidth * 0.5f + filled, barTop + 3.f), IM_COL32(c.r, c.g, c.b, 255));
		}
	}

	float drawLegend(const logisticsNetwork& _items, float _top, float _scale, ImFont* _font, const processingSystem* _machines)
	{
		const float s = _scale;
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		if (_font)
			ImGui::PushFont(_font);
		ImGui::SetNextWindowPos(ImVec2(display.x - 8.f * s, _top), ImGuiCond_Always, ImVec2(1.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, uiTheme::panel());
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f * s, 8.f * s));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f * s, 4.f * s));
		ImGui::Begin("logisticsLegend", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);

		ImGui::TextUnformatted("LOGISTICS VIEW");
		ImGui::SameLine();
		ImGui::TextDisabled("(I to close)");
		ImGui::TextDisabled("tubes lit while their network moves something; item ports marked");
		ImGui::TextDisabled("labels: kg/s into (+) or out of (-) each container or machine port");
		ImGui::TextDisabled("machines: what each is doing, and a bar for its cycle");
		ImGui::Separator();

		int networks = int(_items.network().islands().size()), moving = 0, blocked = 0;
		std::string blockedWhat;
		std::vector<std::pair<const materialInfo*, double>> materials;
		for (int i = 0; i < networks; i++)
		{
			const logisticsIsland& island = _items.island(i);
			moving += island.moving > 1e-9;
			blocked += island.blocked;
			if (blockedWhat.empty())
				blockedWhat = island.blockedWhat;
			for (const auto& [material, rate] : island.materials)
			{
				auto found = std::find_if(materials.begin(), materials.end(), [&](const auto& _m) { return _m.first == material; });
				if (found == materials.end())
					materials.push_back({ material, rate });
				else
					found->second += rate;
			}
		}
		ImGui::Text("networks: %d, %d moving", networks, moving);
		ImGui::Text("moving %.2f kg/s", _items.totalMoving());
		for (const auto& [material, rate] : materials)
		{
			char text[96];
			snprintf(text, sizeof(text), "%s: %.2f kg/s", material->name.c_str(), rate);
			ImGui::SameLine();
			swatch(material->colour, text);
		}
		if (blocked > 0)
			ImGui::TextColored(red, "%d %s blocked: nothing on the network takes %s (no room, or filters)", blocked, blocked == 1 ? "output" : "outputs",
				blockedWhat.c_str());

		//machines that make things: how many are doing what
		if (_machines && !_machines->machines().empty())
		{
			ImGui::Separator();
			int counts[int(machineStatus::working) + 1] = {};
			for (const processorState& state : _machines->machines())
				counts[int(state.status)]++;
			ImGui::Text("machines: %d", int(_machines->machines().size()));
			for (int st = int(machineStatus::working); st >= 0; st--)
			{
				if (counts[st] == 0)
					continue;
				const sf::Color c = machineStatusColour(machineStatus(st));
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f), "%d %s", counts[st], machineStatusName(machineStatus(st)));
			}
		}

		const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
		if (_font)
			ImGui::PopFont();
		return bottom;
	}

	void drawInspector(const logisticsNetwork& _items, int _level, sf::Vector2i _tile, ImFont* _font)
	{
		const int node = _items.network().nodeAt(_level, _tile);
		if (node < 0)
			return;
		const int islandIndex = _items.network().nodes()[node].island;
		const logisticsIsland& island = _items.island(islandIndex);
		if (_font)
			ImGui::PushFont(_font);
		ImGui::BeginTooltip();
		ImGui::Text("Tile %d,%d", _tile.x, _tile.y);
		ImGui::SameLine(0.f, ImGui::CalcTextSize(" ").x);   //a space before what follows (the GUI's style has no gap)
		ImGui::TextDisabled("items network %d: %.2f of %.2f kg/s", islandIndex + 1, island.moving, island.capacity);
		for (const auto& [material, rate] : island.materials)
		{
			char text[96];
			snprintf(text, sizeof(text), "%s: %.2f kg/s", material->name.c_str(), rate);
			swatch(material->colour, text);
		}
		for (const logisticsEndpoint& endpoint : _items.endpoints())
		{
			if (endpoint.node != node)
				continue;
			ImGui::Text("%s (%s): %s", endpoint.store->type->name.c_str(), portFlowName(endpoint.flow), rateText(endpoint.rate).c_str());
			if (endpoint.blocked)
			{
				ImGui::SameLine();
				ImGui::TextColored(red, "blocked");
			}
		}
		if (island.blocked > 0)
			ImGui::TextColored(red, "%d blocked: nothing takes %s", island.blocked, island.blockedWhat.c_str());
		ImGui::EndTooltip();
		if (_font)
			ImGui::PopFont();
	}
}
