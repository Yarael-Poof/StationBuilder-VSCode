#pragma once
#include <imgui.h>

//the GUI's look: an old computer terminal in one warm hue (amber, like a phosphor monitor, or an orange like Elite
//Dangerous's), a few accent colours kept apart from it for things that mean something (danger, warnings, all good,
//information and cold), square corners, thin borders, and the VT323 terminal font (fonts/, SIL Open Font License).
//the glow and scanlines are drawn over it by screenEffects.h
namespace uiTheme
{
	enum class hue { amber, orange };
	const char* hueName(hue _hue);
	hue currentHue();
	//changes the hue and puts it into ImGui's style
	void setHue(hue _hue);

	//the hue's shades, brightest first
	ImVec4 bright();       //headings, what is picked
	ImVec4 text();         //normal text
	ImVec4 mid();          //slider grabs, plot lines
	ImVec4 dim();          //hints, disabled text
	ImVec4 faint();        //borders, separators
	ImVec4 fill();         //buttons, boxes
	ImVec4 fillHover();
	ImVec4 fillActive();   //pressed, or a toggle that is on
	ImVec4 panel();        //window backgrounds (a little see-through)
	ImU32 u32(const ImVec4& _colour);

	//accents: colours that mean something, kept apart from the hue
	inline constexpr ImVec4 danger(1.00f, 0.33f, 0.28f, 1.f);
	inline constexpr ImVec4 warning(1.00f, 0.90f, 0.38f, 1.f);
	inline constexpr ImVec4 good(0.47f, 0.92f, 0.52f, 1.f);
	inline constexpr ImVec4 info(0.42f, 0.80f, 1.00f, 1.f);

	//puts the look into ImGui's style (after the fonts load, and again after the hue changes). padding and spacing are
	//left as they are: each panel sets its own
	void apply();

	//the font file, and its size in pixels for a GUI scale (1 = 1080p). VT323 is drawn from a grid of square pixels, so
	//it is loaded at whole sizes without smoothing to stay crisp
	const char* const fontFile = "fonts/VT323-Regular.ttf";
	const float baseFontSize = 20.f;
	float fontSize(float _scale);
}
