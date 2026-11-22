#include "exteriorView.h"
#include "station.h"
#include "tileCatalogue.h"
#include <imgui.h>
#include "uiTheme.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

namespace
{
	const float depthPerDeck = exteriorView::deckHeight / 32.f;   //a deck's height counted in tiles of depth, for the drawing order

	//something to draw, in order from the back: a hull block's faces (a run of vertices) or an outside object's picture
	struct drawable
	{
		float depth = 0.f;
		int start = 0;
		int count = 0;
		const tileObject* object = nullptr;
		float lift = 0.f;
		sf::Vector2f shift;     //a vessel alongside: where its grid lies on the station's
	};

	sf::Color lighter(sf::Color _c, int _by)
	{
		return sf::Color(sf::Uint8(std::min(255, _c.r + _by)), sf::Uint8(std::min(255, _c.g + _by)), sf::Uint8(std::min(255, _c.b + _by)), _c.a);
	}
	void quad(std::vector<sf::Vertex>& _v, sf::Vector2f _a, sf::Vector2f _b, sf::Vector2f _c, sf::Vector2f _d, sf::Color _colour)
	{
		for (const sf::Vector2f& p : { _a, _b, _c, _a, _c, _d })
			_v.push_back(sf::Vertex(p, _colour));
	}
	//a wall on a tile's side: its own on that edge, or its neighbour's on the edge they share
	bool walled(isometricLevel& _level, sf::Vector2i _tile, int _side)
	{
		bool found = false;
		auto look = [&](sf::Vector2i _at, int _edge) {
			if (!_level.isInLevel(_at))
				return;
			_level.levelTiles[_level.coords2TileIndex(_at.x, _at.y)].forEachObject([&](tileObject* _o) {
				found = found || (_o->type && _o->type->layer == tileLayer::wall && _o->type->wallEdge == _edge);
			});
		};
		look(_tile, _side);
		look(_tile + footprint::step(_side), connections::opposite(_side));
		return found;
	}
	//what is outside on a tile with no floor: a frame, then what is on it or mounted there (a big machine once: on its
	//front tile, where the deck view draws it). cables, pipes and tubes on frames show too
	template <typename F>
	void forEachOutside(isometricLevel& _level, sf::Vector2i _tile, F&& _visit)
	{
		const tile& t = _level.levelTiles[_level.coords2TileIndex(_tile.x, _tile.y)];
		if (t.hasFloor())
			return;
		t.forEachObject([&](tileObject* _o) {
			if (_o->type && ((_o->type->layer == tileLayer::fixture && !_o->isPart()) || _o->type->frame))
				_visit(_o);
		});
	}
}

namespace exteriorView
{
	void draw(sf::RenderWindow& _window, station& _station, int _hoveredDeck, const std::vector<alongside>& _alongside)
	{
		//stars, fixed to the screen (drifting a little as the view moves)
		const sf::View view = _window.getView();
		_window.setView(_window.getDefaultView());
		const sf::Vector2u size = _window.getSize();
		sf::VertexArray stars(sf::Points);
		std::uint32_t seed = 12345u;
		auto next = [&]() { seed = seed * 1664525u + 1013904223u; return seed; };
		for (int i = 0; i < 260; i++)
		{
			const float drift = 0.02f + 0.03f * float(i % 3);
			float x = std::fmod(float(next() % 4096u) - view.getCenter().x * drift, float(size.x));
			float y = std::fmod(float(next() % 4096u) - view.getCenter().y * drift, float(size.y));
			if (x < 0.f) x += float(size.x);
			if (y < 0.f) y += float(size.y);
			const sf::Uint8 b = sf::Uint8(110 + next() % 140);
			stars.append(sf::Vertex(sf::Vector2f(x, y), sf::Color(b, b, sf::Uint8(std::min(255, b + 20)))));
		}
		_window.draw(stars);
		_window.setView(view);

		std::vector<sf::Vertex> vertices;
		std::vector<drawable> drawables;
		const float h = deckHeight;
		//one vessel's decks: the station's, or a ship docked beside it (its grid moved by whole tiles and decks, all sorted
		//together so they overlap right)
		auto addVessel = [&](station& _s, sf::Vector2i _offset, int _deckOffset, sf::Vector2f _shift, int _hovered) {
		const int decks = _s.getLayerListSize();
		for (int deck = 0; deck < decks; deck++)
		{
			isometricLevel& level = *_s.getIsoLevel(float(deck));
			isometricLevel* above = deck + 1 < decks ? _s.getIsoLevel(float(deck + 1)) : nullptr;
			const float z0 = float(deck + _deckOffset) * h, z1 = z0 + h;
			const int glow = deck == _hovered ? 28 : 0;
			const int w = int(level.getSizeW()), rows = int(level.getSizeH());
			for (int y = 1; y <= rows; y++)
			{
				for (int x = 1; x <= w; x++)
				{
					const sf::Vector2i at(x, y);
					if (!level.hasFloor(at))
					{
						forEachOutside(level, at, [&](const tileObject* _o) {
							drawable d;
							d.depth = float(x + _offset.x + y + _offset.y) + float(deck + _deckOffset) * depthPerDeck + 0.5f;
							d.object = _o;
							d.lift = z0;
							d.shift = _shift;
							drawables.push_back(d);
						});
						continue;
					}
					drawable d;
					d.depth = float(x + _offset.x + y + _offset.y) + float(deck + _deckOffset) * depthPerDeck;
					d.start = int(vertices.size());
					const sf::Vector2f c = level.tileCentreWorld(sf::Vector2f(at)) + _shift;
					const sf::Vector2f top = c + sf::Vector2f(0.f, -16.f), right = c + sf::Vector2f(32.f, 0.f), bottom = c + sf::Vector2f(0.f, 16.f),
						left = c + sf::Vector2f(-32.f, 0.f);
					auto up = [](sf::Vector2f _p, float _z) { return _p - sf::Vector2f(0.f, _z); };
					//its roof, where nothing is built on the deck above
					if (!above || !above->hasFloor(at))
					{
						const sf::Color roof = lighter((x + y) % 2 ? sf::Color(138, 146, 158) : sf::Color(146, 154, 166), glow);
						quad(vertices, up(top, z1), up(right, z1), up(bottom, z1), up(left, z1), roof);
					}
					//its two near sides, where the tile beside it is open
					for (const int side : { 2, 3 })
					{
						if (level.hasFloor(at + footprint::step(side)))
							continue;
						const sf::Vector2f a = side == 2 ? left : bottom, b = side == 2 ? bottom : right;
						auto on = [&](float _u, float _z) { return up(a + (b - a) * _u, _z); };
						const bool plated = walled(level, at, side);
						const sf::Color face = plated ? (side == 2 ? sf::Color(118, 126, 140) : sf::Color(88, 95, 108)) :
							(side == 2 ? sf::Color(34, 30, 38) : sf::Color(24, 22, 30));
						quad(vertices, on(0.f, z0), on(1.f, z0), on(1.f, z1), on(0.f, z1), lighter(face, glow));
						if (plated)
						{
							//a window, lit or dark
							const bool lit = ((x * 7 + y * 13 + deck * 5) % 3) != 0;
							quad(vertices, on(0.3f, z0 + h * 0.38f), on(0.7f, z0 + h * 0.38f), on(0.7f, z0 + h * 0.7f), on(0.3f, z0 + h * 0.7f),
								lit ? sf::Color(255, 214, 130) : sf::Color(40, 60, 90));
						}
						else
						{
							//open to space: a red frame
							quad(vertices, on(0.f, z1 - 2.f), on(1.f, z1 - 2.f), on(1.f, z1), on(0.f, z1), sf::Color(230, 60, 60));
							quad(vertices, on(0.f, z0), on(1.f, z0), on(1.f, z0 + 2.f), on(0.f, z0 + 2.f), sf::Color(230, 60, 60));
						}
						//the deck's line along its top
						quad(vertices, on(0.f, z1 - 1.5f), on(1.f, z1 - 1.5f), on(1.f, z1), on(0.f, z1), lighter(face, 45 + glow));
					}
					d.count = int(vertices.size()) - d.start;
					if (d.count > 0)
						drawables.push_back(d);
				}
			}
		}
		};
		addVessel(_station, sf::Vector2i(0, 0), 0, sf::Vector2f(0.f, 0.f), _hoveredDeck);
		for (const alongside& a : _alongside)
		{
			if (!a.layout || a.layout->getLayerListSize() == 0 || _station.getLayerListSize() == 0)
				continue;
			const sf::Vector2f shift = _station.getIsoLevel(0.f)->tileCentreWorld(sf::Vector2f(float(1 + a.offset.x), float(1 + a.offset.y))) -
				a.layout->getIsoLevel(0.f)->tileCentreWorld(sf::Vector2f(1.f, 1.f));
			addVessel(*a.layout, a.offset, a.deckOffset, shift, -1);
		}
		std::stable_sort(drawables.begin(), drawables.end(), [](const drawable& _a, const drawable& _b) { return _a.depth < _b.depth; });

		//from the back: runs of hull faces in one go, each outside object's picture between them
		sf::VertexArray batch(sf::Triangles);
		auto flush = [&]() {
			if (batch.getVertexCount() > 0)
				_window.draw(batch);
			batch.clear();
		};
		for (const drawable& d : drawables)
		{
			if (d.object)
			{
				flush();
				sf::Sprite picture = *d.object;
				picture.move(d.shift.x, d.shift.y - d.lift);
				_window.draw(picture);
				continue;
			}
			for (int i = d.start; i < d.start + d.count; i++)
				batch.append(vertices[i]);
		}
		flush();
	}

	int deckAt(station& _station, sf::Vector2f _mouseWorld)
	{
		//from the top down: a deck's roof, then its sides
		for (int deck = _station.getLayerListSize() - 1; deck >= 0; deck--)
		{
			isometricLevel& level = *_station.getIsoLevel(float(deck));
			const float z0 = float(deck) * deckHeight;
			for (const float z : { z0 + deckHeight, z0 + deckHeight * 0.5f })
				if (level.hasFloor(level.tileAtWorld(_mouseWorld + sf::Vector2f(0.f, z))))
					return deck;
		}
		return -1;
	}

	float drawLegend(station& _station, int _hoveredDeck, float _top, float _scale, ImFont* _font)
	{
		const float s = _scale;
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		//the station's numbers: hull tiles, open edges, what is outside (by name)
		int hull = 0, open = 0, frames = 0;
		std::map<std::string, int> outside;
		for (int deck = 0; deck < _station.getLayerListSize(); deck++)
		{
			isometricLevel& level = *_station.getIsoLevel(float(deck));
			for (int y = 1; y <= int(level.getSizeH()); y++)
				for (int x = 1; x <= int(level.getSizeW()); x++)
				{
					const sf::Vector2i at(x, y);
					if (!level.hasFloor(at))
					{
						//what is mounted outside (not the frames, nor the cables, pipes and tubes along them)
						forEachOutside(level, at, [&](const tileObject* _o) {
							if (_o->type->outside)
								outside[_o->type->name]++;
							else if (_o->type->frame)
								frames++;
						});
						continue;
					}
					hull++;
					for (int side = 0; side < 4; side++)
						open += !level.hasFloor(at + footprint::step(side)) && !walled(level, at, side);
				}
		}
		if (_font)
			ImGui::PushFont(_font);
		ImGui::SetNextWindowPos(ImVec2(display.x - 8.f * s, _top), ImGuiCond_Always, ImVec2(1.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, uiTheme::panel());
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f * s, 8.f * s));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f * s, 4.f * s));
		ImGui::Begin("exteriorLegend", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::TextUnformatted("EXTERIOR VIEW");
		ImGui::SameLine();
		ImGui::TextDisabled("(O to go back inside)");
		ImGui::TextDisabled("the hull is the floors built; walled sides are plated, open ones red");
		ImGui::Separator();
		ImGui::Text("%d decks, %d hull tiles", _station.getLayerListSize(), hull);
		if (frames > 0)
			ImGui::Text("%d frame tiles", frames);
		if (open > 0)
			ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%d edges open to space (no wall)", open);
		if (outside.empty())
			ImGui::TextDisabled("nothing mounted outside");
		for (const auto& [name, count] : outside)
			ImGui::Text("%d x %s", count, name.c_str());
		if (_hoveredDeck >= 0)
		{
			isometricLevel& level = *_station.getIsoLevel(float(_hoveredDeck));
			int floors = 0;
			for (const tile& t : level.levelTiles)
				floors += t.hasFloor();
			ImGui::Separator();
			ImGui::Text("Deck %d: %d hull tiles, area %d", _hoveredDeck, floors, level.areaCount());
			ImGui::TextDisabled("click to go inside it");
		}
		const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
		if (_font)
			ImGui::PopFont();
		return bottom;
	}
}
