#include "uiTheme.h"
#include <algorithm>
#include <cmath>

namespace
{
	uiTheme::hue current = uiTheme::hue::amber;

	//the hue at full strength: amber like a P3 phosphor, or an orange like Elite Dangerous's
	ImVec4 base()
	{
		return current == uiTheme::hue::orange ? ImVec4(1.00f, 0.50f, 0.08f, 1.f) : ImVec4(1.00f, 0.69f, 0.16f, 1.f);
	}
	ImVec4 scaled(float _by, float _alpha = 1.f)
	{
		const ImVec4 b = base();
		return ImVec4(b.x * _by, b.y * _by, b.z * _by, _alpha);
	}
	ImVec4 towardWhite(float _by)
	{
		const ImVec4 b = base();
		return ImVec4(b.x + (1.f - b.x) * _by, b.y + (1.f - b.y) * _by, b.z + (1.f - b.z) * _by, 1.f);
	}
}

namespace uiTheme
{
	const char* hueName(hue _hue) { return _hue == hue::orange ? "orange" : "amber"; }
	hue currentHue() { return current; }

	void setHue(hue _hue)
	{
		current = _hue;
		apply();
	}

	ImVec4 bright() { return towardWhite(0.45f); }
	ImVec4 text() { return base(); }
	ImVec4 mid() { return scaled(0.78f); }
	ImVec4 dim() { return scaled(0.58f); }
	ImVec4 faint() { return scaled(0.36f); }
	ImVec4 fill() { return scaled(0.20f); }
	ImVec4 fillHover() { return scaled(0.32f); }
	ImVec4 fillActive() { return scaled(0.48f); }
	ImVec4 panel() { return ImVec4(0.035f, 0.024f, 0.012f, 0.90f); }
	ImU32 u32(const ImVec4& _c) { return ImGui::ColorConvertFloat4ToU32(_c); }

	float fontSize(float _scale)
	{
		return std::round(baseFontSize * std::max(_scale, 0.5f));
	}

	void apply()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 0.f;
		style.ChildRounding = 0.f;
		style.FrameRounding = 0.f;
		style.PopupRounding = 0.f;
		style.ScrollbarRounding = 0.f;
		style.GrabRounding = 0.f;
		style.TabRounding = 0.f;
		style.WindowBorderSize = 1.f;
		style.ChildBorderSize = 1.f;
		style.PopupBorderSize = 1.f;
		style.FrameBorderSize = 1.f;
		style.TabBorderSize = 1.f;

		ImVec4* c = style.Colors;
		const ImVec4 clear(0.f, 0.f, 0.f, 0.f);
		c[ImGuiCol_Text] = text();
		c[ImGuiCol_TextDisabled] = dim();
		c[ImGuiCol_WindowBg] = panel();
		c[ImGuiCol_ChildBg] = clear;
		c[ImGuiCol_PopupBg] = ImVec4(0.045f, 0.030f, 0.014f, 0.97f);
		c[ImGuiCol_Border] = faint();
		c[ImGuiCol_BorderShadow] = clear;
		c[ImGuiCol_FrameBg] = fill();
		c[ImGuiCol_FrameBgHovered] = fillHover();
		c[ImGuiCol_FrameBgActive] = fillActive();
		c[ImGuiCol_TitleBg] = scaled(0.14f);
		c[ImGuiCol_TitleBgActive] = scaled(0.26f);
		c[ImGuiCol_TitleBgCollapsed] = scaled(0.10f);
		c[ImGuiCol_MenuBarBg] = scaled(0.12f);
		c[ImGuiCol_ScrollbarBg] = scaled(0.06f, 0.9f);
		c[ImGuiCol_ScrollbarGrab] = faint();
		c[ImGuiCol_ScrollbarGrabHovered] = dim();
		c[ImGuiCol_ScrollbarGrabActive] = mid();
		c[ImGuiCol_CheckMark] = bright();
		c[ImGuiCol_SliderGrab] = mid();
		c[ImGuiCol_SliderGrabActive] = bright();
		c[ImGuiCol_Button] = fill();
		c[ImGuiCol_ButtonHovered] = fillHover();
		c[ImGuiCol_ButtonActive] = fillActive();
		c[ImGuiCol_Header] = scaled(0.24f);
		c[ImGuiCol_HeaderHovered] = scaled(0.34f);
		c[ImGuiCol_HeaderActive] = scaled(0.46f);
		c[ImGuiCol_Separator] = faint();
		c[ImGuiCol_SeparatorHovered] = dim();
		c[ImGuiCol_SeparatorActive] = mid();
		c[ImGuiCol_ResizeGrip] = faint();
		c[ImGuiCol_ResizeGripHovered] = dim();
		c[ImGuiCol_ResizeGripActive] = mid();
		c[ImGuiCol_Tab] = scaled(0.16f);
		c[ImGuiCol_TabHovered] = scaled(0.34f);
		c[ImGuiCol_TabSelected] = scaled(0.30f);
		c[ImGuiCol_TabSelectedOverline] = bright();
		c[ImGuiCol_TabDimmed] = scaled(0.12f);
		c[ImGuiCol_TabDimmedSelected] = scaled(0.20f);
		c[ImGuiCol_TabDimmedSelectedOverline] = dim();
		c[ImGuiCol_PlotLines] = mid();
		c[ImGuiCol_PlotLinesHovered] = bright();
		c[ImGuiCol_PlotHistogram] = mid();
		c[ImGuiCol_PlotHistogramHovered] = bright();
		c[ImGuiCol_TableHeaderBg] = scaled(0.18f);
		c[ImGuiCol_TableBorderStrong] = faint();
		c[ImGuiCol_TableBorderLight] = scaled(0.22f);
		c[ImGuiCol_TableRowBg] = clear;
		c[ImGuiCol_TableRowBgAlt] = scaled(0.06f, 0.6f);
		c[ImGuiCol_TextLink] = bright();
		c[ImGuiCol_TextSelectedBg] = scaled(0.40f, 0.6f);
		c[ImGuiCol_DragDropTarget] = warning;
		c[ImGuiCol_NavCursor] = bright();
		c[ImGuiCol_NavWindowingHighlight] = bright();
		c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.f, 0.f, 0.f, 0.5f);
		c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.f, 0.f, 0.f, 0.55f);
	}
}
