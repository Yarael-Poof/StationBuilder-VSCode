#include "machineView.h"
#include "isometricLevel.h"
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace
{
	ImVec2 toPixel(const sf::RenderWindow& _window, sf::Vector2f _world)
	{
		const sf::Vector2i p = _window.mapCoordsToPixel(_world);
		return ImVec2(float(p.x), float(p.y));
	}

	//the middle of a tile's side, from the tile's centre (world units: the floor diamond is 64 x 32)
	sf::Vector2f sideMiddle(int _side)
	{
		static const sf::Vector2f middles[4] = { sf::Vector2f(-16.f, -8.f), sf::Vector2f(16.f, -8.f), sf::Vector2f(-16.f, 8.f), sf::Vector2f(16.f, 8.f) };
		return middles[_side & 3];
	}

	ImU32 withAlpha(ImU32 _colour, int _alpha)
	{
		return (_colour & 0x00FFFFFFu) | (ImU32(std::clamp(_alpha, 0, 255)) << IM_COL32_A_SHIFT);
	}
}

namespace machineView
{
	unsigned int portColour(portInfo::kind _kind)
	{
		switch (_kind)
		{
		case portInfo::kind::power: return IM_COL32(255, 210, 40, 255);
		case portInfo::kind::items: return IM_COL32(225, 140, 60, 255);
		default: return IM_COL32(70, 160, 255, 255);
		}
	}

	void drawPortMarker(ImDrawList* _drawList, const ImVec2& _middle, const ImVec2& _out, float _length, portInfo::kind _kind,
		portInfo::direction _flow, int _alpha)
	{
		//every port is the same tag: a body with its label, and a point at the end it goes towards: out of the face for Out,
		//into it for In, both ends for Any. it lies along the line out of the face. it grows with the zoom up to where its
		//label reads easily, and leaves the label off when it is too small to read
		const float scale = _length / 11.f;
		const float textSize = std::clamp(12.f * scale, 4.f, 19.f);
		const bool labelled = textSize >= 8.f;
		const char* label = _flow == portInfo::direction::in ? "In" : _flow == portInfo::direction::out ? "Out" : "Any";
		ImFont* font = ImGui::GetFont();
		//the body fits the longest label, so all three are the same size
		float longest = textSize * 1.6f;
		if (labelled)
			for (const char* text : { "In", "Out", "Any" })
				longest = std::max(longest, font->CalcTextSizeA(textSize, FLT_MAX, 0.f, text).x);
		const float halfHeight = textSize * 0.62f;
		const float halfBody = longest * 0.5f + textSize * 0.25f;
		const float point = halfHeight;   //a square-cornered point
		const ImVec2 along = _out, across(-_out.y, _out.x);
		//its middle a little out from the face, so the face's edge crosses its body
		const ImVec2 centre(_middle.x + along.x * halfHeight * 0.6f, _middle.y + along.y * halfHeight * 0.6f);
		auto at = [&](float _a, float _c) { return ImVec2(centre.x + along.x * _a + across.x * _c, centre.y + along.y * _a + across.y * _c); };
		ImVec2 points[6];
		int count = 0;
		points[count++] = at(-halfBody, -halfHeight);
		points[count++] = at(halfBody, -halfHeight);
		if (_flow != portInfo::direction::in)
			points[count++] = at(halfBody + point, 0.f);   //pointing out
		points[count++] = at(halfBody, halfHeight);
		points[count++] = at(-halfBody, halfHeight);
		if (_flow != portInfo::direction::out)
			points[count++] = at(-halfBody - point, 0.f);  //pointing in
		_drawList->AddConvexPolyFilled(points, count, withAlpha(portColour(_kind), _alpha));
		_drawList->AddPolyline(points, count, withAlpha(IM_COL32(0, 0, 0, 255), _alpha * 230 / 255), ImDrawFlags_Closed, std::clamp(1.5f * scale, 1.f, 2.f));
		if (!labelled)
			return;
		//its label along the tag, turned the right way up (reading left to right whichever way the tag points)
		const ImVec2 size = font->CalcTextSizeA(textSize, FLT_MAX, 0.f, label);
		const int first = _drawList->VtxBuffer.Size;
		_drawList->AddText(font, textSize, ImVec2(centre.x - size.x * 0.5f, centre.y - size.y * 0.5f), withAlpha(IM_COL32(15, 15, 20, 255), _alpha), label);
		float angle = std::atan2(along.y, along.x);
		if (along.x < 0.f)
			angle += 3.14159265f;
		const float c = std::cos(angle), s = std::sin(angle);
		for (int i = first; i < _drawList->VtxBuffer.Size; i++)
		{
			ImDrawVert& vertex = _drawList->VtxBuffer[i];
			const float dx = vertex.pos.x - centre.x, dy = vertex.pos.y - centre.y;
			vertex.pos = ImVec2(centre.x + dx * c - dy * s, centre.y + dx * s + dy * c);
		}
	}

	void drawPorts(ImDrawList* _drawList, const sf::RenderWindow& _window, const isometricLevel& _level, const tileTypeInfo& _type, int _turns,
		sf::Vector2i _origin, int _kinds, int _alpha)
	{
		for (const portInfo& port : footprint::turnedPorts(_type, _turns))
		{
			if (!(_kinds & kindBit(port.carries)))
				continue;
			const sf::Vector2f centre = _level.tileCentreWorld(sf::Vector2f(_origin + port.cell));
			const sf::Vector2f middle = centre + sideMiddle(port.side);
			//out of the face, towards the neighbour's centre (a step of one tile is 32 across and 16 down per x, the mirror per y)
			const sf::Vector2i s = footprint::step(port.side);
			sf::Vector2f out(32.f * float(s.x - s.y), 16.f * float(s.x + s.y));
			out /= std::hypot(out.x, out.y);
			//in pixels: the view may be zoomed (it is never turned, so the direction is the same on screen). the zoom is
			//measured over a long way, so rounding to whole pixels doesn't bend the tag
			const ImVec2 left = toPixel(_window, sf::Vector2f(0.f, 0.f)), right = toPixel(_window, sf::Vector2f(1000.f, 0.f));
			const float zoom = (right.x - left.x) / 1000.f;
			if (zoom <= 0.f)
				continue;
			drawPortMarker(_drawList, toPixel(_window, middle), ImVec2(out.x, out.y), 11.f * zoom, port.carries, port.flow, _alpha);
		}
	}

	void drawLevelPorts(ImDrawList* _drawList, const sf::RenderWindow& _window, isometricLevel& _level, int _kinds)
	{
		for (tile& t : _level.levelTiles)
		{
			for (tileObject* object : t.allObjects())
			{
				if (object->isPart() || !object->type || object->type->ports.empty())
					continue;
				drawPorts(_drawList, _window, _level, *object->type, object->turns(), object->origin, _kinds);
			}
		}
	}

	void drawFootprint(ImDrawList* _drawList, const sf::RenderWindow& _window, const isometricLevel& _level, sf::Vector2i _origin, sf::Vector2i _size,
		unsigned int _colour, float _width)
	{
		const sf::Vector2i last = _origin + _size - sf::Vector2i(1, 1);
		const ImVec2 top = toPixel(_window, _level.tileCentreWorld(sf::Vector2f(_origin)) + sf::Vector2f(0.f, -16.f));
		const ImVec2 right = toPixel(_window, _level.tileCentreWorld(sf::Vector2f(float(last.x), float(_origin.y))) + sf::Vector2f(32.f, 0.f));
		const ImVec2 bottom = toPixel(_window, _level.tileCentreWorld(sf::Vector2f(last)) + sf::Vector2f(0.f, 16.f));
		const ImVec2 left = toPixel(_window, _level.tileCentreWorld(sf::Vector2f(float(_origin.x), float(last.y))) + sf::Vector2f(-32.f, 0.f));
		_drawList->AddQuad(top, right, bottom, left, IM_COL32(0, 0, 0, 220), _width + 2.f);
		_drawList->AddQuad(top, right, bottom, left, _colour, _width);
	}
}
