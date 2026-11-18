#include "decks.h"
#include "station.h"
#include "tileCatalogue.h"
#include <imgui.h>
#include "uiTheme.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace
{
	const ImVec4 amber = uiTheme::warning;
	const ImVec4 green = uiTheme::good;

	ImVec2 pixel(const sf::RenderWindow& _window, sf::Vector2f _world)
	{
		const sf::Vector2i p = _window.mapCoordsToPixel(_world);
		return ImVec2(float(p.x), float(p.y));
	}
	//the ends of a tile's side, in world units: the corners between tiles sit half a tile from their centres
	void sideEnds(const isometricLevel& _level, sf::Vector2i _tile, int _side, sf::Vector2f& _a, sf::Vector2f& _b)
	{
		const float x = float(_tile.x), y = float(_tile.y);
		static const float ends[4][4] = { { -0.5f, -0.5f, -0.5f, 0.5f }, { -0.5f, -0.5f, 0.5f, -0.5f }, { -0.5f, 0.5f, 0.5f, 0.5f }, { 0.5f, -0.5f, 0.5f, 0.5f } };
		const float* e = ends[_side & 3];
		_a = _level.tileCentreWorld(sf::Vector2f(x + e[0], y + e[1]));
		_b = _level.tileCentreWorld(sf::Vector2f(x + e[2], y + e[3]));
	}
	bool builtOn(isometricLevel& _level, sf::Vector2i _tile)
	{
		return _level.isInLevel(_tile) && _level.levelTiles[_level.coords2TileIndex(_tile.x, _tile.y)].hasAnything();
	}
}

namespace decks
{
	const char* shapeName(shape _shape)
	{
		switch (_shape)
		{
		case shape::octagon: return "Octagon";
		case shape::cross: return "Cross";
		case shape::ellipse: return "Round";
		default: return "Rectangle";
		}
	}

	std::vector<sf::Vector2i> shapeTiles(shape _shape, sf::Vector2i _size, sf::Vector2i _centre)
	{
		std::vector<sf::Vector2i> tiles;
		const int w = std::max(_size.x, 1), h = std::max(_size.y, 1);
		const sf::Vector2i corner = _centre - sf::Vector2i(w / 2, h / 2);
		const int cut = std::max(1, std::min(w, h) / 3);   //an octagon's corners
		for (int j = 0; j < h; j++)
		{
			for (int i = 0; i < w; i++)
			{
				bool in = true;
				switch (_shape)
				{
				case shape::octagon:
					in = i + j >= cut && (w - 1 - i) + j >= cut && i + (h - 1 - j) >= cut && (w - 1 - i) + (h - 1 - j) >= cut;
					break;
				case shape::cross:
					in = (j >= h / 4 && j < h - h / 4) || (i >= w / 4 && i < w - w / 4);
					break;
				case shape::ellipse:
				{
					const double dx = (i + 0.5 - w / 2.0) / (w / 2.0), dy = (j + 0.5 - h / 2.0) / (h / 2.0);
					in = dx * dx + dy * dy <= 1.0;
					break;
				}
				default:
					break;
				}
				if (in)
					tiles.push_back(corner + sf::Vector2i(i, j));
			}
		}
		return tiles;
	}

	int setArea(isometricLevel& _level, const std::vector<sf::Vector2i>& _tiles)
	{
		std::set<std::pair<int, int>> wanted;
		for (const sf::Vector2i& t : _tiles)
			wanted.insert({ t.x, t.y });
		int kept = 0;
		for (int y = 1; y <= int(_level.getSizeH()); y++)
		{
			for (int x = 1; x <= int(_level.getSizeW()); x++)
			{
				const bool in = wanted.count({ x, y }) > 0;
				const bool built = builtOn(_level, sf::Vector2i(x, y));
				kept += !in && built;
				_level.setArea(sf::Vector2i(x, y), in || built);
			}
		}
		return kept;
	}

	void addArea(isometricLevel& _level, sf::Vector2i _from, sf::Vector2i _to)
	{
		for (int y = std::min(_from.y, _to.y); y <= std::max(_from.y, _to.y); y++)
			for (int x = std::min(_from.x, _to.x); x <= std::max(_from.x, _to.x); x++)
				_level.setArea(sf::Vector2i(x, y), true);
	}

	int removeArea(isometricLevel& _level, sf::Vector2i _from, sf::Vector2i _to)
	{
		int kept = 0;
		for (int y = std::min(_from.y, _to.y); y <= std::max(_from.y, _to.y); y++)
		{
			for (int x = std::min(_from.x, _to.x); x <= std::max(_from.x, _to.x); x++)
			{
				if (!_level.inArea(sf::Vector2i(x, y)))
					continue;
				if (builtOn(_level, sf::Vector2i(x, y)))
					kept++;
				else
					_level.setArea(sf::Vector2i(x, y), false);
			}
		}
		return kept;
	}

	sf::Vector2i areaCentre(const isometricLevel& _level)
	{
		long long sx = 0, sy = 0, n = 0;
		for (int y = 1; y <= int(const_cast<isometricLevel&>(_level).getSizeH()); y++)
			for (int x = 1; x <= int(const_cast<isometricLevel&>(_level).getSizeW()); x++)
				if (_level.inArea(sf::Vector2i(x, y)))
				{
					sx += x;
					sy += y;
					n++;
				}
		if (n == 0)
			return sf::Vector2i(int(const_cast<isometricLevel&>(_level).getSizeW()) / 2, int(const_cast<isometricLevel&>(_level).getSizeH()) / 2);
		return sf::Vector2i(int(std::lround(double(sx) / double(n))), int(std::lround(double(sy) / double(n))));
	}

	int hullCount(const isometricLevel& _level)
	{
		int count = 0;
		for (const tile& t : _level.levelTiles)
			count += t.hasFloor();
		return count;
	}

	bool isEmpty(const isometricLevel& _level)
	{
		for (const tile& t : _level.levelTiles)
			if (t.hasAnything())
				return false;
		return true;
	}

	void startingStation(station& _station)
	{
		const sf::Vector2i centre(gridSize / 2, gridSize / 2);
		//the lowest deck: room to grow round a walled 12 x 8 room
		_station.appendNewIsoLevel(float(gridSize), float(gridSize), 64.f);
		isometricLevel& lowest = *_station.getIsoLevel(0.f);
		setArea(lowest, shapeTiles(shape::rectangle, sf::Vector2i(24, 18), centre));
		const sf::Vector2i from = centre - sf::Vector2i(6, 4), to = centre + sf::Vector2i(5, 3);
		for (int y = from.y; y <= to.y; y++)
			for (int x = from.x; x <= to.x; x++)
				lowest.addObjectToTile(sf::Vector2f(float(x), float(y)), "floor");
		for (int x = from.x; x <= to.x; x++)
		{
			lowest.addObjectToTile(sf::Vector2f(float(x), float(from.y)), "wallUR");
			lowest.addObjectToTile(sf::Vector2f(float(x), float(to.y)), "wallLL");
		}
		for (int y = from.y; y <= to.y; y++)
		{
			lowest.addObjectToTile(sf::Vector2f(float(from.x), float(y)), "wallUL");
			lowest.addObjectToTile(sf::Vector2f(float(to.x), float(y)), "wallLR");
		}
		//the deck above: an octagon to build on
		_station.appendNewIsoLevel(float(gridSize), float(gridSize), 64.f);
		setArea(*_station.getIsoLevel(1.f), shapeTiles(shape::octagon, sf::Vector2i(18, 14), centre));
		_station.setCurrentLevel(0);
	}

	void drawGuides(const sf::RenderWindow& _window, station& _station, bool _wholeGrid)
	{
		const int current = _station.getCurrentLevel();
		isometricLevel& level = *_station.getIsoLevel(float(current));
		ImDrawList* list = ImGui::GetBackgroundDrawList();
		const int w = int(level.getSizeW()), h = int(level.getSizeH());
		sf::Vector2f a, b;
		//the whole grid, faintly, while the area is being grown
		if (_wholeGrid)
		{
			for (int j = 0; j <= h; j++)
				list->AddLine(pixel(_window, level.tileCentreWorld(sf::Vector2f(0.5f, j + 0.5f))), pixel(_window, level.tileCentreWorld(sf::Vector2f(w + 0.5f, j + 0.5f))),
					IM_COL32(120, 200, 255, 45), 1.f);
			for (int i = 0; i <= w; i++)
				list->AddLine(pixel(_window, level.tileCentreWorld(sf::Vector2f(i + 0.5f, 0.5f))), pixel(_window, level.tileCentreWorld(sf::Vector2f(i + 0.5f, h + 0.5f))),
					IM_COL32(120, 200, 255, 45), 1.f);
		}
		//the hull of the deck below: where floors on this deck would sit over it
		if (current > 0)
		{
			isometricLevel& lower = *_station.getIsoLevel(float(current - 1));
			for (int y = 1; y <= h; y++)
				for (int x = 1; x <= w; x++)
				{
					if (!lower.hasFloor(sf::Vector2i(x, y)))
						continue;
					for (int side = 0; side < 4; side++)
					{
						if (lower.hasFloor(sf::Vector2i(x, y) + footprint::step(side)))
							continue;
						sideEnds(level, sf::Vector2i(x, y), side, a, b);
						list->AddLine(pixel(_window, a), pixel(_window, b), IM_COL32(90, 220, 255, 120), 2.f);
					}
				}
		}
		//the edge of this deck's area
		if (level.hasWholeArea())
			return;
		for (int y = 1; y <= h; y++)
			for (int x = 1; x <= w; x++)
			{
				if (!level.inArea(sf::Vector2i(x, y)))
					continue;
				for (int side = 0; side < 4; side++)
				{
					if (level.inArea(sf::Vector2i(x, y) + footprint::step(side)))
						continue;
					sideEnds(level, sf::Vector2i(x, y), side, a, b);
					list->AddLine(pixel(_window, a), pixel(_window, b), IM_COL32(255, 200, 80, 190), 2.5f);
				}
			}
	}
}

void deckPanel::say(const std::string& _text, bool _problem)
{
	message = _text;
	messageProblem = _problem;
	messageTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
}

void deckPanel::stopTool()
{
	state = tool::none;
	dragging = false;
}

bool deckPanel::escape()
{
	if (dragging)
	{
		dragging = false;
		return true;
	}
	if (state != tool::none)
	{
		stopTool();
		return true;
	}
	return false;
}

void deckPanel::update(station& _station, sf::Vector2i _hovered, bool _inLevel, bool _leftPressed, bool _leftDown, bool _guiWantsMouse)
{
	if (state == tool::none)
		return;
	if (_leftPressed && !_guiWantsMouse && _inLevel)
	{
		dragging = true;
		dragFrom = dragTo = _hovered;
	}
	if (!dragging)
		return;
	if (_inLevel)
		dragTo = _hovered;
	if (_leftDown)
		return;
	dragging = false;
	isometricLevel& level = *_station.getCurrentIsoLevel();
	if (state == tool::grow)
		decks::addArea(level, dragFrom, dragTo);
	else
	{
		const int kept = decks::removeArea(level, dragFrom, dragTo);
		if (kept > 0)
			say(std::to_string(kept) + (kept == 1 ? " tile has" : " tiles have") + " something built on it: left in the area", true);
	}
}

void deckPanel::highlight(station& _station, sf::Vector2i _hovered, bool _inLevel)
{
	if (state == tool::none)
		return;
	const sf::Color tint = state == tool::grow ? sf::Color(150, 255, 150) : sf::Color(255, 140, 140);
	if (!dragging)
	{
		if (_inLevel)
			_station.highlightTile(sf::Vector2f(_hovered), tint);
		return;
	}
	for (int y = std::min(dragFrom.y, dragTo.y); y <= std::max(dragFrom.y, dragTo.y); y++)
		for (int x = std::min(dragFrom.x, dragTo.x); x <= std::max(dragFrom.x, dragTo.x); x++)
			_station.highlightTile(sf::Vector2f(float(x), float(y)), tint);
}

bool deckPanel::draw(station& _station, float _scale, ImFont* _font)
{
	if (!open)
		return false;
	bool changed = false;
	const float s = _scale;
	if (_font)
		ImGui::PushFont(_font);
	const std::pair<ImGuiCol, ImVec4> colours[] = { { ImGuiCol_WindowBg, uiTheme::panel() } };
	for (const auto& [which, colour] : colours)
		ImGui::PushStyleColor(which, colour);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f * s, 8.f * s));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f * s, 5.f * s));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f * s, 3.f * s));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
	ImGui::SetNextWindowPos(ImVec2(170.f, 60.f * s), ImGuiCond_Appearing);
	if (ImGui::Begin("Decks (D closes)", &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
	{
		const int count = _station.getLayerListSize();
		const int current = _station.getCurrentLevel();
		//every deck, the top one first
		for (int deck = count - 1; deck >= 0; deck--)
		{
			isometricLevel& level = *_station.getIsoLevel(float(deck));
			char label[128];
			snprintf(label, sizeof(label), "Deck %d%s", deck, deck == count - 1 ? " (top)" : deck == 0 ? " (bottom)" : "");
			if (ImGui::Selectable(label, deck == current, 0, ImVec2(150.f * s, 0.f)) && deck != current)
			{
				_station.setCurrentLevel(deck);
				stopTool();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("area %d tiles, hull %d", level.areaCount(), decks::hullCount(level));
		}
		ImGui::Separator();

		//the shown deck's area
		isometricLevel& level = *_station.getCurrentIsoLevel();
		ImGui::Text("Deck %d's area", current);
		ImGui::TextDisabled("where it can be built; its floors are the hull you see outside (O)");
		const bool growing = state == tool::grow, shrinking = state == tool::shrink;
		if (growing)
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.25f, 1.f));
		if (ImGui::Button("Grow it"))
			state = growing ? tool::none : tool::grow;
		if (growing)
			ImGui::PopStyleColor();
		ImGui::SameLine();
		if (shrinking)
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1.f));
		if (ImGui::Button("Shrink it"))
			state = shrinking ? tool::none : tool::shrink;
		if (shrinking)
			ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::TextDisabled(state == tool::none ? "then drag over tiles" : "drag over tiles (Esc stops)");

		//a shape: for this deck's area, or a new deck's
		std::vector<const char*> names;
		for (int i = 0; i < decks::shapeCount; i++)
			names.push_back(decks::shapeName(decks::shape(i)));
		ImGui::SetNextItemWidth(130.f * s);
		ImGui::Combo("##shape", &shapeIndex, names.data(), int(names.size()));
		ImGui::SameLine();
		int size[2] = { shapeSize.x, shapeSize.y };
		ImGui::SetNextItemWidth(110.f * s);
		if (ImGui::InputInt2("##size", size))
			shapeSize = sf::Vector2i(std::clamp(size[0], 2, int(level.getSizeW())), std::clamp(size[1], 2, int(level.getSizeH())));
		ImGui::SameLine();
		ImGui::TextDisabled("tiles");
		if (ImGui::Button("Make this deck's area that shape"))
		{
			const int kept = decks::setArea(level, decks::shapeTiles(decks::shape(shapeIndex), shapeSize, decks::areaCentre(level)));
			say(kept > 0 ? std::to_string(kept) + " tiles with something built on them stay in it too" : "its area is now that shape", kept > 0);
		}

		ImGui::Separator();
		ImGui::TextUnformatted("A new deck, its area that shape:");
		auto addDeck = [&](int _at) {
			const sf::Vector2i centre = decks::areaCentre(level);
			const int at = _station.insertNewIsoLevel(_at, level.getSizeW(), level.getSizeH(), level.getTileSize());
			decks::setArea(*_station.getIsoLevel(float(at)), decks::shapeTiles(decks::shape(shapeIndex), shapeSize, centre));
			stopTool();
			changed = true;
			say("added deck " + std::to_string(at), false);
		};
		if (ImGui::Button("Add above"))
			addDeck(current + 1);
		ImGui::SameLine();
		if (ImGui::Button("Add below"))
			addDeck(current);
		if (count > 1)
		{
			ImGui::SameLine(0.f, 24.f * s);
			if (!decks::isEmpty(level))
				ImGui::TextDisabled("(only an empty deck can be taken away)");
			else if (!confirmRemove)
			{
				if (ImGui::Button("Take this deck away"))
					confirmRemove = true;
			}
			else
			{
				if (ImGui::Button("Really take it away?"))
				{
					_station.deleteIsoLevel(float(current));
					stopTool();
					changed = true;
					confirmRemove = false;
					say("deck taken away", false);
				}
				ImGui::SameLine();
				if (ImGui::Button("Keep it"))
					confirmRemove = false;
			}
		}
		if (ImGui::GetTime() - messageTime < 5.0 && !message.empty())
			ImGui::TextColored(messageProblem ? amber : green, "%s", message.c_str());
	}
	ImGui::End();
	ImGui::PopStyleVar(4);
	ImGui::PopStyleColor(int(sizeof(colours) / sizeof(colours[0])));
	if (_font)
		ImGui::PopFont();
	return changed;
}
