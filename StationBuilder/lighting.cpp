#include "lighting.h"
#include "isometricLevel.h"
#include "tileCatalogue.h"
#include "electricalNetwork.h"
#include "tileNetwork.h"
#include "processing.h"
#include "uiTheme.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>

namespace
{
	//the floor's own units: screen x, and screen y doubled (the 2:1 diamonds undone). a 2 m tile's diagonal is 64 of
	//them, so a metre is about 22.6. a 3 m deck is 40 px high on screen, so a pixel of height is about 1.7 of them
	const float perMetre = 22.6f;
	const float heightScale = 1.7f;
	const float tallest = 160.f;         //px: how far above its floor point a lit pixel can be (a light's quad reaches this high)
	const int highlightRow = 255;        //the glow row for tool highlights (shown bright, not blurred)
	std::uint64_t mapsWritten = 0;       //normal maps written (by the editor): the made maps are read again

	//a normal in the picture's own frame (x right, y up the picture, z out of it towards you), as a colour
	sf::Color normalColour(float _x, float _y, float _z, sf::Uint8 _alpha)
	{
		auto c = [](float _v) { return sf::Uint8(std::lround(std::clamp((_v * 0.5f + 0.5f) * 255.f, 0.f, 255.f))); };
		return sf::Color(c(_x), c(_y), c(_z), _alpha);
	}

	//every item is a box seen from the game's angle (30 degrees down, turned 45), so a pixel's face and its height
	//follow from the picture's outline: the box's height is the opaque run up its leftmost (or rightmost) column, its
	//front corner is its lowest pixel, and the top face's lower edges rise from the front corner's top at half a pixel
	//a pixel. below them the left or right side. flat art (floors, cables, a lamp's pool of light) has no height, so it
	//is all top. _normals gets each pixel's face as a colour, _heights its height above the floor (px)
	void readOutline(const sf::Image& _art, sf::Image& _normals, std::vector<float>& _heights)
	{
		const int w = int(_art.getSize().x), h = int(_art.getSize().y);
		auto solid = [&](int _x, int _y) { return _art.getPixel(unsigned(_x), unsigned(_y)).a >= 128; };
		_normals.create(unsigned(w), unsigned(h), sf::Color(128, 128, 255, 0));
		_heights.assign(size_t(w) * size_t(h), 0.f);
		int bottom = -1, left = w, right = -1;
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
				if (solid(x, y))
				{
					bottom = std::max(bottom, y);
					left = std::min(left, x);
					right = std::max(right, x);
				}
		if (bottom < 0)
			return;
		float front = 0.f;
		int fronts = 0;
		for (int x = 0; x < w; x++)
			if (solid(x, bottom))
			{
				front += float(x);
				fronts++;
			}
		front /= float(std::max(fronts, 1));
		auto run = [&](int _x) {
			int n = 0;
			for (int y = 0; y < h; y++)
				n += solid(_x, y) ? 1 : 0;
			return n;
		};
		const float boxHeight = float(std::max(0, std::max(run(left), run(right)) - 2));
		const float frontTop = float(bottom) - boxHeight;
		//the faces, as seen: the top (up, tipped towards you by the view's 30 degrees), the left and right sides
		//(level, turned 45 degrees to either side)
		const float c30 = 0.8660f, s30 = 0.5f, side = 0.7071f;
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
			{
				const sf::Uint8 alpha = _art.getPixel(unsigned(x), unsigned(y)).a;
				if (alpha < 128)
					continue;
				const float dx = std::abs(float(x) - front);
				float height = boxHeight;
				sf::Color n = normalColour(0.f, c30, s30, alpha);
				if (float(y) > frontTop - dx * 0.5f + 0.5f)
				{
					n = normalColour(x < front ? -side : side, -side * s30, side * c30, alpha);
					height = std::clamp(float(bottom) - dx * 0.5f - float(y), 0.f, boxHeight);
				}
				_normals.setPixel(unsigned(x), unsigned(y), n);
				_heights[size_t(y) * size_t(w) + size_t(x)] = height;
			}
	}

	std::string besideArt(const std::string& _artFile, const char* _suffix)
	{
		const std::filesystem::path p(_artFile);
		return (p.parent_path() / (p.stem().string() + _suffix + p.extension().string())).generic_string();
	}

	//the art file a sprite's picture comes from, or "" when its picture is made (line pieces with their arms)
	std::string artFileOf(const tileObject& _object)
	{
		const tileTypeInfo* type = _object.type;
		if (!type || !type->drawArms.empty())
			return "";
		const int t = _object.turns();
		if (t > 0 && t <= int(type->turnArt.size()) && !type->turnArt[size_t(t) - 1].empty())
			return type->turnArt[size_t(t) - 1];
		return type->art;
	}

	//---- shaders (SFML: GLSL with the fixed-function vertex stage) ----
	//rooms: each sprite's solid pixels in its room's colour
	const char* roomShader = R"(
uniform sampler2D texture;
uniform vec4 id;
void main()
{
    if (texture2D(texture, gl_TexCoord[0].xy).a < 0.5) discard;
    gl_FragColor = id;
})";
	//normals: each sprite's normals and heights from its map (a sprite drawn mirrored: x turned round)
	const char* normalShader = R"(
uniform sampler2D texture;
uniform sampler2D normals;
uniform float mirrored;
void main()
{
    vec2 uv = gl_TexCoord[0].xy;
    if (texture2D(texture, uv).a < 0.5) discard;
    vec4 n = texture2D(normals, uv);
    if (mirrored > 0.5) n.r = 1.0 - n.r;
    gl_FragColor = n;
})";
	//glow: a sprite's glowing pixels with its row of glow levels in alpha (a tool highlight: all of it, tinted); else black
	const char* glowShader = R"(
uniform sampler2D texture;
uniform sampler2D glowMap;
uniform float row;           // its row / 255; 0: nothing on it glows
uniform float highlight;
void main()
{
    vec2 uv = gl_TexCoord[0].xy;
    vec4 c = texture2D(texture, uv);
    if (c.a < 0.5) discard;
    if (highlight > 0.5) { gl_FragColor = vec4(c.rgb * gl_Color.rgb, 1.0); return; }
    vec4 g = texture2D(glowMap, uv);
    if (row > 0.0 && g.a >= 0.5) gl_FragColor = vec4(g.rgb, row);
    else gl_FragColor = vec4(0.0);
})";
	//the glow of a sprite with nothing glowing on it: black, to hide what is behind it
	const char* blackShader = R"(
uniform sampler2D texture;
void main()
{
    if (texture2D(texture, gl_TexCoord[0].xy).a < 0.5) discard;
    gl_FragColor = vec4(0.0);
})";
	//a light: its quad lights the pixels of its room by their normals and heights. a pixel is put back to the floor
	//point under it (its height down the screen), into the floor's own units (y doubled), with its height up. its normal
	//turns from the picture's frame into that one (the view looks 30 degrees down)
	const char* lightShader = R"(
uniform sampler2D normals;
uniform sampler2D rooms;
uniform vec2 resolution;
uniform vec3 lightPos;       // the floor point under it (screen px, y down) and its height (floor units)
uniform vec3 colour;         // halved: the light picture holds up to 2
uniform float radius;        // floor units on screen
uniform vec2 room;           // its room, as two bytes
uniform float useRooms;
uniform float useNormals;
uniform float scale;         // screen px per world px
uniform float heightScale;
void main()
{
    vec2 uv = gl_FragCoord.xy / resolution;
    vec2 got = floor(texture2D(rooms, uv).rg * 255.0 + 0.5);
    if (useRooms > 0.5 && (abs(got.x - room.x) > 0.5 || abs(got.y - room.y) > 0.5)) discard;
    if (got.x + got.y < 0.5) discard;
    vec4 nh = texture2D(normals, uv);
    float h = nh.a * 255.0 * scale;
    vec2 p = vec2(gl_FragCoord.x, resolution.y - gl_FragCoord.y);
    vec3 P = vec3(p.x, (p.y + h) * 2.0, h * heightScale);
    vec3 L = vec3(lightPos.x, lightPos.y * 2.0, lightPos.z) - P;
    float d = length(L);
    if (d >= radius) discard;
    float fall = 1.0 - d / radius;
    fall *= fall;
    float ndl = 1.0;
    if (useNormals > 0.5)
    {
        vec3 s = nh.rgb * 2.0 - 1.0;
        vec3 n = normalize(vec3(s.x, -0.5 * s.y + 0.8660 * s.z, 0.8660 * s.y + 0.5 * s.z));
        ndl = max(dot(n, L / max(d, 0.001)), 0.0);
    }
    gl_FragColor = vec4(colour * fall * ndl, 1.0);
})";
	//the colours times the light, the glow added (its levels a row each in a 256 x 1 texture, halved)
	const char* compositeShader = R"(
uniform sampler2D texture;
uniform sampler2D light;
uniform sampler2D glow;
uniform sampler2D levels;
uniform vec3 ambient;
void main()
{
    vec2 uv = gl_TexCoord[0].xy;
    vec4 a = texture2D(texture, uv);
    vec3 l = texture2D(light, uv).rgb * 2.0;
    vec4 e = texture2D(glow, uv);
    float row = floor(e.a * 255.0 + 0.5);
    vec3 g = row > 0.5 ? e.rgb * texture2D(levels, vec2((row + 0.5) / 256.0, 0.5)).rgb * 2.0 : vec3(0.0);
    gl_FragColor = vec4(a.rgb * (ambient + l) + g, 1.0);
})";
	//the glow alone at its levels, for the blur (not the tool highlights)
	const char* glowSourceShader = R"(
uniform sampler2D texture;
uniform sampler2D levels;
void main()
{
    vec4 e = texture2D(texture, gl_TexCoord[0].xy);
    float row = floor(e.a * 255.0 + 0.5);
    vec3 g = row > 0.5 && row < 254.5 ? e.rgb * texture2D(levels, vec2((row + 0.5) / 256.0, 0.5)).rgb * 2.0 : vec3(0.0);
    gl_FragColor = vec4(g, 1.0);
})";
	const char* blurShader = R"(
uniform sampler2D texture;
uniform vec2 way;
void main()
{
    vec2 uv = gl_TexCoord[0].xy;
    vec3 c = texture2D(texture, uv).rgb * 0.227027;
    c += (texture2D(texture, uv + way).rgb + texture2D(texture, uv - way).rgb) * 0.1945946;
    c += (texture2D(texture, uv + way * 2.0).rgb + texture2D(texture, uv - way * 2.0).rgb) * 0.1216216;
    c += (texture2D(texture, uv + way * 3.0).rgb + texture2D(texture, uv - way * 3.0).rgb) * 0.054054;
    c += (texture2D(texture, uv + way * 4.0).rgb + texture2D(texture, uv - way * 4.0).rgb) * 0.016216;
    gl_FragColor = vec4(c, 1.0);
})";
	const char* addShader = R"(
uniform sampler2D texture;
uniform float strength;
void main()
{
    gl_FragColor = vec4(texture2D(texture, gl_TexCoord[0].xy).rgb * strength, 1.0);
})";

	//how things look over time (the same as the 3D prototype's)
	float pulse(float _t) { return 0.25f + 0.75f * (0.5f + 0.5f * std::sin(_t * 3.5f)); }
	bool blinkOn(float _t) { return std::fmod(_t, 1.f) < 0.5f; }
	float beacon(float _t) { return std::fmod(_t * 0.8f, 1.f) < 0.18f ? 1.f : 0.06f; }
	float fire(float _t) { return 1.f + 0.22f * std::sin(_t * 13.f) + 0.12f * std::sin(_t * 31.f + 1.7f) + 0.08f * std::sin(_t * 57.f + 0.4f); }

	sf::Color mixColour(sf::Color _a, sf::Color _b, float _t)
	{
		auto m = [&](sf::Uint8 _x, sf::Uint8 _y) { return sf::Uint8(std::clamp(int(_x + (int(_y) - int(_x)) * _t), 0, 255)); };
		return sf::Color(m(_a.r, _b.r), m(_a.g, _b.g), m(_a.b, _b.b), m(_a.a, _b.a));
	}

	//a tool's highlight (a coloured tint), not the power view's shading (greys)
	bool highlighted(sf::Color _c)
	{
		const int hi = std::max({ _c.r, _c.g, _c.b }), lo = std::min({ _c.r, _c.g, _c.b });
		return hi > 0 && float(hi - lo) / float(hi) > 0.25f;
	}
}

namespace lighting
{
	std::string normalMapFile(const std::string& _artFile) { return besideArt(_artFile, "_normal"); }
	std::string glowMapFile(const std::string& _artFile) { return besideArt(_artFile, "_glow"); }

	sf::Image madeNormalMap(const sf::Image& _art)
	{
		sf::Image normals;
		std::vector<float> heights;
		readOutline(_art, normals, heights);
		return normals;
	}

	bool writeNormalMap(const std::string& _artFile, bool _replace, std::string& _message)
	{
		sf::Image art;
		if (_artFile.empty() || !art.loadFromFile(_artFile))
		{
			_message = "can't load the art \"" + _artFile + "\"";
			return false;
		}
		const std::string file = normalMapFile(_artFile);
		if (!_replace && std::filesystem::exists(file))
		{
			_message = file + " is there already";
			return false;
		}
		if (!madeNormalMap(art).saveToFile(file))
		{
			_message = "can't write " + file;
			return false;
		}
		_message = "wrote " + file + ": paint over it, then F5";
		mapsWritten++;
		return true;
	}

	bool renderer::available()
	{
		if (!loaded && !failed)
			failed = !load();
		return loaded;
	}

	bool renderer::load()
	{
		if (!sf::Shader::isAvailable())
		{
			std::cout << "[LIGHTING] shaders aren't available here: the decks are drawn unlit" << std::endl;
			return false;
		}
		const std::pair<sf::Shader*, const char*> shaders[] = { { &roomPass, roomShader }, { &normalPass, normalShader },
			{ &glowPass, glowShader }, { &blackPass, blackShader }, { &lightPass, lightShader }, { &composite, compositeShader }, { &glowSource, glowSourceShader },
			{ &blur, blurShader }, { &adder, addShader } };
		for (const auto& [shader, source] : shaders)
		{
			if (!shader->loadFromMemory(source, sf::Shader::Fragment))
			{
				std::cout << "[LIGHTING] a shader didn't compile: the decks are drawn unlit" << std::endl;
				return false;
			}
		}
		levels.create(256, 1);
		levelPixels.assign(256 * 4, 0);
		//a soft dot for the particles
		const unsigned n = 64;
		sf::Image image;
		image.create(n, n, sf::Color::Transparent);
		for (unsigned y = 0; y < n; y++)
			for (unsigned x = 0; x < n; x++)
			{
				const float dx = (x + 0.5f - n * 0.5f) / (n * 0.5f), dy = (y + 0.5f - n * 0.5f) / (n * 0.5f);
				float a = std::clamp(1.f - std::sqrt(dx * dx + dy * dy), 0.f, 1.f);
				a = a * a * (3.f - 2.f * a);
				image.setPixel(x, y, sf::Color(255, 255, 255, sf::Uint8(a * 255.f)));
			}
		dot.loadFromImage(image);
		dot.setSmooth(true);
		loaded = true;
		return true;
	}

	const renderer::artMaps& renderer::mapsFor(const tileObject& _object)
	{
		const sf::Texture* texture = _object.getTexture();
		const auto key = std::make_pair(texture, _object.type);
		auto found = maps.find(key);
		if (found != maps.end())
			return found->second;
		artMaps& m = maps[key];
		const sf::Image art = texture->copyToImage();
		const sf::Vector2u size = art.getSize();
		//the normals: hand-made (tiles/<art>_normal.png) or made from the outline; the heights always from the outline
		sf::Image normalImage;
		std::vector<float> heights;
		readOutline(art, normalImage, heights);
		const std::string artFile = artFileOf(_object);
		sf::Image handMade;
		if (!artFile.empty() && std::filesystem::exists(normalMapFile(artFile)) && handMade.loadFromFile(normalMapFile(artFile)))
		{
			if (handMade.getSize() == size)
				normalImage = handMade;
			else
				std::cout << "[LIGHTING] " << normalMapFile(artFile) << " isn't the size of its art: left out" << std::endl;
		}
		for (unsigned y = 0; y < size.y; y++)
			for (unsigned x = 0; x < size.x; x++)
			{
				sf::Color c = normalImage.getPixel(x, y);
				c.a = sf::Uint8(std::min(heights[size_t(y) * size.x + x], 255.f));
				normalImage.setPixel(x, y, c);
			}
		m.normals.loadFromImage(normalImage);
		//the glow: hand-made (tiles/<art>_glow.png), or the art's bright pixels that are coloured or near white
		sf::Image glowImage;
		glowImage.create(size.x, size.y, sf::Color::Transparent);
		const lightProperties* light = _object.type ? &_object.type->light : nullptr;
		if (!artFile.empty() && std::filesystem::exists(glowMapFile(artFile)) && handMade.loadFromFile(glowMapFile(artFile)))
		{
			if (handMade.getSize() == size)
				glowImage = handMade;
			else
				std::cout << "[LIGHTING] " << glowMapFile(artFile) << " isn't the size of its art: left out" << std::endl;
		}
		else if (light && light->gives && light->glow > 0.0)
		{
			for (unsigned y = 0; y < size.y; y++)
				for (unsigned x = 0; x < size.x; x++)
				{
					const sf::Color c = art.getPixel(x, y);
					if (c.a < 128)
						continue;
					const float hi = std::max({ c.r, c.g, c.b }) / 255.f, lo = std::min({ c.r, c.g, c.b }) / 255.f;
					const float saturation = hi > 0.f ? (hi - lo) / hi : 0.f;
					if (hi >= float(light->glow) && (saturation >= 0.4f || hi >= 0.92f))
						glowImage.setPixel(x, y, sf::Color(c.r, c.g, c.b, 255));
				}
		}
		for (unsigned y = 0; y < size.y && !m.glows; y++)
			for (unsigned x = 0; x < size.x && !m.glows; x++)
				m.glows = glowImage.getPixel(x, y).a >= 128;
		m.glow.loadFromImage(glowImage);
		return m;
	}

	int renderer::roomOf(isometricLevel& _level, const deckScan& _scan, const tileObject& _object, sf::Vector2i _tile) const
	{
		sf::Vector2i at = _tile;
		if (_object.layer == tileLayer::wall && (_object.wallEdge == 2 || _object.wallEdge == 3))
		{
			const sf::Vector2i beyond = _object.wallEdge == 2 ? sf::Vector2i(_tile.x, _tile.y + 1) : sf::Vector2i(_tile.x + 1, _tile.y);
			if (_level.isInLevel(beyond) && _level.inArea(beyond))
				at = beyond;
		}
		return _scan.rooms[size_t(_level.coords2TileIndex(at.x, at.y))];
	}

	renderer::deckScan& renderer::scanOf(isometricLevel& _level)
	{
		deckScan& s = scans[&_level];
		if (s.revision == _level.getRevision() && s.generation == catalogueGeneration() && !s.rooms.empty())
			return s;
		s = deckScan();
		s.revision = _level.getRevision();
		s.generation = catalogueGeneration();
		const int w = int(_level.getSizeW()), h = int(_level.getSizeH());
		auto wallOn = [&](int _x, int _y, int _edge) {
			if (!_level.isInLevel(sf::Vector2i(_x, _y)))
				return false;
			bool found = false;
			_level.levelTiles[size_t(_level.coords2TileIndex(_x, _y))].forEachObject([&](tileObject* _o) {
				if (_o->layer == tileLayer::wall && _o->wallEdge == _edge)
					found = true;
			});
			return found;
		};
		//rooms: tiles joined to their neighbours where no wall stands between them, floors with floors and open tiles with
		//open tiles (walls: 0 UL the -x side, 1 UR -y, 2 LL +y, 3 LR +x)
		s.rooms.assign(_level.levelTiles.size(), 0);
		for (int y0 = 1; y0 <= h; y0++)
			for (int x0 = 1; x0 <= w; x0++)
			{
				const int start = _level.coords2TileIndex(x0, y0);
				if (s.rooms[size_t(start)] != 0 || !_level.inArea(sf::Vector2i(x0, y0)))
					continue;
				const bool floored = _level.hasFloor(sf::Vector2i(x0, y0));
				const int room = ++s.roomCount;
				s.rooms[size_t(start)] = room;
				std::vector<sf::Vector2i> todo{ { x0, y0 } };
				while (!todo.empty())
				{
					const sf::Vector2i t = todo.back();
					todo.pop_back();
					const struct { int dx, dy, mine, theirs; } ways[4] = { { 1, 0, 3, 0 }, { -1, 0, 0, 3 }, { 0, 1, 2, 1 }, { 0, -1, 1, 2 } };
					for (const auto& way : ways)
					{
						const sf::Vector2i n(t.x + way.dx, t.y + way.dy);
						if (!_level.isInLevel(n) || !_level.inArea(n))
							continue;
						const int i = _level.coords2TileIndex(n.x, n.y);
						if (s.rooms[size_t(i)] != 0 || _level.hasFloor(n) != floored)
							continue;
						if (wallOn(t.x, t.y, way.mine) || wallOn(n.x, n.y, way.theirs))
							continue;
						s.rooms[size_t(i)] = room;
						todo.push_back(n);
					}
				}
			}
		//what lights, gives off steam or smoke, and where sparks can fly
		std::uniform_real_distribution<float> phase(0.f, 6.283f);
		for (int y = 1; y <= h; y++)
			for (int x = 1; x <= w; x++)
			{
				if (!_level.inArea(sf::Vector2i(x, y)))
					continue;
				const int index = _level.coords2TileIndex(x, y);
				_level.levelTiles[size_t(index)].forEachObject([&](tileObject* _o) {
					if (!_o->type || _o->isPart())
						return;
					const tileTypeInfo& type = *_o->type;
					if (type.electrical.conductor)
						s.cables.push_back({ x, y });
					if (type.emits.emits())
						s.vents.push_back({ _o, sf::Vector2f(), 0.f });
					if (!type.light.gives)
						return;
					lightThing l;
					l.object = _o;
					l.tile = { x, y };
					l.phase = phase(random);
					l.room = s.rooms[size_t(index)];
					//over its footprint's middle; a docking point's at its dock
					const bool machine = footprint::isMachine(type);
					const sf::Vector2i size = machine ? footprint::turnedSize(type, _o->turns()) : sf::Vector2i(1, 1);
					const sf::Vector2i origin = machine ? _o->origin : sf::Vector2i(x, y);
					l.floor = _level.tileCentreWorld(sf::Vector2f(origin.x + (size.x - 1) * 0.5f, origin.y + (size.y - 1) * 0.5f));
					sf::Vector2i dockTile;
					int face = 0;
					if (shipParts::placedDock(*_o, sf::Vector2i(x, y), dockTile, face) && _level.isInLevel(dockTile))
					{
						l.floor = _level.tileCentreWorld(sf::Vector2f(dockTile));
						l.room = s.rooms[size_t(_level.coords2TileIndex(dockTile.x, dockTile.y))];
					}
					s.lights.push_back(l);
				});
			}
		//where steam and smoke come from: a place in the art (its top, unless the item says), through the sprite's own
		//placing (mirrored sprites included)
		for (ventThing& v : s.vents)
		{
			const emitProperties& e = v.object->type->emits;
			sf::Vector2f at = e.at;
			if (at.x < 0.f || at.y < 0.f)
			{
				const sf::Vector2u size = v.object->getTexture() ? v.object->getTexture()->getSize() : sf::Vector2u(64, 64);
				at = sf::Vector2f(size.x * 0.5f, 0.f);
				if (const sf::Texture* t = v.object->getTexture())
				{
					//the art's top at its middle column
					const sf::Image art = t->copyToImage();
					for (unsigned y = 0; y < size.y; y++)
						if (art.getPixel(size.x / 2, y).a >= 128)
						{
							at.y = float(y) + 2.f;
							break;
						}
				}
			}
			v.at = v.object->getTransform().transformPoint(at);
		}
		return s;
	}

	void renderer::burst(sf::Vector2f _at, const isometricLevel* _level)
	{
		auto uniform = [&](float _a, float _b) { return std::uniform_real_distribution<float>(_a, _b)(random); };
		const int count = int(uniform(50.f, 90.f));
		for (int i = 0; i < count; i++)
		{
			particle p;
			p.position = _at;
			const float a = uniform(0.f, 6.283f), up = uniform(0.1f, 1.f), speed = uniform(60.f, 170.f);
			p.velocity = sf::Vector2f(std::cos(a) * speed * (1.f - up * 0.5f), std::sin(a) * speed * 0.5f * (1.f - up * 0.5f) - up * speed * 1.3f);
			p.life = uniform(0.35f, 1.1f);
			p.size0 = uniform(2.5f, 4.f);
			p.size1 = 1.f;
			p.gravity = 420.f;
			p.drag = 0.4f;
			p.floor = _at.y + uniform(-6.f, 6.f);
			p.from = sf::Color(255, 252, 220, 255);
			p.to = sf::Color(255, 110, 30, 0);
			p.additive = true;
			p.streak = true;
			p.level = _level;
			particles.push_back(p);
		}
	}

	void renderer::sparks(const isometricLevel* _level, sf::Vector2i _tile)
	{
		sparksWanted.push_back({ _level, _tile });
	}

	void renderer::sparksSomewhere()
	{
		randomSparksWanted = true;
	}

	bool renderer::draw(sf::RenderWindow& _window, const std::vector<deck>& _decks, float _dt, bool _paused, bool _viewOn)
	{
		if (!options.enabled || _decks.empty() || !available())
			return false;
		const float dt = _paused ? 0.f : std::min(_dt, 0.1f);
		clock += dt;
		auto uniform = [&](float _a, float _b) { return std::uniform_real_distribution<float>(_a, _b)(random); };

		//the pictures the size of the window
		if (_window.getSize() != size)
		{
			size = _window.getSize();
			for (sf::RenderTexture* t : { &normals, &rooms, &colours, &glows, &lightMap, &scene })
				t->create(size.x, size.y);
			for (sf::RenderTexture* t : { &glowA, &glowB })
			{
				t->create(std::max(size.x / 2, 1u), std::max(size.y / 2, 1u));
				t->setSmooth(true);
			}
			drawnFor.clear();
			glowDrawnFor.clear();
		}
		if (mapsGeneration != catalogueGeneration() || mapsSeen != mapsWritten)
		{
			//new content (F5, or the editor saved), or a map written: the art and its maps may have changed
			maps.clear();
			mapsGeneration = catalogueGeneration();
			mapsSeen = mapsWritten;
		}
		const sf::View view = _window.getView();
		const float scale = float(size.x) / view.getSize().x;   //screen px per world px
		const sf::FloatRect seen(view.getCenter() - view.getSize() * 0.5f, view.getSize());
		onScreen = _decks.front().level;
		for (const deck& d : _decks)
			if (d.moved == sf::Transform::Identity && !d.builtOnly)
				onScreen = d.level;

		//each deck's scan, and where its rooms start among all the decks' (rooms are numbered across them)
		std::vector<deckScan*> deckScans;
		std::vector<int> roomBase;
		int roomTotal = 0;
		for (const deck& d : _decks)
		{
			deckScans.push_back(&scanOf(*d.level));
			roomBase.push_back(roomTotal);
			roomTotal += deckScans.back()->roomCount;
		}

		//every sprite drawn, in the game's order, as it lands on the screen's grid
		auto eachSprite = [&](auto&& _visit) {
			for (size_t k = 0; k < _decks.size(); k++)
			{
				const deck& d = _decks[k];
				isometricLevel& level = *d.level;
				const int w = int(level.getSizeW()), h = int(level.getSizeH());
				for (int y = 1; y <= h; y++)
					for (int x = 1; x <= w; x++)
					{
						if (!level.inArea(sf::Vector2i(x, y)))
							continue;
						tile& t = level.levelTiles[size_t(level.coords2TileIndex(x, y))];
						if (d.builtOnly && !t.hasAnything())
							continue;
						t.forEachObject([&](tileObject* _o) {
							if (!_o->getTexture() || _o->isPart() || (d.builtOnly && _o->layer == tileLayer::background))
								return;
							if (!d.moved.transformRect(_o->getGlobalBounds()).intersects(seen))
								return;
							_visit(k, *_o, sf::Vector2i(x, y));
						});
					}
			}
		};

		//---- the normals and the rooms: drawn again only when the view, the decks or the content change ----
		std::vector<float> key = { view.getCenter().x, view.getCenter().y, view.getSize().x, view.getSize().y, float(mapsGeneration), float(mapsSeen) };
		for (const deck& d : _decks)
		{
			key.push_back(float(reinterpret_cast<std::uintptr_t>(d.level) & 0xffffff));
			key.push_back(float(d.level->getRevision() & 0xffffff));
			const float* m = d.moved.getMatrix();
			key.insert(key.end(), m, m + 16);
		}
		if (key != drawnFor)
		{
			drawnFor = key;
			normals.setView(view);
			rooms.setView(view);
			normals.clear(sf::Color(128, 238, 191, 0));
			rooms.clear(sf::Color::Transparent);
			eachSprite([&](size_t _k, tileObject& _o, sf::Vector2i _tile) {
				const deck& d = _decks[_k];
				const int room = roomBase[_k] + roomOf(*d.level, *deckScans[_k], _o, _tile);
				roomPass.setUniform("id", sf::Glsl::Vec4(float(room % 256) / 255.f, float(room / 256) / 255.f, 1.f, 1.f));
				sf::RenderStates r(sf::BlendNone, d.moved, nullptr, &roomPass);
				rooms.draw(_o, r);
				normalPass.setUniform("normals", mapsFor(_o).normals);
				normalPass.setUniform("mirrored", _o.getScale().x < 0.f ? 1.f : 0.f);
				r.shader = &normalPass;
				normals.draw(_o, r);
			});
			normals.display();
			rooms.display();
		}

		//---- the colours each frame (tool highlights and the power view's shading change them); the glow when the view,
		//the decks or the tool highlights change ----
		//the glow levels' rows: the things that light on screen this frame, and the highlights (row 255)
		std::map<const tileObject*, int> rows;
		std::vector<std::pair<size_t, const lightThing*>> onScreenLights;
		for (size_t k = 0; k < _decks.size(); k++)
			for (const lightThing& l : deckScans[k]->lights)
				if (int(rows.size()) < highlightRow - 1 && _decks[k].moved.transformRect(l.object->getGlobalBounds()).intersects(seen))
				{
					rows[l.object] = int(rows.size()) + 1;
					onScreenLights.push_back({ k, &l });
				}
		colours.setView(view);
		colours.clear(sf::Color::Black);
		std::uint64_t highlights = 1469598103934665603ull;   //which sprites carry a tool's highlight, and its colour
		eachSprite([&](size_t _k, tileObject& _o, sf::Vector2i) {
			colours.draw(_o, sf::RenderStates(_decks[_k].moved));
			if (highlighted(_o.getColor()))
			{
				highlights = (highlights ^ std::uint64_t(reinterpret_cast<std::uintptr_t>(&_o))) * 1099511628211ull;
				highlights = (highlights ^ std::uint64_t(_o.getColor().toInteger())) * 1099511628211ull;
			}
		});
		colours.display();
		std::vector<float> glowKey = drawnFor;
		glowKey.insert(glowKey.end(), { float(highlights & 0xffffff), float((highlights >> 24) & 0xffffff), float(highlights >> 48) });
		if (glowKey != glowDrawnFor)
		{
			glowDrawnFor = glowKey;
			glows.setView(view);
			glows.clear(sf::Color::Transparent);
			eachSprite([&](size_t _k, tileObject& _o, sf::Vector2i) {
				const sf::RenderStates states(sf::BlendNone, _decks[_k].moved, nullptr, nullptr);
				const bool lit = highlighted(_o.getColor());
				const auto row = rows.find(&_o);
				const artMaps* m = row != rows.end() || lit ? &mapsFor(_o) : nullptr;
				if (!lit && (!m || !m->glows))
				{
					//nothing on it glows: black
					sf::RenderStates black = states;
					black.shader = &blackPass;
					glows.draw(_o, black);
					return;
				}
				glowPass.setUniform("glowMap", m->glow);
				glowPass.setUniform("row", !lit ? float(row->second) / 255.f : 0.f);
				glowPass.setUniform("highlight", lit ? 1.f : 0.f);
				sf::RenderStates glowing = states;
				glowing.shader = &glowPass;
				glows.draw(_o, glowing);
			});
			glows.display();
		}

		//---- this frame's lights, from the simulation ----
		struct onLight { sf::Vector2f floor; float lift; sf::Vector3f colour; float reach; int room; };
		std::vector<onLight> lights;
		std::fill(levelPixels.begin(), levelPixels.end(), sf::Uint8(0));
		auto setLevel = [&](int _row, sf::Vector3f _level) {
			auto byte = [](float _v) { return sf::Uint8(std::clamp(_v * 0.5f * 255.f, 0.f, 255.f)); };   //halved: up to 2
			levelPixels[size_t(_row) * 4] = byte(_level.x);
			levelPixels[size_t(_row) * 4 + 1] = byte(_level.y);
			levelPixels[size_t(_row) * 4 + 2] = byte(_level.z);
			levelPixels[size_t(_row) * 4 + 3] = 255;
		};
		setLevel(highlightRow, sf::Vector3f(0.85f, 0.85f, 0.85f));
		//per deck: each electrical device's share of its rated power (by its machine), whether each generator runs,
		//and the networks that have a generator (a lamp on one of those with no power has had its power fail)
		struct deviceState { float share = 0.f; bool running = false; bool failed = false; };
		std::vector<std::map<const tileObject*, deviceState>> devicesOf(_decks.size());
		std::vector<std::map<const tileObject*, float>> speedsOf(_decks.size());
		for (size_t k = 0; k < _decks.size(); k++)
		{
			const deck& d = _decks[k];
			if (d.machines)
				for (const processorState& state : d.machines->machines())
					if (state.machine)
						speedsOf[k][&state.machine->machine()] = float(state.speed);
			if (!d.power)
				continue;
			const electricalNetwork& power = *d.power;
			std::set<int> withSource;
			for (const electricalDevice& device : power.devices())
				if (device.isSource)
					withSource.insert(device.island);
			for (int i = 0; i < int(power.devices().size()); i++)
			{
				const electricalDevice& device = power.devices()[size_t(i)];
				const networkNode& node = power.network().nodes()[size_t(device.node)];
				if (node.level != d.levelIndex || node.tileIndex >= int(d.level->levelTiles.size()))
					continue;
				//the device is the objectOnNode-th electrical object on its tile (as powerView finds it)
				tileObject* sprite = nullptr;
				int electricalIndex = 0;
				for (tileObject* object : d.level->levelTiles[size_t(node.tileIndex)].allObjects())
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
					continue;
				deviceState& s = devicesOf[k][&sprite->machine()];
				if (device.isSource)
				{
					const generatorState* state = power.generatorStatus(i);
					const bool trip = state && state->tripped;
					s.running = device.switchedOn && !trip;
					//a generator tripping throws sparks
					const auto key = std::make_pair(&power, node.level * 1000000 + node.tileIndex * 16 + device.key.ordinal);
					if (trip && !tripped[key] && options.particles)
						sparksWanted.push_back({ d.level, node.tile });
					tripped[key] = trip;
					continue;
				}
				const double rated = electrical::ratedLoadWatts(device.type->electrical);
				s.share = std::max(s.share, float(rated > 0.0 ? std::clamp(device.power.real() / rated, 0.0, 1.2) : 0.0));
				s.failed = withSource.count(device.island) > 0;
			}
			//cables past their insulation's rating spark now and then, more often the hotter they are
			for (const networkNode& node : power.network().nodes())
			{
				if (node.level != d.levelIndex || node.conductors == 0)
					continue;
				const double celsius = power.cableTemperature(int(&node - power.network().nodes().data()));
				if (!std::isfinite(celsius) || celsius < thermalEffects::cableHot)
					continue;
				float& due = cableDue[{ d.level, node.tileIndex }];
				due -= dt;
				if (due <= 0.f)
				{
					if (options.particles)
						sparksWanted.push_back({ d.level, node.tile });
					const double heat = std::clamp((celsius - thermalEffects::cableHot) / (thermalEffects::cableOverheating - thermalEffects::cableHot), 0.0, 2.0);
					due = uniform(0.4f, 1.6f) * float(3.0 / (0.3 + heat));
				}
			}
		}
		for (const auto& [k, l] : onScreenLights)
		{
			const deck& d = _decks[k];
			const lightProperties& p = l->object->type->light;
			const tileObject* machine = &l->object->machine();
			const auto device = devicesOf[k].find(machine);
			const deviceState state = device != devicesOf[k].end() ? device->second : deviceState();
			float on = 1.f;
			bool red = false;
			switch (p.follows)
			{
			case lightDriver::power:
				on = state.share;
				if (on < 0.02f && p.emergency && options.emergency && state.failed)
					red = true;
				break;
			case lightDriver::running:
				on = state.running ? 1.f : 0.f;
				red = !state.running && device != devicesOf[k].end();
				break;
			case lightDriver::working:
			{
				const auto speed = speedsOf[k].find(machine);
				const float s = speed != speedsOf[k].end() ? speed->second : 0.f;
				on = s > 0.f ? 0.3f + 0.7f * s : 0.1f;
				break;
			}
			case lightDriver::always:
				on = 1.f;
				break;
			case lightDriver::blink:
				on = beacon(clock);
				break;
			}
			if (p.flicker > 0.0)
				on *= float(1.0 - p.flicker) + float(p.flicker) * fire(clock + l->phase);
			sf::Vector3f glow(1.3f * on, 1.3f * on, 1.3f * on);
			const sf::Vector3f own(p.colour.r / 255.f, p.colour.g / 255.f, p.colour.b / 255.f);
			sf::Vector3f colour = own * float(p.brightness) * on;
			float reach = float(p.reach);
			if (red)
			{
				//a lamp on its emergency battery, or a generator that has stopped: red, pulsing or blinking
				const float r = p.follows == lightDriver::power ? pulse(clock) : (blinkOn(clock) ? 1.f : 0.f);
				glow = sf::Vector3f(1.6f, 0.12f, 0.08f) * r;
				colour = sf::Vector3f(1.f, 0.1f, 0.06f) * (p.follows == lightDriver::power ? 1.5f * r : 0.8f * r);
				reach = p.follows == lightDriver::power ? std::min(reach, 8.f) : std::min(reach, 3.5f);
			}
			setLevel(rows[l->object], glow);
			if (colour.x + colour.y + colour.z > 0.003f && p.brightness > 0.0)
				lights.push_back({ d.moved.transformPoint(l->floor), float(p.height), colour, reach, roomBase[k] + l->room });
		}

		//sparks asked for (by the panel, a hot cable, a tripped generator), with a flash that lights their room
		if (randomSparksWanted)
		{
			randomSparksWanted = false;
			for (size_t k = 0; k < _decks.size(); k++)
				if (_decks[k].level == onScreen && !deckScans[k]->cables.empty())
					sparksWanted.push_back({ onScreen, deckScans[k]->cables[size_t(random() % deckScans[k]->cables.size())] });
		}
		for (const auto& [level, at] : sparksWanted)
			for (size_t k = 0; k < _decks.size(); k++)
			{
				isometricLevel& here = *_decks[k].level;
				if (&here != level || !here.isInLevel(at))
					continue;
				const sf::Vector2f where = _decks[k].moved.transformPoint(here.tileCentreWorld(sf::Vector2f(at)));
				if (options.particles)
					burst(where, level);
				flashes.push_back({ level, where, deckScans[k]->rooms[size_t(here.coords2TileIndex(at.x, at.y))], 0.f });
			}
		sparksWanted.clear();
		for (flash& f : flashes)
		{
			f.age += dt;
			for (size_t k = 0; k < _decks.size(); k++)
				if (_decks[k].level == f.level && f.age < 0.35f)
				{
					const float fade = 1.f - f.age / 0.35f;
					lights.push_back({ f.at, 8.f, sf::Vector3f(1.f, 0.85f, 0.55f) * (3.5f * fade * fade * uniform(0.6f, 1.f)), 6.f, roomBase[k] + f.room });
				}
		}
		flashes.erase(std::remove_if(flashes.begin(), flashes.end(), [](const flash& _f) { return _f.age >= 0.35f; }), flashes.end());
		levels.update(levelPixels.data());
		lightCount = int(lights.size());

		//---- the light picture: a quad a light, adding ----
		lightMap.setView(view);
		lightMap.clear(sf::Color::Black);
		lightPass.setUniform("normals", normals.getTexture());
		lightPass.setUniform("rooms", rooms.getTexture());
		lightPass.setUniform("resolution", sf::Glsl::Vec2(float(size.x), float(size.y)));
		lightPass.setUniform("useRooms", options.rooms ? 1.f : 0.f);
		lightPass.setUniform("useNormals", options.normalMaps ? 1.f : 0.f);
		lightPass.setUniform("scale", scale);
		lightPass.setUniform("heightScale", heightScale);
		for (const onLight& l : lights)
		{
			const float r = l.reach * perMetre;
			const sf::FloatRect area(l.floor.x - r, l.floor.y - r * 0.5f - tallest, 2.f * r, r + tallest);
			if (!area.intersects(seen))
				continue;
			const sf::Vector2i at = lightMap.mapCoordsToPixel(l.floor, view);
			lightPass.setUniform("lightPos", sf::Glsl::Vec3(float(at.x), float(at.y), l.lift * heightScale * scale));
			lightPass.setUniform("colour", sf::Glsl::Vec3(l.colour.x * 0.5f, l.colour.y * 0.5f, l.colour.z * 0.5f));
			lightPass.setUniform("radius", r * scale);
			lightPass.setUniform("room", sf::Glsl::Vec2(float(l.room % 256), float(l.room / 256)));
			sf::VertexArray quad(sf::TriangleStrip, 4);
			quad[0].position = sf::Vector2f(area.left, area.top);
			quad[1].position = sf::Vector2f(area.left + area.width, area.top);
			quad[2].position = sf::Vector2f(area.left, area.top + area.height);
			quad[3].position = sf::Vector2f(area.left + area.width, area.top + area.height);
			lightMap.draw(quad, sf::RenderStates(sf::BlendAdd, sf::Transform::Identity, nullptr, &lightPass));
		}
		lightMap.display();

		//---- the scene: the colours lit, the glow added ----
		scene.setView(scene.getDefaultView());
		{
			const float a = _viewOn ? std::max(options.ambient, options.viewAmbient) : options.ambient;
			composite.setUniform("texture", sf::Shader::CurrentTexture);
			composite.setUniform("light", lightMap.getTexture());
			composite.setUniform("glow", glows.getTexture());
			composite.setUniform("levels", levels);
			composite.setUniform("ambient", sf::Glsl::Vec3(a, a, std::min(1.f, a * 1.06f)));
			scene.draw(sf::Sprite(colours.getTexture()), sf::RenderStates(sf::BlendNone, sf::Transform::Identity, nullptr, &composite));
		}

		//---- particles: steam and smoke from what gives them off, sparks ----
		std::set<const isometricLevel*> drawnLevels;
		for (const deck& d : _decks)
			drawnLevels.insert(d.level);
		if (options.particles)
		{
			for (size_t k = 0; k < _decks.size(); k++)
				for (ventThing& v : deckScans[k]->vents)
				{
					const emitProperties& e = v.object->type->emits;
					float rate = float(e.rate);
					const tileObject* machine = &v.object->machine();
					if (e.follows == lightDriver::working)
					{
						const auto speed = speedsOf[k].find(machine);
						rate *= speed != speedsOf[k].end() ? speed->second : 0.f;
					}
					else if (e.follows == lightDriver::power || e.follows == lightDriver::running)
					{
						const auto device = devicesOf[k].find(machine);
						rate *= device == devicesOf[k].end() ? 0.f : e.follows == lightDriver::power ? device->second.share : (device->second.running ? 1.f : 0.f);
					}
					if (rate <= 0.01f)
						continue;
					const bool smoke = e.kind == "smoke";
					v.due -= dt * rate;
					while (v.due <= 0.f)
					{
						v.due += smoke ? 1.f / 7.f : 1.f / 12.f;
						particle p;
						p.position = _decks[k].moved.transformPoint(v.at) + sf::Vector2f(uniform(-3.f, 3.f), 0.f);
						p.level = _decks[k].level;
						if (smoke)
						{
							p.velocity = sf::Vector2f(uniform(2.f, 10.f), uniform(-18.f, -12.f));
							p.life = uniform(3.5f, 5.f);
							p.size0 = 10.f;
							p.size1 = 50.f;
							p.drag = 0.25f;
							p.from = sf::Color(92, 86, 80, 170);
							p.to = sf::Color(140, 134, 128, 0);
						}
						else
						{
							p.velocity = sf::Vector2f(uniform(-8.f, 8.f), uniform(-30.f, -22.f));
							p.life = uniform(2.4f, 3.4f);
							p.size0 = 8.f;
							p.size1 = 34.f;
							p.drag = 0.5f;
							p.from = sf::Color(225, 230, 238, 110);
							p.to = sf::Color(205, 210, 220, 0);
						}
						particles.push_back(p);
					}
				}
		}
		for (particle& p : particles)
		{
			p.age += dt;
			p.velocity.y += p.gravity * dt;
			p.velocity *= std::exp(-p.drag * dt);
			p.position += p.velocity * dt;
			if (p.position.y > p.floor && p.velocity.y > 0.f)
			{
				p.position.y = p.floor;
				p.velocity.y = -p.velocity.y * 0.35f;
				p.velocity.x *= 0.6f;
			}
		}
		particles.erase(std::remove_if(particles.begin(), particles.end(), [&](const particle& _p) {
			return _p.age >= _p.life || !drawnLevels.count(_p.level) || !options.particles; }), particles.end());
		//soft dots (and sparks' streaks): the glowing ones, or steam and smoke
		sf::VertexArray smokeDots(sf::Triangles), sparkDots(sf::Triangles), sparkStreaks(sf::Lines);
		for (const particle& p : particles)
		{
			const float t = p.age / p.life;
			const sf::Color c = mixColour(p.from, p.to, t);
			const float s = (p.size0 + (p.size1 - p.size0) * t) * 0.5f;
			const sf::Vector2f corners[4] = { { -s, -s }, { s, -s }, { s, s }, { -s, s } };
			const sf::Vector2f uv[4] = { { 0.f, 0.f }, { 64.f, 0.f }, { 64.f, 64.f }, { 0.f, 64.f } };
			sf::VertexArray& dots = p.additive ? sparkDots : smokeDots;
			for (int k : { 0, 1, 2, 0, 2, 3 })
				dots.append(sf::Vertex(p.position + corners[k], c, uv[k]));
			if (p.streak)
			{
				sparkStreaks.append(sf::Vertex(p.position, c));
				sparkStreaks.append(sf::Vertex(p.position - p.velocity * 0.035f, sf::Color(c.r, c.g, c.b, 0)));
			}
		}
		scene.setView(view);
		{
			sf::RenderStates soft;
			soft.texture = &dot;
			scene.draw(smokeDots, soft);
			soft.blendMode = sf::BlendAdd;
			scene.draw(sparkDots, soft);
			scene.draw(sparkStreaks, sf::RenderStates(sf::BlendAdd));
		}
		scene.display();

		//---- the glow: what glows at its levels and the sparks, blurred ----
		if (options.glow)
		{
			glowA.setView(glowA.getDefaultView());
			glowA.clear(sf::Color::Black);
			glowSource.setUniform("texture", sf::Shader::CurrentTexture);
			glowSource.setUniform("levels", levels);
			sf::Sprite source(glows.getTexture());
			source.setScale(0.5f, 0.5f);
			glowA.draw(source, sf::RenderStates(&glowSource));
			glowA.setView(view);
			sf::RenderStates add;
			add.texture = &dot;
			add.blendMode = sf::BlendAdd;
			glowA.draw(sparkDots, add);
			glowA.draw(sparkStreaks, sf::RenderStates(sf::BlendAdd));
			glowA.display();
			const sf::Vector2f texel(1.f / float(glowA.getSize().x), 1.f / float(glowA.getSize().y));
			auto blurInto = [&](sf::RenderTexture& _from, sf::RenderTexture& _to, sf::Vector2f _way) {
				_to.setView(_to.getDefaultView());
				blur.setUniform("texture", sf::Shader::CurrentTexture);
				blur.setUniform("way", sf::Glsl::Vec2(_way));
				_to.draw(sf::Sprite(_from.getTexture()), sf::RenderStates(sf::BlendNone, sf::Transform::Identity, nullptr, &blur));
				_to.display();
			};
			blurInto(glowA, glowB, { texel.x * 1.5f, 0.f });
			blurInto(glowB, glowA, { 0.f, texel.y * 1.5f });
			blurInto(glowA, glowB, { texel.x * 4.f, 0.f });
			blurInto(glowB, glowA, { 0.f, texel.y * 4.f });
		}

		//---- onto the window: one window pixel per picture pixel. (not the window's default view: SFML keeps that at the
		//size the window was made, so after a resize the lit deck would be stretched away from everything else) ----
		_window.setView(sf::View(sf::FloatRect(0.f, 0.f, float(size.x), float(size.y))));
		_window.draw(sf::Sprite(scene.getTexture()));
		if (options.glow)
		{
			adder.setUniform("texture", sf::Shader::CurrentTexture);
			adder.setUniform("strength", options.glowStrength);
			sf::Sprite glow(glowA.getTexture());
			glow.setScale(2.f, 2.f);
			_window.draw(glow, sf::RenderStates(sf::BlendAdd, sf::Transform::Identity, nullptr, &adder));
		}
		_window.setView(view);
		return true;
	}

	void renderer::drawPanel(bool& _open, float _scale, ImFont* _font)
	{
		if (!_open)
			return;
		if (_font)
			ImGui::PushFont(_font);
		ImGui::SetNextWindowPos(ImVec2(240.f * _scale, 90.f * _scale), ImGuiCond_FirstUseEver);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f * _scale, 8.f * _scale));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f * _scale, 5.f * _scale));
		if (ImGui::Begin("Lighting", &_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
		{
			const float wide = 220.f * _scale;
			ImGui::Checkbox("Lighting (F9)", &options.enabled);
			if (!available())
				ImGui::TextColored(uiTheme::warning, "shaders don't work here: the decks are drawn unlit");
			ImGui::SetNextItemWidth(wide);
			ImGui::SliderFloat("ambient", &options.ambient, 0.f, 1.f, "%.2f");
			ImGui::SetNextItemWidth(wide);
			ImGui::SliderFloat("ambient in the views", &options.viewAmbient, 0.f, 1.f, "%.2f");
			ImGui::TextDisabled("  the light everywhere; the views (power, heat...) keep at least the second");
			ImGui::Checkbox("normal maps (the sides lit by where the light is)", &options.normalMaps);
			ImGui::Checkbox("walls keep light in its room", &options.rooms);
			ImGui::Checkbox("glow", &options.glow);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(150.f * _scale);
			ImGui::SliderFloat("##glowStrength", &options.glowStrength, 0.f, 2.f, "strength %.2f");
			ImGui::Checkbox("steam, smoke and sparks", &options.particles);
			ImGui::Checkbox("emergency lights (lamps pulse red when their power fails)", &options.emergency);
			if (ImGui::Button("Sparks (a test)"))
				sparksSomewhere();
			ImGui::SameLine();
			if (ImGui::Button("Read the maps again"))
				forgetMaps();
			ImGui::TextDisabled("%d lights, %d particles", lightCount, particleCount());
			ImGui::TextDisabled("items' lights: the content files (light = {...}), or the editor (F7)");
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
		if (_font)
			ImGui::PopFont();
	}
}
