//StationBuilder's lighting on its own: the README's station drawn by the game's lighting (StationBuilder/lighting.h),
//with its simulation running, for trying the lighting out and tuning it without the rest of the game. it began as the
//2D lighting prototype; the lighting itself now lives in the game.
//  - Shift+F9 or the panel: the lighting's settings, as in the game
//  - emergency power: the generators switch off (the lamps and heaters lose their power, the lamps pulse red)
//
//build:  cmake --build build/debug --target Lighting2DDemo
//run it from the StationBuilder folder (where the game's art, content and fonts are), or with VS Code's "2D lighting
//prototype (SFML)". "shots <folder>" runs it hidden and saves a few pictures of a short tour.
//"normalmap <art file>..." writes the normal map the lighting makes for each art file beside it (<art>_normal.png), as a
//start to paint over, and stops (a map already there is left alone).
#include <SFML/Graphics.hpp>
#include <imgui.h>
#include <imgui-SFML.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
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
#include "blueprint.h"
#include "lineArt.h"
#include "lighting.h"
#include "uiTheme.h"

namespace
{
	const unsigned screenWidth = 1920, screenHeight = 1080;

	//---- the README's station (a copy of tools/readmeShots.cpp's) ----
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
	void placeBlueprint(isometricLevel& _level, const std::vector<blueprint>& _library, const char* _name, int _x, int _y)
	{
		for (const blueprint& b : _library)
			if (b.name == _name)
				blueprints::paste(_level, b, sf::Vector2i(_x, _y));
	}
	void buildStation(station& _station, const std::vector<blueprint>& _library)
	{
		decks::startingStation(_station);
		isometricLevel& lowest = *_station.getIsoLevel(0);
		isometricLevel& middle = *_station.getIsoLevel(1);
		placeBlueprint(lowest, _library, "Iron smelting line", 19, 15);
		placeBlueprint(lowest, _library, "Ice to water", 19, 29);
		placeBlueprint(lowest, _library, "Waste recycling", 28, 29);
		placeBlueprint(lowest, _library, "Powered room", 12, 21);
		if (tileObject* crate = machineAt(lowest, 28, 30, "crate"))
			storage::put(*crate, *findMaterial("waste"), 400);
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
		put(lowest, 22, 28, "radiatorPanel");
		put(lowest, 30, 21, "solarArray");
		put(lowest, 29, 21, "cable");
		put(lowest, 29, 22, "cable");
		put(lowest, 31, 25, "dockingJetty");
		put(lowest, 29, 25, "tube");
		put(lowest, 28, 25, "crate");
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
		put(upper, 27, 22, "largeRadiator");
		_station.setCurrentLevel(0);
	}
}

int main(int argc, char** argv)
{
	if (argc > 2 && std::string(argv[1]) == "normalmap")
	{
		for (int i = 2; i < argc; i++)
		{
			std::string message;
			lighting::writeNormalMap(argv[i], false, message);
			std::cout << message << std::endl;
		}
		return 0;
	}
	const bool shots = argc > 2 && std::string(argv[1]) == "shots";
	const std::string shotFolder = shots ? argv[2] : "";

	sf::RenderWindow window(sf::VideoMode(screenWidth, screenHeight), "StationBuilder lighting", sf::Style::Default);
	window.setVerticalSyncEnabled(true);
	if (shots)
		window.setVisible(false);
	if (!ImGui::SFML::Init(window, false))
		return 2;
	ImGui::GetIO().IniFilename = nullptr;
	ImFontConfig crisp;
	crisp.OversampleH = 1;
	crisp.OversampleV = 1;
	crisp.PixelSnapH = true;
	ImFontAtlas* fonts = ImGui::GetIO().Fonts;
	ImFont* font = fonts->AddFontFromFileTTF(uiTheme::fontFile, uiTheme::fontSize(1.f), &crisp, fonts->GetGlyphRangesDefault());
	if (!font || !ImGui::SFML::UpdateFontTexture())
	{
		std::cout << "cannot load " << uiTheme::fontFile << ": run this from the StationBuilder folder" << std::endl;
		return 3;
	}
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowPadding = ImVec2(10, 10);
		style.FramePadding = ImVec2(6, 3);
		style.ItemSpacing = ImVec2(6, 5);
		uiTheme::apply();
	}

	//the station, its art, and the simulation (wired as main.cpp wires it)
	textureManager textures;
	content::load("content");
	content::loadArt(textures);
	std::vector<std::string> problems;
	const std::vector<blueprint> library = blueprints::loadAll(blueprints::folderName, problems);
	station st(&textures);
	buildStation(st, library);
	lineArt::update(st, textures);
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
	for (int i = 0; i < 600; i++)
		sim.step();

	lighting::renderer lighting;
	if (std::getenv("LIGHTING_OFF"))
		lighting.options.enabled = false;   //for timing the unlit drawing
	bool panelOpen = true, emergency = false;
	int deck = 0;
	float zoom = 0.8f;
	sf::Vector2f centre = st.getIsoLevel(0)->tileCentreWorld(sf::Vector2f(24.f, 24.f));
	sf::Clock frameClock;
	int frame = 0, shot = 0;
	double drawSeconds = 0.0;   //time spent in the lighting's draw (shots: printed at the end)
	int drawFrames = 0;

	auto setEmergency = [&](bool _on)
	{
		//every generator with a switch on every deck: off for an emergency, on again after
		emergency = _on;
		for (int lv = 0; lv < st.getLayerListSize(); lv++)
		{
			isometricLevel& level = *st.getIsoLevel(float(lv));
			for (tile& t : level.levelTiles)
				t.forEachObject([&](tileObject* _o) {
					if (_o->type && !_o->isPart() && _o->type->electrical.source)
						level.changeObjectSetting(_o, "on", _on ? 0.0 : 1.0);
				});
		}
	};

	while (window.isOpen())
	{
		const float dt = shots ? 1.f / 60.f : std::min(frameClock.restart().asSeconds(), 0.1f);
		sf::Event event;
		ImGuiIO& io = ImGui::GetIO();
		while (window.pollEvent(event))
		{
			ImGui::SFML::ProcessEvent(window, event);
			if (event.type == sf::Event::Closed)
				window.close();
			else if (event.type == sf::Event::MouseWheelScrolled && !io.WantCaptureMouse)
				zoom = std::clamp(zoom * std::pow(0.88f, event.mouseWheelScroll.delta), 0.2f, 3.f);
			else if (event.type == sf::Event::KeyPressed && !io.WantCaptureKeyboard)
			{
				if (event.key.code == sf::Keyboard::PageUp)
					deck = std::min(deck + 1, st.getLayerListSize() - 1);
				else if (event.key.code == sf::Keyboard::PageDown)
					deck = std::max(deck - 1, 0);
				else if (event.key.code == sf::Keyboard::L)
					setEmergency(!emergency);
				else if (event.key.code == sf::Keyboard::K)
					lighting.sparksSomewhere();
				else if (event.key.code == sf::Keyboard::F9)
				{
					if (event.key.shift)
						panelOpen = !panelOpen;
					else
						lighting.options.enabled = !lighting.options.enabled;
				}
			}
		}
		static sf::Vector2i lastMouse = sf::Mouse::getPosition(window);
		const sf::Vector2i mouse = sf::Mouse::getPosition(window);
		if (!shots && window.hasFocus() && !io.WantCaptureMouse && sf::Mouse::isButtonPressed(sf::Mouse::Right))
			centre -= sf::Vector2f(float(mouse.x - lastMouse.x), float(mouse.y - lastMouse.y)) * zoom;
		lastMouse = mouse;
		if (shots)
		{
			//the tour: the deck lit; without normal maps; without walls keeping light in; an emergency with sparks; the
			//steam and the smelter; the deck above
			isometricLevel& lowest = *st.getIsoLevel(0);
			if (frame == 40) lighting.options.normalMaps = false;
			if (frame == 80) { lighting.options.normalMaps = true; lighting.options.rooms = false; }
			if (frame == 120) { lighting.options.rooms = true; zoom = 0.45f; centre = lowest.tileCentreWorld(sf::Vector2f(15.f, 22.f)); setEmergency(true); }
			if (frame == 150) lighting.sparks(&lowest, sf::Vector2i(15, 22));
			if (frame == 175) { setEmergency(false); zoom = 0.5f; centre = lowest.tileCentreWorld(sf::Vector2f(22.5f, 21.f)); }
			if (frame == 235) { deck = 1; zoom = 0.6f; centre = lowest.tileCentreWorld(sf::Vector2f(24.f, 24.f)); }
			if (frame == 275) { deck = 2; zoom = 0.7f; }
			if (frame == 315) { deck = 0; zoom = 0.5f; centre = lowest.tileCentreWorld(sf::Vector2f(24.f, 23.f)); window.setSize(sf::Vector2u(1600, 900)); }
		}
		sf::View view(centre, sf::Vector2f(window.getSize()) * zoom);
		window.setView(view);
		sim.update(dt);
		power.update();

		ImGui::SFML::Update(window, sf::seconds(dt));
		lighting.drawPanel(panelOpen, 1.f, font);
		ImGui::SetNextWindowPos(ImVec2(float(window.getSize().x) - 12.f, 12.f), ImGuiCond_Always, ImVec2(1.f, 0.f));
		ImGui::SetNextWindowBgAlpha(0.88f);
		ImGui::Begin("bench", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
		ImGui::TextColored(uiTheme::bright(), "STATIONBUILDER LIGHTING");
		ImGui::TextDisabled("the game's lighting, the README's station");
		ImGui::Text("deck %d of %d", deck, st.getLayerListSize());
		ImGui::SameLine();
		if (ImGui::SmallButton("up")) deck = std::min(deck + 1, st.getLayerListSize() - 1);
		ImGui::SameLine();
		if (ImGui::SmallButton("down")) deck = std::max(deck - 1, 0);
		bool wantEmergency = emergency;
		if (ImGui::Checkbox("emergency power (L): the generators off", &wantEmergency))
			setEmergency(wantEmergency);
		ImGui::TextDisabled("wheel zooms, right drag moves, Page Up/Down decks");
		ImGui::TextDisabled("K sparks, F9 lighting, Shift+F9 its settings");
		ImGui::End();

		window.clear(sf::Color::Black);
		st.setCurrentLevel(deck);
		st.highlightAllTiles(sf::Color::White);
		lighting::deck shown;
		shown.level = st.getIsoLevel(float(deck));
		shown.levelIndex = deck;
		shown.power = &power;
		shown.machines = &machines;
		std::vector<lighting::deck> decks{ shown };
		if (shots && frame >= 275 && frame < 315)
		{
			//the last picture: tool highlights in the dark (the build cursor's red, a copy's cyan), and the deck below drawn
			//moved beside this one, as a docked ship is
			st.highlightTile(sf::Vector2f(23.f, 23.f), sf::Color::Red);
			st.highlightTile(sf::Vector2f(24.f, 24.f), sf::Color(120, 220, 255));
			lighting::deck beside;
			beside.level = st.getIsoLevel(1.f);
			beside.levelIndex = 1;
			beside.moved.translate(st.getIsoLevel(1.f)->tileCentreWorld(sf::Vector2f(9.f, 1.f)) - st.getIsoLevel(1.f)->tileCentreWorld(sf::Vector2f(1.f, 1.f)));
			beside.builtOnly = true;
			beside.power = &power;
			beside.machines = &machines;
			decks.push_back(beside);
		}
		sf::Clock drawClock;
		if (!lighting.draw(window, decks, dt, false, false))
			st.drawLayer(window, float(deck));
		drawSeconds += drawClock.getElapsedTime().asSeconds();
		drawFrames++;
		if (shots && frame >= 315)
		{
			//after a resize: a plain outline (not a sprite) round the generator's and a lamp's tiles must sit on them
			for (const sf::Vector2f t : { sf::Vector2f(24.f, 22.f), sf::Vector2f(25.f, 21.f) })
			{
				const sf::Vector2f c = st.getIsoLevel(0.f)->tileCentreWorld(t);
				sf::VertexArray outline(sf::LineStrip, 5);
				const sf::Vector2f corners[5] = { { -32.f, 0.f }, { 0.f, -16.f }, { 32.f, 0.f }, { 0.f, 16.f }, { -32.f, 0.f } };
				for (int i = 0; i < 5; i++)
					outline[size_t(i)] = sf::Vertex(c + corners[i], sf::Color::Cyan);
				window.draw(outline);
			}
		}
		window.setView(window.getDefaultView());
		ImGui::SFML::Render(window);
		const int shotFrames[] = { 30, 70, 110, 160, 230, 270, 310, 330 };
		if (shots && std::find(std::begin(shotFrames), std::end(shotFrames), frame) != std::end(shotFrames))
		{
			sf::Texture capture;
			capture.create(window.getSize().x, window.getSize().y);
			capture.update(window);
			capture.copyToImage().saveToFile(shotFolder + "/lighting2d_" + std::to_string(shot++) + ".png");
		}
		window.display();
		frame++;
		if (shots && frame > 330)
		{
			std::cout << "the lighting took " << 1000.0 * drawSeconds / std::max(drawFrames, 1) << " ms a frame on average" << std::endl;
			window.close();
		}
	}
	ImGui::SFML::Shutdown();
	return 0;
}
