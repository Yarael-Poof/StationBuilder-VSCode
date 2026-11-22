//StationBuilder in 3D: a prototype to get the feel. it reads scene.txt (written by the game's tools/sceneExport.cpp:
//the station and its ships, deck by deck) and draws it with raylib:
//  - the building view: an orthographic camera at the game's isometric angle, turning in quarter turns (Q, E), decks
//    above the one shown cut away, walls cut low, a tile picked under the mouse (a left click lays or lifts a floor)
//  - the space view: a perspective camera round the station, stars, a planet and the sun, the free ship flying a
//    banked loop round the station, the docked ship at its jetty
//  - Dear ImGui with the game's amber theme and VT323 for the panel
//the lighting:
//  - the sun, with a shadow map: the decks' roofs keep it out, so the rooms are lit by their own lights
//  - lamps, heaters, generators, consoles, the smelter, the docks and the engine as point lights. each has a shadow
//    cube (six tiles of one depth atlas), drawn in its vessel's own frame: nothing in a vessel moves against its own
//    lights, so a cube is drawn once and again only when the vessel is built on
//  - glow: the glowing parts drawn on their own, blurred and added on top
//  - particles: steam from the heat exchangers, smoke from the smelter, sparks from a cable, the engine's plume
//  - emergency power: the lamps go out and pulse red, the generators blink red
//items are boxes for now (their footprint, a height and a colour taken from their art): real models come later.
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "imgui.h"
#include "imguiRaylib.h"
#include "uiTheme.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	const float T = 2.f;              //a tile's side, m
	const float deckHeight = 3.f;     //m from one deck to the next
	const float floorThick = 0.25f;
	const float wallHeight = 2.75f;     //up to the roof (or the next deck's floor): no gap for light to leak by
	const float wallCut = 0.7f;       //walls cut away to this in the building view
	const float wallThick = 0.2f;
	const float isoPitch = 30.f;      //the game's 2:1 isometric diamonds: the camera 30 degrees above the deck
	const int maxLights = 32;         //lights a vessel is drawn with (MAXLIGHTS in the shader)
	const float lampNear = 0.05f;     //m: the near side of the lights' shadow cubes

	//---- the scene ----
	struct item { int x, y, w, h; float height; Color colour; bool outside, light; std::string key; };
	struct line { int x, y, kind, mask; bool riser; };
	struct deckData
	{
		std::set<std::pair<int, int>> floors, frames;
		std::vector<std::array<int, 3>> walls;
		std::vector<line> lines;
		std::vector<item> items;
		std::vector<std::array<int, 3>> docks;
	};

	//---- lights and the things that steam and smoke ----
	enum class lightKind { lamp, heater, generator, console, smelter, dock, plume, spark };
	struct lightSource
	{
		lightKind kind = lightKind::lamp;
		Vector3 local{};      //in its vessel's own frame, m
		float reach = 10.f;   //m: as far as it lights, and the far side of its shadow cube
		int deck = 0;
		float phase = 0.f;    //for flicker
		int tile = -1;        //its first of six tiles in the shadow atlas; -1: it casts no shadows
	};
	struct vent { Vector3 local{}; int deck = 0; bool smoke = false; float due = 0.f; };
	struct cableSpot { Vector3 local{}; int deck = 0; };

	struct vesselData
	{
		std::string name;
		bool ship = false;
		Vector2 position{};       //centre of mass in the area, m (x right, y down on the nav map: x and z here)
		float heading = 0.f;      //rad
		Vector2 centre{};         //centre of mass from its grid's corner, m
		Vector2 forwards{};       //the way its engines push, in its own frame
		bool docked = false;
		std::vector<deckData> decks;
		//drawn
		struct deckModels { Model base{}, wallsFull{}, wallsCut{}, roof{}; bool built = false; };
		std::vector<deckModels> models;
		float radius = 10.f;
		Matrix transform = MatrixIdentity();
		std::vector<lightSource> lights;
		std::vector<vent> vents;
		std::vector<cableSpot> cables;
	};

	std::vector<vesselData> loadScene(const std::string& _file)
	{
		std::vector<vesselData> scene;
		std::ifstream in(_file);
		std::string row;
		while (std::getline(in, row))
		{
			std::istringstream s(row);
			std::string what;
			s >> what;
			if (what == "vessel")
			{
				vesselData v;
				int ship = 0, docked = 0;
				s >> v.name >> ship >> v.position.x >> v.position.y >> v.heading >> v.centre.x >> v.centre.y >> v.forwards.x >> v.forwards.y >> docked;
				v.ship = ship != 0;
				v.docked = docked != 0;
				scene.push_back(v);
			}
			else if (scene.empty())
				continue;
			else if (what == "deck")
				scene.back().decks.emplace_back();
			else if (scene.back().decks.empty())
				continue;
			else
			{
				deckData& d = scene.back().decks.back();
				int x = 0, y = 0;
				if (what == "floor" && (s >> x >> y))
					d.floors.insert({ x, y });
				else if (what == "frame" && (s >> x >> y))
					d.frames.insert({ x, y });
				else if (what == "wall")
				{
					std::array<int, 3> w{};
					s >> w[0] >> w[1] >> w[2];
					d.walls.push_back(w);
				}
				else if (what == "line")
				{
					line l{};
					int riser = 0;
					s >> l.x >> l.y >> l.kind >> l.mask >> riser;
					l.riser = riser != 0;
					d.lines.push_back(l);
				}
				else if (what == "box")
				{
					item b{};
					int r = 0, g = 0, bl = 0, outside = 0, light = 0;
					s >> b.x >> b.y >> b.w >> b.h >> b.height >> r >> g >> bl >> outside >> light >> b.key;
					b.colour = Color{ (unsigned char)r, (unsigned char)g, (unsigned char)bl, 255 };
					b.outside = outside != 0;
					b.light = light != 0;
					d.items.push_back(b);
				}
				else if (what == "dock")
				{
					std::array<int, 3> k{};
					s >> k[0] >> k[1] >> k[2];
					d.docks.push_back(k);
				}
			}
		}
		return scene;
	}

	//---- meshes: coloured, lit triangles gathered into one mesh per vessel deck ----
	struct meshBuilder
	{
		std::vector<float> positions, normals;
		std::vector<unsigned char> colours;
		void vertex(Vector3 _p, Vector3 _n, Color _c)
		{
			positions.insert(positions.end(), { _p.x, _p.y, _p.z });
			normals.insert(normals.end(), { _n.x, _n.y, _n.z });
			colours.insert(colours.end(), { _c.r, _c.g, _c.b, _c.a });
		}
		//four corners anticlockwise seen from outside
		void quad(Vector3 _a, Vector3 _b, Vector3 _c, Vector3 _d, Color _colour)
		{
			const Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(_b, _a), Vector3Subtract(_c, _a)));
			for (const Vector3& p : { _a, _b, _c, _a, _c, _d })
				vertex(p, n, _colour);
		}
		void box(Vector3 _lo, Vector3 _hi, Color _side, Color _top)
		{
			const float x0 = _lo.x, y0 = _lo.y, z0 = _lo.z, x1 = _hi.x, y1 = _hi.y, z1 = _hi.z;
			quad({ x0, y1, z0 }, { x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 }, _top);       //top
			quad({ x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 }, _side);      //bottom
			quad({ x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x1, y0, z1 }, _side);      //+x
			quad({ x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 }, { x0, y0, z0 }, _side);      //-x
			quad({ x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 }, { x0, y0, z1 }, _side);      //+z
			quad({ x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 }, { x1, y0, z0 }, _side);      //-z
		}
		bool empty() const { return positions.empty(); }
		Model build(const Shader& _shader)
		{
			Mesh mesh{};
			mesh.vertexCount = int(positions.size() / 3);
			mesh.triangleCount = mesh.vertexCount / 3;
			mesh.vertices = (float*)MemAlloc(unsigned(positions.size() * sizeof(float)));
			mesh.normals = (float*)MemAlloc(unsigned(normals.size() * sizeof(float)));
			mesh.colors = (unsigned char*)MemAlloc(unsigned(colours.size()));
			std::copy(positions.begin(), positions.end(), mesh.vertices);
			std::copy(normals.begin(), normals.end(), mesh.normals);
			std::copy(colours.begin(), colours.end(), mesh.colors);
			UploadMesh(&mesh, false);
			Model model = LoadModelFromMesh(mesh);
			model.materials[0].shader = _shader;
			return model;
		}
	};

	Color shade(Color _c, float _by)
	{
		auto f = [&](unsigned char _v) { return (unsigned char)std::clamp(int(_v * _by), 0, 255); };
		return Color{ f(_c.r), f(_c.g), f(_c.b), _c.a };
	}
	Color mixColour(Color _a, Color _b, float _t)
	{
		auto m = [&](unsigned char _x, unsigned char _y) { return (unsigned char)std::clamp(int(_x + (_y - _x) * _t), 0, 255); };
		return Color{ m(_a.r, _b.r), m(_a.g, _b.g), m(_a.b, _b.b), m(_a.a, _b.a) };
	}
	//glowing parts: an alpha under 0.75 marks them (the shader draws them unlit and bright, and into the glow). its
	//value says which group, so each group can be dimmed, flickered or blinked on its own
	const unsigned char glowLamp = 128, glowFire = 96, glowSteady = 64, glowDock = 48, glowStatus = 32;
	Color glowing(Color _c, unsigned char _group) { _c.a = _group; return _c; }

	void freeModel(Model& _model)
	{
		if (_model.meshCount == 0)
			return;
		_model.materials[0].shader = Shader{ rlGetShaderIdDefault(), rlGetShaderLocsDefault() };   //the shared shader stays
		UnloadModel(_model);
		_model = Model{};
	}

	//a tile's corner in a vessel's own frame (m from its centre of mass), at a height
	Vector3 at(const vesselData& _v, float _gx, float _gy, float _h)
	{
		return Vector3{ _gx - _v.centre.x, _h, _gy - _v.centre.y };
	}
	void itemBounds(const vesselData& _v, int _deck, const item& _b, Vector3& _lo, Vector3& _hi)
	{
		const float base = _deck * deckHeight, inset = 0.15f;
		_lo = at(_v, (_b.x - 1) * T + inset, (_b.y - 1) * T + inset, base + floorThick);
		_hi = at(_v, (_b.x - 1 + _b.w) * T - inset, (_b.y - 1 + _b.h) * T - inset, base + floorThick + _b.height);
	}
	//the way a dock's face looks out, in its vessel's frame (UL, UR, LL, LR)
	Vector3 dockOutwards(int _face)
	{
		switch (_face)
		{
		case 0: return { -1.f, 0.f, 0.f };
		case 1: return { 0.f, 0.f, -1.f };
		case 2: return { 0.f, 0.f, 1.f };
		default: return { 1.f, 0.f, 0.f };
		}
	}
	//the middle of a dock's plate, at its top
	Vector3 dockTop(const vesselData& _v, int _deck, const std::array<int, 3>& _k)
	{
		const Vector3 out = dockOutwards(_k[2]);
		return at(_v, (_k[0] - 0.5f + out.x * 0.5f) * T, (_k[1] - 0.5f + out.z * 0.5f) * T, _deck * deckHeight + 2.f);
	}

	void buildDeck(vesselData& _v, int _deck, const Shader& _shader)
	{
		vesselData::deckModels& m = _v.models[size_t(_deck)];
		freeModel(m.base);
		freeModel(m.wallsFull);
		freeModel(m.wallsCut);
		freeModel(m.roof);
		const deckData& d = _v.decks[size_t(_deck)];
		const float base = _deck * deckHeight;
		meshBuilder parts, full, cut, roof;
		//floors: a whole slab (no gap for light to get by), a checker of greys on top with a hair's gap between
		for (const auto& [x, y] : d.floors)
		{
			const Color c = (x + y) % 2 ? Color{ 138, 146, 158, 255 } : Color{ 148, 156, 168, 255 };
			parts.box(at(_v, (x - 1) * T, (y - 1) * T, base), at(_v, x * T, y * T, base + floorThick - 0.03f), shade(c, 0.8f), shade(c, 0.55f));
			parts.box(at(_v, (x - 1) * T + 0.03f, (y - 1) * T + 0.03f, base + floorThick - 0.03f), at(_v, x * T - 0.03f, y * T - 0.03f, base + floorThick), shade(c, 0.8f), c);
			//a roof where the deck above has no floor
			const bool covered = _deck + 1 < int(_v.decks.size()) && _v.decks[size_t(_deck) + 1].floors.count({ x, y });
			if (!covered)
				roof.box(at(_v, (x - 1) * T, (y - 1) * T, base + deckHeight - 0.05f), at(_v, x * T, y * T, base + deckHeight + 0.15f),
					Color{ 110, 118, 132, 255 }, (x + y) % 2 ? Color{ 150, 158, 170, 255 } : Color{ 158, 166, 178, 255 });
		}
		//frames: girders round each tile's edge
		const Color girder{ 150, 156, 166, 255 };
		for (const auto& [x, y] : d.frames)
		{
			const float x0 = (x - 1) * T, x1 = x * T, y0 = (y - 1) * T, y1 = y * T, g = 0.12f;
			parts.box(at(_v, x0, y0, base), at(_v, x1, y0 + g, base + g), shade(girder, 0.8f), girder);
			parts.box(at(_v, x0, y1 - g, base), at(_v, x1, y1, base + g), shade(girder, 0.8f), girder);
			parts.box(at(_v, x0, y0, base), at(_v, x0 + g, y1, base + g), shade(girder, 0.8f), girder);
			parts.box(at(_v, x1 - g, y0, base), at(_v, x1, y1, base + g), shade(girder, 0.8f), girder);
		}
		//walls on their edges: full height, and cut low for the building view
		const Color wall{ 112, 124, 144, 255 }, wallTop{ 168, 178, 196, 255 };
		for (const auto& w : d.walls)
		{
			const float x0 = (w[0] - 1) * T, x1 = w[0] * T, y0 = (w[1] - 1) * T, y1 = w[1] * T;
			Vector3 lo{}, hi{};
			switch (w[2])
			{
			case 0: lo = at(_v, x0, y0, base); hi = at(_v, x0 + wallThick, y1, base); break;   //UL: the -x side
			case 1: lo = at(_v, x0, y0, base); hi = at(_v, x1, y0 + wallThick, base); break;   //UR: -z
			case 2: lo = at(_v, x0, y1 - wallThick, base); hi = at(_v, x1, y1, base); break;   //LL: +z
			default: lo = at(_v, x1 - wallThick, y0, base); hi = at(_v, x1, y1, base); break;  //LR: +x
			}
			Vector3 top = hi;
			top.y = base + floorThick + wallHeight;
			full.box(lo, top, wall, wallTop);
			top.y = base + floorThick + wallCut;
			cut.box(lo, top, wall, wallTop);
		}
		//cables, pipes, tubes: thin bars from the tile's middle to each side they join
		for (const line& l : d.lines)
		{
			const Color colours[4] = { { 230, 140, 60, 255 }, { 120, 160, 200, 255 }, { 200, 176, 120, 255 }, { 150, 150, 160, 255 } };
			const float sizes[4] = { 0.12f, 0.22f, 0.3f, 0.45f }, heights[4] = { 0.1f, 0.3f, 0.6f, 1.2f };
			const Color c = colours[std::clamp(l.kind, 0, 3)];
			const float s = sizes[std::clamp(l.kind, 0, 3)] * 0.5f, h = base + floorThick + heights[std::clamp(l.kind, 0, 3)];
			const float cx = (l.x - 0.5f) * T, cy = (l.y - 0.5f) * T;
			parts.box(at(_v, cx - s, cy - s, h - s), at(_v, cx + s, cy + s, h + s), shade(c, 0.8f), c);
			const float ends[4][2] = { { (l.x - 1) * T, cy }, { cx, (l.y - 1) * T }, { cx, l.y * T }, { l.x * T, cy } };   //UL UR LL LR
			for (int side = 0; side < 4; side++)
			{
				if (!(l.mask & (1 << side)))
					continue;
				const float ex = ends[side][0], ey = ends[side][1];
				parts.box(at(_v, std::min(cx, ex) - s, std::min(cy, ey) - s, h - s), at(_v, std::max(cx, ex) + s, std::max(cy, ey) + s, h + s), shade(c, 0.8f), c);
			}
			if (l.riser)
				parts.box(at(_v, cx - s, cy - s, base), at(_v, cx + s, cy + s, base + deckHeight), shade(c, 0.8f), c);
		}
		//items: boxes over their footprint, their height and colour from their art; lamps, and the glowing parts of
		//heaters, generators, consoles and the smelter
		for (const item& b : d.items)
		{
			Vector3 lo{}, hi{};
			itemBounds(_v, _deck, b, lo, hi);
			const float mx = (lo.x + hi.x) * 0.5f, mz = (lo.z + hi.z) * 0.5f, floorTop = base + floorThick;
			if (b.light)
			{
				const float cx = (b.x - 0.5f) * T, cy = (b.y - 0.5f) * T;
				const Color head = glowing({ 255, 226, 150, 255 }, glowLamp);
				parts.box(at(_v, cx - 0.06f, cy - 0.06f, floorTop), at(_v, cx + 0.06f, cy + 0.06f, floorTop + 1.4f), Color{ 70, 72, 80, 255 }, Color{ 70, 72, 80, 255 });
				parts.box(at(_v, cx - 0.22f, cy - 0.22f, floorTop + 1.4f), at(_v, cx + 0.22f, cy + 0.22f, floorTop + 1.8f), head, head);
				continue;
			}
			parts.box(lo, hi, shade(b.colour, 0.85f), shade(b.colour, 1.15f));
			if (b.key == "heater")
			{
				const Color c = glowing({ 255, 120, 50, 255 }, glowSteady);
				parts.box({ lo.x + 0.25f, hi.y, lo.z + 0.25f }, { hi.x - 0.25f, hi.y + 0.05f, hi.z - 0.25f }, c, c);
			}
			else if (b.key == "generator")
			{
				const Color c = glowing({ 225, 255, 225, 255 }, glowStatus);
				parts.box({ hi.x - 0.45f, hi.y, hi.z - 0.45f }, { hi.x - 0.2f, hi.y + 0.12f, hi.z - 0.2f }, c, c);
			}
			else if (b.key == "flightConsole")
			{
				const Color c = glowing({ 255, 176, 80, 255 }, glowSteady);
				parts.box({ lo.x + 0.2f, hi.y, lo.z + 0.2f }, { hi.x - 0.2f, hi.y + 0.03f, hi.z - 0.2f }, c, c);
			}
			else if (b.key == "smelter")
			{
				//its glowing mouth, on its +z face
				const Color c = glowing({ 255, 140, 40, 255 }, glowFire);
				parts.box({ mx - 0.5f, floorTop + 0.5f, hi.z }, { mx + 0.5f, floorTop + 1.3f, hi.z + 0.05f }, c, c);
			}
			(void)mz;
		}
		//docking points: a yellow plate on the face ships dock against, a light on top
		for (const auto& k : d.docks)
		{
			const float x0 = (k[0] - 1) * T, x1 = k[0] * T, y0 = (k[1] - 1) * T, y1 = k[1] * T, p = 0.1f;
			const Color c{ 240, 200, 70, 255 };
			Vector3 lo{}, hi{};
			switch (k[2])
			{
			case 0: lo = at(_v, x0 - p, y0 + 0.3f, base + 0.3f); hi = at(_v, x0, y1 - 0.3f, base + 2.f); break;
			case 1: lo = at(_v, x0 + 0.3f, y0 - p, base + 0.3f); hi = at(_v, x1 - 0.3f, y0, base + 2.f); break;
			case 2: lo = at(_v, x0 + 0.3f, y1, base + 0.3f); hi = at(_v, x1 - 0.3f, y1 + p, base + 2.f); break;
			default: lo = at(_v, x1, y0 + 0.3f, base + 0.3f); hi = at(_v, x1 + p, y1 - 0.3f, base + 2.f); break;
			}
			parts.box(lo, hi, c, c);
			const Vector3 top = dockTop(_v, _deck, k);
			const Color bulb = glowing({ 255, 214, 100, 255 }, glowDock);
			parts.box({ top.x - 0.12f, top.y, top.z - 0.12f }, { top.x + 0.12f, top.y + 0.2f, top.z + 0.12f }, bulb, bulb);
		}
		if (!parts.empty()) m.base = parts.build(_shader);
		if (!full.empty()) m.wallsFull = full.build(_shader);
		if (!cut.empty()) m.wallsCut = cut.build(_shader);
		if (!roof.empty()) m.roof = roof.build(_shader);
		m.built = true;
	}

	//a vessel's lights, the vents that steam or smoke, and its cables (where sparks fly)
	void gatherLights(vesselData& _v)
	{
		_v.lights.clear();
		_v.vents.clear();
		_v.cables.clear();
		std::mt19937 random(unsigned(_v.name.size() * 131 + _v.decks.size()));
		std::uniform_real_distribution<float> phase(0.f, 6.283f);
		for (int d = 0; d < int(_v.decks.size()); d++)
		{
			const deckData& k = _v.decks[size_t(d)];
			const float base = d * deckHeight, floorTop = base + floorThick;
			for (const item& b : k.items)
			{
				Vector3 lo{}, hi{};
				itemBounds(_v, d, b, lo, hi);
				const Vector3 middle{ (lo.x + hi.x) * 0.5f, hi.y, (lo.z + hi.z) * 0.5f };
				lightSource l;
				l.deck = d;
				l.phase = phase(random);
				bool lit = true;
				if (b.light) { l.kind = lightKind::lamp; l.local = at(_v, (b.x - 0.5f) * T, (b.y - 0.5f) * T, floorTop + 1.6f); l.reach = 12.f; }
				else if (b.key == "heater") { l.kind = lightKind::heater; l.local = Vector3Add(middle, { 0.f, 0.45f, 0.f }); l.reach = 5.5f; }
				else if (b.key == "generator") { l.kind = lightKind::generator; l.local = Vector3Add(middle, { 0.f, 0.35f, 0.f }); l.reach = 3.5f; }
				else if (b.key == "flightConsole") { l.kind = lightKind::console; l.local = Vector3Add(middle, { 0.f, 0.45f, 0.f }); l.reach = 4.5f; }
				else if (b.key == "smelter") { l.kind = lightKind::smelter; l.local = Vector3{ middle.x, floorTop + 1.f, hi.z + 0.6f }; l.reach = 9.f; }
				else lit = false;
				if (lit)
					_v.lights.push_back(l);
				if (b.key == "exchanger")
					_v.vents.push_back({ Vector3{ middle.x, hi.y + 0.05f, middle.z }, d, false, 0.f });
				if (b.key == "smelter")
					_v.vents.push_back({ Vector3{ middle.x, floorTop + 1.4f, hi.z + 0.25f }, d, true, 0.f });
			}
			for (const auto& dock : k.docks)
			{
				lightSource l;
				l.kind = lightKind::dock;
				l.deck = d;
				l.local = Vector3Add(Vector3Add(dockTop(_v, d, dock), Vector3Scale(dockOutwards(dock[2]), 0.7f)), { 0.f, 0.3f, 0.f });
				l.reach = 7.f;
				_v.lights.push_back(l);
			}
			for (const line& c : k.lines)
				if (c.kind == 0)
					_v.cables.push_back({ at(_v, (c.x - 0.5f) * T, (c.y - 0.5f) * T, floorTop + 0.1f), d });
		}
		if (_v.ship && !_v.docked)
		{
			//the engine's light, out behind it, where its plume starts
			lightSource l;
			l.kind = lightKind::plume;
			const Vector3 own = Vector3Normalize(Vector3{ _v.forwards.x, 0.f, _v.forwards.y });
			l.local = Vector3Add(Vector3Scale(own, -(_v.radius * 0.75f + 2.5f)), { 0.f, 1.3f, 0.f });
			l.reach = 20.f;
			_v.lights.push_back(l);
		}
	}

	void buildVessel(vesselData& _v, const Shader& _shader)
	{
		_v.models.resize(_v.decks.size());
		float radius = 4.f;
		for (int d = 0; d < int(_v.decks.size()); d++)
		{
			buildDeck(_v, d, _shader);
			for (const auto& [x, y] : _v.decks[size_t(d)].floors)
				radius = std::max(radius, Vector2Length(Vector2{ (x - 0.5f) * T - _v.centre.x, (y - 0.5f) * T - _v.centre.y }) + T);
			for (const auto& [x, y] : _v.decks[size_t(d)].frames)
				radius = std::max(radius, Vector2Length(Vector2{ (x - 0.5f) * T - _v.centre.x, (y - 0.5f) * T - _v.centre.y }) + T);
		}
		_v.radius = radius;
		gatherLights(_v);
	}

	//where a vessel is: its own frame turned by its heading (as the game's nav map turns it) and moved to its place
	Matrix placeOf(const vesselData& _v)
	{
		return MatrixMultiply(MatrixRotateY(-_v.heading), MatrixTranslate(_v.position.x, 0.f, _v.position.y));
	}

	void drawAt(const Model& _m, const Matrix& _place)
	{
		if (_m.meshCount == 0)
			return;
		Model m = _m;
		m.transform = _place;
		DrawModel(m, Vector3{ 0.f, 0.f, 0.f }, 1.f, WHITE);
	}
	void drawModel(const Model& _m, const Matrix& _place, const Shader& _shader, int _dimLoc, float _dim)
	{
		if (_m.meshCount == 0)
			return;
		SetShaderValue(_shader, _dimLoc, &_dim, SHADER_UNIFORM_FLOAT);
		drawAt(_m, _place);
	}

	//---- shadows ----
	//a framebuffer with only a depth texture
	RenderTexture2D loadDepthTarget(int _size)
	{
		RenderTexture2D t{};
		t.id = rlLoadFramebuffer();
		t.texture.width = _size;    //BeginTextureMode takes the viewport from here
		t.texture.height = _size;
		rlEnableFramebuffer(t.id);
		t.depth.id = rlLoadTextureDepth(_size, _size, false);
		t.depth.width = _size;
		t.depth.height = _size;
		t.depth.format = 19;
		t.depth.mipmaps = 1;
		rlFramebufferAttach(t.id, t.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
		if (!rlFramebufferComplete(t.id))
			TraceLog(LOG_WARNING, "a shadow map's framebuffer is not complete");
		rlDisableFramebuffer();
		return t;
	}

	//the lights' shadow cubes: six square tiles each in one depth texture
	struct shadowAtlas
	{
		RenderTexture2D target{};
		int size = 4096, tile = 256;
		int across() const { return size / tile; }
		int lights() const { return across() * across() / 6; }
	};

	//the six faces of a light's cube, looking out along each axis. the shader picks a face and works out where in it
	//a point falls from these same ways and ups
	const Vector3 faceWays[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
	const Vector3 faceUps[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };

	//draws a light's cube from its place in its vessel's own frame (call between BeginTextureMode on the atlas and
	//EndTextureMode, with the shader drawing depth only)
	void bakeLight(const shadowAtlas& _atlas, const vesselData& _v, const lightSource& _l)
	{
		if (_l.tile < 0)
			return;
		const Matrix projection = MatrixPerspective(90.0 * DEG2RAD, 1.0, lampNear, _l.reach);
		for (int f = 0; f < 6; f++)
		{
			const int k = _l.tile + f, x = (k % _atlas.across()) * _atlas.tile, y = (k / _atlas.across()) * _atlas.tile;
			rlViewport(x, y, _atlas.tile, _atlas.tile);
			rlScissor(x, y, _atlas.tile, _atlas.tile);
			rlClearScreenBuffers();
			rlSetMatrixProjection(projection);
			rlSetMatrixModelview(MatrixLookAt(_l.local, Vector3Add(_l.local, faceWays[f]), faceUps[f]));
			for (const vesselData::deckModels& m : _v.models)
			{
				drawAt(m.base, MatrixIdentity());
				drawAt(m.wallsFull, MatrixIdentity());
				drawAt(m.roof, MatrixIdentity());
			}
		}
	}

	//---- particles ----
	struct particle
	{
		Vector3 position{}, velocity{};
		float age = 0.f, life = 1.f, size0 = 0.1f, size1 = 0.1f, drag = 0.f, gravity = 0.f, floor = -1e9f;
		Color from{}, to{};
		bool additive = false, streak = false;
		int deck = 0;
	};

	Texture2D softDot()
	{
		const int n = 64;
		std::vector<Color> pixels(size_t(n * n));
		for (int y = 0; y < n; y++)
			for (int x = 0; x < n; x++)
			{
				const float dx = (x + 0.5f - n * 0.5f) / (n * 0.5f), dy = (y + 0.5f - n * 0.5f) / (n * 0.5f);
				float a = std::clamp(1.f - std::sqrt(dx * dx + dy * dy), 0.f, 1.f);
				a = a * a * (3.f - 2.f * a);
				pixels[size_t(y * n + x)] = Color{ 255, 255, 255, (unsigned char)(a * 255.f) };
			}
		Image image{ pixels.data(), n, n, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
		Texture2D t = LoadTextureFromImage(image);
		SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
		return t;
	}

	//---- shaders ----
	const char* vertexShader = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragWorld;
out vec3 fragLocal;          // in the drawn vessel's own frame (its meshes are built in it)
out vec3 fragNormal;
out vec3 fragLocalNormal;
out vec4 fragColor;
invariant gl_Position;
void main()
{
    fragWorld = vec3(matModel * vec4(vertexPosition, 1.0));
    fragLocal = vertexPosition;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragLocalNormal = vertexNormal;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})";

	const char* fragmentShader = R"(#version 330
in vec3 fragWorld;
in vec3 fragLocal;
in vec3 fragNormal;
in vec3 fragLocalNormal;
in vec4 fragColor;
uniform vec4 colDiffuse;
uniform int mode;                // 0: lit; 1: depth for a shadow map (glowing parts let light by); 2: only what glows
uniform vec3 sunDirection;       // the way the sunlight goes
uniform vec3 sunColour;
uniform vec3 ambient;
uniform float dim;
uniform mat4 sunMatrix;          // the world to the sun's shadow map
uniform sampler2D sunShadow;
uniform int sunShadows;
uniform float sunTexel;          // a shadow-map texel, in its uv
uniform float sunOffset;         // m along the normal before looking (against acne)
uniform float sunBias;
uniform sampler2D lampShadows;   // six tiles per light: its cube's faces
uniform float atlasTiles;        // tiles along the atlas's side
uniform float tileTexels;        // texels along a tile's side
uniform float lampNear;
#define MAXLIGHTS 32
uniform int lightCount;
uniform vec3 lightPosition[MAXLIGHTS];   // in the drawn vessel's own frame
uniform vec3 lightColour[MAXLIGHTS];
uniform float lightRadius[MAXLIGHTS];
uniform vec2 lightShadow[MAXLIGHTS];     // its first tile (-1: no shadows), the far side its cube was drawn to
uniform vec3 eye;                        // the camera, in the drawn vessel's own frame
uniform vec3 lampGlow;
uniform float fireGlow;
uniform float dockGlow;
uniform vec3 statusGlow;
out vec4 finalColor;

float sunLight(vec3 n)
{
    if (sunShadows == 0) return 1.0;
    vec4 s = sunMatrix * vec4(fragWorld + n * sunOffset, 1.0);
    vec3 p = s.xyz / s.w * 0.5 + 0.5;
    if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0 || p.z <= 0.0) return 1.0;
    float lit = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            lit += p.z - sunBias > texture(sunShadow, p.xy + vec2(x, y) * sunTexel).r ? 0.0 : 1.0;
    return lit / 9.0;
}

// L: from light i to the point, in the vessel's own frame
float lampLight(int i, vec3 L)
{
    if (lightShadow[i].x < 0.0) return 1.0;
    vec3 a = abs(L);
    vec3 d; vec3 up; float face;
    if (a.x >= a.y && a.x >= a.z) { d = vec3(sign(L.x), 0.0, 0.0); up = vec3(0.0, 1.0, 0.0); face = L.x > 0.0 ? 0.0 : 1.0; }
    else if (a.y >= a.z) { d = vec3(0.0, sign(L.y), 0.0); up = vec3(0.0, 0.0, 1.0); face = L.y > 0.0 ? 2.0 : 3.0; }
    else { d = vec3(0.0, 0.0, sign(L.z)); up = vec3(0.0, 1.0, 0.0); face = L.z > 0.0 ? 4.0 : 5.0; }
    // as raymath's MatrixLookAt makes the face's view
    vec3 vz = -d;
    vec3 vx = normalize(cross(up, vz));
    vec3 vy = cross(vz, vx);
    float major = dot(d, L);
    float edge = 1.5 / tileTexels;
    vec2 inTile = clamp(vec2(dot(vx, L), dot(vy, L)) / major * 0.5 + 0.5, vec2(edge), vec2(1.0 - edge));
    float k = lightShadow[i].x + face;
    vec2 tile = vec2(mod(k, atlasTiles), floor(k / atlasTiles));
    float n = lampNear, f = lightShadow[i].y;
    float texel = 1.0 / (tileTexels * atlasTiles);
    float lit = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
        {
            float z = texture(lampShadows, (tile + inTile) / atlasTiles + vec2(x, y) * texel).r * 2.0 - 1.0;
            float stored = 2.0 * n * f / ((f + n) - z * (f - n));
            lit += major - 0.04 > stored ? 0.0 : 1.0;
        }
    return lit / 9.0;
}

void main()
{
    vec3 base = fragColor.rgb * colDiffuse.rgb;
    bool glowing = fragColor.a < 0.75;
    if (mode == 1) { if (glowing) discard; finalColor = vec4(1.0); return; }
    if (glowing)
    {
        float g = fragColor.a * 255.0;
        vec3 level = g > 112.0 ? lampGlow : g > 80.0 ? vec3(fireGlow) : g > 56.0 ? vec3(1.0) : g > 40.0 ? vec3(dockGlow) : statusGlow;
        finalColor = vec4(base * level * dim, 1.0);
        return;
    }
    if (mode == 2) { finalColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    vec3 n = normalize(fragNormal);
    float sky = 0.5 + 0.5 * n.y;   // a little more light from above than below
    vec3 light = ambient * (0.6 + 0.4 * sky) + sunColour * max(dot(n, -sunDirection), 0.0) * sunLight(n);
    vec3 shine = vec3(0.0);
    vec3 ln = normalize(fragLocalNormal);
    vec3 v = normalize(eye - fragLocal);
    for (int i = 0; i < lightCount; i++)
    {
        vec3 L = fragLocal - lightPosition[i];
        float d = length(L);
        if (d >= lightRadius[i]) continue;
        vec3 toLight = -L / max(d, 0.001);
        float ndl = dot(ln, toLight);
        if (ndl <= 0.0) continue;
        float fall = 1.0 - d / lightRadius[i];
        fall *= fall;
        float s = lampLight(i, fragLocal + ln * 0.05 - lightPosition[i]);
        if (s <= 0.0) continue;
        light += lightColour[i] * ndl * fall * s;
        vec3 h = normalize(toLight + v);
        shine += lightColour[i] * pow(max(dot(ln, h), 0.0), 40.0) * 0.3 * fall * s;
    }
    finalColor = vec4((base * light + shine) * dim, 1.0);
})";

	//the glow: a blur along one way (raylib's own vertex shader)
	const char* blurShader = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 way;   // a step along the blur, in uv
out vec4 finalColor;
void main()
{
    float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 c = texture(texture0, fragTexCoord).rgb * w[0];
    for (int i = 1; i < 5; i++)
    {
        c += texture(texture0, fragTexCoord + way * float(i)).rgb * w[i];
        c += texture(texture0, fragTexCoord - way * float(i)).rgb * w[i];
    }
    finalColor = vec4(c, 1.0);
})";

	//the glow added on top, at a strength
	const char* addShader = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform float strength;
out vec4 finalColor;
void main()
{
    finalColor = vec4(texture(texture0, fragTexCoord).rgb * strength, 1.0);
})";

	//the uniforms the scene's shader takes
	struct sceneUniforms
	{
		int mode, sunDirection, sunColour, ambient, dim, sunMatrix, sunShadow, sunShadows, sunTexel, sunOffset, sunBias;
		int lampShadows, atlasTiles, tileTexels, lampNear, lightCount, lightPosition, lightColour, lightRadius, lightShadow;
		int eye, lampGlow, fireGlow, dockGlow, statusGlow;
		explicit sceneUniforms(const Shader& _s)
		{
			auto at = [&](const char* _name) { return GetShaderLocation(_s, _name); };
			mode = at("mode"); sunDirection = at("sunDirection"); sunColour = at("sunColour"); ambient = at("ambient");
			dim = at("dim"); sunMatrix = at("sunMatrix"); sunShadow = at("sunShadow"); sunShadows = at("sunShadows");
			sunTexel = at("sunTexel"); sunOffset = at("sunOffset"); sunBias = at("sunBias"); lampShadows = at("lampShadows");
			atlasTiles = at("atlasTiles"); tileTexels = at("tileTexels"); lampNear = at("lampNear"); lightCount = at("lightCount");
			lightPosition = at("lightPosition"); lightColour = at("lightColour"); lightRadius = at("lightRadius");
			lightShadow = at("lightShadow"); eye = at("eye"); lampGlow = at("lampGlow"); fireGlow = at("fireGlow");
			dockGlow = at("dockGlow"); statusGlow = at("statusGlow");
		}
	};

	//how the lights look this frame
	struct lightLook
	{
		float time = 0.f;
		bool emergency = false, flying = true;
		float pulse() const { return 0.25f + 0.75f * (0.5f + 0.5f * std::sin(time * 3.5f)); }                   //emergency red
		bool blink() const { return std::fmod(time, 1.f) < 0.5f; }                                               //generators in an emergency
		float dock() const { return std::fmod(time * 0.8f, 1.f) < 0.18f ? 1.f : 0.06f; }                         //dock beacons
		float fire() const { return 1.f + 0.22f * std::sin(time * 13.f) + 0.12f * std::sin(time * 31.f + 1.7f) + 0.08f * std::sin(time * 57.f + 0.4f); }
	};
	//a light's colour (its brightness folded in) and reach now
	Vector3 lightNow(const lightSource& _l, const lightLook& _k, float& _reach)
	{
		_reach = _l.reach;
		const float t = _k.time;
		switch (_l.kind)
		{
		case lightKind::lamp:
			if (_k.emergency)
			{
				_reach = 8.f;
				return Vector3Scale({ 1.f, 0.1f, 0.06f }, 1.5f * _k.pulse());
			}
			return Vector3Scale({ 1.f, 0.84f, 0.62f }, 1.2f);
		case lightKind::heater: return Vector3Scale({ 1.f, 0.42f, 0.16f }, 0.9f + 0.1f * std::sin(t * 1.3f + _l.phase));
		case lightKind::generator:
			if (_k.emergency)
				return _k.blink() ? Vector3{ 0.8f, 0.08f, 0.04f } : Vector3{ 0.f, 0.f, 0.f };
			return Vector3Scale({ 0.35f, 1.f, 0.5f }, 0.45f);
		case lightKind::console: return Vector3Scale({ 1.f, 0.68f, 0.3f }, 0.7f * (0.95f + 0.05f * std::sin(t * 7.f + _l.phase)));
		case lightKind::smelter: return Vector3Scale({ 1.f, 0.5f, 0.16f }, 1.5f * _k.fire());
		case lightKind::dock: return Vector3Scale({ 1.f, 0.78f, 0.25f }, 1.6f * _k.dock());
		case lightKind::plume: return _k.flying ? Vector3Scale({ 1.f, 0.6f, 0.25f }, 2.4f * (0.85f + 0.15f * std::sin(t * 40.f))) : Vector3{ 0.f, 0.f, 0.f };
		default: return Vector3{ 0.f, 0.f, 0.f };
		}
	}
}

int main(int argc, char** argv)
{
	//"shots <folder>": a hidden window, a short tour, a picture at each stop, then out (for checking it builds and draws)
	const bool shots = argc > 2 && std::string(argv[1]) == "shots";
	const std::string shotFolder = shots ? argv[2] : "";
	SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI | (shots ? FLAG_WINDOW_HIDDEN : 0));
	InitWindow(1600, 900, "StationBuilder 3D prototype");
	SetTargetFPS(144);

	imguiRaylib::init();
	ImGuiIO& io = ImGui::GetIO();
	ImFontConfig crisp;
	crisp.OversampleH = 1;
	crisp.OversampleV = 1;
	crisp.PixelSnapH = true;
	//on a scaled display the font is made at the framebuffer's size and drawn back down, so it stays crisp
	const float dpi = std::max(GetWindowScaleDPI().x, 1.f);
	if (!io.Fonts->AddFontFromFileTTF("../../StationBuilder/fonts/VT323-Regular.ttf", std::round(20.f * dpi), &crisp, io.Fonts->GetGlyphRangesDefault()))
		io.Fonts->AddFontDefault();
	io.FontGlobalScale = 1.f / dpi;
	imguiRaylib::buildFonts();
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowPadding = ImVec2(10, 10);
		style.FramePadding = ImVec2(6, 3);
		style.ItemSpacing = ImVec2(6, 5);
		uiTheme::apply();
	}

	Shader shader = LoadShaderFromMemory(vertexShader, fragmentShader);
	const sceneUniforms u(shader);
	Shader blur = LoadShaderFromMemory(nullptr, blurShader);
	const int blurWayLoc = GetShaderLocation(blur, "way");
	Shader adder = LoadShaderFromMemory(nullptr, addShader);
	const int strengthLoc = GetShaderLocation(adder, "strength");
	auto setMode = [&](int _mode) { SetShaderValue(shader, u.mode, &_mode, SHADER_UNIFORM_INT); };

	std::vector<vesselData> scene = loadScene("scene.txt");
	if (scene.empty())
	{
		//nothing to show: a message until the window closes
		while (!WindowShouldClose())
		{
			BeginDrawing();
			ClearBackground(BLACK);
			DrawText("scene.txt is missing or empty: run the game's SceneExport tool (see README.md)", 40, 40, 20, ORANGE);
			EndDrawing();
		}
		CloseWindow();
		return 1;
	}
	for (vesselData& v : scene)
		buildVessel(v, shader);
	int flyer = -1;   //the ship that flies a loop: the first free one
	for (int i = 0; i < int(scene.size()); i++)
		if (scene[size_t(i)].ship && !scene[size_t(i)].docked && flyer < 0)
			flyer = i;

	//---- shadow maps ----
	const int sunSize = 4096;
	RenderTexture2D sunMap = loadDepthTarget(sunSize);
	shadowAtlas atlas;
	atlas.target = loadDepthTarget(atlas.size);
	//the lights' tiles, in order, and one cube kept for a spark's flash
	int lightsShadowed = 0;
	for (vesselData& v : scene)
		for (lightSource& l : v.lights)
			l.tile = lightsShadowed < atlas.lights() - 1 ? 6 * lightsShadowed++ : -1;
	struct
	{
		int vessel = -1;
		lightSource light;
		float age = 9.f;
	} spark;
	spark.light.kind = lightKind::spark;
	spark.light.reach = 6.f;
	spark.light.tile = 6 * (atlas.lights() - 1);
	auto bake = [&](int _vessel, bool _sparkOnly)
	{
		setMode(1);
		BeginTextureMode(atlas.target);
		rlEnableDepthTest();                 //raylib turns it on only in BeginMode3D
		rlSetCullFace(RL_CULL_FACE_FRONT);   //their far sides: no acne on the lit faces
		rlEnableScissorTest();
		if (_sparkOnly)
			bakeLight(atlas, scene[size_t(spark.vessel)], spark.light);
		else
			for (int i = 0; i < int(scene.size()); i++)
				if (_vessel < 0 || i == _vessel)
					for (const lightSource& l : scene[size_t(i)].lights)
						bakeLight(atlas, scene[size_t(i)], l);
		rlDisableScissorTest();
		rlSetCullFace(RL_CULL_FACE_BACK);
		rlDisableDepthTest();
		EndTextureMode();
		setMode(0);
	};
	bake(-1, false);
	bool sparkBake = false;
	int rebake = -1;
	auto bindShadows = [&](bool _on)
	{
		//the shadow maps on slots of their own, kept off while they are drawn into
		rlActiveTextureSlot(13);
		if (_on) rlEnableTexture(sunMap.depth.id); else rlDisableTexture();
		rlActiveTextureSlot(14);
		if (_on) rlEnableTexture(atlas.target.depth.id); else rlDisableTexture();
		rlActiveTextureSlot(0);
		if (_on)
		{
			int s = 13;
			SetShaderValue(shader, u.sunShadow, &s, SHADER_UNIFORM_INT);
			s = 14;
			SetShaderValue(shader, u.lampShadows, &s, SHADER_UNIFORM_INT);
		}
	};
	{
		const float tiles = float(atlas.across()), texels = float(atlas.tile), nearSide = lampNear;
		SetShaderValue(shader, u.atlasTiles, &tiles, SHADER_UNIFORM_FLOAT);
		SetShaderValue(shader, u.tileTexels, &texels, SHADER_UNIFORM_FLOAT);
		SetShaderValue(shader, u.lampNear, &nearSide, SHADER_UNIFORM_FLOAT);
	}

	//---- the glow's render targets (half the window's pixels), made again when the window changes size ----
	RenderTexture2D glowTarget{}, blurA{}, blurB{}, blurC{};
	int glowWidth = 0, glowHeight = 0;
	auto makeGlowTargets = [&]()
	{
		const int w = std::max(GetRenderWidth() / 2, 1), h = std::max(GetRenderHeight() / 2, 1);
		if (w == glowWidth && h == glowHeight)
			return;
		for (RenderTexture2D* t : { &glowTarget, &blurA, &blurB, &blurC })
		{
			if (t->id != 0)
				UnloadRenderTexture(*t);
			*t = LoadRenderTexture(w, h);
			SetTextureFilter(t->texture, TEXTURE_FILTER_BILINEAR);
		}
		glowWidth = w;
		glowHeight = h;
	};
	makeGlowTargets();

	//the sky: stars on a sphere round the camera, a planet and the sun (drawn first, far away, never closer)
	std::vector<Vector3> stars;
	{
		std::mt19937 random(7);
		std::normal_distribution<float> gauss(0.f, 1.f);
		for (int i = 0; i < 2500; i++)
			stars.push_back(Vector3Normalize(Vector3{ gauss(random), gauss(random), gauss(random) }));
	}
	Model planet = LoadModelFromMesh(GenMeshSphere(1.f, 48, 64));
	planet.materials[0].shader = shader;
	const Vector3 planetDirection = Vector3Normalize(Vector3{ 0.8f, -0.22f, -0.57f });   //off to the side of the sun: half lit

	//particles
	Texture2D dot = softDot();
	std::vector<particle> particles;
	std::mt19937 random(11);
	auto uniform = [&](float _a, float _b) { return std::uniform_real_distribution<float>(_a, _b)(random); };

	//the cameras
	enum class viewKind { building, space };
	viewKind view = viewKind::building;
	int focus = 0;               //the vessel the cameras look at and the building view works on
	int deck = 0;
	float yaw = 45.f, yawGoal = 45.f;            //the building view's turn (degrees), eased to its goal
	float zoom = 70.f;                           //the building view's height in metres
	Vector3 pan{ 0.f, 0.f, 0.f };               //moved from the vessel's middle
	float orbitYaw = 35.f, orbitPitch = 22.f, orbitDistance = 220.f;
	bool followFlyer = false, flying = true, cutWalls = true, showRoofs = false;
	float sunAngle = 40.f;
	float flightTime = 0.f, clock = 0.f;
	//the lighting's settings
	bool sunOn = true, sunShadowsOn = true, lampShadowsOn = true, sunInside = false, glowOn = true, particlesOn = true;
	bool emergency = false, faulty = false;
	float ambientLevel = 0.45f, glowStrength = 1.f;
	float faultyDue = 1.f;
	int faultyCable = -1;
	std::string message;
	int frame = 0, shot = 0;
	Vector3 lastStern{};
	bool sternKnown = false;
	int lightsDrawn = 0;

	//a burst of sparks at one of a vessel's cables, with a flash that lights the room (its shadow cube drawn now)
	auto sparksAt = [&](int _vessel, int _cable)
	{
		const vesselData& v = scene[size_t(_vessel)];
		if (_cable < 0 || _cable >= int(v.cables.size()))
			return;
		const cableSpot& c = v.cables[size_t(_cable)];
		const Matrix place = placeOf(v);
		const Vector3 from = Vector3Transform(c.local, place);
		const float floorY = Vector3Transform(Vector3{ c.local.x, c.deck * deckHeight + floorThick, c.local.z }, place).y;
		const int count = int(uniform(50.f, 90.f));
		for (int i = 0; i < count; i++)
		{
			particle p;
			p.position = from;
			const float a = uniform(0.f, 6.283f), up = uniform(-0.1f, 1.f);
			const Vector3 way = Vector3Normalize(Vector3{ std::cos(a) * (1.f - up * 0.5f), up + 0.3f, std::sin(a) * (1.f - up * 0.5f) });
			p.velocity = Vector3Scale(way, uniform(2.f, 7.f));
			p.life = uniform(0.35f, 1.1f);
			p.size0 = uniform(0.1f, 0.17f);
			p.size1 = 0.04f;
			p.gravity = 9.8f;
			p.drag = 0.4f;
			p.floor = floorY + 0.02f;
			p.from = Color{ 255, 252, 220, 255 };
			p.to = Color{ 255, 110, 30, 0 };
			p.additive = true;
			p.streak = true;
			p.deck = c.deck;
			particles.push_back(p);
		}
		spark.vessel = _vessel;
		spark.light.local = Vector3Add(c.local, { 0.f, 0.3f, 0.f });
		spark.light.deck = c.deck;
		spark.age = 0.f;
		sparkBake = true;
	};
	//a cable on the deck shown (any if it has none), the nearest to a point in its vessel's frame or else any
	auto pickCable = [&](int _vessel, int _deck, const Vector3* _near) -> int
	{
		const vesselData& v = scene[size_t(_vessel)];
		std::vector<int> choices;
		for (int i = 0; i < int(v.cables.size()); i++)
			if (v.cables[size_t(i)].deck == _deck)
				choices.push_back(i);
		if (choices.empty())
			for (int i = 0; i < int(v.cables.size()); i++)
				choices.push_back(i);
		if (choices.empty())
			return -1;
		if (_near)
		{
			int best = choices[0];
			for (int i : choices)
				if (Vector3Distance(v.cables[size_t(i)].local, *_near) < Vector3Distance(v.cables[size_t(best)].local, *_near))
					best = i;
			return best;
		}
		return choices[size_t(random() % choices.size())];
	};

	while (!WindowShouldClose())
	{
		const float dt = shots ? 1.f / 60.f : GetFrameTime();
		clock += dt;
		if (shots)
		{
			//the tour: the station's lamps, turned a quarter, an emergency with sparks, space with the sun's shadows,
			//the flying ship's plume, the docked ship's cabin, the steam and the smelter
			if (frame == 0) { view = viewKind::building; zoom = 95.f; }
			if (frame == 70) yawGoal += 90.f;
			if (frame == 140) { yawGoal -= 90.f; zoom = 40.f; pan = Vector3{ -15.f, 0.f, -2.f }; emergency = true; }
			if (frame == 158)
			{
				const Vector3 spot{ -15.f, 0.f, -2.f };
				sparksAt(0, pickCable(0, 0, &spot));
			}
			if (frame == 180) { emergency = false; view = viewKind::space; orbitYaw = 140.f; orbitPitch = 30.f; orbitDistance = 115.f; pan = Vector3{}; }
			if (frame == 250) { followFlyer = true; orbitDistance = 70.f; orbitYaw = 20.f; orbitPitch = 16.f; }
			if (frame == 320) { focus = 1; followFlyer = false; view = viewKind::building; deck = 0; zoom = 40.f; }
			if (frame == 390) { focus = 0; zoom = 40.f; pan = Vector3{ -2.f, 0.f, -4.f }; }
			if (frame == 460) { sunInside = true; sunAngle = 30.f; zoom = 55.f; }
		}
		imguiRaylib::newFrame();
		makeGlowTargets();
		const bool mouseFree = !io.WantCaptureMouse, keysFree = !io.WantCaptureKeyboard;

		//the free ship's loop: round the station, rising and falling, banked into the turn
		Matrix flyerPlace = MatrixIdentity();
		Vector3 flyerPosition{}, flyerForwards{}, flyerVelocity{};
		if (flyer >= 0)
		{
			if (flying)
				flightTime += dt;
			vesselData& f = scene[size_t(flyer)];
			const float radius = 170.f, speed = 0.09f;
			const float a = flightTime * speed;
			flyerPosition = Vector3{ std::cos(a) * radius, 28.f * std::sin(a * 2.3f), std::sin(a) * radius };
			flyerVelocity = Vector3{ -std::sin(a) * radius * speed, 28.f * 2.3f * speed * std::cos(a * 2.3f), std::cos(a) * radius * speed };
			const Vector3 velocity = Vector3Normalize(flyerVelocity);
			flyerForwards = velocity;
			//its own forwards (in its grid) turned onto the way it is going, pitched up or down, banked into the turn
			const Vector3 ownForwards = Vector3Normalize(Vector3{ f.forwards.x, 0.f, f.forwards.y });
			const float yawTo = std::atan2(velocity.z, velocity.x) - std::atan2(ownForwards.z, ownForwards.x);
			const float pitch = std::asin(std::clamp(velocity.y, -1.f, 1.f));
			const Vector3 sideways = Vector3CrossProduct(ownForwards, Vector3{ 0.f, 1.f, 0.f });
			Matrix m = MatrixRotate(ownForwards, -0.35f);                       //banked
			m = MatrixMultiply(m, MatrixRotate(sideways, -pitch));             //nose up or down
			m = MatrixMultiply(m, MatrixRotateY(-yawTo));                     //along its way
			flyerPlace = MatrixMultiply(m, MatrixTranslate(flyerPosition.x, flyerPosition.y, flyerPosition.z));
		}
		auto placeAt = [&](int _i) { return _i == flyer ? flyerPlace : placeOf(scene[size_t(_i)]); };

		//---- input ----
		if (keysFree)
		{
			if (IsKeyPressed(KEY_V))
				view = view == viewKind::building ? viewKind::space : viewKind::building;
			if (IsKeyPressed(KEY_Q))
				yawGoal -= 90.f;
			if (IsKeyPressed(KEY_E))
				yawGoal += 90.f;
			if (IsKeyPressed(KEY_PAGE_UP))
				deck = std::min(deck + 1, int(scene[size_t(focus)].decks.size()) - 1);
			if (IsKeyPressed(KEY_PAGE_DOWN))
				deck = std::max(deck - 1, 0);
			if (IsKeyPressed(KEY_F) && flyer >= 0)
				followFlyer = !followFlyer;
			if (IsKeyPressed(KEY_TAB))
			{
				focus = (focus + 1) % int(scene.size());
				deck = std::min(deck, int(scene[size_t(focus)].decks.size()) - 1);
				pan = Vector3{};
			}
			if (IsKeyPressed(KEY_L))
				emergency = !emergency;
			if (IsKeyPressed(KEY_K) && focus != flyer)
				sparksAt(focus, pickCable(focus, deck, nullptr));
		}
		yaw += (yawGoal - yaw) * std::min(1.f, dt * 10.f);
		const vesselData& focused = scene[size_t(focus)];
		const Vector3 focusPoint{ focused.position.x, deck * deckHeight, focused.position.y };

		Camera3D camera{};
		camera.up = Vector3{ 0.f, 1.f, 0.f };
		if (view == viewKind::building)
		{
			const float y = yaw * DEG2RAD, p = isoPitch * DEG2RAD;
			const Vector3 back{ std::cos(p) * std::cos(y), std::sin(p), std::cos(p) * std::sin(y) };
			if (mouseFree)
			{
				const float wheel = GetMouseWheelMove();
				if (wheel != 0.f)
					zoom = std::clamp(zoom * std::pow(0.88f, wheel), 12.f, 420.f);
				if (zoom >= 420.f && wheel < 0.f)
				{
					//zoomed right out: on into space
					view = viewKind::space;
					orbitYaw = yaw;
					orbitPitch = isoPitch;
					orbitDistance = 260.f;
				}
				if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
				{
					//drag the deck along: across the screen, and up the screen (along the ground, so further)
					const Vector2 d = GetMouseDelta();
					const float perPixel = zoom / float(GetScreenHeight());
					const Vector3 right{ std::sin(y), 0.f, -std::cos(y) };
					const Vector3 away{ -std::cos(y), 0.f, -std::sin(y) };
					pan = Vector3Add(pan, Vector3Scale(right, -d.x * perPixel));
					pan = Vector3Add(pan, Vector3Scale(away, d.y * perPixel / std::sin(p)));
				}
			}
			camera.projection = CAMERA_ORTHOGRAPHIC;
			camera.fovy = zoom;
			camera.target = Vector3Add(focusPoint, pan);
			camera.position = Vector3Add(camera.target, Vector3Scale(back, 400.f));
		}
		else
		{
			if (mouseFree)
			{
				const float wheel = GetMouseWheelMove();
				if (wheel != 0.f)
					orbitDistance = std::clamp(orbitDistance * std::pow(0.88f, wheel), 20.f, 700.f);
				if (orbitDistance <= 20.f && wheel > 0.f && !followFlyer)
				{
					//zoomed right in: back down to the deck
					view = viewKind::building;
					zoom = 400.f;
				}
				if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
				{
					const Vector2 d = GetMouseDelta();
					orbitYaw += d.x * 0.3f;
					orbitPitch = std::clamp(orbitPitch + d.y * 0.3f, -80.f, 85.f);
				}
			}
			const Vector3 target = followFlyer && flyer >= 0 ? flyerPosition : Vector3{ focused.position.x, 4.f, focused.position.y };
			const float y = orbitYaw * DEG2RAD, p = orbitPitch * DEG2RAD;
			camera.projection = CAMERA_PERSPECTIVE;
			camera.fovy = 50.f;
			camera.target = target;
			camera.position = Vector3Add(target, Vector3Scale(Vector3{ std::cos(p) * std::cos(y), std::sin(p), std::cos(p) * std::sin(y) }, orbitDistance));
		}

		//the tile under the mouse (building view): where its ray meets the deck's floor, in the focused vessel's grid
		bool picked = false;
		int pickX = 0, pickY = 0;
		if (view == viewKind::building && mouseFree)
		{
			const Ray ray = GetScreenToWorldRay(GetMousePosition(), camera);
			const float floorTop = deck * deckHeight + floorThick;
			if (std::abs(ray.direction.y) > 1e-5f)
			{
				const float t = (floorTop - ray.position.y) / ray.direction.y;
				const Vector3 hit = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
				const Vector3 local = Vector3Transform(Vector3Subtract(hit, Vector3{ focused.position.x, 0.f, focused.position.y }), MatrixRotateY(focused.heading));
				pickX = int(std::floor((local.x + focused.centre.x) / T)) + 1;
				pickY = int(std::floor((local.z + focused.centre.y) / T)) + 1;
				picked = true;
				if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
				{
					//lay or lift a floor (only where nothing else is built: it is a feel for picking, not the game)
					deckData& d = scene[size_t(focus)].decks[size_t(deck)];
					bool busy = false;
					for (const item& b : d.items)
						busy = busy || (pickX >= b.x && pickX < b.x + b.w && pickY >= b.y && pickY < b.y + b.h);
					for (const auto& w : d.walls)
						busy = busy || (w[0] == pickX && w[1] == pickY);
					for (const line& l : d.lines)
						busy = busy || (l.x == pickX && l.y == pickY);
					if (busy)
						message = "something is built on that tile";
					else
					{
						if (!d.floors.erase({ pickX, pickY }))
							d.floors.insert({ pickX, pickY });
						d.frames.erase({ pickX, pickY });
						buildDeck(scene[size_t(focus)], deck, shader);
						if (deck > 0)
							buildDeck(scene[size_t(focus)], deck - 1, shader);   //its roof
						rebake = focus;
						message.clear();
					}
				}
			}
		}

		//---- the lights this frame ----
		lightLook look;
		look.time = clock;
		look.emergency = emergency;
		look.flying = flying;
		spark.age += dt;
		if (faulty && focus != flyer)
		{
			if (faultyCable < 0 || faultyCable >= int(scene[size_t(focus)].cables.size()))
				faultyCable = pickCable(focus, deck, nullptr);
			faultyDue -= dt;
			if (faultyDue <= 0.f)
			{
				sparksAt(focus, faultyCable);
				faultyDue = uniform(0.8f, 3.5f);
			}
		}
		else
			faultyCable = -1;
		if (rebake >= 0)
		{
			bindShadows(false);
			bake(rebake, false);
			rebake = -1;
		}
		if (sparkBake && spark.vessel >= 0)
		{
			bindShadows(false);
			bake(-1, true);
			sparkBake = false;
		}
		{
			const Vector3 lampGlow = emergency ? Vector3Scale({ 1.6f, 0.12f, 0.08f }, look.pulse()) : Vector3{ 1.3f, 1.3f, 1.3f };
			const Vector3 statusGlow = emergency ? (look.blink() ? Vector3{ 1.4f, 0.2f, 0.15f } : Vector3{ 0.15f, 0.05f, 0.05f }) : Vector3{ 0.5f, 1.2f, 0.6f };
			const float fire = 1.25f * look.fire(), dock = 1.3f * look.dock();
			SetShaderValue(shader, u.lampGlow, &lampGlow, SHADER_UNIFORM_VEC3);
			SetShaderValue(shader, u.statusGlow, &statusGlow, SHADER_UNIFORM_VEC3);
			SetShaderValue(shader, u.fireGlow, &fire, SHADER_UNIFORM_FLOAT);
			SetShaderValue(shader, u.dockGlow, &dock, SHADER_UNIFORM_FLOAT);
		}
		//a vessel's lights for its draw: those that are on, the nearest the camera's target if there are too many
		lightsDrawn = 0;
		auto uploadLights = [&](int _vessel, const Matrix& _place)
		{
			const Matrix toLocal = MatrixInvert(_place);
			const Vector3 targetLocal = Vector3Transform(camera.target, toLocal);
			struct onLight { Vector3 position, colour; float reach; Vector2 shadow; float distance; };
			std::vector<onLight> on;
			auto add = [&](const lightSource& _l, Vector3 _colour, float _reach)
			{
				if (_colour.x + _colour.y + _colour.z < 0.003f)
					return;
				const float tile = lampShadowsOn && _l.tile >= 0 ? float(_l.tile) : -1.f;
				on.push_back({ _l.local, _colour, _reach, Vector2{ tile, _l.reach }, Vector3Distance(_l.local, targetLocal) });
			};
			for (const lightSource& l : scene[size_t(_vessel)].lights)
			{
				float reach = 0.f;
				const Vector3 colour = lightNow(l, look, reach);
				add(l, colour, reach);
			}
			if (spark.vessel == _vessel && spark.age < 0.35f)
			{
				const float f = 1.f - spark.age / 0.35f;
				add(spark.light, Vector3Scale({ 1.f, 0.85f, 0.55f }, 3.5f * f * f * uniform(0.6f, 1.f)), spark.light.reach);
			}
			if (int(on.size()) > maxLights)
			{
				std::partial_sort(on.begin(), on.begin() + maxLights, on.end(), [](const onLight& _a, const onLight& _b) { return _a.distance < _b.distance; });
				on.resize(size_t(maxLights));
			}
			std::vector<Vector3> positions, colours;
			std::vector<float> reaches;
			std::vector<Vector2> shadows;
			for (const onLight& l : on)
			{
				positions.push_back(l.position);
				colours.push_back(l.colour);
				reaches.push_back(l.reach);
				shadows.push_back(l.shadow);
			}
			const int count = int(on.size());
			SetShaderValue(shader, u.lightCount, &count, SHADER_UNIFORM_INT);
			if (count > 0)
			{
				SetShaderValueV(shader, u.lightPosition, positions.data(), SHADER_UNIFORM_VEC3, count);
				SetShaderValueV(shader, u.lightColour, colours.data(), SHADER_UNIFORM_VEC3, count);
				SetShaderValueV(shader, u.lightRadius, reaches.data(), SHADER_UNIFORM_FLOAT, count);
				SetShaderValueV(shader, u.lightShadow, shadows.data(), SHADER_UNIFORM_VEC2, count);
			}
			const Vector3 eye = Vector3Transform(camera.position, toLocal);
			SetShaderValue(shader, u.eye, &eye, SHADER_UNIFORM_VEC3);
			lightsDrawn += count;
		};

		//the sun
		const float s = sunAngle * DEG2RAD;
		const Vector3 sunDirection = Vector3Normalize(Vector3{ -std::cos(s) * 0.6f, -std::sin(s), -std::cos(s) * 0.8f });
		const Vector3 sunColour = sunOn ? Vector3{ 0.95f, 0.9f, 0.82f } : Vector3{ 0.f, 0.f, 0.f };
		const Vector3 ambient = Vector3Scale(view == viewKind::space ? Vector3{ 0.45f, 0.5f, 0.62f } : Vector3{ 0.62f, 0.62f, 0.68f }, ambientLevel);
		SetShaderValue(shader, u.sunDirection, &sunDirection, SHADER_UNIFORM_VEC3);
		SetShaderValue(shader, u.sunColour, &sunColour, SHADER_UNIFORM_VEC3);
		SetShaderValue(shader, u.ambient, &ambient, SHADER_UNIFORM_VEC3);

		//---- particles: steam, smoke, the plume; then they move ----
		if (particlesOn)
		{
			for (int i = 0; i < int(scene.size()); i++)
			{
				const Matrix place = placeAt(i);
				for (vent& vt : scene[size_t(i)].vents)
				{
					vt.due -= dt;
					while (vt.due <= 0.f)
					{
						vt.due += vt.smoke ? 1.f / 7.f : 1.f / 12.f;
						particle p;
						p.position = Vector3Transform(Vector3Add(vt.local, { uniform(-0.15f, 0.15f), 0.f, uniform(-0.15f, 0.15f) }), place);
						p.deck = vt.deck;
						if (vt.smoke)
						{
							p.velocity = Vector3{ uniform(-0.12f, 0.12f), uniform(0.45f, 0.7f), uniform(0.1f, 0.35f) };
							p.life = uniform(3.5f, 5.f);
							p.size0 = 0.45f;
							p.size1 = 2.6f;
							p.drag = 0.25f;
							p.from = Color{ 92, 86, 80, 170 };
							p.to = Color{ 140, 134, 128, 0 };
						}
						else
						{
							p.velocity = Vector3{ uniform(-0.25f, 0.25f), uniform(0.9f, 1.35f), uniform(-0.25f, 0.25f) };
							p.life = uniform(2.4f, 3.4f);
							p.size0 = 0.3f;
							p.size1 = 1.8f;
							p.drag = 0.5f;
							p.from = Color{ 225, 230, 238, 120 };
							p.to = Color{ 205, 210, 220, 0 };
						}
						particles.push_back(p);
					}
				}
			}
			if (flyer >= 0 && flying)
			{
				const vesselData& f = scene[size_t(flyer)];
				const Vector3 stern = Vector3Subtract(flyerPosition, Vector3Scale(flyerForwards, f.radius * 0.75f));
				if (!sternKnown)
					lastStern = stern;
				const int count = std::max(1, int(240.f * dt + uniform(0.f, 1.f)));
				for (int k = 0; k < count; k++)
				{
					particle p;
					const float along = (k + uniform(0.f, 1.f)) / float(count);
					p.position = Vector3Lerp(lastStern, stern, along);
					p.velocity = Vector3Add(Vector3Subtract(flyerVelocity, Vector3Scale(flyerForwards, 30.f)), { uniform(-2.5f, 2.5f), uniform(-2.5f, 2.5f), uniform(-2.5f, 2.5f) });
					p.life = uniform(0.45f, 0.8f);
					p.age = (1.f - along) * dt;
					p.size0 = 1.3f;
					p.size1 = 4.f;
					p.from = Color{ 255, 214, 150, 230 };
					p.to = Color{ 255, 70, 20, 0 };
					p.additive = true;
					particles.push_back(p);
				}
				lastStern = stern;
				sternKnown = true;
			}
			else
				sternKnown = false;
		}
		for (particle& p : particles)
		{
			p.age += dt;
			p.velocity.y -= p.gravity * dt;
			p.velocity = Vector3Scale(p.velocity, std::exp(-p.drag * dt));
			p.position = Vector3Add(p.position, Vector3Scale(p.velocity, dt));
			if (p.position.y < p.floor)
			{
				p.position.y = p.floor;
				p.velocity.y = -p.velocity.y * 0.35f;
				p.velocity.x *= 0.6f;
				p.velocity.z *= 0.6f;
			}
		}
		particles.erase(std::remove_if(particles.begin(), particles.end(), [](const particle& _p) { return _p.age >= _p.life; }), particles.end());
		if (!particlesOn)
			particles.clear();
		auto drawParticles = [&](bool _glowOnly)
		{
			rlDrawRenderBatchActive();
			rlDisableDepthMask();
			BeginBlendMode(BLEND_ADDITIVE);
			for (const particle& p : particles)
			{
				if (!p.additive || (view == viewKind::building && p.deck > deck))
					continue;
				const float t = p.age / p.life;
				const Color c = mixColour(p.from, p.to, t);
				if (p.streak)
					DrawLine3D(p.position, Vector3Subtract(p.position, Vector3Scale(p.velocity, 0.05f)), c);
				DrawBillboard(camera, dot, p.position, p.size0 + (p.size1 - p.size0) * t, c);
			}
			EndBlendMode();
			if (!_glowOnly)
			{
				//steam and smoke: back to front
				std::vector<std::pair<float, const particle*>> order;
				for (const particle& p : particles)
					if (!p.additive && !(view == viewKind::building && p.deck > deck))
						order.push_back({ Vector3DistanceSqr(p.position, camera.position), &p });
				std::sort(order.begin(), order.end(), [](const auto& _a, const auto& _b) { return _a.first > _b.first; });
				for (const auto& [distance, p] : order)
				{
					const float t = p->age / p->life;
					DrawBillboard(camera, dot, p->position, p->size0 + (p->size1 - p->size0) * t, mixColour(p->from, p->to, t));
				}
				rlDrawRenderBatchActive();
			}
			rlEnableDepthMask();
		};

		//---- the sun's shadow map: everything as it is built (roofs and full walls), over what the camera sees ----
		Matrix sunMatrix = MatrixIdentity();
		const bool sunShadowing = sunOn && sunShadowsOn;
		if (sunShadowing)
		{
			const float half = view == viewKind::building ? std::max(zoom * 1.1f, 30.f) : std::clamp(orbitDistance * 0.7f, 40.f, 320.f);
			const Vector3 centre = camera.target;
			const Vector3 up = std::abs(sunDirection.y) > 0.99f ? Vector3{ 1.f, 0.f, 0.f } : Vector3{ 0.f, 1.f, 0.f };
			const Matrix sunView = MatrixLookAt(Vector3Subtract(centre, Vector3Scale(sunDirection, 600.f)), centre, up);
			const Matrix sunProjection = MatrixOrtho(-half, half, -half, half, 1.0, 1200.0);
			bindShadows(false);
			setMode(1);
			BeginTextureMode(sunMap);
			rlClearScreenBuffers();
			rlEnableDepthTest();
			rlSetCullFace(RL_CULL_FACE_FRONT);
			rlSetMatrixProjection(sunProjection);
			rlSetMatrixModelview(sunView);
			for (int i = 0; i < int(scene.size()); i++)
			{
				const Matrix place = placeAt(i);
				for (int d = 0; d < int(scene[size_t(i)].models.size()); d++)
				{
					const vesselData::deckModels& m = scene[size_t(i)].models[size_t(d)];
					if (sunInside && view == viewKind::building)
					{
						//only what is shown: the sun into the cut-away rooms
						if (d > deck)
							continue;
						drawAt(m.base, place);
						drawAt(d == deck && cutWalls ? m.wallsCut : m.wallsFull, place);
						continue;
					}
					drawAt(m.base, place);
					drawAt(m.wallsFull, place);
					drawAt(m.roof, place);
				}
			}
			rlSetCullFace(RL_CULL_FACE_BACK);
			rlDisableDepthTest();
			EndTextureMode();
			setMode(0);
			sunMatrix = MatrixMultiply(sunView, sunProjection);
			const float texel = 1.f / float(sunSize), offset = 1.5f * 2.f * half / float(sunSize), bias = 0.03f / 1199.f;
			SetShaderValue(shader, u.sunTexel, &texel, SHADER_UNIFORM_FLOAT);
			SetShaderValue(shader, u.sunOffset, &offset, SHADER_UNIFORM_FLOAT);
			SetShaderValue(shader, u.sunBias, &bias, SHADER_UNIFORM_FLOAT);
		}
		SetShaderValueMatrix(shader, u.sunMatrix, sunMatrix);
		{
			const int on = sunShadowing ? 1 : 0;
			SetShaderValue(shader, u.sunShadows, &on, SHADER_UNIFORM_INT);
		}
		bindShadows(true);

		//the vessels, as each view shows them (mode 0 lit, 2 only what glows)
		auto drawVessels = [&](bool _lit)
		{
			for (int i = 0; i < int(scene.size()); i++)
			{
				vesselData& v = scene[size_t(i)];
				const Matrix place = placeAt(i);
				if (_lit)
					uploadLights(i, place);
				for (int d = 0; d < int(v.models.size()); d++)
				{
					const vesselData::deckModels& m = v.models[size_t(d)];
					if (view == viewKind::building)
					{
						if (d > deck)
							continue;   //decks above the one shown are cut away
						const float dim = d == deck ? 1.f : 0.45f;
						drawModel(m.base, place, shader, u.dim, dim);
						drawModel(d == deck && cutWalls ? m.wallsCut : m.wallsFull, place, shader, u.dim, dim);
						if (showRoofs && d < deck)
							drawModel(m.roof, place, shader, u.dim, dim);
					}
					else
					{
						drawModel(m.base, place, shader, u.dim, 1.f);
						drawModel(m.wallsFull, place, shader, u.dim, 1.f);
						drawModel(m.roof, place, shader, u.dim, 1.f);
					}
				}
			}
		};
		auto drawFlame = [&]()
		{
			if (flyer < 0 || view != viewKind::space || !flying)
				return;
			//its engines' flame, out behind it
			const vesselData& f = scene[size_t(flyer)];
			const Vector3 stern = Vector3Subtract(flyerPosition, Vector3Scale(flyerForwards, f.radius * 0.75f));
			const float flicker = 0.85f + 0.15f * std::sin(flightTime * 40.f);
			DrawCylinderEx(stern, Vector3Subtract(stern, Vector3Scale(flyerForwards, 9.f * flicker)), 1.4f, 0.1f, 12, Color{ 255, 170, 70, 255 });
			DrawCylinderEx(stern, Vector3Subtract(stern, Vector3Scale(flyerForwards, 4.f * flicker)), 0.8f, 0.1f, 12, Color{ 255, 240, 200, 255 });
		};
		const Vector3 sunAt = Vector3Add(camera.position, Vector3Scale(sunDirection, -880.f));

		//---- the glow: what glows, drawn alone (the rest black, to hide what is behind), then blurred ----
		if (glowOn)
		{
			setMode(2);
			BeginTextureMode(glowTarget);
			ClearBackground(BLACK);
			BeginMode3D(camera);
			if (view == viewKind::space && sunOn)
			{
				rlDisableDepthMask();
				DrawSphere(sunAt, 14.f, Color{ 255, 244, 214, 255 });
				rlDrawRenderBatchActive();
				rlEnableDepthMask();
			}
			drawVessels(false);
			drawFlame();
			drawParticles(true);
			EndMode3D();
			EndTextureMode();
			setMode(0);
			auto blurInto = [&](const RenderTexture2D& _from, RenderTexture2D& _to, Vector2 _way)
			{
				BeginTextureMode(_to);
				ClearBackground(BLACK);
				BeginShaderMode(blur);
				SetShaderValue(blur, blurWayLoc, &_way, SHADER_UNIFORM_VEC2);
				DrawTextureRec(_from.texture, Rectangle{ 0.f, 0.f, float(_from.texture.width), -float(_from.texture.height) }, Vector2{ 0.f, 0.f }, WHITE);
				EndShaderMode();
				EndTextureMode();
			};
			const float tx = 1.f / float(glowWidth), ty = 1.f / float(glowHeight);
			blurInto(glowTarget, blurA, { tx * 1.2f, 0.f });   //a tight glow into B
			blurInto(blurA, blurB, { 0.f, ty * 1.2f });
			blurInto(blurB, blurA, { tx * 3.f, 0.f });          //and a wide one into C
			blurInto(blurA, blurC, { 0.f, ty * 3.f });
			blurInto(blurC, blurA, { tx * 6.f, 0.f });
			blurInto(blurA, blurC, { 0.f, ty * 6.f });
		}

		//---- drawing ----
		BeginDrawing();
		ClearBackground(view == viewKind::space ? Color{ 2, 3, 8, 255 } : Color{ 10, 9, 8, 255 });
		BeginMode3D(camera);
		if (view == viewKind::space)
		{
			//the sky, round the camera, behind everything
			rlDisableDepthMask();
			rlBegin(RL_LINES);
			for (size_t i = 0; i < stars.size(); i++)
			{
				const unsigned char b = (unsigned char)(120 + (i * 37) % 130);
				rlColor4ub(b, b, (unsigned char)std::min(255, b + 25), 255);
				const Vector3 p = Vector3Add(camera.position, Vector3Scale(stars[i], 900.f));
				rlVertex3f(p.x, p.y, p.z);
				rlVertex3f(p.x + 0.6f, p.y + 0.6f, p.z);
			}
			rlEnd();
			const float full = 1.f;
			const int none = 0;
			SetShaderValue(shader, u.dim, &full, SHADER_UNIFORM_FLOAT);
			SetShaderValue(shader, u.lightCount, &none, SHADER_UNIFORM_INT);
			SetShaderValue(shader, u.sunShadows, &none, SHADER_UNIFORM_INT);
			const Vector3 planetAt = Vector3Add(camera.position, Vector3Scale(planetDirection, 850.f));
			DrawModel(planet, planetAt, 190.f, Color{ 90, 140, 200, 255 });
			const int on = sunShadowing ? 1 : 0;
			SetShaderValue(shader, u.sunShadows, &on, SHADER_UNIFORM_INT);
			if (sunOn)
				DrawSphere(sunAt, 14.f, Color{ 255, 244, 214, 255 });
			rlDrawRenderBatchActive();
			rlEnableDepthMask();
		}
		drawVessels(true);
		drawFlame();
		if (particlesOn)
			drawParticles(false);
		if (picked)
		{
			//the picked tile's outline, on the deck
			const Vector3 middleLocal{ (pickX - 0.5f) * T - focused.centre.x, deck * deckHeight + floorThick + 0.05f, (pickY - 0.5f) * T - focused.centre.y };
			const Vector3 middle = Vector3Transform(middleLocal, placeOf(focused));
			DrawCubeWires(middle, T, 0.1f, T, Color{ 255, 190, 60, 255 });
		}
		EndMode3D();
		if (glowOn)
		{
			//the glow added on top: the tight one and the wide one
			BeginBlendMode(BLEND_ADDITIVE);
			BeginShaderMode(adder);
			const Rectangle source{ 0.f, 0.f, float(glowWidth), -float(glowHeight) }, screen{ 0.f, 0.f, float(GetScreenWidth()), float(GetScreenHeight()) };
			float strength = 0.7f * glowStrength;
			SetShaderValue(adder, strengthLoc, &strength, SHADER_UNIFORM_FLOAT);
			DrawTexturePro(blurB.texture, source, screen, Vector2{ 0.f, 0.f }, 0.f, WHITE);
			rlDrawRenderBatchActive();
			strength = 1.1f * glowStrength;
			SetShaderValue(adder, strengthLoc, &strength, SHADER_UNIFORM_FLOAT);
			DrawTexturePro(blurC.texture, source, screen, Vector2{ 0.f, 0.f }, 0.f, WHITE);
			EndShaderMode();
			EndBlendMode();
		}

		//---- the panel ----
		ImGui::SetNextWindowPos(ImVec2(float(GetScreenWidth()) - 12.f, 12.f), ImGuiCond_Always, ImVec2(1.f, 0.f));
		ImGui::SetNextWindowBgAlpha(0.88f);
		ImGui::Begin("prototype", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
		ImGui::TextColored(uiTheme::bright(), "STATIONBUILDER 3D PROTOTYPE");
		ImGui::TextDisabled("raylib, boxes from scene.txt");
		ImGui::Separator();
		int viewChoice = view == viewKind::building ? 0 : 1;
		if (ImGui::RadioButton("Building (isometric)", viewChoice == 0)) view = viewKind::building;
		ImGui::SameLine();
		if (ImGui::RadioButton("Space", viewChoice == 1)) view = viewKind::space;
		ImGui::TextDisabled("V switches; zoom out far to go to space");
		ImGui::SetNextItemWidth(220.f);
		if (ImGui::BeginCombo("focus", scene[size_t(focus)].name.c_str()))
		{
			for (int i = 0; i < int(scene.size()); i++)
				if (ImGui::Selectable(scene[size_t(i)].name.c_str(), i == focus))
				{
					focus = i;
					deck = std::min(deck, int(scene[size_t(focus)].decks.size()) - 1);
					pan = Vector3{};
				}
			ImGui::EndCombo();
		}
		if (view == viewKind::building)
		{
			ImGui::Text("deck %d of %d", deck, int(focused.decks.size()));
			ImGui::SameLine();
			if (ImGui::SmallButton("up")) deck = std::min(deck + 1, int(focused.decks.size()) - 1);
			ImGui::SameLine();
			if (ImGui::SmallButton("down")) deck = std::max(deck - 1, 0);
			if (ImGui::Button("< turn")) yawGoal -= 90.f;
			ImGui::SameLine();
			if (ImGui::Button("turn >")) yawGoal += 90.f;
			ImGui::Checkbox("walls cut away", &cutWalls);
			ImGui::Checkbox("roofs on the decks below", &showRoofs);
			if (picked)
				ImGui::Text("tile %d,%d", pickX, pickY);
			else
				ImGui::TextDisabled("tile -");
			if (!message.empty())
				ImGui::TextColored(uiTheme::warning, "%s", message.c_str());
		}
		else
		{
			ImGui::Checkbox("the free ship flies", &flying);
			ImGui::Checkbox("follow it (F)", &followFlyer);
		}
		ImGui::Separator();
		ImGui::TextColored(uiTheme::bright(), "LIGHTING");
		ImGui::Checkbox("sun", &sunOn);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(170.f);
		ImGui::SliderFloat("##sun", &sunAngle, 5.f, 85.f, "%.0f deg up");
		ImGui::SetNextItemWidth(220.f);
		ImGui::SliderFloat("ambient", &ambientLevel, 0.f, 1.f, "%.2f");
		ImGui::Checkbox("sun shadows", &sunShadowsOn);
		ImGui::SameLine();
		ImGui::Checkbox("lamp shadows", &lampShadowsOn);
		ImGui::Checkbox("sunlight into the cut-away rooms", &sunInside);
		ImGui::Checkbox("glow", &glowOn);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(170.f);
		ImGui::SliderFloat("##glow", &glowStrength, 0.f, 2.f, "strength %.2f");
		ImGui::Checkbox("particles", &particlesOn);
		ImGui::Checkbox("emergency power (L)", &emergency);
		if (ImGui::Button("sparks (K)") && focus != flyer)
			sparksAt(focus, pickCable(focus, deck, nullptr));
		ImGui::SameLine();
		ImGui::Checkbox("a faulty cable", &faulty);
		ImGui::TextDisabled("%d lights drawn, %d particles", lightsDrawn, int(particles.size()));
		ImGui::Separator();
		ImGui::TextDisabled("building: Q E turn, wheel zooms, right drag moves,");
		ImGui::TextDisabled("  Page Up/Down decks, left click lays/lifts a floor");
		ImGui::TextDisabled("space: right drag orbits, wheel zooms, F follows");
		ImGui::TextDisabled("Tab: next vessel     %d fps", GetFPS());
		ImGui::End();
		imguiRaylib::render();
		const int shotFrames[] = { 60, 130, 168, 240, 310, 380, 450, 520 };
		if (shots && std::find(std::begin(shotFrames), std::end(shotFrames), frame) != std::end(shotFrames))
		{
			Image picture = LoadImageFromScreen();
			ExportImage(picture, (shotFolder + "/demo_" + std::to_string(shot++) + ".png").c_str());
			UnloadImage(picture);
		}
		EndDrawing();
		frame++;
		if (shots && frame > 520)
			break;
	}

	for (vesselData& v : scene)
		for (vesselData::deckModels& m : v.models)
		{
			freeModel(m.base);
			freeModel(m.wallsFull);
			freeModel(m.wallsCut);
			freeModel(m.roof);
		}
	freeModel(planet);
	for (RenderTexture2D* t : { &glowTarget, &blurA, &blurB, &blurC })
		UnloadRenderTexture(*t);
	rlUnloadFramebuffer(sunMap.id);
	rlUnloadFramebuffer(atlas.target.id);
	UnloadTexture(dot);
	UnloadShader(blur);
	UnloadShader(adder);
	UnloadShader(shader);
	imguiRaylib::shutdown();
	CloseWindow();
	return 0;
}
