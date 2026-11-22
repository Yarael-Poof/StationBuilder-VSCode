#pragma once
#include <SFML/Graphics.hpp>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

class isometricLevel;
class electricalNetwork;
class processingSystem;
class tileObject;
struct tileTypeInfo;
struct ImFont;

//the deck view, lit (lighting.cpp):
//  - items with a light (content: light = { ... }) light the room they are in: walls keep light in. lamps, heaters,
//    generators' status lights, machines' fires, docks' beacons
//  - each sprite's sides are lit by its normal map: a side facing a lamp is lit, the far side isn't
//  - the bright parts of the art glow; tool highlights (building, copying, the deck tools) glow too, to stay readable in
//    the dark
//  - steam and smoke rise from items that give them off (content: emits = { ... }); sparks fly from cables past their
//    insulation's rating and from a generator as it trips
//the simulation drives it: a lamp gives the light its power allows, a generator's light shows whether it is running,
//a machine's light follows how fast it works, and a lamp whose power fails pulses red.
//
//the art's maps: each item's art can have hand-made maps beside it, made in any paint program (content/README.md,
//"Lighting"):
//  - tiles/<art>_normal.png: the way each pixel faces, as a colour (red: right, green: up the picture, blue: out of the
//    picture, towards you)
//  - tiles/<art>_glow.png: what glows, in the colour it glows; clear everywhere else
//without them, the normal map is made from the art's outline (every item is a box seen from the game's angle) and the
//glow from its bright, coloured pixels. writeNormalMap saves the made one as a start to paint over.
//
//how it draws (all into pictures the size of the window): the deck's normals and heights and its rooms (again only
//when the view or the station changes), its colours and its glow (each frame, so highlights and shading show); a quad
//for each light adds its light to the pixels of its room; the colours times the light, plus the glow; particles; the
//glow blurred and added on top
namespace lighting
{
	struct settings
	{
		bool enabled = true;          //F9
		float ambient = 0.2f;         //light everywhere, 0..1 (1: the art as it is drawn)
		float viewAmbient = 0.8f;     //at least this while a view (power, heat, liquid, items) is shown, to read it by
		bool normalMaps = true;       //the sides lit by their normal maps (off: every pixel lit as if facing the light)
		bool rooms = true;            //walls keep light in its room (off: light reaches through walls)
		bool glow = true;
		float glowStrength = 1.f;
		bool particles = true;
		bool emergency = true;        //lamps whose power fails pulse red
	};

	//one deck to draw: the deck on screen, or the deck level with it on a vessel docked beside it (moved onto its grid)
	struct deck
	{
		isometricLevel* level = nullptr;
		int levelIndex = 0;                         //its index in its station (the networks' level numbers)
		sf::Transform moved;                        //from its own grid to the screen's (none for the deck on screen)
		bool builtOnly = false;                     //only what is built, not its blank tiles (a vessel beside)
		const electricalNetwork* power = nullptr;   //its vessel's networks: what drives its lights
		const processingSystem* machines = nullptr;
	};

	//the files the lighting looks for beside an item's art: "tiles/heater.png" -> "tiles/heater_normal.png", "_glow.png"
	std::string normalMapFile(const std::string& _artFile);
	std::string glowMapFile(const std::string& _artFile);
	//the normal map the lighting makes from a picture's outline (rgb: the way each pixel faces; alpha: the picture's
	//own, so it lines up with it in a paint program)
	sf::Image madeNormalMap(const sf::Image& _art);
	//saves the made normal map beside an art file (normalMapFile), as a start to paint over. false, and why in
	//_message, if the art won't load, or the map is there already and _replace is false
	bool writeNormalMap(const std::string& _artFile, bool _replace, std::string& _message);

	class renderer
	{
	public:
		settings options;

		//shaders and render textures work here
		bool available();
		//draws the decks lit into the window (with its view), in order: those behind the deck on screen first. _dt: real
		//seconds since the last frame; _paused: the simulation is paused (flicker and particles stop too); _viewOn: a
		//view (power, heat...) is shown. false if the lighting is off or can't work here: draw the decks the usual way
		bool draw(sf::RenderWindow& _window, const std::vector<deck>& _decks, float _dt, bool _paused, bool _viewOn);
		//a burst of sparks on a tile of a level (shown if that level is drawn)
		void sparks(const isometricLevel* _level, sf::Vector2i _tile);
		//sparks from a random cable of the deck on screen (the panel's test button)
		void sparksSomewhere();
		//the made maps again (after the art or its maps changed on disk)
		void forgetMaps() { maps.clear(); }
		int lightsDrawn() const { return lightCount; }
		int particleCount() const { return int(particles.size()); }
		//the settings (Shift+F9); _open closes it
		void drawPanel(bool& _open, float _scale, ImFont* _font);

	private:
		bool load();

		//a sprite's maps: its normals (rgb, the way each pixel faces; alpha, its height above the floor in px) and its glow
		struct artMaps
		{
			sf::Texture normals;
			sf::Texture glow;
			bool glows = false;
		};
		const artMaps& mapsFor(const tileObject& _object);

		//a thing that lights, and where steam or smoke comes from
		struct lightThing
		{
			tileObject* object = nullptr;
			sf::Vector2i tile;          //its tile (a big machine: its front tile, where it is drawn)
			sf::Vector2f floor;         //on its own grid: the floor point under its light
			int room = 0;               //its room on its deck
			float phase = 0.f;
		};
		struct ventThing
		{
			tileObject* object = nullptr;
			sf::Vector2f at;            //on its own grid
			float due = 0.f;
		};
		//what the lighting knows of a deck: its rooms (by tile index), its lights, vents and cables
		struct deckScan
		{
			std::uint64_t revision = 0;
			std::uint64_t generation = 0;
			std::vector<int> rooms;
			int roomCount = 0;
			std::vector<lightThing> lights;
			std::vector<ventThing> vents;
			std::vector<sf::Vector2i> cables;
		};
		deckScan& scanOf(isometricLevel& _level);
		//the room a sprite on a tile is seen in: a front wall's face is seen from the tile beyond it
		int roomOf(isometricLevel& _level, const deckScan& _scan, const tileObject& _object, sf::Vector2i _tile) const;

		struct particle
		{
			sf::Vector2f position, velocity;    //screen grid px, px/s
			float age = 0.f, life = 1.f, size0 = 4.f, size1 = 4.f, drag = 0.f, gravity = 0.f, floor = 1e9f;
			sf::Color from, to;
			bool additive = false, streak = false;
			const isometricLevel* level = nullptr;
		};
		void burst(sf::Vector2f _at, const isometricLevel* _level);

		bool loaded = false, failed = false;
		sf::Shader roomPass, normalPass, glowPass, blackPass, lightPass, composite, glowSource, blur, adder;
		sf::RenderTexture normals, rooms, colours, glows, lightMap, scene, glowA, glowB;
		sf::Vector2u size;
		sf::Texture levels;                     //256 x 1: each glowing thing's glow this frame (halved: up to 2)
		std::vector<sf::Uint8> levelPixels;
		sf::Texture dot;                        //a soft dot for particles
		std::map<std::pair<const sf::Texture*, const tileTypeInfo*>, artMaps> maps;
		std::uint64_t mapsGeneration = 0, mapsSeen = 0;
		std::map<const isometricLevel*, deckScan> scans;
		//what the cached pictures were drawn for: the normals and rooms, and the glow (those and the tool highlights)
		std::vector<float> drawnFor, glowDrawnFor;
		std::vector<particle> particles;
		std::mt19937 random{ 11 };
		float clock = 0.f;
		int lightCount = 0;
		//the flash of sparks: where, how long ago
		struct flash { const isometricLevel* level = nullptr; sf::Vector2f at; int room = 0; float age = 9.f; };
		std::vector<flash> flashes;
		std::vector<std::pair<const isometricLevel*, sf::Vector2i>> sparksWanted;
		bool randomSparksWanted = false;
		//generators that had tripped last frame (to spark as one trips), and how long until each hot cable sparks again
		std::map<std::pair<const electricalNetwork*, int>, bool> tripped;
		std::map<std::pair<const isometricLevel*, int>, float> cableDue;
		const isometricLevel* onScreen = nullptr;
	};
}
