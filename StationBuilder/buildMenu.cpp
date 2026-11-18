#include "buildMenu.h"
#include "tileCatalogue.h"
#include "lineArt.h"
#include "machineView.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "uiTheme.h"
#include <imgui-SFML.h>
#include <algorithm>
#include <cfloat>

namespace
{
	//sizes at scale 1 (the 1080p layout); everything but panelLeft is multiplied by the GUI scale
	const float panelWidth = 1100.f;        //wide enough for every tab's title in one row (more tabs scroll)
	const float minPanelWidth = 600.f;      //on a window too narrow for that
	const float panelHeight = 210.f;
	const float panelLeft = 130.f;          //right of the button column on the left, which doesn't scale
	const float panelMargin = 8.f;          //gap under the panel and its inner padding
	const float rightColumn = 346.f;        //kept clear on the right: the tile panel and the views' keys (330 wide)
	const float catalogueWidth = 520.f;     //the item icons, under the tabs; the preview gets the rest
	const float iconSize = 48.f;
	const float iconGap = 4.f;
	const float previewSize = 96.f;
	const ImVec2 tabPadding(8.f, 3.f);      //round each tab's title
	const float warningSeconds = 2.5f;      //how long a refused-placement warning stays up
	const float warningFadeSeconds = 0.5f;  //the last part of that time, when it fades out

	//the mouse wheel over a row of tabs that doesn't fit scrolls it (ImGui scrolls one only with Shift held, or a
	//sideways wheel). call between BeginTabBar and the first BeginTabItem
	void scrollTabsWithWheel()
	{
		ImGuiTabBar* bar = GImGui->CurrentTabBar;
		const ImGuiIO& io = ImGui::GetIO();
		if (!bar || io.MouseWheel == 0.f || io.KeyShift || !ImGui::IsWindowHovered() ||
			!ImGui::IsMouseHoveringRect(bar->BarRect.Min, bar->BarRect.Max))
			return;
		bar->ScrollingTargetDistToVisibility = 0.f;
		bar->ScrollingTarget -= io.MouseWheel * bar->BarRect.GetWidth() / 3.f;   //ImGui keeps it in range as it lays them out
	}
}

std::string buildMenu::selectedKey() const
{
	return selected;
}

void buildMenu::select(const std::string& _key)
{
	if (selected != _key)
	{
		rotation = 0;   //a newly picked item starts unturned, and a cable or pipe on Auto
		lineShape = connections::shape::automatic;
	}
	selected = _key;
}

std::string buildMenu::baseKey() const
{
	const kindFamily* kinds = kindFamilyOf(selected);
	if (!kinds)
		return selected;
	return kinds->keys[std::clamp(kindChoice, 0, int(kinds->keys.size()) - 1)];
}

void buildMenu::setKind(int _kind)
{
	kindChoice = std::max(_kind, 0);
	//a turn only means something to a shaped piece: keep it in range for the new kind's shape
	if (selectedIsLine())
		rotation %= connections::rotations(lineShape);
}

bool buildMenu::selectedIsLine() const
{
	const tileTypeInfo* type = findTileType(baseKey());
	return type && findSetting(*type, connections::settingKey);
}

bool buildMenu::selectedIsMachine() const
{
	const tileTypeInfo* type = findTileType(baseKey());
	return type && footprint::isMachine(*type);
}

std::string buildMenu::placementKey() const
{
	if (selected.empty())
		return "";
	return selectedIsMachine() ? baseKey() : rotateKey(baseKey(), rotation);   //a machine keeps its turn in a setting
}

int buildMenu::placementMask() const
{
	return selectedIsLine() ? connections::shapeMask(lineShape, rotation) : connections::all;
}

std::string buildMenu::placementSettings() const
{
	if (selectedIsMachine())
		return rotation > 0 ? std::string(footprint::rotationKey) + "=" + std::to_string(rotation) : "";
	if (!selectedIsLine() || placementMask() == connections::all)
		return "";
	return std::string(connections::settingKey) + "=" + std::to_string(placementMask());
}

void buildMenu::rotate()
{
	if (selected.empty())
		return;
	const int turns = familyOf(selected) || selectedIsMachine() ? 4 : (selectedIsLine() ? connections::rotations(lineShape) : 1);
	rotation = (rotation + 1) % turns;
}

void buildMenu::nextShape()
{
	if (!selectedIsLine())
		return;
	lineShape = connections::nextShape(lineShape);
	rotation = 0;
}

std::string buildMenu::placementTexture(textureManager* _textures) const
{
	const std::string key = placementKey();
	if (selectedIsLine())
	{
		const std::string variant = lineArt::variantKey(key, placementMask());
		if (_textures->hasTexture(variant))
			return variant;
	}
	if (selectedIsMachine())
	{
		//its art for the turn (if it has its own), on the stubs it connects on, turned
		const tileTypeInfo* type = findTileType(key);
		const std::string turned = key + "@" + std::to_string(rotation);
		const std::string art = rotation > 0 && _textures->hasTexture(turned) ? turned : key;
		int sides = lineArt::ownSides(*type);
		for (int t = 0; t < rotation; t++)
			sides = connections::rotate(sides);
		const std::string variant = lineArt::variantKey(art, sides);
		return _textures->hasTexture(variant) ? variant : art;
	}
	return iconTexture(key, _textures);
}

std::string buildMenu::iconTexture(const std::string& _key, textureManager* _textures)
{
	//a pump on its in-line stubs, an exchanger on all four; cables and pipes are their four-way cross either way
	const tileTypeInfo* type = findTileType(_key);
	const std::string variant = lineArt::variantKey(_key, type ? lineArt::ownSides(*type) : 0);
	return type && _textures->hasTexture(variant) ? variant : _key;
}

void buildMenu::drawGhost(sf::RenderWindow& _window, isometricLevel* _level, sf::Vector2i _tile, textureManager* _textures) const
{
	const std::string key = placementKey();
	if (key.empty() || !_level->isInLevel(_tile) || !_textures->hasTexture(key))
		return;
	const sf::Vector2f tileCoords(_tile);
	const sf::Texture& texture = *_textures->getTexture(placementTexture(_textures));
	sf::Sprite ghost(texture);
	ghost.setPosition(_level->tileWorldPosition(tileCoords));
	const tileTypeInfo* type = findTileType(key);
	sf::Vector2i origin = _tile, size(1, 1);
	if (type && footprint::isMachine(*type))
	{
		//a machine: over its whole footprint, turned, with the pointed tile at its middle; drawn from its front tile as
		//the placed one is (mirrored on a quarter turn when it is not square and has no art of its own for it)
		size = footprint::turnedSize(*type, rotation);
		origin = footprint::originFor(_tile, size);
		const sf::Vector2i front = origin + size - sf::Vector2i(1, 1);
		const sf::Vector2f frontRoot = _level->tileWorldPosition(sf::Vector2f(_tile)) +
			(_level->tileCentreWorld(sf::Vector2f(front)) - _level->tileCentreWorld(sf::Vector2f(_tile)));
		ghost.setPosition(frontRoot + sf::Vector2f(32.f - 32.f * float(size.x), 64.f - float(texture.getSize().y)));
		const bool ownArt = rotation > 0 && _textures->hasTexture(key + "@" + std::to_string(rotation));
		if ((rotation & 1) && type->footprint.x != type->footprint.y && !ownArt)
		{
			ghost.setOrigin(float(texture.getSize().x), 0.f);
			ghost.setScale(-1.f, 1.f);
		}
	}
	//see-through if a click would place it, a dark silhouette if the tile would refuse it (a red tint wouldn't show on
	//the red walls, or on the hovered tile, which build mode already tints red)
	const bool fits = _level->canAddToTile(tileCoords, key, placementSettings());
	ghost.setColor(fits ? sf::Color(255, 255, 255, 170) : sf::Color(25, 25, 25, 190));
	_window.draw(ghost);
	//a machine: its footprint outlined and its ports marked, so it can be turned to face the right way
	if (type && footprint::isMachine(*type))
	{
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		machineView::drawFootprint(drawList, _window, *_level, origin, size, fits ? IM_COL32(255, 255, 255, 200) : IM_COL32(230, 60, 60, 220), 2.f);
		machineView::drawPorts(drawList, _window, *_level, *type, rotation, origin);
	}
}

void buildMenu::setScale(float _scale, ImFont* _font)
{
	scale = _scale;
	font = _font;
}

void buildMenu::draw(textureManager* _textures)
{
	const float s = scale;
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	if (font)
		ImGui::PushFont(font);
	//as wide as it likes, but clear of the column of panels on the right on a narrow window
	const float width = std::max(std::min(panelWidth * s, display.x - panelLeft - rightColumn * s), minPanelWidth * s);
	ImGui::SetNextWindowPos(ImVec2(panelLeft, display.y - (panelHeight + panelMargin) * s));
	ImGui::SetNextWindowSize(ImVec2(width, panelHeight * s));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, uiTheme::panel());
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(panelMargin * s, panelMargin * s));
	ImGui::Begin("buildMenu", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse);

	//the tabs: every category that has at least one item with a registered texture, in the order the content files'
	//[menu] tabs list gives, then any others in catalogue order
	std::vector<std::string> categories;
	for (const tileTypeInfo& type : tileCatalogue())
	{
		if (type.category.empty() || type.removed || !_textures->hasTexture(type.key))
			continue;
		if (std::find(categories.begin(), categories.end(), type.category) == categories.end())
			categories.push_back(type.category);
	}
	const std::vector<std::string>& order = catalogueStore::tabOrder();
	auto rank = [&](const std::string& _tab) { return int(std::find(order.begin(), order.end(), _tab) - order.begin()); };
	std::stable_sort(categories.begin(), categories.end(), [&](const std::string& _a, const std::string& _b) { return rank(_a) < rank(_b); });

	//the tabs, in a row across the top with each title in full: a row too long to fit scrolls (the mouse wheel over it,
	//or the arrows at its end). the open tab's items are drawn under it, beside the preview
	std::string openTab;
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(tabPadding.x * s, tabPadding.y * s));
	const bool tabsShown = ImGui::BeginTabBar("buildTabs", ImGuiTabBarFlags_FittingPolicyScroll);
	if (tabsShown)
	{
		scrollTabsWithWheel();
		for (const std::string& category : categories)
		{
			if (ImGui::BeginTabItem(category.c_str()))
			{
				openTab = category;
				ImGui::EndTabItem();
			}
		}
		ImGui::EndTabBar();
	}
	ImGui::PopStyleVar();
	ImGui::Dummy(ImVec2(0.f, iconGap * s));

	//the open tab's items, as many to a row as fit (the rest scroll)
	ImGui::BeginChild("catalogue", ImVec2(std::min(catalogueWidth * s, (width - 2.f * panelMargin * s) * 0.5f), 0.f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(iconGap * s, iconGap * s));
	const float iconWidth = iconSize * s + 2.f * ImGui::GetStyle().FramePadding.x;   //an ImageButton's
	const int iconsPerRow = std::max(1, int((ImGui::GetContentRegionAvail().x + iconGap * s) / (iconWidth + iconGap * s)));
	int shown = 0;
	for (const tileTypeInfo& type : tileCatalogue())
	{
		if (openTab.empty() || type.category != openTab || type.removed || !_textures->hasTexture(type.key))
			continue;
		if (shown % iconsPerRow != 0)
			ImGui::SameLine();
		shown++;

		const bool isSelected = (selected == type.key);
		//a riser's icon shows the kind it will be placed as
		const kindFamily* kinds = kindFamilyOf(type.key);
		const std::string iconKey = kinds ? kinds->keys[std::clamp(kindChoice, 0, int(kinds->keys.size()) - 1)] : type.key;
		ImGui::PushID(type.key.c_str());
		if (isSelected)
			ImGui::PushStyleColor(ImGuiCol_Button, uiTheme::fillActive());
		if (ImGui::ImageButton(type.key.c_str(), *_textures->getTexture(iconTexture(_textures->hasTexture(iconKey) ? iconKey : type.key, _textures)),
			sf::Vector2f(iconSize * s, iconSize * s)))
			select(type.key);
		if (isSelected)
			ImGui::PopStyleColor();
		if (ImGui::IsItemHovered())
		{
			const rotationFamily* family = familyOf(type.key);
			ImGui::SetTooltip("%s", family ? family->name.c_str() : (kinds ? kinds->name.c_str() : type.name.c_str()));
		}
		ImGui::PopID();
	}
	ImGui::PopStyleVar();
	ImGui::EndChild();

	//the preview: what is selected, its texture at a larger size, and beside it its title and what it does (wrapped to
	//fit, scrolling if there is a lot)
	ImGui::SameLine(0.f, panelMargin * s);
	ImGui::BeginChild("preview", ImVec2(0.f, 0.f));
	const tileTypeInfo* chosen = findTileType(placementKey());
	if (chosen && _textures->hasTexture(chosen->key))
	{
		//the picture shows what a click places: turned, or just the arms of a cable's or pipe's shape
		ImGui::Image(*_textures->getTexture(placementTexture(_textures)), sf::Vector2f(previewSize * s, previewSize * s));
		ImGui::SameLine(0.f, panelMargin * s);
		ImGui::BeginGroup();
		ImGui::PushTextWrapPos(0.f);
		const rotationFamily* family = familyOf(chosen->key);
		if (const kindFamily* kinds = kindFamilyOf(chosen->key))
		{
			//a riser: its name, and what it carries to pick from
			ImGui::TextColored(uiTheme::bright(), "%s", kinds->name.c_str());
			for (int k = 0; k < int(kinds->keys.size()); k++)
			{
				if (k > 0)
					ImGui::SameLine(0.f, panelMargin * s);
				if (ImGui::RadioButton(kinds->labels[k].c_str(), kinds->keys[k] == chosen->key))
					setKind(k);
			}
		}
		else
			ImGui::TextColored(uiTheme::bright(), "%s", family ? family->name.c_str() : chosen->name.c_str());
		static const char* sides[4] = { "upper left", "upper right", "lower left", "lower right" };
		if (family && chosen->layer == tileLayer::wall)
			ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.f), "on the %s edge (R turns it)", sides[chosen->wallEdge]);
		else if (family && chosen->fluid.pump && chosen->fluid.pumpSide >= 0)
			ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.f), "pushes %s (R turns it)", sides[chosen->fluid.pumpSide]);
		else if (footprint::isMachine(*chosen))
		{
			//a machine: its size as turned, and its ports (marked on the ghost in the world)
			const sf::Vector2i size = footprint::turnedSize(*chosen, rotation);
			std::string ports;
			for (const portInfo& port : chosen->ports)
				ports += std::string(ports.empty() ? "" : ", ") + portKindName(port.carries) +
					(port.flow == portInfo::direction::both ? "" : std::string(" ") + portFlowName(port.flow));
			if (size != sf::Vector2i(1, 1))
				ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.f), "%d x %d tiles", size.x, size.y);
			if (!ports.empty())
				ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.f), "ports: %s", ports.c_str());
			ImGui::TextDisabled("R turns it");
		}
		else if (selectedIsLine())
		{
			ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.f), "%s: %s", connections::shapeName(lineShape),
				lineShape == connections::shape::automatic ? "joins whatever is next to it" : connections::describe(placementMask()).c_str());
			ImGui::TextDisabled("R turns it, Shift+R changes shape");
		}
		const std::string electricalText = electrical::summary(chosen->electrical);
		if (!electricalText.empty())
			ImGui::TextColored(ImVec4(1.f, 0.85f, 0.3f, 1.f), "%s", electricalText.c_str());
		const std::string fluidText = fluid::summary(chosen->fluid);
		if (!fluidText.empty() && !(chosen->fluid.pump && familyOf(chosen->key)))   //an old-style pump's direction is shown above
			ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.f, 1.f), "%s", fluidText.c_str());
		const std::string storageText = storage::summary(chosen->storage);
		if (!storageText.empty())
			ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.55f, 1.f), "%s", storageText.c_str());
		const std::string thermalText = thermal::summary(chosen->thermal);
		if (!thermalText.empty())
			ImGui::TextColored(ImVec4(0.75f, 0.85f, 0.95f, 1.f), "%s", thermalText.c_str());
		const std::string recipeText = recipes::summary(*chosen);
		if (!recipeText.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.4f, 1.f));
			ImGui::TextWrapped("%s", recipeText.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();
	}
	else
	{
		ImGui::TextDisabled("nothing selected");
		ImGui::TextDisabled("pick an item from a tab");
	}
	ImGui::EndChild();

	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
	if (font)
		ImGui::PopFont();
}

void buildMenu::warnAt(sf::Vector2f _tileCoords, const std::string& _message)
{
	warningActive = true;
	warningTile = _tileCoords;
	warningText = _message;
	warningStart = ImGui::GetTime();
}

void buildMenu::drawWarning(sf::RenderWindow& _window, isometricLevel* _level)
{
	if (!warningActive)
		return;
	const float age = float(ImGui::GetTime() - warningStart);
	if (age > warningSeconds)
	{
		warningActive = false;
		return;
	}
	const float alpha = age > warningSeconds - warningFadeSeconds ? (warningSeconds - age) / warningFadeSeconds : 1.f;

	//the tile sprite starts at its top corner, so aim at roughly the middle of its diamond,
	//and put the text up and to the right of it, its bottom edge a fixed distance above the tile
	ImFont* textFont = font ? font : ImGui::GetFont();
	const float fontSize = textFont->FontSize;
	const sf::Vector2i centre = _window.mapCoordsToPixel(_level->tileCentreWorld(warningTile));
	const ImVec2 textSize = textFont->CalcTextSizeA(fontSize, FLT_MAX, 0.f, warningText.c_str());
	const ImVec2 topLeft((float)centre.x + 24.f, (float)centre.y - 44.f - textSize.y);
	const ImVec2 bottomRight(topLeft.x + textSize.x, topLeft.y + textSize.y);
	const ImVec2 pad(4.f * scale, 3.f * scale);

	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	drawList->AddRectFilled(ImVec2(topLeft.x - pad.x, topLeft.y - pad.y), ImVec2(bottomRight.x + pad.x, bottomRight.y + pad.y),
		IM_COL32(0, 0, 0, (int)(200 * alpha)));
	drawList->AddText(textFont, fontSize, topLeft, IM_COL32(255, 70, 70, (int)(255 * alpha)), warningText.c_str());
}
