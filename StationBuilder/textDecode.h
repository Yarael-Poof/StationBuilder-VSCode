#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

struct ImDrawData;
struct ImFont;
struct ImFontGlyph;

//text that types itself in, the way text-mode Dear ImGui (ImTui) shows it in hnterm: a character that appears where
//there was none a moment before starts hidden, flickers through a few random characters in a bright colour, and settles
//on its own, each line filling from the left (all lines at once). text that only changes in place (a number ticking
//over) doesn't. it works on ImGui's finished draw data, between ImGui::Render() and drawing it, and only on text in
//windows: the views' labels over the level move with the camera and are left alone. a window that moves doesn't type
//itself in again (characters are tracked by where they sit in their window's contents). nothing types in while it is
//being scrolled or typed into: text a window, tab bar or text box scrolls into view, and text typed, shows at once
class textDecode
{
public:
	bool enabled = true;
	float charDelay = 0.006f;     //s from one new character of a line starting to the next
	float scrambleTime = 0.09f;   //s each one shows random characters before settling
	float flipTime = 0.03f;       //s between random characters
	float quietTime = 0.25f;      //s after a scroll (or the last frame of typing) that new text still shows at once

	void apply(ImDrawData* _data, float _seconds);

	//what the last frame did (for checking it): characters found in windows, new ones, hidden, flickering
	struct counts { int glyphs = 0, fresh = 0, hidden = 0, flickering = 0; };
	counts last;

private:
	struct cell
	{
		float age = 0.f;      //s since it appeared
		float start = 0.f;    //s after appearing that it starts showing (its place along its line)
		int lastFrame = -100;
		unsigned seed = 0;
	};
	struct glyphRef
	{
		const ImFont* font = nullptr;
		const ImFontGlyph* glyph = nullptr;
	};
	//something that scrolls (a window, a tab bar) or is typed into, and how long since it last did
	struct motion
	{
		float a = 0.f, b = 0.f;   //where it was last frame
		float since = 1e9f;       //s since it last moved
		int lastFrame = -100;
	};
	void buildLookup();
	//notes how far _key has moved since last frame; true while it moved within quietTime
	bool moving(std::uint64_t _key, float _a, float _b, float _seconds);

	std::unordered_map<std::uint64_t, cell> cells;
	std::unordered_map<std::uint64_t, glyphRef> byUv;                       //a glyph by its texture corner
	std::unordered_map<const ImFont*, std::vector<const ImFontGlyph*>> pool;   //each font's printable glyphs
	std::unordered_map<std::uint64_t, motion> motions;   //by window or tab bar
	std::uint64_t builtFor = 0;
	int frame = 0;
	unsigned randomState = 12345u;
};
