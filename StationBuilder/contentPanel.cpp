#include "contentPanel.h"
#include "content.h"
#include "tileCatalogue.h"
#include "textureManager.h"
#include <imgui.h>
#include "uiTheme.h"
#include <imgui-SFML.h>
#include <algorithm>

namespace
{
	const ImVec4 red = uiTheme::danger;
	const ImVec4 amber = uiTheme::warning;
	const ImVec4 green = uiTheme::good;
	const float noticeSeconds = 3.5f;

	const char* layerName(tileLayer _layer)
	{
		switch (_layer)
		{
		case tileLayer::background: return "background";
		case tileLayer::floor: return "floor";
		case tileLayer::wall: return "wall";
		case tileLayer::fixture: return "fixture";
		default: return "none";
		}
	}

	const char* sideName(int _side)
	{
		static const char* names[4] = { "UL", "UR", "LL", "LR" };
		return _side >= 0 && _side < 4 ? names[_side] : "-";
	}

	//everything known about one item, for the preview
	void itemDetails(const tileTypeInfo& _type, textureManager* _textures, float _s)
	{
		if (_textures->hasTexture(_type.key))
			ImGui::Image(*_textures->getTexture(_type.key), sf::Vector2f(128.f * _s, 128.f * _s));
		ImGui::Text("%s", _type.name.c_str());
		ImGui::TextDisabled("%s", _type.origin.c_str());   //"content/power.toml:14 [item.generator60]"
		if (_type.removed)
			ImGui::TextColored(amber, "no longer in the files: placed ones stay, it can't be placed again");
		ImGui::Text("tab: %s   layer: %s%s%s", _type.category.empty() ? "(not in the menu)" : _type.category.c_str(), layerName(_type.layer),
			_type.layer == tileLayer::wall ? "   edge: " : "", _type.layer == tileLayer::wall ? sideName(_type.wallEdge) : "");
		ImGui::TextDisabled("art: %s", _type.art.c_str());
		if (!_type.drawArms.empty())
			ImGui::TextDisabled("draw_arms: %s", _type.drawArms.c_str());
		if (const rotationFamily* family = familyOf(_type.key))
			ImGui::TextDisabled("turns: %s", family->name.c_str());
		if (const kindFamily* kinds = kindFamilyOf(_type.key))
			ImGui::TextDisabled("kinds: %s (%s)", kinds->name.c_str(), kinds->labels[std::max(kindIndex(_type.key), 0)].c_str());
		ImGui::Separator();

		const std::string electricalText = electrical::summary(_type.electrical);
		if (!electricalText.empty())
			ImGui::TextColored(ImVec4(1.f, 0.85f, 0.3f, 1.f), "%s", electricalText.c_str());
		const std::string fluidText = fluid::summary(_type.fluid);
		if (!fluidText.empty())
			ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.f, 1.f), "%s", fluidText.c_str());
		const std::string storageText = storage::summary(_type.storage);
		if (!storageText.empty())
			ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.55f, 1.f), "%s", storageText.c_str());
		const std::string ductText = thermal::summary(_type.thermal);
		if (!ductText.empty())
			ImGui::TextColored(ImVec4(0.75f, 0.85f, 0.95f, 1.f), "%s", ductText.c_str());
		const std::string shipText = shipParts::summary(_type);
		if (!shipText.empty())
			ImGui::TextColored(uiTheme::info, "%s", shipText.c_str());
		if (_type.thermal.heatCapacity > 0.0)
			ImGui::Text("adds %.1f kJ/K to its tile's heat capacity", _type.thermal.heatCapacity / 1000.0);
		if (_type.thermal.wallConductance > 0.0)
			ImGui::Text("lets %.2f W/K through", _type.thermal.wallConductance);
		//what it makes: one line per recipe
		for (const recipeInfo& recipe : _type.recipes)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.4f, 1.f));
			ImGui::TextWrapped("%s: %s -> %s, %g s%s", recipe.name.c_str(), recipes::describe(recipe.inputs).c_str(),
				recipes::describe(recipe.outputs).c_str(), recipe.seconds, recipe.heat != 0.0 ? (", " + std::to_string(int(recipe.heat)) + " W heat").c_str() : "");
			ImGui::PopStyleColor();
		}
		if (_type.maxTemperature > 0.0)
			ImGui::Text("stops above %.0f C", _type.maxTemperature);

		if (!_type.settings.empty())
		{
			ImGui::Separator();
			ImGui::TextUnformatted("settings");
			for (const itemSetting& setting : _type.settings)
			{
				if (setting.type == itemSetting::kind::toggle)
					ImGui::BulletText("%s \"%s\": on/off, %s%s", setting.key.c_str(), setting.label.c_str(), setting.defaultValue != 0.0 ? "on" : "off",
						setting.hidden ? " (hidden)" : "");
				else
					ImGui::BulletText("%s \"%s\": %g %s (%g to %g, steps of %g)%s", setting.key.c_str(), setting.label.c_str(), setting.defaultValue,
						setting.unit.c_str(), setting.minimum, setting.maximum, setting.step, setting.hidden ? " (hidden)" : "");
			}
		}
	}
}

void contentPanel::notice(const std::string& _text, bool _problem)
{
	noticeText = _text;
	noticeProblem = _problem;
	noticeStart = ImGui::GetTime();
}

void contentPanel::draw(textureManager* _textures, float _scale, ImFont* _font)
{
	const float s = _scale;
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	if (_font)
		ImGui::PushFont(_font);

	//the notice after a reload, along the top
	const float age = float(ImGui::GetTime() - noticeStart);
	if (age < noticeSeconds && !noticeText.empty())
	{
		ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, 12.f * s), ImGuiCond_Always, ImVec2(0.5f, 0.f));
		ImGui::SetNextWindowBgAlpha(0.9f);
		ImGui::Begin("contentNotice", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
		ImGui::TextColored(noticeProblem ? red : green, "%s", noticeText.c_str());
		ImGui::End();
	}

	if (visible)
	{
		const content::report& report = content::lastReport();
		ImGui::SetNextWindowPos(ImVec2(140.f, 60.f * s), ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(820.f * s, 600.f * s), ImGuiCond_Appearing);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, uiTheme::panel());
		if (ImGui::Begin("Content (F5 reloads the files, F6 closes)", &visible, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse))
		{
			ImGui::Text("%s: %s, %s, %s", content::count(int(report.files.size()), "file").c_str(), content::count(report.items, "item").c_str(),
				content::count(report.materials, "material").c_str(), content::count(report.families, "family", "families").c_str());
			ImGui::SameLine();
			if (report.errors() > 0)
				ImGui::TextColored(red, "  %s", content::count(report.errors(), "error").c_str());
			else
				ImGui::TextColored(green, "  no errors");
			if (report.warnings() > 0)
			{
				ImGui::SameLine();
				ImGui::TextColored(amber, "  %s", content::count(report.warnings(), "warning").c_str());
			}

			if (!report.messages.empty() && ImGui::CollapsingHeader("Problems", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::BeginChild("problems", ImVec2(0.f, std::min(170.f * s, (report.messages.size() + 1) * ImGui::GetTextLineHeightWithSpacing() * 1.6f)), true);
				for (const content::message& m : report.messages)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, m.error ? red : amber);
					ImGui::TextWrapped("%s", m.where.c_str());
					ImGui::PopStyleColor();
					ImGui::Indent();
					ImGui::TextWrapped("%s", m.text.c_str());
					ImGui::Unindent();
				}
				ImGui::EndChild();
			}

			if (ImGui::BeginTabBar("contentTabs"))
			{
				if (ImGui::BeginTabItem("Items"))
				{
					ImGui::BeginChild("itemList", ImVec2(250.f * s, 0.f), true);
					for (const tileTypeInfo& type : tileCatalogue())
					{
						if (type.removed)
							ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
						const std::string label = type.key + (type.name != type.key ? "  (" + type.name + ")" : "");
						if (ImGui::Selectable(label.c_str(), selectedItem == type.key))
							selectedItem = type.key;
						if (type.removed)
							ImGui::PopStyleColor();
					}
					ImGui::EndChild();
					ImGui::SameLine();
					ImGui::BeginChild("itemDetails", ImVec2(0.f, 0.f), true);
					if (const tileTypeInfo* type = findTileType(selectedItem))
					{
						if (onEditItem && ImGui::Button("Edit it (F7 editor)"))
							onEditItem(type->key);
						if (onNewItem)
						{
							ImGui::SameLine();
							if (ImGui::Button("New item"))
								onNewItem();
						}
						itemDetails(*type, _textures, s);
					}
					else
					{
						ImGui::TextDisabled("pick an item on the left");
						if (onNewItem && ImGui::Button("New item"))
							onNewItem();
					}
					ImGui::EndChild();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Materials"))
				{
					if (materialCatalogue().empty())
						ImGui::TextDisabled("no materials: add [material.<key>] tables");
					for (const materialInfo& material : materialCatalogue())
					{
						const float size = ImGui::GetTextLineHeight();
						const ImVec2 p = ImGui::GetCursorScreenPos();
						ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + size, p.y + size),
							IM_COL32(material.colour.r, material.colour.g, material.colour.b, 255));
						ImGui::Dummy(ImVec2(size, size));
						ImGui::SameLine();
						ImGui::Text("%s (%s): %s, %g kg/m3, %g J/(kg K)%s", material.name.c_str(), material.key.c_str(), phaseName(material.state),
							material.density, material.heatCapacity, material.removed ? "  [no longer in the files]" : "");
						ImGui::SameLine();
						ImGui::TextDisabled("%s", material.origin.c_str());
						if (onEditMaterial)
						{
							ImGui::SameLine();
							if (ImGui::SmallButton(("edit##" + material.key).c_str()))
								onEditMaterial(material.key);
						}
					}
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Files"))
				{
					for (const std::string& file : report.files)
						ImGui::BulletText("%s", file.c_str());
					ImGui::TextDisabled("every .toml file in the content folder and its subfolders is read, in name order");
					ImGui::TextDisabled("what a file can hold: content/README.md");
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::End();
		ImGui::PopStyleColor();
	}
	if (_font)
		ImGui::PopFont();
}
