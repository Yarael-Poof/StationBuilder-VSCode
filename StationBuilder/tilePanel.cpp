#include "tilePanel.h"
#include "station.h"
#include "tileCatalogue.h"
#include "electricalNetwork.h"
#include "powerView.h"
#include "thermalNetwork.h"
#include "thermalView.h"
#include "fluidNetwork.h"
#include "machineView.h"
#include "logistics.h"
#include "processing.h"
#include <imgui.h>
#include "uiTheme.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace
{
	const ImVec4 red = uiTheme::danger;
	const ImVec4 amber = uiTheme::warning;
	const ImVec4 green = uiTheme::good;

	ImVec4 toImGuiVec(sf::Color _c) { return ImVec4(_c.r / 255.f, _c.g / 255.f, _c.b / 255.f, 1.f); }

	//a small square of a material's colour, then its text
	void swatchText(sf::Color _colour, const std::string& _text)
	{
		const float size = ImGui::GetTextLineHeight();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + size, p.y + size), IM_COL32(_colour.r, _colour.g, _colour.b, 255));
		ImGui::Dummy(ImVec2(size, size));
		ImGui::SameLine();
		ImGui::TextUnformatted(_text.c_str());
	}

	//how many decimals a number setting shows, from its step: 1 -> "230", 0.5 -> "230.5"
	const char* numberFormat(const itemSetting& _setting)
	{
		if (_setting.step >= 1.0) return "%.0f";
		if (_setting.step >= 0.1) return "%.1f";
		return "%.2f";
	}

	//live readings for a generator: what it is doing, and its output
	void generatorReadings(const electricalNetwork& _power, int _device)
	{
		const electricalDevice& device = _power.devices()[_device];
		const generatorState* state = _power.generatorStatus(_device);
		const acSolution& sol = _power.islandSolution(device.island);
		const double rated = device.type->electrical.ratedPower;
		if (state && state->tripped)
		{
			ImGui::TextColored(red, "TRIPPED: overloaded for %.0f s", generatorProtection::tripSeconds);
			return;
		}
		if (!device.switchedOn)
		{
			ImGui::TextDisabled("switched off");
			return;
		}
		if (sol.state == islandState::frequencyMismatch)
		{
			ImGui::TextColored(red, "no output: frequency mismatch on its network");
			return;
		}
		if (state && state->demand > generatorProtection::overloadMargin)
			ImGui::TextColored(amber, "OVERLOADED (%.0f%% asked) - trips in %.1f s", 100.0 * state->demand,
				std::max(0.0, generatorProtection::tripSeconds - state->overloadSeconds));
		else if (state && state->emfScale < 0.999)
			ImGui::TextColored(amber, "at its limit: EMF held at %.0f%%", 100.0 * state->emfScale);
		else
			ImGui::TextColored(green, "running");
		ImGui::Text("%s, %s  (%.0f%% of %s)", powerView::formatPower(device.power.real()).c_str(),
			powerView::formatPower(device.power.imag(), "var").c_str(), rated > 0.0 ? 100.0 * std::abs(device.power) / rated : 0.0,
			powerView::formatPower(rated, "VA").c_str());
		ImGui::TextDisabled("%.2f A at %.1f V terminal", std::abs(device.current), std::abs(_power.nodeVoltage(device.node)));
		if (state && state->effectiveRating > 0.0 && state->effectiveRating < rated * 0.995)
			ImGui::TextColored(amber, "hot (%.0f\xC2\xB0" "C): can only give %s, %.0f%% of its rating", state->tileTemperature,
				powerView::formatPower(state->effectiveRating, "VA").c_str(), 100.0 * state->effectiveRating / rated);
		const double efficiency = device.type->electrical.efficiency;
		if (efficiency > 0.0 && device.power.real() > 0.0)
			ImGui::TextDisabled("waste heat %s (%.0f%% efficient)", powerView::formatPower(device.power.real() * (1.0 / efficiency - 1.0)).c_str(),
				100.0 * efficiency);
	}

	//a tile's cable temperature, coloured by how hot
	void cableTemperatureLine(const electricalNetwork& _power, int _level, sf::Vector2i _tile)
	{
		const double t = _power.cableTemperature(_power.network().nodeAt(_level, _tile));
		if (std::isnan(t))
			return;
		if (t > thermalEffects::cableOverheating)
			ImGui::TextColored(red, "cables at %.0f\xC2\xB0" "C: OVERHEATING, more current than they are rated for", t);
		else if (t > thermalEffects::cableHot)
			ImGui::TextColored(amber, "cables at %.0f\xC2\xB0" "C: running hot", t);
		else
			ImGui::TextDisabled("cables at %.0f\xC2\xB0" "C", t);
	}

	//a riser's links through the floor and ceiling, one line each: _linked(edge, "up"/"down", other level) writes
	//the line for a link there is; otherwise why there isn't one ("no pipe riser on level 2 at 3,4").
	//_isLink(edge) says whether a network edge between the levels counts (the heat network links every pair of floors)
	template <typename L, typename F>
	void levelLinks(const tileNetwork& _net, int _node, int _level, sf::Vector2i _tile, const char* _partner, L&& _isLink, F&& _linked)
	{
		const networkNode& n = _net.nodes()[_node];
		for (const bool up : { true, false })
		{
			const int edge = up ? n.edgeUp : n.edgeDown;
			const int otherLevel = _level + (up ? 1 : -1);
			const char* way = up ? "up" : "down";
			if (edge >= 0 && _isLink(edge))
				_linked(edge, way, otherLevel);
			else if (otherLevel < 0 || otherLevel >= _net.levelCount())
				ImGui::TextDisabled("%s: no level %s", way, up ? "above" : "below");
			else
				ImGui::TextColored(amber, "%s: no %s on level %d at %d,%d", way, _partner, otherLevel, _tile.x, _tile.y);
		}
	}

	//a cable riser's links: the current through each, or why there isn't one
	void riserLinks(const electricalNetwork& _power, int _level, sf::Vector2i _tile)
	{
		const int node = _power.network().nodeAt(_level, _tile);
		if (node < 0)
			return;
		const bool live = _power.islandSolution(_power.network().nodes()[node].island).state == islandState::live;
		levelLinks(_power.network(), node, _level, _tile, "cable riser", [](int) { return true; }, [&](int _edge, const char* _way, int _other) {
			if (live)
				ImGui::Text("%s: linked to level %d, %.2f A", _way, _other, std::abs(_power.edgeCurrent(_edge)));
			else
				ImGui::Text("%s: linked to level %d, no power", _way, _other);
		});
	}

	//a pipe riser's links: the liquid going up or down each one (edges run from the lower level to the upper)
	void pipeRiserLinks(const fluidNetwork& _fluid, int _level, sf::Vector2i _tile)
	{
		const int node = _fluid.network().nodeAt(_level, _tile);
		if (node < 0)
			return;
		const bool full = _fluid.islandState(_fluid.network().nodes()[node].island) == fluidState::filled;
		levelLinks(_fluid.network(), node, _level, _tile, "pipe riser", [](int) { return true; }, [&](int _edge, const char* _way, int _other) {
			const double rising = _fluid.flow(_edge);
			if (!full)
				ImGui::Text("%s: linked to level %d, not full", _way, _other);
			else if (std::abs(rising) < 1e-4)
				ImGui::Text("%s: linked to level %d, still", _way, _other);
			else
				ImGui::Text("%s: linked to level %d, %.2f L/s %s", _way, _other, std::abs(rising), rising > 0 ? "rising" : "falling");
		});
	}

	//an air duct riser's links: the heat the air carries up or down through each one
	void ductLinks(const thermalNetwork& _heat, int _level, sf::Vector2i _tile)
	{
		const int node = _heat.network().nodeAt(_level, _tile);
		if (node < 0)
		{
			ImGui::TextColored(amber, "no floor: a duct needs floors at both ends to move air");
			return;
		}
		levelLinks(_heat.network(), node, _level, _tile, "floored air duct riser", [&](int _edge) { return _heat.ductLink(_edge); },
			[&](int _edge, const char* _way, int _other) {
				const double rising = _heat.heatFlow(_edge);
				if (std::abs(rising) < 0.5)
					ImGui::Text("%s: linked to level %d, even", _way, _other);
				else
					ImGui::Text("%s: linked to level %d, %s %s", _way, _other, powerView::formatPower(std::abs(rising)).c_str(), rising > 0 ? "rising" : "sinking");
			});
	}

	//a fluid item's liquid: its network, temperature and flow; a tank's contents; a pump's push; an exchanger's heat.
	//_fluidIndex: which of the tile's fluid objects it is; _tankIndex: which of its tanks
	void fluidReadings(const fluidNetwork& _fluid, int _level, sf::Vector2i _tile, const tileTypeInfo& _type, int _fluidIndex, int _tankIndex)
	{
		const int node = _fluid.network().nodeAt(_level, _tile);
		if (node < 0)
			return;
		const int island = _fluid.network().nodes()[node].island;
		const fluidState state = _fluid.islandState(island);
		if (state == fluidState::mixed)
		{
			std::string names;
			for (const materialInfo* liquid : _fluid.islandLiquidTypes(island))
				names += (names.empty() ? "" : " and ") + liquid->name;
			ImGui::TextColored(red, "mixed liquids: %s", names.c_str());
			ImGui::TextColored(red, "nothing flows until they are separated, or a tank is drained");
		}
		else if (state != fluidState::filled)
			ImGui::TextColored(amber, "%s: has %.0f L, needs %.0f L", fluidStateName(state), _fluid.islandLiquid(island), _fluid.islandNeeds(island));
		if (_type.fluid.tank)
		{
			int seen = 0;
			for (const fluidTank& tank : _fluid.tanks())
			{
				if (tank.node != node || seen++ != _tankIndex)
					continue;
				if (tank.liquid)
					swatchText(tank.liquid->colour, tank.liquid->name + ": " + std::to_string(int(std::round(tank.contents))) + " of " +
						std::to_string(int(std::round(tank.capacity))) + " L" +
						(std::abs(tank.outflow) > 1e-4 ? (tank.outflow > 0 ? ", emptying" : ", filling") : ""));
				else
					ImGui::TextDisabled("empty (holds %.0f L)", tank.capacity);
			}
		}
		else if (const materialInfo* liquid = _fluid.islandLiquidType(island))
			swatchText(liquid->colour, liquid->name);
		if (_type.fluid.radiatingArea > 0.0)
		{
			for (const fluidRadiator& radiator : _fluid.radiators())
			{
				if (radiator.node != node)
					continue;
				if (state != fluidState::filled)
					ImGui::TextDisabled("not radiating: its pipes aren't full");
				else
					ImGui::Text("radiating %.2f kW to space, liquid at %.1f C", radiator.heatToSpace / 1000.0, _fluid.liquidTemperature(node));
			}
		}
		if (_type.fluid.pump)
		{
			for (const fluidPump& pump : _fluid.pumps())
			{
				if (pump.node != node || pump.objectOnNode != _fluidIndex)
					continue;
				if (!pump.switchedOn)
					ImGui::TextDisabled("switched off");
				else if (state == fluidState::mixed)
					ImGui::TextColored(red, "can't pump: mixed liquids in its network");
				else if (pump.powerFraction <= 0.0)
					ImGui::TextColored(red, "no power: it needs a cable on its tile, on a live network");
				else if (pump.edge < 0)
					ImGui::TextColored(amber, "nothing on its %s side to push into", tileSides::name(pump.side));
				else
					ImGui::Text("pushing %s: %.0f kPa, %.2f L/s", tileSides::name(pump.side), pump.head, pump.flow);
			}
		}
		if (_type.fluid.exchange > 0.0)
		{
			const double watts = _fluid.heatToAir(node);
			if (std::abs(watts) < 0.5)
				ImGui::TextDisabled("idle: liquid and air at the same temperature");
			else if (watts > 0)
				ImGui::Text("warming the air by %s", powerView::formatPower(watts).c_str());
			else
				ImGui::Text("cooling the air by %s", powerView::formatPower(-watts).c_str());
		}
		if (state == fluidState::filled && !_type.fluid.tank)
			ImGui::TextDisabled("liquid %.1f\xC2\xB0" "C, %.1f kPa", _fluid.liquidTemperature(node), _fluid.pressure(node));
	}

	//live readings for a load: how much of its rated power it is getting
	void loadReadings(const electricalNetwork& _power, int _device)
	{
		const electricalDevice& device = _power.devices()[_device];
		const double rated = electrical::ratedLoadWatts(device.type->electrical);
		if (!device.switchedOn)
		{
			ImGui::TextDisabled("switched off");
			return;
		}
		if (!std::isnan(device.thermostat))
		{
			const double t = _power.tileTemperature(device.node);
			if (_power.thermostatHeating(_device))
				ImGui::TextColored(amber, "heating: tile %.1f\xC2\xB0" "C, thermostat %.0f\xC2\xB0" "C", t, device.thermostat);
			else
			{
				ImGui::TextColored(green, "idle: tile %.1f\xC2\xB0" "C, warm enough for %.0f\xC2\xB0" "C", t, device.thermostat);
				return;
			}
		}
		if (_power.islandSolution(device.island).state != islandState::live)
		{
			ImGui::TextDisabled("off: no power");
			return;
		}
		ImGui::Text("%s of %s (%.0f%%)", powerView::formatPower(device.power.real()).c_str(), powerView::formatPower(rated).c_str(),
			100.0 * powerView::loadBrightness(device.power.real(), rated));
		ImGui::TextDisabled("%.2f A at %.1f V", std::abs(device.current), std::abs(_power.nodeVoltage(device.node)));
	}
}

void tilePanel::select(int _level, sf::Vector2i _tile)
{
	if (!selected || _level != selectedLevel || _tile != selectedTile)
	{
		kindMessage.clear();
		tankMessage.clear();
		fillLiquid.clear();
		storeMessage.clear();
	}
	selected = true;
	selectedLevel = _level;
	selectedTile = _tile;
}

void tilePanel::clear()
{
	selected = false;
}

void tilePanel::draw(station& _station, electricalNetwork& _power, const thermalNetwork* _heat, const fluidNetwork* _fluid, float _top,
	float _scale, ImFont* _font, const logisticsNetwork* _items, const processingSystem* _machines)
{
	if (!selected)
		return;
	if (selectedLevel < 0 || selectedLevel >= _station.getLayerListSize() || !_station.getIsoLevel(float(selectedLevel))->isInLevel(selectedTile))
	{
		clear();   //its level or tile is gone
		return;
	}
	isometricLevel* level = _station.getIsoLevel(float(selectedLevel));
	const std::vector<tileObject*> objects = level->levelTiles[level->coords2TileIndex(selectedTile.x, selectedTile.y)].allObjects();
	_power.update();   //readings for the station as it is now

	const float s = _scale;
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	if (_font)
		ImGui::PushFont(_font);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, uiTheme::panel());
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f * s, 8.f * s));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f * s, 5.f * s));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f * s, 3.f * s));
	ImGui::SetNextWindowPos(ImVec2(display.x - 8.f * s, _top), ImGuiCond_Always, ImVec2(1.f, 0.f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(330.f * s, 0.f), ImVec2(FLT_MAX, FLT_MAX));
	ImGui::Begin("tilePanel", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);

	//header: which tile, and a close button at the right
	ImGui::Text("Tile %d,%d", selectedTile.x, selectedTile.y);
	ImGui::SameLine();
	ImGui::TextDisabled("level %d   (Esc to close)", selectedLevel);
	const float closeWidth = ImGui::CalcTextSize("x").x + 2.f * ImGui::GetStyle().FramePadding.x;
	ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - closeWidth));
	if (ImGui::Button("x"))
		clear();
	if (_heat)
	{
		const double t = _heat->temperatureAt(selectedLevel, selectedTile);
		if (std::isnan(t))
			ImGui::TextDisabled("no floor: no air, no temperature");
		else
			ImGui::TextColored(toImGuiVec(thermalView::temperatureColour(t)), "%.1f\xC2\xB0" "C", t);
	}
	ImGui::Separator();

	int electricalIndex = 0;   //which of the tile's electrical objects this is: how the network numbers them
	int fluidIndex = 0;        //and which of its fluid objects
	int tankIndex = 0;         //and which of its tanks
	bool anything = false;
	tileObject* switchObject = nullptr;   //a riser to switch to another kind once the panel is drawn
	std::string switchTo;
	for (int i = 0; i < int(objects.size()) && selected; i++)
	{
		tileObject* object = objects[i];
		if (!object->type || object->type->layer == tileLayer::background)
			continue;
		anything = true;
		const tileTypeInfo& type = *object->type;
		ImGui::PushID(i);
		ImGui::TextUnformatted(type.name.c_str());
		if (onEditDefinition)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("edit"))
				onEditDefinition(type.key);
		}

		//a machine bigger than one tile, whichever of its tiles is selected: the machine itself, its readings taken where
		//it joins each network (its power port's tile, its first liquid port's tile), where it is the only thing
		const bool bigMachine = footprint::isMultiTile(type);
		if (bigMachine)
		{
			object = &object->machine();
			const sf::Vector2i size = footprint::turnedSize(type, object->turns());
			ImGui::TextDisabled("%d x %d tiles, turned %d", size.x, size.y, object->turns());
		}
		auto tileOf = [&](portInfo::kind _kind) {
			return bigMachine ? object->origin + footprint::mainCell(type, object->turns(), _kind) : selectedTile;
		};
		const sf::Vector2i powerTile = tileOf(portInfo::kind::power), liquidTile = tileOf(portInfo::kind::liquid);

		int device = -1;
		if (bigMachine ? (type.electrical.load || type.electrical.source) : electricalMedium.hasNode(*object))
			device = _power.deviceAt(selectedLevel, powerTile, bigMachine ? 0 : electricalIndex++);
		if (device >= 0 && _power.devices()[device].isSource)
			generatorReadings(_power, device);
		else if (device >= 0)
			loadReadings(_power, device);
		else if (type.electrical.conductor)
		{
			ImGui::TextDisabled("rated %.0f A, %.3f ohm", type.electrical.ratedCurrent, type.electrical.resistance);
			cableTemperatureLine(_power, selectedLevel, selectedTile);
		}
		if (type.electrical.conductor && type.electrical.vertical)
			riserLinks(_power, selectedLevel, selectedTile);
		if (type.fluid.carriesLiquid && _fluid)
			fluidReadings(*_fluid, selectedLevel, liquidTile, type, bigMachine ? 0 : fluidIndex, bigMachine ? 0 : tankIndex);
		if (type.fluid.vertical && _fluid)
			pipeRiserLinks(*_fluid, selectedLevel, selectedTile);
		if (type.thermal.ductConductance > 0.0 && _heat)
			ductLinks(*_heat, selectedLevel, selectedTile);
		if (type.items.tube && type.items.vertical && _items)
		{
			const int node = _items->network().nodeAt(selectedLevel, selectedTile);
			if (node >= 0)
				levelLinks(_items->network(), node, selectedLevel, selectedTile, "tube riser", [](int) { return true; },
					[](int, const char* _way, int _other) { ImGui::Text("%s: linked to level %d", _way, _other); });
		}
		if (type.fluid.carriesLiquid)
			fluidIndex++;
		if (type.fluid.tank)
		{
			tankIndex++;
			tankControls(object, level, s);
		}
		if (type.storage.isStore())
			storageControls(object, s, _items);
		if (recipes::isProcessor(type))
			processorControls(&object->machine(), level, s, _machines, _items);
		//an item tube: its network's rate and what it is moving
		if (type.items.tube && _items)
		{
			const int node = _items->network().nodeAt(selectedLevel, selectedTile);
			if (node >= 0)
			{
				const logisticsIsland& island = _items->island(_items->network().nodes()[node].island);
				if (island.moving > 1e-9)
					ImGui::Text("moving %.2f of %.2f kg/s", island.moving, island.capacity);
				else
					ImGui::TextDisabled("idle (it can move %.2f kg/s)", island.capacity);
				for (const auto& [material, rate] : island.materials)
				{
					char line[96];
					snprintf(line, sizeof(line), "%s: %.2f kg/s", material->name.c_str(), rate);
					swatchText(material->colour, line);
				}
				if (island.blocked > 0)
					ImGui::TextColored(red, "%d %s blocked: nothing on it takes %s", island.blocked, island.blocked == 1 ? "output" : "outputs", island.blockedWhat.c_str());
			}
		}
		//a machine's ports: where each is and what it carries (they are drawn on its tiles too)
		if (!type.ports.empty())
		{
			std::string line;
			for (const portInfo& port : footprint::turnedPorts(type, object->turns()))
			{
				const sf::Vector2i at = object->origin + port.cell;
				line += std::string(line.empty() ? "" : ", ") + portKindName(port.carries) + (port.flow == portInfo::direction::both ? "" : std::string(" ") +
					portFlowName(port.flow)) + " at " + std::to_string(at.x) + "," + std::to_string(at.y) + " " + tileSides::name(port.side);
			}
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420.f * s);
			ImGui::TextDisabled("ports: %s", line.c_str());
			ImGui::PopTextWrapPos();
		}

		//a cable's, riser's or pipe's shape: the sides it connects on (set when it was placed)
		if (findSetting(type, connections::settingKey))
			ImGui::TextDisabled("connects on: %s", connections::describe(connections::sides(type, object->allSettingValues())).c_str());

		//a riser: what it carries. switching it switches the risers stacked above and below with it (done after the
		//panel is drawn, since it swaps the objects)
		if (const kindFamily* kinds = kindFamilyOf(type.key))
		{
			ImGui::TextDisabled("carries");
			for (int k = 0; k < int(kinds->keys.size()); k++)
			{
				ImGui::SameLine();
				if (ImGui::RadioButton(kinds->labels[k].c_str(), kinds->keys[k] == type.key) && kinds->keys[k] != type.key)
				{
					switchObject = object;
					switchTo = kinds->keys[k];
				}
			}
			if (!kindMessage.empty() && kindMessageKey == type.key)
				ImGui::TextColored(red, "can't switch: %s", kindMessage.c_str());
			else
				ImGui::TextDisabled("switching changes the whole riser shaft");
		}

		//its settings: a checkbox for a toggle, a slider with - and + for a number (hidden ones are set by placing it)
		for (const itemSetting& setting : type.settings)
		{
			if (setting.hidden)
				continue;
			if (setting.type == itemSetting::kind::toggle)
			{
				bool on = object->getSetting(setting.key) != 0.0;
				if (ImGui::Checkbox(setting.label.c_str(), &on))
					level->changeObjectSetting(object, setting.key, on ? 1.0 : 0.0);
				continue;
			}
			double value = object->getSetting(setting.key);
			//the unit goes into a printf format, so a "%" in it has to be written "%%"
			std::string unit;
			for (char ch : setting.unit)
				unit += ch == '%' ? std::string("%%") : std::string(1, ch);
			const std::string format = std::string(numberFormat(setting)) + " " + unit;
			ImGui::PushID(setting.key.c_str());
			if (ImGui::Button("-"))
				level->changeObjectSetting(object, setting.key, value - setting.step);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(150.f * s);
			if (ImGui::SliderScalar("##value", ImGuiDataType_Double, &value, &setting.minimum, &setting.maximum, format.c_str()))
			{
				const double snapped = setting.minimum + std::round((value - setting.minimum) / setting.step) * setting.step;
				level->changeObjectSetting(object, setting.key, snapped);
			}
			ImGui::SameLine();
			if (ImGui::Button("+"))
				level->changeObjectSetting(object, setting.key, value + setting.step);
			ImGui::SameLine();
			ImGui::TextUnformatted(setting.label.c_str());
			ImGui::PopID();
		}

		//a tripped generator's reset
		const generatorState* state = device >= 0 ? _power.generatorStatus(device) : nullptr;
		if (state && state->tripped)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.12f, 0.12f, 1.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.18f, 0.18f, 1.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.25f, 0.25f, 1.f));
			if (ImGui::Button("Reset"))
				_power.resetGenerator(device);
			ImGui::PopStyleColor(3);
		}
		ImGui::Spacing();
		ImGui::PopID();
	}
	if (!anything && selected)
		ImGui::TextDisabled("nothing built here");

	ImGui::End();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(1);
	if (_font)
		ImGui::PopFont();

	if (switchObject)
	{
		std::string why;
		const std::string from = switchObject->tileTextureKey;
		if (_station.changeRiserKind(selectedLevel, selectedTile, switchObject, switchTo, &why) > 0)
			kindMessage.clear();
		else
		{
			kindMessage = why;
			kindMessageKey = from;
		}
	}
}

void tilePanel::tankControls(tileObject* _tank, isometricLevel* _level, float _scale)
{
	std::vector<const materialInfo*> liquids;
	for (const materialInfo& material : materialCatalogue())
	{
		if (material.state == materialInfo::phase::liquid && !material.removed)
			liquids.push_back(&material);
	}
	if (liquids.empty() || !_tank->type)
		return;
	//the liquid to fill it with: the one picked, or else what it holds (or was made for)
	const materialInfo* chosen = findMaterial(fillLiquid);
	if (!chosen || chosen->removed || chosen->state != materialInfo::phase::liquid)
	{
		chosen = findMaterial(_tank->contents().empty() ? _tank->type->fluid.liquid : _tank->contents().front().material);
		if (!chosen)
			chosen = liquids.front();
	}
	ImGui::SetNextItemWidth(140.f * _scale);
	if (ImGui::BeginCombo("##liquid", chosen->name.c_str()))
	{
		for (const materialInfo* liquid : liquids)
		{
			if (ImGui::Selectable(liquid->name.c_str(), liquid == chosen))
			{
				fillLiquid = liquid->key;
				chosen = liquid;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("Fill"))
	{
		const std::vector<tileObject::heldMaterial>& held = _tank->contents();
		if (!held.empty() && held.front().material != chosen->key)
		{
			const materialInfo* inside = findMaterial(held.front().material);
			tankMessage = "drain it first: it holds " + (inside ? inside->name : held.front().material);
		}
		else
		{
			_tank->setContents(chosen->key, _tank->type->fluid.capacity);
			_level->markChanged();   //the liquid network reads it back from the tank
			tankMessage.clear();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Drain"))
	{
		_tank->clearContents();
		_level->markChanged();
		tankMessage.clear();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(for testing)");
	if (!tankMessage.empty())
		ImGui::TextColored(amber, "%s", tankMessage.c_str());
}

void tilePanel::storageControls(tileObject* _store, float _scale, const logisticsNetwork* _items)
{
	const storageProperties& capacity = _store->type->storage;
	const double litres = storage::usedLitres(*_store), kg = storage::usedKg(*_store);
	//how full: by volume, or by weight if that is the nearer limit
	const double full = std::max(litres / capacity.litres, capacity.kg > 0.0 ? kg / capacity.kg : 0.0);
	char text[96];
	snprintf(text, sizeof(text), "%.0f of %.0f L", litres, capacity.litres);
	ImGui::ProgressBar(float(std::clamp(full, 0.0, 1.0)), ImVec2(260.f * _scale, 0.f), text);
	if (capacity.kg > 0.0)
		ImGui::TextDisabled("%.0f of %.0f kg", kg, capacity.kg);
	else
		ImGui::TextDisabled("%.0f kg", kg);
	if (_store->contents().empty())
		ImGui::TextDisabled("empty");
	for (const tileObject::heldMaterial& held : _store->contents())
	{
		const materialInfo* material = findMaterial(held.material);
		if (!material)
			continue;
		char line[128];
		snprintf(line, sizeof(line), "%s: %.0f kg (%.0f L)", material->name.c_str(), held.amount, held.amount / material->kilogramsPerLitre());
		swatchText(material->colour, line);
	}

	//the tubes: what is going in or out, and its filter (the one solid it takes from them)
	if (_items)
	{
		const double rate = _items->rateOf(*_store);
		if (std::abs(rate) > 1e-6)
			ImGui::Text("%s %.2f kg/s by tube", rate > 0.0 ? "taking in" : "sending out", std::abs(rate));
	}
	const materialInfo* filter = findMaterial(_store->accepts());
	ImGui::SetNextItemWidth(170.f * _scale);
	if (ImGui::BeginCombo("##accepts", filter ? filter->name.c_str() : "anything"))
	{
		if (ImGui::Selectable("anything", !filter))
			_store->setAccepts("");
		for (const materialInfo& material : materialCatalogue())
		{
			if (material.state == materialInfo::phase::solid && !material.removed && ImGui::Selectable(material.name.c_str(), filter == &material))
				_store->setAccepts(material.key);
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("takes from the tubes");

	//for testing, until tubes and machines bring materials: put some of a solid in, take some out, empty it
	std::vector<const materialInfo*> solids;
	for (const materialInfo& material : materialCatalogue())
	{
		if (material.state == materialInfo::phase::solid && !material.removed)
			solids.push_back(&material);
	}
	if (solids.empty())
		return;
	const materialInfo* chosen = findMaterial(storeMaterial);
	if (!chosen || chosen->removed || chosen->state != materialInfo::phase::solid)
		chosen = solids.front();
	ImGui::SetNextItemWidth(140.f * _scale);
	if (ImGui::BeginCombo("##solid", chosen->name.c_str()))
	{
		for (const materialInfo* solid : solids)
		{
			if (ImGui::Selectable(solid->name.c_str(), solid == chosen))
			{
				storeMaterial = solid->key;
				chosen = solid;
			}
		}
		ImGui::EndCombo();
	}
	for (const double amount : { 10.0, 100.0, 1000.0 })
	{
		ImGui::SameLine();
		snprintf(text, sizeof(text), "+%.0f", amount);
		if (ImGui::Button(text))
		{
			const double added = storage::put(*_store, *chosen, amount);
			storeMessage = added < amount - 1e-9 ? "only " + std::to_string(int(std::round(added))) + " kg fitted" : "";
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("-100"))
	{
		storage::take(*_store, chosen->key, 100.0);
		storeMessage.clear();
	}
	ImGui::SameLine();
	if (ImGui::Button("Empty"))
	{
		storage::empty(*_store);
		storeMessage.clear();
	}
	ImGui::TextDisabled("kg of %s, for testing", chosen->name.c_str());
	if (!storeMessage.empty())
	{
		ImGui::SameLine();
		ImGui::TextColored(amber, "%s", storeMessage.c_str());
	}
}

void tilePanel::processorControls(tileObject* _machine, isometricLevel* _level, float _scale, const processingSystem* _machines,
	const logisticsNetwork* _items)
{
	const tileTypeInfo& type = *_machine->type;
	const int chosen = recipes::currentIndex(*_machine);
	const recipeInfo* recipe = recipes::current(*_machine);
	//its recipe: picking another sends what it had in for the old one back out
	ImGui::SetNextItemWidth(220.f * _scale);
	if (ImGui::BeginCombo("##recipe", recipe ? recipe->name.c_str() : "nothing"))
	{
		for (int r = 0; r < int(type.recipes.size()); r++)
		{
			if (ImGui::Selectable(type.recipes[r].name.c_str(), r == chosen) && r != chosen)
			{
				recipes::choose(*_machine, r);
				_level->markChanged();
				recipe = recipes::current(*_machine);
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("recipe");
	if (!recipe)
		return;
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420.f * _scale);
	ImGui::TextDisabled("%s -> %s, %g s a cycle", recipes::describe(recipe->inputs).c_str(), recipes::describe(recipe->outputs).c_str(), recipe->seconds);
	if (recipe->heat != 0.0)
		ImGui::TextDisabled("gives off %.0f W of heat while working", recipe->heat);
	ImGui::PopTextWrapPos();

	//what it is doing, and how far through its cycle
	const processorState* state = _machines ? _machines->stateOf(*_machine) : nullptr;
	if (state)
	{
		const sf::Color c = machineStatusColour(state->status);
		ImGui::TextColored(ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f), "%s", processingSystem::describe(*state).c_str());
	}
	char text[128];
	const double left = (1.0 - _machine->progress) * recipe->seconds / std::max(state ? state->speed : 1.0, 1e-6);
	if (state && state->speed > 0.0)
		snprintf(text, sizeof(text), "%.0f%%, %.1f s to go", _machine->progress * 100.0, left);
	else
		snprintf(text, sizeof(text), "%.0f%%", _machine->progress * 100.0);
	ImGui::ProgressBar(float(std::clamp(_machine->progress, 0.0, 1.0)), ImVec2(260.f * _scale, 0.f), text);
	if (state && (state->liquidIn > 1e-6 || state->liquidOut > 1e-6))
		ImGui::Text("liquid: %.2f L/s in, %.2f L/s out", state->liquidIn, state->liquidOut);
	if (_items)
	{
		const double rate = _items->rateOf(*_machine);
		if (std::abs(rate) > 1e-6)
			ImGui::Text("%s %.2f kg/s by tube", rate > 0.0 ? "taking in" : "sending out", std::abs(rate));
	}

	//its buffers: each holds up to a couple of cycles' worth
	auto bufferLine = [&](const std::string& _key, double _have, double _perCycle) {
		const materialInfo* material = findMaterial(_key);
		const bool liquid = material && material->state == materialInfo::phase::liquid;
		char line[160];
		if (_perCycle > 0.0)
			snprintf(line, sizeof(line), "%s: %.1f of %.0f %s", material ? material->name.c_str() : _key.c_str(), _have,
				_perCycle * recipes::bufferCycles, liquid ? "L" : "kg");
		else
			snprintf(line, sizeof(line), "%s: %.1f %s (not used now: it goes back out)", material ? material->name.c_str() : _key.c_str(), _have,
				liquid ? "L" : "kg");
		swatchText(material ? material->colour : sf::Color::White, line);
	};
	auto amountIn = [](const std::vector<tileObject::heldMaterial>& _list, const std::string& _key) {
		for (const tileObject::heldMaterial& held : _list)
			if (held.material == _key)
				return held.amount;
		return 0.0;
	};
	if (!recipe->inputs.empty() || !_machine->contents().empty())
	{
		ImGui::TextDisabled("in:");
		for (const auto& [key, amount] : recipe->inputs)
			bufferLine(key, amountIn(_machine->contents(), key), amount);
		for (const tileObject::heldMaterial& held : _machine->contents())
			if (recipes::inputPerCycle(*recipe, held.material) <= 0.0)
				bufferLine(held.material, held.amount, 0.0);
	}
	if (!recipe->outputs.empty() || !_machine->outputs().empty())
	{
		ImGui::TextDisabled("out:");
		for (const auto& [key, amount] : recipe->outputs)
			bufferLine(key, amountIn(_machine->outputs(), key), amount);
		for (const tileObject::heldMaterial& held : _machine->outputs())
			if (recipes::outputPerCycle(*recipe, held.material) <= 0.0)
				bufferLine(held.material, held.amount, 0.0);
	}

	//for testing, until there are mines and tubes everywhere: fill its inputs, empty its outputs
	if (!recipe->inputs.empty() && ImGui::Button("Fill inputs"))
	{
		for (const auto& [key, amount] : recipe->inputs)
			if (const materialInfo* material = findMaterial(key))
				recipes::putInput(*_machine, *material, amount * recipes::bufferCycles);
	}
	if (!recipe->inputs.empty())
		ImGui::SameLine();
	if (ImGui::Button("Empty outputs"))
		_machine->outputsToChange().clear();
	ImGui::SameLine();
	ImGui::TextDisabled("(for testing)");
}

void tilePanel::drawOutline(station& _station, int _currentLevel, const sf::RenderWindow& _window) const
{
	if (!selected || selectedLevel != _currentLevel || selectedLevel >= _station.getLayerListSize())
		return;
	isometricLevel* level = _station.getIsoLevel(float(selectedLevel));
	if (!level->isInLevel(selectedTile))
		return;
	const sf::Vector2f c = level->tileCentreWorld(sf::Vector2f(selectedTile));
	auto pixel = [&](sf::Vector2f _offset) {
		const sf::Vector2i p = _window.mapCoordsToPixel(c + _offset);
		return ImVec2(float(p.x), float(p.y));
	};
	const ImVec2 top = pixel(sf::Vector2f(0.f, -16.f)), right = pixel(sf::Vector2f(32.f, 0.f));
	const ImVec2 bottom = pixel(sf::Vector2f(0.f, 16.f)), left = pixel(sf::Vector2f(-32.f, 0.f));
	const float width = std::clamp((right.x - left.x) * 0.04f, 2.f, 6.f);
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	//a machine on the tile: round its whole footprint, with its ports marked
	for (tileObject* object : level->levelTiles[level->coords2TileIndex(selectedTile.x, selectedTile.y)].allObjects())
	{
		if (!object->type || !footprint::isMachine(*object->type))
			continue;
		const tileObject& machine = object->machine();
		machineView::drawFootprint(drawList, _window, *level, machine.origin, footprint::turnedSize(*machine.type, machine.turns()), IM_COL32(0, 230, 255, 255), width);
		machineView::drawPorts(drawList, _window, *level, *machine.type, machine.turns(), machine.origin);
		return;
	}
	drawList->AddQuad(top, right, bottom, left, IM_COL32(0, 0, 0, 220), width + 2.f);
	drawList->AddQuad(top, right, bottom, left, IM_COL32(0, 230, 255, 255), width);
}
