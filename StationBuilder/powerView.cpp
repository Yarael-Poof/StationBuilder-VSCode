#include "powerView.h"
#include "machineView.h"
#include "electricalNetwork.h"
#include "tileCatalogue.h"
#include "station.h"
#include <imgui.h>
#include "uiTheme.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	const char* degree = "\xC2\xB0";   //the degree sign, in UTF-8

	struct colourStop { double at; sf::Color colour; };

	//colour at _x along a list of stops, blending between neighbours and holding the end colours beyond them
	template <size_t N>
	sf::Color blend(const colourStop (&_stops)[N], double _x)
	{
		if (_x <= _stops[0].at)
			return _stops[0].colour;
		for (size_t i = 1; i < N; i++)
		{
			if (_x <= _stops[i].at)
			{
				const double t = (_x - _stops[i - 1].at) / (_stops[i].at - _stops[i - 1].at);
				const sf::Color& a = _stops[i - 1].colour;
				const sf::Color& b = _stops[i].colour;
				return sf::Color(sf::Uint8(a.r + (b.r - a.r) * t), sf::Uint8(a.g + (b.g - a.g) * t), sf::Uint8(a.b + (b.b - a.b) * t));
			}
		}
		return _stops[N - 1].colour;
	}

	const colourStop voltageStops[] = {
		{ 0.70, sf::Color(220, 40, 40) },    //red: badly browned out
		{ 0.85, sf::Color(255, 140, 30) },   //orange
		{ 0.93, sf::Color(245, 215, 50) },   //yellow
		{ 0.97, sf::Color(90, 220, 90) },    //green: healthy
		{ 1.00, sf::Color(60, 220, 110) },
	};
	const colourStop currentStops[] = {
		{ 0.0, sf::Color(40, 90, 200) },     //blue: lightly used
		{ 0.5, sf::Color(40, 200, 240) },    //cyan
		{ 0.8, sf::Color(240, 220, 60) },    //yellow
		{ 1.0, sf::Color(255, 140, 30) },    //orange: at its rating
		{ 1.2, sf::Color(230, 40, 40) },     //red: over it
	};
	const sf::Color noPowerColour(110, 110, 120);
	const sf::Color faultColour(230, 40, 40);
	const double cableRatingFallback = 32.0;   //amps, for colouring cables that have no rating of their own

	ImU32 toImGui(sf::Color _c, int _alpha = 255)
	{
		return IM_COL32(_c.r, _c.g, _c.b, _alpha);
	}
	ImVec4 toImGuiVec(sf::Color _c)
	{
		return ImVec4(_c.r / 255.f, _c.g / 255.f, _c.b / 255.f, 1.f);
	}

	std::string powerText(double _value, const char* _unit = "W")
	{
		return powerView::formatPower(_value, _unit);
	}

	//how many pixels wide one tile is at the current zoom
	float tilePixelWidth(const sf::RenderWindow& _window)
	{
		const sf::Vector2i left = _window.mapCoordsToPixel(sf::Vector2f(0.f, 0.f));
		const sf::Vector2i right = _window.mapCoordsToPixel(sf::Vector2f(64.f, 0.f));
		return std::hypot(float(right.x - left.x), float(right.y - left.y));
	}
	ImVec2 toPixel(const sf::RenderWindow& _window, sf::Vector2f _world)
	{
		const sf::Vector2i p = _window.mapCoordsToPixel(_world);
		return ImVec2(float(p.x), float(p.y));
	}

	//total rated current of the conductors on a node (parallel cables add up); just its risers for a link between levels
	double nodeRating(const networkNode& _node, bool _risersOnly = false)
	{
		double amps = 0.0;
		for (const tileTypeInfo* type : _node.objects)
		{
			if (type->electrical.conductor && (!_risersOnly || type->electrical.vertical))
				amps += type->electrical.ratedCurrent;
		}
		return amps;
	}

	//a coloured square followed by its label; the next item goes on the same line unless _endLine
	void swatch(sf::Color _colour, const char* _label, bool _endLine = false)
	{
		const float size = ImGui::GetTextLineHeight();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + size, p.y + size), toImGui(_colour));
		ImGui::Dummy(ImVec2(size, size));
		ImGui::SameLine();
		ImGui::TextUnformatted(_label);
		if (!_endLine)
			ImGui::SameLine();
	}
}

namespace powerView
{
	void levelLinkMarker(ImDrawList* _drawList, ImVec2 _centre, float _size, bool _up, unsigned int _colour, bool _linked)
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

	std::string formatPower(double _value, const char* _unit)
	{
		char text[48];
		const double size = std::abs(_value);
		if (size >= 1e6)
			snprintf(text, sizeof(text), "%.2f M%s", _value / 1e6, _unit);
		else if (size >= 1e3)
			snprintf(text, sizeof(text), "%.2f k%s", _value / 1e3, _unit);
		else
			snprintf(text, sizeof(text), "%.2f %s", _value, _unit);
		return text;
	}

	double loadBrightness(double _watts, double _ratedWatts)
	{
		if (_ratedWatts <= 0.0)
			return _watts > 0.0 ? 1.0 : 0.0;
		return std::clamp(_watts / _ratedWatts, 0.0, 1.0);
	}

	sf::Color loadColour(double _brightness)
	{
		if (_brightness < 0.02)
			return sf::Color(65, 65, 75);   //off: dark
		//lamps look dimmer than their power suggests (light output falls faster than power), so the sprite
		//darkens with the square of the fraction: 84% power (a sagging generator) is clearly dimmer, half is half-dark
		const double b = std::clamp(_brightness, 0.0, 1.0);
		const sf::Uint8 level = sf::Uint8(255.0 * (0.3 + 0.7 * b * b));
		return sf::Color(level, level, level);
	}

	sf::Color voltageColour(double _fractionOfNominal)
	{
		return blend(voltageStops, _fractionOfNominal);
	}

	sf::Color currentColour(double _fractionOfRating)
	{
		return blend(currentStops, _fractionOfRating);
	}

	double linkRating(const electricalNetwork& _power, int _edge)
	{
		const networkEdge& edge = _power.network().edges()[_edge];
		const double a = nodeRating(_power.network().nodes()[edge.a], edge.vertical);
		const double b = nodeRating(_power.network().nodes()[edge.b], edge.vertical);
		if (a <= 0.0) return b;
		if (b <= 0.0) return a;
		return std::min(a, b);
	}

	int countLiveNetworks(const electricalNetwork& _power)
	{
		int live = 0;
		for (int i = 0; i < int(_power.network().islands().size()); i++)
		{
			if (_power.islandSolution(i).state == islandState::live)
				live++;
		}
		return live;
	}

	void applyDeviceShading(const electricalNetwork& _power, station& _station, int _level)
	{
		if (_level < 0 || _level >= _station.getLayerListSize())
			return;
		isometricLevel* level = _station.getIsoLevel(float(_level));
		for (int d = 0; d < int(_power.devices().size()); d++)
		{
			const electricalDevice& device = _power.devices()[d];
			const generatorState* state = device.isSource ? _power.generatorStatus(d) : nullptr;
			if (device.isSource && device.switchedOn && !(state && state->tripped))
				continue;   //a running generator keeps its normal look
			const networkNode& node = _power.network().nodes()[device.node];
			if (node.level != _level || node.tileIndex >= int(level->levelTiles.size()))
				continue;
			//the device is the objectOnNode-th electrical object on its tile
			tileObject* sprite = nullptr;
			int electricalIndex = 0;
			for (tileObject* object : level->levelTiles[node.tileIndex].allObjects())
			{
				if (!object->type || !electricalMedium.hasNode(*object))
					continue;
				if (electricalIndex++ == device.objectOnNode)
				{
					sprite = object;
					break;
				}
			}
			if (!sprite || sprite->type != device.type)
				continue;   //the station changed since the network was built: next frame will catch up
			sprite = &sprite->machine();   //a big machine's picture is on the machine, not the part with its power port
			if (device.isSource)
			{
				sprite->setColor(generatorOffColour);
				continue;
			}
			const double brightness = loadBrightness(device.power.real(), electrical::ratedLoadWatts(device.type->electrical));
			sprite->setColor(loadColour(brightness));
		}
	}

	void drawOverlay(const electricalNetwork& _power, int _level, isometricLevel* _isoLevel, const sf::RenderWindow& _window, double _animationSeconds)
	{
		const std::vector<networkNode>& nodes = _power.network().nodes();
		const std::vector<networkEdge>& edges = _power.network().edges();
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		const float tilePixels = tilePixelWidth(_window);
		const float lineWidth = std::clamp(tilePixels * 0.07f, 2.f, 12.f);
		const float pulse = 0.5f + 0.5f * float(std::sin(ImGui::GetTime() * 6.0));   //faults throb, even while paused
		auto onScreen = [&](ImVec2 _p) {
			return _p.x > -tilePixels && _p.y > -tilePixels && _p.x < display.x + tilePixels && _p.y < display.y + tilePixels;
		};

		//dim everything a little so the power view stands out
		drawList->AddRectFilled(ImVec2(0.f, 0.f), display, IM_COL32(0, 0, 10, 90));

		//wired tiles, tinted by their voltage
		for (int n = 0; n < int(nodes.size()); n++)
		{
			const networkNode& node = nodes[n];
			if (node.level != _level)
				continue;
			const sf::Vector2f c = _isoLevel->tileCentreWorld(sf::Vector2f(node.tile));
			const ImVec2 centre = toPixel(_window, c);
			if (!onScreen(centre))
				continue;
			const islandState state = _power.islandSolution(node.island).state;
			ImU32 colour;
			if (state == islandState::live)
				colour = toImGui(voltageColour(std::abs(_power.nodeVoltage(n)) / electrical::nominalVolts), 150);
			else if (state == islandState::dead)
				colour = toImGui(noPowerColour, 120);
			else
				colour = toImGui(faultColour, int(110 + 80 * pulse));
			const float k = 0.92f;   //a little smaller than the tile, so neighbouring tiles stay distinct
			drawList->AddQuadFilled(toPixel(_window, c + sf::Vector2f(0.f, -16.f * k)), toPixel(_window, c + sf::Vector2f(32.f * k, 0.f)),
				toPixel(_window, c + sf::Vector2f(0.f, 16.f * k)), toPixel(_window, c + sf::Vector2f(-32.f * k, 0.f)), colour);
		}

		//cable links, coloured by current, with dots moving the way the power flows
		for (int e = 0; e < int(edges.size()); e++)
		{
			const networkNode& a = nodes[edges[e].a];
			const networkNode& b = nodes[edges[e].b];
			if (a.level != _level || edges[e].vertical)
				continue;   //links between levels are drawn as markers on the riser tiles, below
			const ImVec2 from = toPixel(_window, _isoLevel->tileCentreWorld(sf::Vector2f(a.tile)));
			const ImVec2 to = toPixel(_window, _isoLevel->tileCentreWorld(sf::Vector2f(b.tile)));
			if (!onScreen(from) && !onScreen(to))
				continue;
			const bool live = _power.islandSolution(a.island).state == islandState::live;
			const complexd current = _power.edgeCurrent(e);
			const double rating = linkRating(_power, e);
			const double loading = std::abs(current) / (rating > 0.0 ? rating : cableRatingFallback);
			const float width = lineWidth * float(0.7 + 0.6 * std::min(loading, 1.5));
			drawList->AddLine(from, to, IM_COL32(0, 0, 0, 200), width + 2.f);
			drawList->AddLine(from, to, live ? toImGui(currentColour(loading)) : toImGui(noPowerColour), width);

			if (!live || std::abs(current) < 0.005)
				continue;
			//real power flows from a to b when Re(Va conj(Iab)) > 0; faster dots for busier cables
			const bool forward = (_power.nodeVoltage(edges[e].a) * std::conj(current)).real() >= 0.0;
			const ImVec2 start = forward ? from : to;
			const ImVec2 end = forward ? to : from;
			const double speed = 0.6 + 1.8 * std::min(loading, 1.5);   //tile lengths per second
			for (double offset : { 0.0, 0.5 })
			{
				const float t = float(std::fmod(_animationSeconds * speed + offset, 1.0));
				const ImVec2 dot(start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t);
				drawList->AddCircleFilled(dot, width * 0.55f, IM_COL32(255, 255, 230, 230));
			}
		}

		//risers: an arrow up and/or down, coloured by the current through that link (an outline where the riser has
		//no partner on a level that exists)
		const float markerSize = std::clamp(tilePixels * 0.11f, 4.f, 16.f);
		for (const networkNode& node : nodes)
		{
			if (node.level != _level || node.verticalConductors == 0)
				continue;
			const ImVec2 centre = toPixel(_window, _isoLevel->tileCentreWorld(sf::Vector2f(node.tile)));
			if (!onScreen(centre))
				continue;
			const bool live = _power.islandSolution(node.island).state == islandState::live;
			for (const bool up : { true, false })
			{
				const int edge = up ? node.edgeUp : node.edgeDown;
				const bool levelThere = up ? node.level + 1 < _power.network().levelCount() : node.level > 0;
				if (edge < 0 && !levelThere)
					continue;
				if (edge < 0)
				{
					levelLinkMarker(drawList, centre, markerSize, up, toImGui(noPowerColour), false);
					continue;
				}
				const double rating = linkRating(_power, edge);
				const double loading = std::abs(_power.edgeCurrent(edge)) / (rating > 0.0 ? rating : cableRatingFallback);
				levelLinkMarker(drawList, centre, markerSize, up, live ? toImGui(currentColour(loading)) : toImGui(noPowerColour), true);
			}
		}

		//machines' power ports, where they join the cables
		machineView::drawLevelPorts(drawList, _window, *_isoLevel, machineView::kindBit(portInfo::kind::power));

		//a load bar under each generator
		for (int d = 0; d < int(_power.devices().size()); d++)
		{
			const electricalDevice& device = _power.devices()[d];
			if (!device.isSource)
				continue;
			const networkNode& node = nodes[device.node];
			if (node.level != _level)
				continue;
			const ImVec2 centre = toPixel(_window, _isoLevel->tileCentreWorld(sf::Vector2f(node.tile)));
			if (!onScreen(centre))
				continue;
			const generatorState* state = _power.generatorStatus(d);
			const double rated = device.type->electrical.ratedPower;
			const double loading = rated > 0.0 ? std::abs(device.power) / rated : 0.0;
			const float barWidth = tilePixels * 0.5f;
			const float barHeight = std::max(3.f, tilePixels * 0.06f);
			const ImVec2 topLeft(centre.x - barWidth * 0.5f, centre.y + tilePixels * 0.2f);
			const ImVec2 bottomRight(topLeft.x + barWidth, topLeft.y + barHeight);
			drawList->AddRectFilled(ImVec2(topLeft.x - 1.f, topLeft.y - 1.f), ImVec2(bottomRight.x + 1.f, bottomRight.y + 1.f), IM_COL32(0, 0, 0, 230));
			if (state && state->tripped)
			{
				drawList->AddRect(topLeft, bottomRight, toImGui(faultColour), 0.f, 0, 2.f);
				continue;
			}
			if (!device.switchedOn)
			{
				drawList->AddRect(topLeft, bottomRight, toImGui(noPowerColour), 0.f, 0, 2.f);
				continue;
			}
			drawList->AddRectFilled(topLeft, ImVec2(topLeft.x + barWidth * float(std::min(loading, 1.0)), bottomRight.y), toImGui(currentColour(loading)));
		}
	}

	void drawAlerts(const electricalNetwork& _power, int _level, isometricLevel* _isoLevel, const sf::RenderWindow& _window, ImFont* _font)
	{
		ImFont* font = _font ? _font : ImGui::GetFont();
		const float fontSize = font->FontSize;
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		const float tilePixels = tilePixelWidth(_window);
		const float pad = fontSize * 0.25f;

		std::vector<int> labelsOnNode(_power.network().nodes().size(), 0);   //stack labels when a tile has several
		//a label centred over a tile, above the generator's box, stacked upwards when the tile already has one
		auto label = [&](int _node, const char* _text, ImU32 _colour) {
			const networkNode& node = _power.network().nodes()[_node];
			const ImVec2 centre = toPixel(_window, _isoLevel->tileCentreWorld(sf::Vector2f(node.tile)));
			const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, _text);
			const float stack = labelsOnNode[_node]++ * (size.y + pad * 3.f);
			const ImVec2 topLeft(centre.x - size.x * 0.5f, centre.y - tilePixels * 0.55f - size.y - stack);
			drawList->AddRectFilled(ImVec2(topLeft.x - pad, topLeft.y - pad), ImVec2(topLeft.x + size.x + pad, topLeft.y + size.y + pad),
				IM_COL32(0, 0, 0, 210));
			drawList->AddText(font, fontSize, topLeft, _colour, _text);
		};

		//cables carrying more than they are rated for get hot
		for (int n = 0; n < int(_power.network().nodes().size()); n++)
		{
			if (_power.network().nodes()[n].level != _level)
				continue;
			const double t = _power.cableTemperature(n);
			if (std::isnan(t) || t <= thermalEffects::cableOverheating)
				continue;
			char text[64];
			snprintf(text, sizeof(text), "CABLE %.0f%sC - overloaded", t, degree);
			label(n, text, IM_COL32(255, 70, 70, 255));
		}

		for (int d = 0; d < int(_power.devices().size()); d++)
		{
			const electricalDevice& device = _power.devices()[d];
			if (!device.isSource)
				continue;
			const networkNode& node = _power.network().nodes()[device.node];
			if (node.level != _level)
				continue;
			const generatorState* state = _power.generatorStatus(d);
			const bool mismatch = _power.islandSolution(device.island).state == islandState::frequencyMismatch;

			char text[96];
			ImU32 colour;
			if (state && state->tripped)
			{
				snprintf(text, sizeof(text), "TRIPPED - select it to reset");
				colour = IM_COL32(255, 70, 70, 255);
			}
			else if (!device.switchedOn)
				continue;   //off on purpose: nothing to warn about
			else if (mismatch)
			{
				snprintf(text, sizeof(text), "frequency mismatch");
				colour = IM_COL32(255, 70, 70, 255);
			}
			else if (state && state->demand > generatorProtection::overloadMargin)
			{
				snprintf(text, sizeof(text), "OVERLOAD %.0f%% - trips in %.1f s", 100.0 * state->demand,
					std::max(0.0, generatorProtection::tripSeconds - state->overloadSeconds));
				colour = IM_COL32(255, 190, 50, 255);
			}
			else if (state && state->effectiveRating > 0.0 && state->effectiveRating < device.type->electrical.ratedPower * 0.98)
			{
				snprintf(text, sizeof(text), "HOT %.0f%sC - %.0f%% capacity", state->tileTemperature, degree,
					100.0 * state->effectiveRating / device.type->electrical.ratedPower);
				colour = IM_COL32(255, 190, 50, 255);
			}
			else
				continue;
			label(device.node, text, colour);
		}
	}

	float drawLegend(const electricalNetwork& _power, float _top, float _scale, ImFont* _font)
	{
		const float s = _scale;
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		if (_font)
			ImGui::PushFont(_font);
		ImGui::SetNextWindowPos(ImVec2(display.x - 8.f * s, _top), ImGuiCond_Always, ImVec2(1.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, uiTheme::panel());
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f * s, 8.f * s));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f * s, 4.f * s));
		ImGui::Begin("powerLegend", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);

		ImGui::TextUnformatted("POWER VIEW");
		ImGui::SameLine();
		ImGui::TextDisabled("(P to close)");
		ImGui::TextDisabled("tiles: voltage");
		swatch(voltageColour(1.0), "100%");
		swatch(voltageColour(0.93), "93%");
		swatch(voltageColour(0.85), "85%");
		swatch(voltageColour(0.70), "70%");
		swatch(noPowerColour, "off");
		swatch(faultColour, "fault");
		ImGui::NewLine();
		ImGui::TextDisabled("cables: current vs rating");
		swatch(currentColour(0.0), "0%");
		swatch(currentColour(0.5), "50%");
		swatch(currentColour(0.8), "80%");
		swatch(currentColour(1.0), "100%");
		swatch(currentColour(1.2), "over");
		ImGui::NewLine();
		ImGui::Separator();

		//station totals, all levels
		int live = 0, dead = 0, faulted = 0;
		double generated = 0.0, consumed = 0.0, lost = 0.0;
		for (int i = 0; i < int(_power.network().islands().size()); i++)
		{
			const acSolution& sol = _power.islandSolution(i);
			if (sol.state == islandState::live)
			{
				live++;
				generated += sol.generatedWatts;
				consumed += sol.consumedWatts;
				lost += sol.lossWatts;
			}
			else if (sol.state == islandState::dead)
				dead++;
			else
				faulted++;
		}
		int running = 0, off = 0, tripped = 0;
		double capacity = 0.0;
		for (int d = 0; d < int(_power.devices().size()); d++)
		{
			const generatorState* state = _power.generatorStatus(d);
			if (!state)
				continue;
			if (state->tripped)
				tripped++;
			else if (!_power.devices()[d].switchedOn)
				off++;
			else
			{
				running++;
				//what it can give right now: less than its rating when it is hot
				capacity += state->effectiveRating > 0.0 ? state->effectiveRating : _power.devices()[d].type->electrical.ratedPower;
			}
		}
		ImGui::Text("networks: %d live, %d without power, %d faulted", live, dead, faulted);
		ImGui::Text("generators: %d running (%s), %d off, %d tripped", running, powerText(capacity, "VA").c_str(), off, tripped);
		ImGui::Text("generating %s: loads %s, cables %s", powerText(generated).c_str(), powerText(consumed).c_str(), powerText(lost).c_str());
		const double kWh = 3.6e6;
		ImGui::TextDisabled("so far: %.3f kWh generated, %.3f used, %.3f lost", _power.energy().generatedJoules / kWh,
			_power.energy().consumedJoules / kWh, _power.energy().lostJoules / kWh);

		const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
		if (_font)
			ImGui::PopFont();
		return bottom;
	}

	void drawInspector(const electricalNetwork& _power, int _level, sf::Vector2i _tile, ImFont* _font)
	{
		const int node = _power.network().nodeAt(_level, _tile);
		if (node < 0)
			return;
		const networkNode& n = _power.network().nodes()[node];
		const acSolution& sol = _power.islandSolution(n.island);
		const bool live = sol.state == islandState::live;
		const ImVec4 red = uiTheme::danger, amber = uiTheme::warning;

		if (_font)
			ImGui::PushFont(_font);
		ImGui::BeginTooltip();

		ImGui::Text("Tile %d,%d", _tile.x, _tile.y);
		ImGui::SameLine(0.f, ImGui::CalcTextSize(" ").x);   //a space before what follows (the GUI's style has no gap)
		const int networkNumber = n.island + 1;   //players count from 1
		if (live)
			ImGui::TextDisabled("network %d, live at %.0f Hz", networkNumber, sol.frequency);
		else if (sol.state == islandState::dead)
			ImGui::TextDisabled("network %d, no power", networkNumber);
		else
			ImGui::TextColored(red, "network %d: %s", networkNumber, islandStateName(sol.state));

		if (live)
		{
			const complexd v = _power.nodeVoltage(node);
			const double fraction = std::abs(v) / electrical::nominalVolts;
			ImGui::TextColored(toImGuiVec(voltageColour(fraction)), "%.1f V at %.2f%s  (%.1f%% of %.0f V)", std::abs(v),
				std::arg(v) * 57.29577951308232, degree, 100.0 * fraction, electrical::nominalVolts);
		}

		//cable links from this tile: current, how loaded, which way the power goes
		if (n.conductors > 0)
		{
			ImGui::TextDisabled("%d cable%s here, rated %.0f A", n.conductors, n.conductors > 1 ? "s" : "", nodeRating(n));
			const double cableC = _power.cableTemperature(node);
			if (!std::isnan(cableC))
			{
				if (cableC > thermalEffects::cableOverheating)
					ImGui::TextColored(red, "  cables at %.0f%sC: OVERHEATING", cableC, degree);
				else if (cableC > thermalEffects::cableHot)
					ImGui::TextColored(amber, "  cables at %.0f%sC: running hot", cableC, degree);
				else
					ImGui::TextDisabled("  cables at %.0f%sC", cableC, degree);
			}
			const std::vector<networkEdge>& edges = _power.network().edges();
			for (int e = 0; e < int(edges.size()); e++)
			{
				if (edges[e].a != node && edges[e].b != node)
					continue;
				const int other = edges[e].a == node ? edges[e].b : edges[e].a;
				const networkNode& otherNode = _power.network().nodes()[other];
				char where[48];
				if (edges[e].vertical)
					snprintf(where, sizeof(where), "%s to level %d", otherNode.level > n.level ? "up" : "down", otherNode.level);
				else
					snprintf(where, sizeof(where), "to %d,%d", otherNode.tile.x, otherNode.tile.y);
				if (!live)
				{
					ImGui::Text("  %s: no current", where);
					continue;
				}
				const complexd outward = edges[e].a == node ? _power.edgeCurrent(e) : -_power.edgeCurrent(e);
				const double rating = linkRating(_power, e);
				const double loading = rating > 0.0 ? std::abs(outward) / rating : 0.0;
				const bool powerOut = (_power.nodeVoltage(node) * std::conj(outward)).real() >= 0.0;
				char text[128];
				snprintf(text, sizeof(text), "%s: %.2f A (%.0f%% of %.0f A), power %s", where,
					std::abs(outward), 100.0 * loading, rating, std::abs(outward) < 0.005 ? "idle" : (powerOut ? "going out" : "coming in"));
				swatch(currentColour(loading), text, true);   //the cable colour as a square: dark blue text wouldn't be readable
			}
		}

		//devices on the tile
		for (int d : _power.devicesOnNode(node))
		{
			const electricalDevice& device = _power.devices()[d];
			const char* name = device.type->name.c_str();
			if (device.isSource)
			{
				const generatorState* state = _power.generatorStatus(d);
				const double rated = device.type->electrical.ratedPower;
				if (state && state->tripped)
				{
					ImGui::TextColored(red, "%s: TRIPPED - select the tile to reset it", name);
					continue;
				}
				if (!device.switchedOn)
				{
					ImGui::TextDisabled("%s: switched off", name);
					continue;
				}
				if (!live)
				{
					ImGui::Text("%s: not supplying (%s)", name, islandStateName(sol.state));
					continue;
				}
				ImGui::Text("%s: %s, %s, %s = %.0f%% of %s, %.2f A", name, powerText(device.power.real()).c_str(),
					powerText(device.power.imag(), "var").c_str(), powerText(std::abs(device.power), "VA").c_str(),
					rated > 0.0 ? 100.0 * std::abs(device.power) / rated : 0.0, powerText(rated, "VA").c_str(), std::abs(device.current));
				if (state && state->emfScale < 0.999)
					ImGui::TextColored(amber, "  sagging: EMF at %.0f%%, asked for %.0f%% of its rating", 100.0 * state->emfScale, 100.0 * state->demand);
				if (state && state->overloadSeconds > 0.0)
					ImGui::TextColored(amber, "  overload %.1f s of %.1f s before it trips", state->overloadSeconds, generatorProtection::tripSeconds);
				if (state && state->effectiveRating > 0.0 && state->effectiveRating < rated * 0.995)
					ImGui::TextColored(amber, "  hot (%.0f%sC): can only give %s", state->tileTemperature, degree,
						powerText(state->effectiveRating, "VA").c_str());
			}
			else
			{
				const double rated = electrical::ratedLoadWatts(device.type->electrical);
				if (!device.switchedOn)
				{
					ImGui::TextDisabled("%s: switched off", name);
					continue;
				}
				if (!std::isnan(device.thermostat) && !_power.thermostatHeating(d))
				{
					ImGui::TextDisabled("%s: idle, its tile has reached %.0f%sC", name, device.thermostat, degree);
					continue;
				}
				if (!live)
				{
					ImGui::Text("%s: off, no power", name);
					continue;
				}
				const double va = std::abs(device.power);
				ImGui::Text("%s: %s of %s (%.0f%%), %.2f A, pf %.2f", name, powerText(device.power.real()).c_str(), powerText(rated).c_str(),
					100.0 * loadBrightness(device.power.real(), rated), std::abs(device.current), va > 0.0 ? device.power.real() / va : 1.0);
			}
		}

		if (live)
			ImGui::TextDisabled("network: generating %s, loads %s, cables %s", powerText(sol.generatedWatts).c_str(),
				powerText(sol.consumedWatts).c_str(), powerText(sol.lossWatts).c_str());
		if (node < int(_power.nodeHeatWatts().size()) && _power.nodeHeatWatts()[node] > 0.0)
			ImGui::TextDisabled("heat made on this tile: %s", powerText(_power.nodeHeatWatts()[node]).c_str());

		ImGui::EndTooltip();
		if (_font)
			ImGui::PopFont();
	}
}
