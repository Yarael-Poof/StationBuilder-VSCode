//the README's screenshots (docs/screenshots/*.png), drawn the way the game draws its screen: 1920 x 1080, GUI scale 1,
//the VT323 font and the terminal theme with its glow and scanlines, in a hidden window (nothing is sent to any other
//window: the mouse positions and clicks below go to ImGui only).
//
//it builds a station to show: the starting station with the four example blueprints placed round its room, a pumped
//coolant loop out to a radiator on the hull, solar arrays, a docking jetty and two more decks. it runs the simulation
//for a while (5 minutes of game time by default) so the machines are working and the temperatures have spread, then
//takes one picture per view.
//
//build:  cmake --build build/debug --target ReadmeShots
//run it from the StationBuilder folder (where the game's art, content, fonts and blueprints are):
//        ..\build\debug\ReadmeShots.exe [output folder] [ticks] [one shot's name]
//the output folder defaults to ../docs/screenshots. VS Code: the "README screenshots" launch configuration.
//
//the icon column, labels and time controls are copies of main.cpp's (they live in main.cpp's gui namespace): keep
//them in step when those change
#include <SFML/Graphics.hpp>
#include <imgui.h>
#include <imgui-SFML.h>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include "station.h"
#include "tileCatalogue.h"
#include "content.h"
#include "decks.h"
#include "simulation.h"
#include "electricalNetwork.h"
#include "fluidNetwork.h"
#include "thermalNetwork.h"
#include "logistics.h"
#include "processing.h"
#include "powerView.h"
#include "thermalView.h"
#include "fluidView.h"
#include "logisticsView.h"
#include "exteriorView.h"
#include "tilePanel.h"
#include "buildMenu.h"
#include "blueprint.h"
#include "blueprintTool.h"
#include "contentPanel.h"
#include "contentEditor.h"
#include "lineArt.h"
#include "uiTheme.h"
#include "screenEffects.h"
#include "textDecode.h"

namespace
{
	const unsigned screenWidth = 1920, screenHeight = 1080;

	//--- copies of main.cpp's GUI pieces ---
	const ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;

	sf::Color iconTint()
	{
		const ImVec4 c = uiTheme::text();
		return sf::Color(sf::Uint8(c.x * 255.f), sf::Uint8(c.y * 255.f), sf::Uint8(c.z * 255.f));
	}

	//a 48 x 48 icon button, lit while its mode is on
	void iconToggle(const char* _id, const sf::Texture& _icon, ImVec2 _pos, bool _on)
	{
		ImGui::SetNextWindowPos(_pos);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
		ImGui::Begin(_id, nullptr, overlayFlags);
		if (_on)
			ImGui::PushStyleColor(ImGuiCol_Button, uiTheme::fillActive());
		ImGui::ImageButton(_id, _icon, sf::Vector2f(48.f, 48.f), sf::Color::Transparent, iconTint());
		if (_on)
			ImGui::PopStyleColor();
		ImGui::End();
		ImGui::PopStyleVar();
	}

	//text on a panel-coloured box
	void label(ImVec2 _pos, ImVec2 _size, const std::string& _text, bool _centred = false)
	{
		ImDrawList* dl = ImGui::GetForegroundDrawList();
		const ImVec2 max(_pos.x + _size.x, _pos.y + _size.y);
		dl->AddRectFilled(_pos, max, uiTheme::u32(uiTheme::panel()));
		dl->AddRect(_pos, max, uiTheme::u32(uiTheme::faint()));
		const ImVec2 textSize = ImGui::CalcTextSize(_text.c_str());
		const float x = _centred ? _pos.x + (_size.x - textSize.x) * 0.5f : _pos.x + 3.f;
		dl->AddText(ImVec2(x, _pos.y + (_size.y - textSize.y) * 0.5f), uiTheme::u32(uiTheme::text()), _text.c_str());
	}

	std::string clockText(double _seconds)
	{
		const long long total = (long long)_seconds;
		char text[32];
		snprintf(text, sizeof(text), "%lld:%02lld:%02lld", total / 3600, (total / 60) % 60, total % 60);
		return text;
	}

	//the time controls in the top right corner (running at 1x); returns their bottom edge
	float timeControls(const simulationManager& _sim, ImFont* _font)
	{
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		ImGui::PushFont(_font);
		ImGui::SetNextWindowPos(ImVec2(display.x - 8.f, 8.f), ImGuiCond_Always, ImVec2(1.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, uiTheme::panel());
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 6));
		ImGui::Begin("timeControls", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::Button("Pause", ImVec2(56, 28));
		for (int speed : { 1, 2, 4 })
		{
			ImGui::SameLine();
			if (speed == 1)
				ImGui::PushStyleColor(ImGuiCol_Button, uiTheme::fillActive());
			ImGui::Button((std::to_string(speed) + "x").c_str(), ImVec2(36, 28));
			if (speed == 1)
				ImGui::PopStyleColor();
		}
		ImGui::Text("%s   tick %llu", clockText(_sim.getSimTime()).c_str(), (unsigned long long)_sim.getTickCount());
		const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
		ImGui::PopFont();
		return bottom;
	}

	sf::Texture loadIcon(const std::string& _path)
	{
		sf::Texture texture;
		if (!texture.loadFromFile(_path))
			std::cout << "cannot load icon " << _path << std::endl;
		texture.setSmooth(true);
		return texture;
	}

	//--- the station ---
	void put(isometricLevel& _level, int _x, int _y, const char* _key, const std::string& _settings = "")
	{
		if (!_level.addObjectToTile(sf::Vector2f(float(_x), float(_y)), _key, _settings))
			std::cout << "refused " << _key << " at " << _x << "," << _y << std::endl;
	}

	tileObject* machineAt(isometricLevel& _level, int _x, int _y, const char* _key)
	{
		for (tileObject* object : _level.levelTiles[_level.coords2TileIndex(_x, _y)].allObjects())
			if (object->tileTextureKey == _key)
				return &object->machine();
		return nullptr;
	}

	const blueprint* findBlueprint(const std::vector<blueprint>& _library, const char* _name)
	{
		for (const blueprint& b : _library)
			if (b.name == _name)
				return &b;
		std::cout << "no blueprint called " << _name << std::endl;
		return nullptr;
	}

	void placeBlueprint(isometricLevel& _level, const std::vector<blueprint>& _library, const char* _name, int _x, int _y)
	{
		const blueprint* b = findBlueprint(_library, _name);
		if (!b)
			return;
		const blueprints::pasteResult result = blueprints::paste(_level, *b, sf::Vector2i(_x, _y));
		if (result.refused > 0)
			std::cout << _name << ": " << result.refused << " refused, the first " << result.firstRefused << std::endl;
	}

	//the starting station (48 x 48; the lowest deck's area is x 12-35, y 15-32 round a walled room at x 18-29, y 20-27)
	//with everything the screenshots show. a machine is placed by its middle tile, (size - 1) / 2 in from its upper one
	void buildStation(station& _station, const std::vector<blueprint>& _library)
	{
		decks::startingStation(_station);
		isometricLevel& lowest = *_station.getIsoLevel(0);
		isometricLevel& middle = *_station.getIsoLevel(1);

		//the example production chains round the room
		placeBlueprint(lowest, _library, "Iron smelting line", 19, 15);
		placeBlueprint(lowest, _library, "Ice to water", 19, 29);
		placeBlueprint(lowest, _library, "Waste recycling", 28, 29);
		placeBlueprint(lowest, _library, "Powered room", 12, 21);
		if (tileObject* crate = machineAt(lowest, 28, 30, "crate"))
			storage::put(*crate, *findMaterial("waste"), 400);   //something for the recycler to work on

		//in the room: a generator powering two heaters, three lights and a pump
		for (int x = 24; x <= 28; x++)
			put(lowest, x, 22, "cable");
		put(lowest, 24, 22, "generator");
		put(lowest, 26, 22, "heater");
		put(lowest, 28, 22, "heater");
		put(lowest, 25, 21, "cable");
		put(lowest, 25, 21, "light");
		for (int y = 23; y <= 26; y++)
			put(lowest, 24, y, "cable");
		put(lowest, 24, 26, "light");
		for (int x = 22; x <= 23; x++)
			put(lowest, x, 24, "cable");
		//the coolant loop: from the radiator's out (its tile 23,28) up the right leg through an exchanger to the pump at
		//22,24, which pushes to the upper left, down the left leg through another exchanger to the radiator's in (21,28)
		put(lowest, 23, 27, "pipe");
		put(lowest, 23, 26, "exchanger");
		put(lowest, 23, 25, "pipe");
		put(lowest, 23, 24, "pipe");
		put(lowest, 22, 24, "pump");
		put(lowest, 21, 24, "pipe");
		put(lowest, 21, 25, "exchanger");
		put(lowest, 21, 26, "pipe");
		put(lowest, 21, 27, "pipe");
		put(lowest, 20, 24, "coolantTank");
		put(lowest, 22, 28, "radiatorPanel");   //21-23, 28, its ports facing the room
		//outside the room's right wall: a solar array wired in, and a docking jetty with a tube to a crate
		put(lowest, 30, 21, "solarArray");
		put(lowest, 29, 21, "cable");
		put(lowest, 29, 22, "cable");
		put(lowest, 31, 25, "dockingJetty");    //30-33, 25
		put(lowest, 29, 25, "tube");
		put(lowest, 28, 25, "crate");

		//the deck above: a walled room over the lowest one's, lit, with a solar array on the lowest deck's roof
		for (int y = 21; y <= 26; y++)
			for (int x = 20; x <= 27; x++)
				put(middle, x, y, "floor");
		for (int x = 20; x <= 27; x++)
		{
			put(middle, x, 21, "wallUR");
			put(middle, x, 26, "wallLL");
		}
		for (int y = 21; y <= 26; y++)
		{
			put(middle, 20, y, "wallUL");
			put(middle, 27, y, "wallLR");
		}
		for (int x = 22; x <= 26; x++)
			put(middle, x, 23, "cable");
		put(middle, 23, 23, "light");
		put(middle, 25, 23, "light");
		put(middle, 28, 22, "solarArray");
		put(middle, 27, 22, "cable");
		put(middle, 27, 23, "cable");

		//a round top deck with a small tower and a large radiator on the middle deck's roof
		const int top = _station.insertNewIsoLevel(2, float(decks::gridSize), float(decks::gridSize));
		isometricLevel& upper = *_station.getIsoLevel(float(top));
		decks::setArea(upper, decks::shapeTiles(decks::shape::ellipse, sf::Vector2i(12, 10), sf::Vector2i(24, 24)));
		for (int y = 22; y <= 25; y++)
			for (int x = 22; x <= 25; x++)
				put(upper, x, y, "floor");
		for (int x = 22; x <= 25; x++)
		{
			put(upper, x, 22, "wallUR");
			put(upper, x, 25, "wallLL");
		}
		for (int y = 22; y <= 25; y++)
		{
			put(upper, 22, y, "wallUL");
			put(upper, 25, y, "wallLR");
		}
		put(upper, 27, 22, "largeRadiator");    //26-29, 22-23
		_station.setCurrentLevel(0);
	}

	//--- the pictures ---
	enum class view { none, power, heat, liquid, items, exterior };
	struct shot
	{
		std::string name;              //its file: <name>.png
		int level = 0;
		sf::Vector2f centre;           //the tile at the middle of the screen
		float zoom = 1.f;              //the view's zoom (smaller is closer)
		sf::Vector2f screenOffset;     //then moved this far on screen, to leave room for the panels
		view shown = view::none;
		sf::Vector2i selected{ -1, -1 };   //the tile whose panel is open
		sf::Vector2i hovered{ -1, -1 };    //the tile under the mouse
		bool building = false;         //build mode, the smelter picked from the Industry tab
		bool blueprints = false;       //the Blueprints menu open, a chain being placed
		bool editing = false;          //the content editor open on the smelter
	};

	std::vector<shot> shotList()
	{
		std::vector<shot> shots;
		shot s;
		s = shot(); s.name = "station"; s.centre = { 24.f, 24.f }; s.zoom = 0.9f; s.screenOffset = { 40.f, -70.f };
		s.building = true; s.hovered = { 26, 24 }; shots.push_back(s);
		s = shot(); s.name = "power"; s.centre = { 22.f, 23.f }; s.zoom = 0.62f; s.screenOffset = { -120.f, 0.f };
		s.shown = view::power; s.selected = { 24, 22 }; s.hovered = { 24, 25 }; shots.push_back(s);
		s = shot(); s.name = "thermal"; s.centre = { 23.5f, 24.f }; s.zoom = 0.8f; s.screenOffset = { -60.f, 0.f };
		s.shown = view::heat; s.hovered = { 14, 22 }; shots.push_back(s);
		s = shot(); s.name = "liquid"; s.centre = { 22.f, 25.5f }; s.zoom = 0.55f; s.screenOffset = { -120.f, 0.f };
		s.shown = view::liquid; s.selected = { 22, 24 }; s.hovered = { 22, 28 }; shots.push_back(s);
		s = shot(); s.name = "industry"; s.centre = { 24.f, 17.f }; s.zoom = 0.6f; s.screenOffset = { -120.f, 0.f };
		s.shown = view::items; s.selected = { 25, 16 }; s.hovered = { 23, 16 }; shots.push_back(s);
		s = shot(); s.name = "blueprints"; s.level = 1; s.centre = { 24.f, 24.f }; s.zoom = 0.75f; s.screenOffset = { 230.f, -60.f };
		s.blueprints = true; s.hovered = { 26, 28 }; shots.push_back(s);   //the copy covers 23-30, 27-29
		s = shot(); s.name = "exterior"; s.centre = { 24.f, 24.f }; s.zoom = 0.95f; s.screenOffset = { -60.f, -40.f };
		s.shown = view::exterior; shots.push_back(s);
		s = shot(); s.name = "editor"; s.centre = { 24.f, 24.f }; s.zoom = 0.9f; s.editing = true; shots.push_back(s);
		return shots;
	}

	const sf::Vector2f industryTab(994.f, 880.f);   //where the build menu's Industry tab is at 1080p (clicked to open it)
	const int powerOnFrames = 110;                  //the screen's power-on, before the first picture
	const int settleFrames = 45;                    //each picture's frames: new text has typed itself in by the last
}

int main(int argc, char** argv)
{
	const std::string outputFolder = argc > 1 ? argv[1] : "../docs/screenshots";
	const int ticks = argc > 2 ? std::stoi(argv[2]) : 3000;
	const std::string onlyShot = argc > 3 ? argv[3] : "";

	sf::RenderWindow window(sf::VideoMode(screenWidth, screenHeight), "README screenshots", sf::Style::None);
	window.setVisible(false);
	if (!ImGui::SFML::Init(window, false))
		return 2;
	ImGui::GetIO().IniFilename = nullptr;
	//the game's fonts and style (main.cpp's loadFonts and applyStyle)
	ImFontConfig crisp;
	crisp.OversampleH = 1;
	crisp.OversampleV = 1;
	crisp.PixelSnapH = true;
	ImFontAtlas* fonts = ImGui::GetIO().Fonts;
	ImFont* normal = fonts->AddFontFromFileTTF(uiTheme::fontFile, uiTheme::fontSize(1.f), &crisp, fonts->GetGlyphRangesDefault());
	ImFont* panel = fonts->AddFontFromFileTTF(uiTheme::fontFile, uiTheme::fontSize(1.f), &crisp, fonts->GetGlyphRangesDefault());
	if (!normal || !panel || !ImGui::SFML::UpdateFontTexture())
	{
		std::cout << "cannot load " << uiTheme::fontFile << ": run this from the StationBuilder folder" << std::endl;
		return 3;
	}
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowPadding = ImVec2(0, 0);
	style.WindowMinSize = ImVec2(1, 1);
	style.FramePadding = ImVec2(0, 0);
	style.ItemSpacing = ImVec2(0, 0);
	style.ScrollbarSize = 14.f;
	uiTheme::apply();

	const sf::Texture upIcon = loadIcon("up.png"), downIcon = loadIcon("down.png"), buildIcon = loadIcon("gui/build.png"),
		bugIcon = loadIcon("gui/bug.png"), saveIcon = loadIcon("gui/save.png"), bulldozerIcon = loadIcon("gui/bulldozer.png"),
		loadStationIcon = loadIcon("gui/load.png"), powerIcon = loadIcon("gui/power.png"), heatIcon = loadIcon("gui/thermal.png"),
		liquidIcon = loadIcon("gui/liquid.png"), itemsIcon = loadIcon("gui/items.png"), copyIcon = loadIcon("gui/copy.png"),
		blueprintIcon = loadIcon("gui/blueprint.png"), decksIcon = loadIcon("gui/decks.png"), exteriorIcon = loadIcon("gui/exterior.png");

	textureManager textures;
	content::load("content");
	content::loadArt(textures);
	std::vector<std::string> problems;
	const std::vector<blueprint> library = blueprints::loadAll(blueprints::folderName, problems);
	for (const std::string& problem : problems)
		std::cout << "blueprint problem: " << problem << std::endl;
	station st(&textures);
	buildStation(st, library);
	lineArt::update(st, textures);

	//the simulation, wired as main.cpp wires it
	electricalNetwork power(st);
	fluidNetwork liquid(st, &power);
	thermalNetwork heat(st, &power);
	heat.addHeatSource(&liquid);
	liquid.setTemperatureSource(&heat);
	power.setTemperatureSource(&heat);
	logisticsNetwork items(st);
	processingSystem machines(st, &power, &liquid, &heat);
	heat.addHeatSource(&machines);
	simulationManager sim;
	sim.addSystem(&power);
	sim.addSystem(&liquid);
	sim.addSystem(&heat);
	sim.addSystem(&items);
	sim.addSystem(&machines);
	for (int i = 0; i < ticks; i++)
		sim.step();
	for (const processorState& state : machines.machines())
		std::cout << state.machine->type->name << ": " << processingSystem::describe(state) << std::endl;

	screenEffects screen;
	textDecode decoder;
	tilePanel selection;
	buildMenu buildBar;
	buildBar.setScale(1.f, panel);
	blueprintTool blueprintMaker;
	contentPanel contentWindow;
	contentEditor editor("content");

	int extraFrames = powerOnFrames;
	for (const shot& s : shotList())
	{
		if (!onlyShot.empty() && s.name != onlyShot)
			continue;
		st.setCurrentLevel(s.level);
		isometricLevel* level = st.getIsoLevel(float(s.level));
		sf::View camera = window.getDefaultView();
		camera.setCenter(level->tileCentreWorld(s.centre));
		camera.zoom(s.zoom);
		camera.move(-s.screenOffset * s.zoom);
		window.setView(camera);
		if (s.selected.x >= 0)
			selection.select(s.level, s.selected);
		else
			selection.clear();
		if (s.building)
			buildBar.select("smelter");
		if (s.blueprints)
		{
			blueprintMaker.togglePanel();
			if (const blueprint* b = findBlueprint(library, "Ice to water"))
				blueprintMaker.startPlacing(*b);
		}
		if (s.editing)
			editor.editItem("smelter");

		const int frames = extraFrames + settleFrames;
		extraFrames = 0;
		for (int frame = 0; frame < frames; frame++)
		{
			//the mouse: over the Industry tab for a click in build mode's first frames, then over the hovered tile
			ImGuiIO& io = ImGui::GetIO();
			const bool clickingTab = s.building && frame >= 2 && frame < 8;
			sf::Vector2i mouse(-100000, -100000);
			if (clickingTab)
				mouse = sf::Vector2i(industryTab);
			else if (s.hovered.x >= 0)
				mouse = window.mapCoordsToPixel(level->tileCentreWorld(sf::Vector2f(s.hovered)), camera);
			io.AddMousePosEvent(float(mouse.x), float(mouse.y));
			if (clickingTab && frame == 4)
				io.AddMouseButtonEvent(0, true);
			if (clickingTab && frame == 5)
				io.AddMouseButtonEvent(0, false);
			ImGui::SFML::Update(window, sf::milliseconds(16));

			iconToggle("upLayer", upIcon, ImVec2(4, 20), false);
			iconToggle("downLayer", downIcon, ImVec2(4, 90), false);
			iconToggle("build", buildIcon, ImVec2(4, 160), s.building);
			iconToggle("delete", bulldozerIcon, ImVec2(56, 160), false);
			iconToggle("powerView", powerIcon, ImVec2(4, 300), s.shown == view::power);
			iconToggle("heatView", heatIcon, ImVec2(56, 300), s.shown == view::heat);
			iconToggle("liquidView", liquidIcon, ImVec2(108, 300), s.shown == view::liquid);
			iconToggle("itemsView", itemsIcon, ImVec2(4, 352), s.shown == view::items);
			iconToggle("copyTool", copyIcon, ImVec2(4, 416), false);
			iconToggle("blueprints", blueprintIcon, ImVec2(56, 416), s.blueprints);
			iconToggle("decks", decksIcon, ImVec2(108, 416), false);
			iconToggle("exterior", exteriorIcon, ImVec2(56, 352), s.shown == view::exterior);
			iconToggle("bug", bugIcon, ImVec2(4, 230), false);
			iconToggle("save", saveIcon, ImVec2(4, 480), false);
			iconToggle("load", loadStationIcon, ImVec2(56, 480), false);

			//the right-hand column: time controls, the view's key, the selected tile's panel
			float bottom = timeControls(sim, panel);
			const int hoveredDeck = s.shown == view::exterior ? 1 : -1;
			if (s.shown == view::exterior)
				bottom = exteriorView::drawLegend(st, hoveredDeck, bottom + 8.f, 1.f, panel);
			else if (s.shown == view::power)
				bottom = powerView::drawLegend(power, bottom + 8.f, 1.f, panel);
			else if (s.shown == view::heat)
				bottom = thermalView::drawLegend(heat, bottom + 8.f, 1.f, panel);
			else if (s.shown == view::liquid)
				bottom = fluidView::drawLegend(liquid, bottom + 8.f, 1.f, panel);
			else if (s.shown == view::items)
				bottom = logisticsView::drawLegend(items, bottom + 8.f, 1.f, panel, &machines);
			selection.draw(st, power, &heat, &liquid, bottom + 8.f, 1.f, panel, &items, &machines);
			blueprintMaker.drawPanels(st, textures, 1.f, panel);

			window.clear(sf::Color::Black);
			st.highlightAllTiles(sf::Color::White);
			power.update();
			powerView::applyDeviceShading(power, st, s.level);
			if (s.blueprints)
				blueprintMaker.highlight(st, s.hovered, true);
			if (s.building)
			{
				buildBar.draw(&textures);
				label(ImVec2(108, 164), ImVec2(67.2f, 18), std::to_string(mouse.x) + "," + std::to_string(mouse.y));
				label(ImVec2(108, 184), ImVec2(67.2f, 18), std::to_string(s.hovered.x) + "," + std::to_string(s.hovered.y));
				st.highlightTile(sf::Vector2f(s.hovered), sf::Color::Red);
			}
			contentWindow.draw(&textures, 1.f, panel);
			editor.draw(st, textures, contentWindow, 1.f, panel);
			const double animationSeconds = 600.0 + frame * 0.016;
			if (s.shown == view::power)
				powerView::drawOverlay(power, s.level, level, window, animationSeconds);
			else if (s.shown == view::heat)
				thermalView::drawOverlay(heat, s.level, level, window);
			else if (s.shown == view::liquid)
				fluidView::drawOverlay(liquid, s.level, level, window, animationSeconds, panel);
			else if (s.shown == view::items)
				logisticsView::drawOverlay(items, s.level, level, window, animationSeconds, panel, &machines);
			if (s.shown != view::exterior)
			{
				selection.drawOutline(st, s.level, window);
				decks::drawGuides(window, st, false);
			}
			powerView::drawAlerts(power, s.level, level, window, panel);
			if (!clickingTab && s.hovered.x >= 0)
			{
				if (s.shown == view::power)
					powerView::drawInspector(power, s.level, s.hovered, panel);
				else if (s.shown == view::heat)
					thermalView::drawInspector(heat, s.level, s.hovered, panel);
				else if (s.shown == view::liquid)
					fluidView::drawInspector(liquid, s.level, s.hovered, panel);
				else if (s.shown == view::items)
					logisticsView::drawInspector(items, s.level, s.hovered, panel);
			}
			label(ImVec2(13, 70), ImVec2(30, 18), std::to_string(s.level), true);

			if (s.shown == view::exterior)
				exteriorView::draw(window, st, hoveredDeck);
			else
				st.drawLayer(window, s.level);
			if (s.building && !clickingTab)
				buildBar.drawGhost(window, level, s.hovered, &textures);
			if (s.blueprints)
				blueprintMaker.drawGhost(window, st, textures, s.hovered, true);

			ImGui::Render();
			decoder.apply(ImGui::GetDrawData(), 0.016f);
			ImGui::SFML::Render(screen.begin(window));
			screen.present(window, 1.f);
			if (frame == frames - 1)
			{
				sf::Texture capture;
				capture.create(screenWidth, screenHeight);
				capture.update(window);
				const std::string file = outputFolder + "/" + s.name + ".png";
				if (capture.copyToImage().saveToFile(file))
					std::cout << "saved " << file << std::endl;
				else
					std::cout << "cannot save " << file << std::endl;
			}
			window.display();
			sf::sleep(sf::milliseconds(15));   //real time passes for the screen's power-on
		}
		if (s.blueprints)
		{
			blueprintMaker.stop();
			blueprintMaker.togglePanel();
		}
	}
	ImGui::SFML::Shutdown();
	return 0;
}
