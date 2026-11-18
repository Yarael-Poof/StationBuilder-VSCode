#include "textDecode.h"
#include "uiTheme.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cstring>
#include <iterator>

namespace
{
	std::uint64_t uvKey(const ImVec2& _uv)
	{
		std::uint32_t u = 0, v = 0;
		std::memcpy(&u, &_uv.x, 4);
		std::memcpy(&v, &_uv.y, 4);
		return (std::uint64_t(u) << 32) | v;
	}
	std::uint64_t mix(std::uint64_t _a, std::uint64_t _b)
	{
		_a ^= _b + 0x9e3779b97f4a7c15ull + (_a << 6) + (_a >> 2);
		return _a;
	}
	unsigned hash32(unsigned _x)
	{
		_x ^= _x >> 16;
		_x *= 0x7feb352du;
		_x ^= _x >> 15;
		_x *= 0x846ca68bu;
		_x ^= _x >> 16;
		return _x;
	}
}

void textDecode::buildLookup()
{
	//every glyph of every font, by the top left corner of its picture in the font texture (which is what a drawn
	//character's first corner carries)
	ImFontAtlas* atlas = ImGui::GetIO().Fonts;
	std::uint64_t signature = std::uint64_t(atlas->TexID);   //an ImU64 handle
	for (ImFont* font : atlas->Fonts)
		signature = mix(signature, mix(reinterpret_cast<std::uintptr_t>(font), std::uint64_t(font->Glyphs.Size)));
	if (signature == builtFor)
		return;
	builtFor = signature;
	byUv.clear();
	pool.clear();
	for (ImFont* font : atlas->Fonts)
	{
		std::vector<const ImFontGlyph*>& printable = pool[font];
		for (const ImFontGlyph& glyph : font->Glyphs)
		{
			if (!glyph.Visible)
				continue;
			byUv[uvKey(ImVec2(glyph.U0, glyph.V0))] = glyphRef{ font, &glyph };
			if (glyph.Codepoint >= 33 && glyph.Codepoint <= 126)
				printable.push_back(&glyph);
		}
	}
}

bool textDecode::moving(std::uint64_t _key, float _a, float _b, float _seconds)
{
	motion& m = motions[_key];
	if (m.lastFrame == frame - 1 && (m.a != _a || m.b != _b))
		m.since = 0.f;
	else if (m.lastFrame == frame - 1)
		m.since += _seconds;
	else
		m.since = 1e9f;   //just appeared (or back after a while): that is not a scroll
	m.a = _a;
	m.b = _b;
	m.lastFrame = frame;
	return m.since < quietTime;
}

void textDecode::apply(ImDrawData* _data, float _seconds)
{
	if (!enabled || !_data)
		return;
	buildLookup();
	frame++;
	ImGuiContext& g = *GImGui;
	const ImTextureID fontTexture = ImGui::GetIO().Fonts->TexID;
	const ImVec2 white = ImGui::GetIO().Fonts->TexUvWhitePixel;
	const ImU32 scrambleColour = uiTheme::u32(uiTheme::bright());

	//which window each draw list belongs to, and where that window is
	std::unordered_map<const ImDrawList*, const ImGuiWindow*> owners;
	for (const ImGuiWindow* window : g.Windows)
		if (window->Active && window->DrawList)
			owners[window->DrawList] = window;

	//what is scrolling or being typed into: text that comes into view there shows at once. a window scrolls (and so do
	//the child windows in it); a text box being typed into scrolls on its own (a one line box sideways, inside its
	//window; a multi-line box is a child window of its own), and so does a tab bar with more tabs than fit
	const bool typing = g.ActiveId != 0 && g.ActiveId == g.InputTextState.ID;
	std::unordered_map<const ImGuiWindow*, bool> movedItself;
	for (const ImGuiWindow* window : g.Windows)
	{
		if (!window->Active)
			continue;
		const bool typedIn = typing && (window == g.ActiveIdWindow || window->ChildId == g.ActiveId);
		//(both are noted every frame, so | rather than ||). being typed into counts as a change every frame
		movedItself[window] = moving(window->ID, window->Scroll.x, window->Scroll.y, _seconds) |
			moving(mix(window->ID, 0x7e57ull), typedIn ? float(1 + (frame & 1)) : 0.f, 0.f, _seconds);
	}
	auto quietWindow = [&](const ImGuiWindow* _window)
	{
		for (const ImGuiWindow* w = _window; w; w = (w->Flags & ImGuiWindowFlags_ChildWindow) ? w->ParentWindow : nullptr)
		{
			const auto found = movedItself.find(w);
			if (found != movedItself.end() && found->second)
				return true;
		}
		return false;
	};
	std::vector<ImRect> quietRects;
	for (int i = 0; i < g.TabBars.GetMapSize(); i++)
	{
		const ImGuiTabBar* bar = g.TabBars.TryGetMapData(i);
		if (bar && bar->CurrFrameVisible == g.FrameCount && moving(mix(bar->ID, 0x7ab5ull), bar->ScrollingAnim, 0.f, _seconds))
			quietRects.push_back(bar->BarRect);
	}

	//how many new characters each line of each window has met so far this frame: their places along the line
	std::unordered_map<std::uint64_t, int> newOnLine;
	last = counts();
	for (int n = 0; n < _data->CmdListsCount; n++)
	{
		ImDrawList* list = _data->CmdLists[n];
		const auto owner = owners.find(list);
		if (owner == owners.end())
			continue;
		const ImGuiWindow* window = owner->second;
		const bool quiet = quietWindow(window);
		for (const ImDrawCmd& cmd : list->CmdBuffer)
		{
			if (cmd.UserCallback || cmd.GetTexID() != fontTexture)
				continue;
			//one triangle at a time (other shapes have odd numbers of them: a combo's arrow is one), two at a time over a
			//character
			for (unsigned i = cmd.IdxOffset, step = 3; i + 6 <= cmd.IdxOffset + cmd.ElemCount; i += step)
			{
				step = 3;
				//a character is a quad: indices v, v+1, v+2, v, v+2, v+3
				const ImDrawIdx* idx = &list->IdxBuffer[int(i)];
				const unsigned v = unsigned(idx[0]) + cmd.VtxOffset;
				if (unsigned(idx[1]) + cmd.VtxOffset != v + 1 || unsigned(idx[2]) + cmd.VtxOffset != v + 2 || unsigned(idx[3]) + cmd.VtxOffset != v ||
					unsigned(idx[5]) + cmd.VtxOffset != v + 3 || int(v + 3) >= list->VtxBuffer.Size)
					continue;
				ImDrawVert* q = &list->VtxBuffer[int(v)];
				if (q[0].uv.x == white.x && q[0].uv.y == white.y)
					continue;
				const auto found = byUv.find(uvKey(q[0].uv));
				if (found == byUv.end())
					continue;   //not a whole glyph (one clipped at a window's edge)
				step = 6;
				const ImFontGlyph& glyph = *found->second.glyph;
				const float scale = glyph.X1 > glyph.X0 ? (q[1].pos.x - q[0].pos.x) / (glyph.X1 - glyph.X0) : 1.f;
				const ImVec2 origin(q[0].pos.x - glyph.X0 * scale, q[0].pos.y - glyph.Y0 * scale);
				//where it sits in the window's contents, so text that scrolls is still the same text
				const int x = int(std::lround(origin.x - window->Pos.x + window->Scroll.x));
				const int y = int(std::lround(origin.y - window->Pos.y + window->Scroll.y));
				const std::uint64_t line = mix(window->ID, std::uint64_t(y) & 0xffffffffull);
				const std::uint64_t key = mix(line, std::uint64_t(x) & 0xffffffffull);

				//new here (nothing on this spot last frame): it waits its turn along the line
				last.glyphs++;
				cell& c = cells[key];
				if (c.lastFrame != frame - 1 && c.lastFrame != frame)
				{
					last.fresh++;
					const int place = newOnLine[line]++;
					randomState = hash32(randomState + unsigned(key));
					c.age = 0.f;
					c.start = place * charDelay + float(randomState % 1000u) / 1000.f * charDelay * 2.f;
					c.seed = randomState;
					//(any part of it, or touching it: one cut off at the end of a scrolling tab row starts outside it)
					bool inQuietRect = false;
					const ImRect box(q[0].pos.x - 1.f, q[0].pos.y - 1.f, q[2].pos.x + 1.f, q[2].pos.y + 1.f);
					for (const ImRect& r : quietRects)
						inQuietRect = inQuietRect || r.Overlaps(box);
					if (quiet || inQuietRect)
					{
						c.start = -scrambleTime - 1.f;   //scrolled or typed into view: settled already
						newOnLine[line]--;
					}
				}
				else if (c.lastFrame == frame - 1)
					c.age += _seconds;
				c.lastFrame = frame;

				if (c.age >= c.start + scrambleTime)
					continue;   //settled: drawn as it is
				if (c.age < c.start)
				{
					//not reached yet: hidden
					last.hidden++;
					for (int k = 0; k < 4; k++)
						q[k].col &= ~IM_COL32_A_MASK;
					continue;
				}
				//flickering through random characters of the same font
				const std::vector<const ImFontGlyph*>& printable = pool[found->second.font];
				if (printable.empty())
					continue;
				last.flickering++;
				const unsigned flip = unsigned((c.age - c.start) / flipTime);
				const ImFontGlyph& r = *printable[hash32(c.seed + flip * 2654435761u) % printable.size()];
				const ImVec2 a(origin.x + r.X0 * scale, origin.y + r.Y0 * scale), b(origin.x + r.X1 * scale, origin.y + r.Y1 * scale);
				q[0].pos = a;                  q[0].uv = ImVec2(r.U0, r.V0);
				q[1].pos = ImVec2(b.x, a.y);   q[1].uv = ImVec2(r.U1, r.V0);
				q[2].pos = b;                  q[2].uv = ImVec2(r.U1, r.V1);
				q[3].pos = ImVec2(a.x, b.y);   q[3].uv = ImVec2(r.U0, r.V1);
				for (int k = 0; k < 4; k++)
					q[k].col = (scrambleColour & ~IM_COL32_A_MASK) | (q[k].col & IM_COL32_A_MASK);
			}
		}
	}

	//forget characters gone for a while
	if (frame % 120 == 0)
	{
		for (auto it = cells.begin(); it != cells.end();)
			it = it->second.lastFrame < frame - 2 ? cells.erase(it) : std::next(it);
		for (auto it = motions.begin(); it != motions.end();)
			it = it->second.lastFrame < frame - 2 ? motions.erase(it) : std::next(it);
	}
}
