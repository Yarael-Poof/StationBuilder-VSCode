#include "contentEditor.h"
#include "contentPanel.h"
#include "contentWriter.h"
#include "machineView.h"
#include "station.h"
#include "textureManager.h"
#include <imgui.h>
#include "uiTheme.h"
#include <imgui-SFML.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
	namespace fs = std::filesystem;
	const ImVec4 red = uiTheme::danger;
	const ImVec4 amber = uiTheme::warning;
	const ImVec4 green = uiTheme::good;
	const double unlimited = std::numeric_limits<double>::infinity();
	const char* const sideNames[4] = { "UL", "UR", "LL", "LR" };
	const std::vector<std::string> sideList = { "UL", "UR", "LL", "LR" };

	float labelColumn = 160.f;   //where fields start, set each frame from the scale

	//a label in the form's left column, its field to the right
	void label(const char* _text)
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(_text);
		ImGui::SameLine(labelColumn);
	}
	void hint(const char* _text)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("%s", _text);
	}
	bool editText(const char* _id, std::string& _value, float _width, const char* _placeholder = nullptr)
	{
		char buffer[512];
		snprintf(buffer, sizeof(buffer), "%s", _value.c_str());
		ImGui::SetNextItemWidth(_width);
		const bool changed = _placeholder ? ImGui::InputTextWithHint(_id, _placeholder, buffer, sizeof(buffer)) : ImGui::InputText(_id, buffer, sizeof(buffer));
		if (changed)
			_value = buffer;
		return changed;
	}
	bool editNumber(const char* _id, double& _value, float _width, double _min = -unlimited, double _max = unlimited)
	{
		double value = _value;
		ImGui::SetNextItemWidth(_width);
		if (!ImGui::InputDouble(_id, &value, 0.0, 0.0, "%g"))
			return false;
		_value = std::clamp(value, _min, _max);
		return true;
	}
	//a combo over some names; true when another is picked
	bool pick(const char* _id, int& _index, const std::vector<std::string>& _names, float _width)
	{
		bool changed = false;
		ImGui::SetNextItemWidth(_width);
		if (ImGui::BeginCombo(_id, _index >= 0 && _index < int(_names.size()) ? _names[_index].c_str() : "-"))
		{
			for (int i = 0; i < int(_names.size()); i++)
			{
				if (ImGui::Selectable(_names[i].c_str(), i == _index) && i != _index)
				{
					_index = i;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		return changed;
	}
	void swatch(sf::Color _colour, float _size)
	{
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + _size, p.y + _size), IM_COL32(_colour.r, _colour.g, _colour.b, 255));
		ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + _size, p.y + _size), IM_COL32(0, 0, 0, 255));
		ImGui::Dummy(ImVec2(_size, _size));
	}
	ImTextureID textureId(const sf::Texture& _texture)
	{
		ImTextureID id{};
		const unsigned int handle = _texture.getNativeHandle();
		std::memcpy(&id, &handle, sizeof(handle));
		return id;
	}
	std::string lowerCase(std::string _text)
	{
		for (char& c : _text)
			c = char(std::tolower(static_cast<unsigned char>(c)));
		return _text;
	}
	std::string defaultBlocked(const std::string& _name) { return "cannot place " + lowerCase(_name) + " here"; }
	bool reservedSetting(const itemSetting& _s)
	{
		return _s.hidden && (_s.key == connections::settingKey || _s.key == footprint::rotationKey || _s.key == recipes::settingKey);
	}

	//every tab: the menu's order first, then the others in the order the items name them
	std::vector<std::string> tabsInOrder()
	{
		std::vector<std::string> tabs = catalogueStore::tabOrder();
		for (const tileTypeInfo& type : tileCatalogue())
			if (!type.category.empty() && !type.removed && std::find(tabs.begin(), tabs.end(), type.category) == tabs.end())
				tabs.push_back(type.category);
		return tabs;
	}
	//materials a recipe can use (solids and liquids)
	std::vector<const materialInfo*> usableMaterials(bool _liquidsOnly = false)
	{
		std::vector<const materialInfo*> list;
		for (const materialInfo& m : materialCatalogue())
			if (!m.removed && m.state != materialInfo::phase::gas && (!_liquidsOnly || m.state == materialInfo::phase::liquid))
				list.push_back(&m);
		return list;
	}

	//the preview's geometry, in world units like the game (a tile's floor diamond is 64 x 32): the centre of a cell of the
	//turned footprint, from the centre of its upper tile
	sf::Vector2f cellCentre(sf::Vector2i _cell) { return sf::Vector2f(32.f * float(_cell.x - _cell.y), 16.f * float(_cell.x + _cell.y)); }
	sf::Vector2f sideMiddle(int _side)
	{
		static const sf::Vector2f middles[4] = { sf::Vector2f(-16.f, -8.f), sf::Vector2f(16.f, -8.f), sf::Vector2f(-16.f, 8.f), sf::Vector2f(16.f, 8.f) };
		return middles[_side & 3];
	}
	sf::Vector2f outOf(int _side)
	{
		const sf::Vector2i s = footprint::step(_side);
		const sf::Vector2f out(32.f * float(s.x - s.y), 16.f * float(s.x + s.y));
		return out / std::hypot(out.x, out.y);
	}
}

contentEditor::contentEditor(const std::string& _contentFolder)
	: folder(_contentFolder)
{
	while (!folder.empty() && (folder.back() == '/' || folder.back() == '\\'))
		folder.pop_back();
}

bool contentEditor::hasUnsavedChanges() const
{
	return editing != mode::none && (isNew || text != savedText);
}

void contentEditor::whenSafe(const std::function<void()>& _action)
{
	visible = true;
	if (hasUnsavedChanges())
	{
		pending = _action;
		askDiscard = true;
		return;
	}
	_action();
}

void contentEditor::editItem(const std::string& _key)
{
	whenSafe([this, _key]() { loadItem(_key); });
}

void contentEditor::editMaterial(const std::string& _key)
{
	whenSafe([this, _key]() { loadMaterial(_key); });
}

void contentEditor::loadItem(const std::string& _key)
{
	const tileTypeInfo* type = findTileType(_key);
	if (!type)
		return;
	editing = mode::item;
	item = *type;
	isNew = false;
	file = fileOf(type->origin);
	text = contentWriter::itemText(item);
	savedText = text;
	checkedText.clear();
	afterSave.clear();
	saveError.clear();
	savedNote.clear();
	previewTurn = 0;
	pickedPort = -1;
	listMaterials = false;
}

void contentEditor::loadMaterial(const std::string& _key)
{
	const materialInfo* found = findMaterial(_key);
	if (!found)
		return;
	editing = mode::material;
	material = *found;
	isNew = false;
	file = fileOf(found->origin);
	text = contentWriter::materialText(material);
	savedText = text;
	checkedText.clear();
	afterSave.clear();
	saveError.clear();
	savedNote.clear();
	listMaterials = true;
}

void contentEditor::newItem()
{
	whenSafe([this]() {
		editing = mode::item;
		item = tileTypeInfo();
		item.key = uniqueKey("newItem", false);
		item.name = "New item";
		item.category = "Custom";
		for (const std::string& path : artFiles())
			if (path.find("crate") != std::string::npos || item.art.empty())
				item.art = path;
		isNew = true;
		file = fs::path(folder).filename().generic_string() + "/custom.toml";
		savedText.clear();
		checkedText.clear();
		afterSave.clear();
		saveError.clear();
		savedNote.clear();
		previewTurn = 0;
		pickedPort = -1;
		listMaterials = false;
	});
}

void contentEditor::newMaterial()
{
	whenSafe([this]() {
		editing = mode::material;
		material = materialInfo();
		material.key = uniqueKey("newMaterial", true);
		material.name = "New material";
		material.colour = sf::Color(190, 190, 190);
		isNew = true;
		file = fs::path(folder).filename().generic_string() + "/custom.toml";
		savedText.clear();
		checkedText.clear();
		afterSave.clear();
		saveError.clear();
		savedNote.clear();
		listMaterials = true;
	});
}

std::string contentEditor::fileOf(const std::string& _origin) const
{
	//"content/power.toml:12 [item.cable]" -> "content/power.toml"
	const std::string place = _origin.substr(0, _origin.rfind(" ["));
	const size_t colon = place.rfind(':');
	if (place.empty() || colon == std::string::npos || colon < 2)
		return fs::path(folder).filename().generic_string() + "/custom.toml";
	return place.substr(0, colon);
}

std::string contentEditor::diskPath(const std::string& _file) const
{
	return (fs::path(folder).parent_path() / fs::path(_file)).generic_string();
}

std::string contentEditor::uniqueKey(const std::string& _base, bool _material) const
{
	auto taken = [&](const std::string& _key) {
		if (_material)
		{
			const materialInfo* m = findMaterial(_key);
			return m && !m->removed;
		}
		const tileTypeInfo* t = findTileType(_key);
		return t && !t->removed;
	};
	if (!taken(_base))
		return _base;
	for (int n = 2;; n++)
		if (!taken(_base + std::to_string(n)))
			return _base + std::to_string(n);
}

const sf::Texture* contentEditor::art(const std::string& _path)
{
	if (_path.empty())
		return nullptr;
	std::error_code problem;
	const fs::path path = diskPath(_path);
	const fs::file_time_type time = fs::last_write_time(path, problem);
	if (problem)
		return nullptr;
	artEntry& entry = artCache[_path];
	if ((!entry.texture && !entry.failed) || entry.time != time)
	{
		entry.time = time;
		entry.texture = std::make_unique<sf::Texture>();
		entry.failed = !entry.texture->loadFromFile(path.string());
		if (entry.failed)
			entry.texture.reset();
	}
	return entry.texture.get();
}

const std::vector<std::string>& contentEditor::artFiles()
{
	//the game folder's tiles/ (and any folders in it), looked at again every few seconds
	if (ImGui::GetCurrentContext() && ImGui::GetTime() - artListTime < 3.0 && !artList.empty())
		return artList;
	artListTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
	artList.clear();
	std::error_code problem;
	const fs::path game = fs::path(folder).parent_path();
	for (fs::recursive_directory_iterator it(game / "tiles", problem), end; !problem && it != end; it.increment(problem))
	{
		if (it->is_regular_file() && lowerCase(it->path().extension().string()) == ".png")
			artList.push_back(fs::relative(it->path(), game.empty() ? fs::path(".") : game, problem).generic_string());
	}
	std::sort(artList.begin(), artList.end());
	return artList;
}

void contentEditor::check()
{
	if (text == checkedText)
		return;
	checkedText = text;
	if (editing == mode::item)
	{
		tileTypeInfo read;
		problems = content::readItemText(text, item.key, "", read);
		const tileTypeInfo* existing = findTileType(item.key);
		if (isNew && existing && !existing->removed)
			problems.insert(problems.begin(), content::message{ true, "", "key: there is already an item called \"" + item.key + "\" (" + existing->origin + ")" });
		if (!isNew && existing && !existing->removed && (existing->layer != read.layer || existing->wallEdge != read.wallEdge ||
			existing->footprint != read.footprint || footprint::isMachine(*existing) != footprint::isMachine(read)))
			problems.push_back(content::message{ false, "", "its layer, wall edge, footprint or being a machine changed: it is saved, but the game keeps the old one until it restarts" });
	}
	else if (editing == mode::material)
	{
		materialInfo read;
		problems = content::readMaterialText(text, material.key, "", read);
		const materialInfo* existing = findMaterial(material.key);
		if (isNew && existing && !existing->removed)
			problems.insert(problems.begin(), content::message{ true, "", "key: there is already a material called \"" + material.key + "\" (" + existing->origin + ")" });
	}
	else
		problems.clear();
}

void contentEditor::draw(station& _station, textureManager& _textures, contentPanel& _panel, float _scale, ImFont* _font)
{
	if (!visible)
		return;
	const float s = _scale;
	labelColumn = 165.f * s;
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	if (_font)
		ImGui::PushFont(_font);
	ImGui::SetNextWindowSize(ImVec2(std::min(1400.f * s, display.x - 40.f), std::min(880.f * s, display.y - 40.f)), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	//its own look: the game's style has no padding or spacing and black buttons, which suits the icon bar but not a form
	const std::pair<ImGuiCol, ImVec4> colours[] = { { ImGuiCol_WindowBg, uiTheme::panel() } };
	for (const auto& [which, colour] : colours)
		ImGui::PushStyleColor(which, colour);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f * s, 8.f * s));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f * s, 5.f * s));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f * s, 3.f * s));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(5.f * s, 4.f * s));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.f);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.f);
	if (ImGui::Begin("Content editor (F7 closes)", &visible, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse))
	{
		const float footer = ImGui::GetFrameHeightWithSpacing() * (saveError.empty() ? 1.4f : 2.4f);
		ImGui::BeginChild("list", ImVec2(240.f * s, -footer), true);
		drawList(s);
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("form", ImVec2(std::min(600.f * s, ImGui::GetContentRegionAvail().x * 0.55f), -footer), true);
		if (editing == mode::item)
			drawItemForm(s);
		else if (editing == mode::material)
			drawMaterialForm(s);
		else
		{
			ImGui::TextDisabled("Pick an item or material on the left to change it,");
			ImGui::TextDisabled("or make a new one.");
			ImGui::Spacing();
			ImGui::TextDisabled("Saving writes it into its content file and reloads, like F5.");
			ImGui::TextDisabled("Every field is described in content/README.md.");
		}
		ImGui::EndChild();
		//the text it writes, from the form as it is now
		if (editing == mode::item)
			text = contentWriter::itemText(item);
		else if (editing == mode::material)
			text = contentWriter::materialText(material);
		check();
		ImGui::SameLine();
		ImGui::BeginChild("preview", ImVec2(0.f, -footer), true);
		drawPreview(s);
		ImGui::EndChild();
		drawFooter(_station, _textures, _panel, s);

		//asking first
		if (askDiscard)
		{
			ImGui::OpenPopup("Discard changes?");
			askDiscard = false;
		}
		if (ImGui::BeginPopupModal("Discard changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("%s has changes that aren't saved.", editing == mode::item ? item.key.c_str() : material.key.c_str());
			if (ImGui::Button("Discard them"))
			{
				const std::function<void()> action = pending;
				pending = nullptr;
				editing = mode::none;   //so the action doesn't ask again
				ImGui::CloseCurrentPopup();
				if (action)
					action();
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep editing"))
			{
				pending = nullptr;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		if (askDelete)
		{
			ImGui::OpenPopup("Delete it?");
			askDelete = false;
		}
		if (ImGui::BeginPopupModal("Delete it?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			const std::string key = editing == mode::item ? item.key : material.key;
			ImGui::Text("Take %s out of %s?", key.c_str(), file.c_str());
			ImGui::TextDisabled("Anything already placed stays, but it leaves the build menu, as when it is deleted from the file by hand.");
			ImGui::TextDisabled("The file's text from before your first change this session is in %s.bak.", file.c_str());
			if (ImGui::Button("Delete"))
			{
				remove(_station, _textures, _panel);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(8);
	ImGui::PopStyleColor(int(sizeof(colours) / sizeof(colours[0])));
	if (_font)
		ImGui::PopFont();
}

void contentEditor::drawList(float _s)
{
	if (ImGui::RadioButton("Items", !listMaterials))
		listMaterials = false;
	ImGui::SameLine();
	if (ImGui::RadioButton("Materials", listMaterials))
		listMaterials = true;
	editText("##filter", filter, -1.f, "filter");
	const std::string wanted = lowerCase(filter);
	auto matches = [&](const std::string& _a, const std::string& _b, const std::string& _c) {
		return wanted.empty() || lowerCase(_a).find(wanted) != std::string::npos || lowerCase(_b).find(wanted) != std::string::npos ||
			lowerCase(_c).find(wanted) != std::string::npos;
	};
	if (!listMaterials)
	{
		if (ImGui::Button("New item"))
			newItem();
		if (editing == mode::item)
		{
			ImGui::SameLine();
			if (ImGui::Button("Duplicate"))
			{
				const tileTypeInfo copy = item;
				whenSafe([this, copy]() {
					editing = mode::item;
					item = copy;
					item.key = uniqueKey(copy.key + "Copy", false);
					item.name = copy.name + " copy";
					if (copy.blockedMessage == defaultBlocked(copy.name))
						item.blockedMessage.clear();
					item.origin.clear();
					item.removed = false;
					isNew = true;
					savedText.clear();
					checkedText.clear();
					afterSave.clear();
					saveError.clear();
					savedNote.clear();
				});
			}
		}
		ImGui::BeginChild("entries", ImVec2(0.f, 0.f), false);
		std::vector<std::string> tabs = tabsInOrder();
		tabs.push_back("");
		for (const std::string& tab : tabs)
		{
			bool heading = false;
			for (const tileTypeInfo& type : tileCatalogue())
			{
				if (type.category != tab || !matches(type.key, type.name, type.category))
					continue;
				if (!heading)
				{
					ImGui::TextDisabled("%s", tab.empty() ? "not in the menu" : tab.c_str());
					heading = true;
				}
				if (type.removed)
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				const bool selected = editing == mode::item && !isNew && item.key == type.key;
				const std::string labelText = "  " + type.name + (type.name != type.key ? "##" + type.key : "");
				if (ImGui::Selectable(labelText.c_str(), selected) && !selected)
					editItem(type.key);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s\n%s", type.key.c_str(), type.origin.c_str());
				if (type.removed)
					ImGui::PopStyleColor();
			}
		}
		ImGui::EndChild();
		return;
	}
	if (ImGui::Button("New material"))
		newMaterial();
	if (editing == mode::material)
	{
		ImGui::SameLine();
		if (ImGui::Button("Duplicate"))
		{
			const materialInfo copy = material;
			whenSafe([this, copy]() {
				editing = mode::material;
				material = copy;
				material.key = uniqueKey(copy.key + "Copy", true);
				material.name = copy.name + " copy";
				material.origin.clear();
				material.removed = false;
				isNew = true;
				savedText.clear();
				checkedText.clear();
				afterSave.clear();
				saveError.clear();
				savedNote.clear();
			});
		}
	}
	ImGui::BeginChild("entries", ImVec2(0.f, 0.f), false);
	for (const materialInfo& m : materialCatalogue())
	{
		if (!matches(m.key, m.name, phaseName(m.state)))
			continue;
		swatch(m.colour, ImGui::GetTextLineHeight());
		ImGui::SameLine();
		const bool selected = editing == mode::material && !isNew && material.key == m.key;
		if (m.removed)
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		if (ImGui::Selectable((m.name + "##" + m.key).c_str(), selected) && !selected)
			editMaterial(m.key);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s (%s)\n%s", m.key.c_str(), phaseName(m.state), m.origin.c_str());
		if (m.removed)
			ImGui::PopStyleColor();
	}
	ImGui::EndChild();
	(void)_s;
}

void contentEditor::drawItemForm(float _s)
{
	const float wide = 300.f * _s, narrow = 110.f * _s;
	tileTypeInfo& t = item;
	if (t.removed && !isNew)
		ImGui::TextColored(amber, "No longer in the files: saving puts it back.");
	if (ImGui::SmallButton("open all"))
		openSections = 1;
	ImGui::SameLine();
	if (ImGui::SmallButton("close all"))
		openSections = 0;
	//each section; "open all" / "close all" set them all this frame
	auto section = [this](const char* _title, bool _openAtFirst) {
		if (openSections >= 0)
			ImGui::SetNextItemOpen(openSections == 1);
		return ImGui::CollapsingHeader(_title, _openAtFirst ? ImGuiTreeNodeFlags_DefaultOpen : 0);
	};

	if (section("Basics", true))
	{
		label("Key");
		if (isNew)
		{
			editText("##key", t.key, wide);
			hint("(what saves call it)");
		}
		else
		{
			ImGui::TextUnformatted(t.key.c_str());
			hint("(what saves call it: fixed)");
		}
		label("Name");
		const std::string oldName = t.name;
		if (editText("##name", t.name, wide) && t.blockedMessage == defaultBlocked(oldName))
			t.blockedMessage = defaultBlocked(t.name);
		label("Build menu tab");
		ImGui::SetNextItemWidth(narrow * 1.5f);
		if (ImGui::BeginCombo("##tab", t.category.empty() ? "(not in it)" : t.category.c_str()))
		{
			for (const std::string& tab : tabsInOrder())
				if (ImGui::Selectable(tab.c_str(), tab == t.category))
					t.category = tab;
			if (ImGui::Selectable("(not in the menu)", t.category.empty()))
				t.category.clear();
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		editText("##newTab", newTab, narrow, "new tab");
		ImGui::SameLine();
		ImGui::BeginDisabled(newTab.empty());
		if (ImGui::Button("Use"))
		{
			t.category = newTab;
			newTab.clear();
		}
		ImGui::EndDisabled();
		label("Layer");
		int layer = t.layer == tileLayer::floor ? 1 : t.layer == tileLayer::wall ? 2 : t.layer == tileLayer::background ? 3 : 0;
		if (pick("##layer", layer, { "fixture (on a floor or wall)", "floor", "wall", "background" }, wide))
		{
			t.layer = layer == 1 ? tileLayer::floor : layer == 2 ? tileLayer::wall : layer == 3 ? tileLayer::background : tileLayer::fixture;
			t.wallEdge = t.layer == tileLayer::wall ? std::max(t.wallEdge, 0) : -1;
		}
		if (t.layer == tileLayer::wall)
		{
			label("Edge");
			int edge = std::max(t.wallEdge, 0);
			if (pick("##edge", edge, sideList, narrow))
				t.wallEdge = edge;
		}
		label("Art");
		editText("##art", t.art, wide, "tiles/....png");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFrameHeight());
		if (ImGui::BeginCombo("##artPick", "", ImGuiComboFlags_NoPreview | ImGuiComboFlags_HeightLarge))
		{
			for (const std::string& path : artFiles())
			{
				if (const sf::Texture* picture = art(path))
				{
					const sf::Vector2u size = picture->getSize();
					const float h = ImGui::GetTextLineHeight() * 1.6f;
					ImGui::Image(*picture, sf::Vector2f(h * float(size.x) / float(std::max(size.y, 1u)), h));
					ImGui::SameLine();
				}
				if (ImGui::Selectable(path.c_str(), path == t.art))
					t.art = path;
			}
			ImGui::EndCombo();
		}
		if (footprint::isMachine(t))
		{
			label("");
			bool ownArt = t.turnArt.size() == 3;
			if (ImGui::Checkbox("its own art for each quarter turn", &ownArt))
			{
				if (ownArt)
					t.turnArt.assign(3, t.art);
				else
					t.turnArt.clear();
			}
			for (int i = 0; ownArt && i < 3; i++)
			{
				char name[32], id[16];
				snprintf(name, sizeof(name), "  turned %d", i + 1);
				snprintf(id, sizeof(id), "##turn%d", i);
				label(name);
				editText(id, t.turnArt[i], wide);
			}
		}
		label("Can't place it");
		editText("##blocked", t.blockedMessage, wide, defaultBlocked(t.name).c_str());
	}

	if (section("Size, turning and ports", true))
	{
		label("Footprint");
		int size[2] = { t.footprint.x, t.footprint.y };
		ImGui::SetNextItemWidth(narrow);
		if (ImGui::InputInt2("##footprint", size))
		{
			t.footprint = sf::Vector2i(std::clamp(size[0], 1, 8), std::clamp(size[1], 1, 8));
			//ports that are now off it, or face into it, go
			t.ports.erase(std::remove_if(t.ports.begin(), t.ports.end(), [&](const portInfo& _p) {
				const sf::Vector2i across = _p.cell + footprint::step(_p.side);
				const bool inside = across.x >= 0 && across.y >= 0 && across.x < t.footprint.x && across.y < t.footprint.y;
				return _p.cell.x >= t.footprint.x || _p.cell.y >= t.footprint.y || inside;
			}), t.ports.end());
			previewTurn = 0;
		}
		hint("tiles: w along the lower right, h along the lower left");
		label("Mount");
		int mount = t.outside ? 1 : 0;
		if (pick("##mount", mount, { "inside (on a floor or wall)", "outside (bare tile beside the hull, or a roof)" }, wide))
			t.outside = mount == 1;
		label("Turns with R");
		if (footprint::isMultiTile(t) || !t.ports.empty())
			ImGui::TextDisabled("yes (a machine: it has ports, or covers more than one tile)");
		else
			ImGui::Checkbox("##rotates", &t.rotates);
		label("Line piece");
		bool shape = findSetting(t, connections::settingKey) != nullptr;
		if (ImGui::Checkbox("##shape", &shape))
		{
			t.settings.erase(std::remove_if(t.settings.begin(), t.settings.end(), [](const itemSetting& _s) { return _s.key == connections::settingKey; }), t.settings.end());
			if (shape)
			{
				itemSetting piece = settings::number(connections::settingKey, "Connects on", connections::all, 0.0, 15.0, 1.0, "");
				piece.hidden = true;
				t.settings.insert(t.settings.begin(), piece);
			}
		}
		hint("placed as Auto, straight, corner or T, like cables and pipes");
		label("Draw arms");
		int arms = t.drawArms.empty() ? 0 : t.drawArms == "cross" ? 1 : t.drawArms == "cross_upright" ? 2 : 3;
		if (pick("##arms", arms, { "as drawn", "cross (cut to the arms it links)", "cross, keeping an upright", "over another item's arms" }, wide))
			t.drawArms = arms == 1 ? "cross" : arms == 2 ? "cross_upright" : arms == 3 ? "pipe" : "";
		if (arms == 3)
		{
			label("  that item");
			editText("##armsItem", t.drawArms, narrow * 1.5f);
		}
		drawPortsTable(_s);
	}

	if (section("Electrical", false))
	{
		electricalProperties& e = t.electrical;
		label("Carries power");
		int cable = !e.conductor ? 0 : e.vertical ? 2 : 1;
		if (pick("##cable", cable, { "no", "a cable (links to those beside it)", "a riser (and above and below)" }, wide))
		{
			e.conductor = cable > 0;
			e.vertical = cable == 2;
			if (e.conductor && e.resistance <= 0.0)
				e.resistance = 0.01;
		}
		if (e.conductor)
		{
			label("  resistance");
			editNumber("##ohms", e.resistance, narrow, 1e-9);
			hint("ohms a tile");
			label("  inductance");
			editNumber("##henries", e.inductance, narrow, 0.0);
			hint("henries a tile");
			label("  rated current");
			editNumber("##amps", e.ratedCurrent, narrow, 0.0);
			hint("A (0: none)");
		}
		label("Generator");
		if (ImGui::Checkbox("##generator", &e.source) && e.source && e.emf <= 0.0)
		{
			const electricalProperties g = electrical::generator(230.0, 50.0, 5000.0, 0.1, 0.6e-3, 0.75);
			e.emf = g.emf; e.frequency = g.frequency; e.ratedPower = g.ratedPower;
			e.sourceResistance = g.sourceResistance; e.sourceInductance = g.sourceInductance; e.efficiency = g.efficiency;
		}
		if (e.source)
		{
			label("  voltage");
			editNumber("##volts", e.emf, narrow, 1e-9);
			hint("V with nothing connected");
			label("  frequency");
			editNumber("##hz", e.frequency, narrow, 1e-9);
			hint("Hz (generators on one network must match)");
			label("  rating");
			editNumber("##va", e.ratedPower, narrow, 1e-9);
			hint("VA before it overloads");
			label("  internal resistance");
			editNumber("##sohms", e.sourceResistance, narrow, 1e-9);
			hint("ohms");
			label("  internal inductance");
			editNumber("##shenries", e.sourceInductance, narrow, 0.0);
			hint("henries");
			label("  efficiency");
			editNumber("##efficiency", e.efficiency, narrow, 0.0, 1.0);
			hint("0..1, the rest is waste heat (0: none counted)");
		}
		label("Uses power");
		if (ImGui::Checkbox("##load", &e.load) && e.load && e.loadResistance <= 0.0)
			e.loadResistance = electrical::nominalVolts * electrical::nominalVolts / 100.0;
		if (e.load)
		{
			double watts = e.loadResistance > 0.0 ? electrical::nominalVolts * electrical::nominalVolts / e.loadResistance : 0.0;
			label("  power");
			if (editNumber("##watts", watts, narrow, 1e-6))
				e.loadResistance = electrical::nominalVolts * electrical::nominalVolts / watts;
			hint("W at 230 V");
			label("  inductance");
			editNumber("##lhenries", e.loadInductance, narrow, 0.0);
			hint("henries (motors)");
			label("  capacitance");
			editNumber("##farads", e.loadCapacitance, narrow, 0.0);
			hint("farads");
		}
		if (footprint::isMultiTile(t) && (e.load || e.source))
			ImGui::TextDisabled("A machine bigger than one tile takes its power through a power port.");
	}

	if (section("Liquid", false))
	{
		fluidProperties& f = t.fluid;
		int kind = f.tank ? 4 : f.pump ? 3 : f.exchange > 0.0 ? 5 : f.radiatingArea > 0.0 ? 6 : f.carriesLiquid ? (f.vertical ? 2 : 1) : 0;
		label("It is");
		if (pick("##fluidKind", kind, { "nothing to do with liquid", "a pipe", "a pipe riser", "a pump", "a tank", "a heat exchanger", "a radiator (to space)" }, wide))
		{
			switch (kind)
			{
			case 1: f = fluid::pipe(0.5, 2.0); break;
			case 2: f = fluid::riser(0.5, 2.0); break;
			case 3: f = fluid::pump(-1, 100.0, 1.0, 0.5, 2.0); break;
			case 4:
				f = fluid::tank(1000.0, 5.0);
				f.liquid = findMaterial("water") ? "water" : (usableMaterials(true).empty() ? "" : usableMaterials(true).front()->key);
				break;
			case 5: f = fluid::exchanger(300.0, 1.0, 10.0); break;
			case 6: f = fluid::radiator(6.0, 0.9, 1.0, 10.0); break;
			default: f = fluidProperties(); break;
			}
		}
		if (kind == 3)
		{
			label("  pushes towards");
			int side = f.pumpSide + 1;
			if (pick("##pushes", side, { "(from its in port to its out port)", "UL", "UR", "LL", "LR" }, wide))
				f.pumpSide = side - 1;
			label("  pressure");
			editNumber("##kpa", f.pumpHead, narrow, 1e-9);
			hint("kPa with nothing flowing");
			label("  flow");
			editNumber("##flow", f.pumpFlow, narrow, 1e-9);
			hint("L/s against no pressure");
		}
		if (kind == 4)
		{
			label("  holds");
			editNumber("##capacity", f.capacity, narrow, 1e-9);
			hint("L");
			label("  placed full of");
			std::vector<std::string> names;
			int chosen = -1;
			for (const materialInfo* m : usableMaterials(true))
			{
				if (m->key == f.liquid)
					chosen = int(names.size());
				names.push_back(m->name);
			}
			if (pick("##liquid", chosen, names, narrow * 1.5f))
				f.liquid = usableMaterials(true)[chosen]->key;
		}
		if (kind == 5)
		{
			label("  exchange");
			editNumber("##exchange", f.exchange, narrow, 1e-9);
			hint("W/K with its tile's air");
		}
		if (kind == 6)
		{
			label("  area");
			editNumber("##area", f.radiatingArea, narrow, 1e-6);
			hint("m2 radiating (both faces)");
			label("  emissivity");
			editNumber("##emissivity", f.emissivity, narrow, 1e-6, 1.0);
			hint("0..1");
			if (!t.outside)
				ImGui::TextDisabled("A radiator belongs outside: set Mount to outside.");
		}
		if (kind > 0)
		{
			label("  conductance");
			editNumber("##conductance", f.conductance, narrow, 1e-9);
			hint("(L/s)/kPa: how easily liquid flows through");
		}
		if (kind > 0 && kind != 4)
		{
			label("  holds");
			editNumber("##litres", f.volume, narrow, 0.0);
			hint("L of the network's liquid");
		}
		if (kind == 0 && !t.recipes.empty())
			ImGui::TextDisabled("A machine that makes things moves liquid through its liquid ports by itself.");
	}

	if (section("Heat", false))
	{
		label("Heat capacity");
		editNumber("##mass", t.thermal.heatCapacity, narrow, 0.0);
		hint("J/K it adds to its tiles");
		if (t.layer == tileLayer::wall)
		{
			label("Lets through");
			editNumber("##wall", t.thermal.wallConductance, narrow, 0.0);
			hint("W/K (a wall)");
		}
		label("Air duct");
		editNumber("##duct", t.thermal.ductConductance, narrow, 0.0);
		hint("W/K to a duct above or below (0: not one)");
	}

	if (section("Storage and item tubes", false))
	{
		label("Holds solids");
		bool store = t.storage.isStore();
		if (ImGui::Checkbox("##store", &store))
			t.storage = store ? storageProperties{ 1000.0, 0.0 } : storageProperties();
		if (store)
		{
			label("  volume");
			editNumber("##storeLitres", t.storage.litres, narrow, 1e-6);
			hint("L (1000 L = 1 m3)");
			label("  most mass");
			editNumber("##storeKg", t.storage.kg, narrow, 0.0);
			hint("kg (0: only its volume limits it)");
		}
		label("Item tube");
		int tube = !t.items.tube ? 0 : t.items.vertical ? 2 : 1;
		if (pick("##tube", tube, { "no", "a tube", "a tube riser" }, wide))
		{
			t.items.tube = tube > 0;
			t.items.vertical = tube == 2;
			if (t.items.tube && t.items.rate <= 0.0)
				t.items.rate = 2.0;
		}
		if (t.items.tube)
		{
			label("  carries");
			editNumber("##rate", t.items.rate, narrow, 1e-9);
			hint("kg/s");
		}
	}

	if (section("Settings", false))
		drawSettings(_s);
	if (section("Recipes (what it makes)", false))
		drawRecipes(_s);
	openSections = -1;
}

void contentEditor::drawPortsTable(float _s)
{
	tileTypeInfo& t = item;
	ImGui::Spacing();
	ImGui::TextUnformatted("Ports");
	hint("click a face in the preview to add one");
	const std::vector<std::string> kinds = { "power", "liquid", "items" };
	const std::vector<std::string> flows = { "in", "out", "any" };
	for (int i = 0; i < int(t.ports.size()); i++)
	{
		portInfo& port = t.ports[i];
		ImGui::PushID(i);
		if (i == pickedPort)
			ImGui::TextColored(amber, ">");
		else
			ImGui::TextDisabled("%d", i + 1);
		ImGui::SameLine(26.f * _s);
		int cell[2] = { port.cell.x, port.cell.y };
		ImGui::SetNextItemWidth(90.f * _s);
		if (ImGui::InputInt2("##tile", cell))
			port.cell = sf::Vector2i(std::clamp(cell[0], 0, t.footprint.x - 1), std::clamp(cell[1], 0, t.footprint.y - 1));
		ImGui::SameLine();
		int side = port.side;
		if (pick("##side", side, sideList, 62.f * _s))
			port.side = side;
		ImGui::SameLine();
		int kind = int(port.carries);
		if (pick("##carries", kind, kinds, 80.f * _s))
			port.carries = portInfo::kind(kind);
		ImGui::SameLine();
		int flow = int(port.flow);
		if (pick("##flow", flow, flows, 66.f * _s))
			port.flow = portInfo::direction(flow);
		ImGui::SameLine();
		if (ImGui::SmallButton("x"))
		{
			t.ports.erase(t.ports.begin() + i);
			pickedPort = -1;
			ImGui::PopID();
			break;
		}
		ImGui::PopID();
	}
	if (ImGui::SmallButton("+ port"))
		t.ports.push_back(portInfo{ sf::Vector2i(0, 0), 0, newPortKind, portInfo::direction::both });
	if (!t.ports.empty())
		hint("tile [x, y] of the footprint unturned, its face, what it carries, which way");
}

void contentEditor::drawSettings(float _s)
{
	tileTypeInfo& t = item;
	const float n = 70.f * _s;
	int shown = 0;
	for (int i = 0; i < int(t.settings.size()); i++)
	{
		itemSetting& setting = t.settings[i];
		if (reservedSetting(setting))
			continue;
		shown++;
		ImGui::PushID(i);
		editText("##key", setting.key, n, "key");
		ImGui::SameLine();
		editText("##label", setting.label, 170.f * _s, "label");
		ImGui::SameLine();
		bool toggle = setting.type == itemSetting::kind::toggle;
		if (ImGui::Checkbox("on/off", &toggle))
		{
			setting.type = toggle ? itemSetting::kind::toggle : itemSetting::kind::number;
			setting.minimum = 0.0;
			setting.maximum = toggle ? 1.0 : 100.0;
			setting.defaultValue = toggle ? 1.0 : 0.0;
			setting.step = 1.0;
		}
		ImGui::SameLine();
		ImGui::Checkbox("hidden", &setting.hidden);
		ImGui::SameLine();
		if (ImGui::SmallButton("x"))
		{
			t.settings.erase(t.settings.begin() + i);
			ImGui::PopID();
			break;
		}
		if (toggle)
		{
			bool on = setting.defaultValue != 0.0;
			ImGui::Indent();
			if (ImGui::Checkbox("on when placed", &on))
				setting.defaultValue = on ? 1.0 : 0.0;
			ImGui::Unindent();
		}
		else
		{
			ImGui::Indent();
			ImGui::TextDisabled("default");
			ImGui::SameLine();
			editNumber("##default", setting.defaultValue, n);
			ImGui::SameLine();
			ImGui::TextDisabled("from");
			ImGui::SameLine();
			editNumber("##min", setting.minimum, n);
			ImGui::SameLine();
			ImGui::TextDisabled("to");
			ImGui::SameLine();
			editNumber("##max", setting.maximum, n);
			ImGui::SameLine();
			ImGui::TextDisabled("step");
			ImGui::SameLine();
			editNumber("##step", setting.step, n * 0.8f, 1e-9);
			ImGui::SameLine();
			editText("##unit", setting.unit, n * 0.8f, "unit");
			ImGui::Unindent();
		}
		ImGui::PopID();
	}
	if (shown == 0)
		ImGui::TextDisabled("none");
	if (ImGui::SmallButton("+ setting"))
	{
		itemSetting fresh = settings::toggle(findSetting(t, "on") ? "setting" + std::to_string(shown + 1) : "on", "Running", true);
		t.settings.insert(t.settings.begin() + shown, fresh);
	}
	ImGui::TextDisabled("keys the game knows: on (a switch), v (generator volts), t (thermostat C), s (pump speed %%)");
}

void contentEditor::drawRecipes(float _s)
{
	tileTypeInfo& t = item;
	const float narrow = 90.f * _s;
	label("Stops above");
	editNumber("##maxT", t.maxTemperature, narrow, 0.0);
	hint("C, its tiles' air (0: never too hot)");
	const std::vector<const materialInfo*> materials = usableMaterials();
	std::vector<std::string> names;
	for (const materialInfo* m : materials)
		names.push_back(m->name);
	for (int i = 0; i < int(t.recipes.size()); i++)
	{
		recipeInfo& recipe = t.recipes[i];
		ImGui::PushID(i);
		const std::string title = recipe.name + "###recipe";
		if (ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			label("Name");
			editText("##name", recipe.name, 220.f * _s);
			label("Time");
			editNumber("##time", recipe.seconds, narrow, 1e-3);
			hint("s a cycle at full power");
			label("Heat");
			editNumber("##heat", recipe.heat, narrow);
			hint("W while working (negative cools)");
			for (const bool output : { false, true })
			{
				std::vector<std::pair<std::string, double>>& list = output ? recipe.outputs : recipe.inputs;
				label(output ? "Gives" : "Takes");
				if (list.empty())
					ImGui::TextDisabled("nothing");
				for (int j = 0; j < int(list.size()); j++)
				{
					ImGui::PushID(j + (output ? 1000 : 0));
					if (j > 0)
						ImGui::SetCursorPosX(labelColumn);
					int chosen = -1;
					for (int m = 0; m < int(materials.size()); m++)
						if (materials[m]->key == list[j].first)
							chosen = m;
					if (pick("##material", chosen, names, 130.f * _s))
						list[j].first = materials[chosen]->key;
					ImGui::SameLine();
					editNumber("##amount", list[j].second, narrow, 1e-9);
					ImGui::SameLine();
					ImGui::TextDisabled(chosen >= 0 && materials[chosen]->state == materialInfo::phase::liquid ? "L" : "kg");
					ImGui::SameLine();
					if (ImGui::SmallButton("x"))
					{
						list.erase(list.begin() + j);
						ImGui::PopID();
						break;
					}
					ImGui::PopID();
				}
				label("");
				if (ImGui::SmallButton(output ? "+ output" : "+ input") && !materials.empty())
					list.push_back({ materials.front()->key, 1.0 });
			}
			if (ImGui::SmallButton("remove this recipe"))
			{
				t.recipes.erase(t.recipes.begin() + i);
				ImGui::TreePop();
				ImGui::PopID();
				break;
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (ImGui::SmallButton("+ recipe"))
	{
		recipeInfo fresh;
		fresh.name = "Recipe " + std::to_string(t.recipes.size() + 1);
		fresh.seconds = 10.0;
		t.recipes.push_back(fresh);
	}
	if (!t.recipes.empty())
		ImGui::TextDisabled("solids come in and go out through item ports, liquids through liquid ports");
}

void contentEditor::drawMaterialForm(float _s)
{
	const float wide = 260.f * _s, narrow = 110.f * _s;
	materialInfo& m = material;
	if (m.removed && !isNew)
		ImGui::TextColored(amber, "No longer in the files: saving puts it back.");
	label("Key");
	if (isNew)
		editText("##key", m.key, wide);
	else
	{
		ImGui::TextUnformatted(m.key.c_str());
		hint("(what items and saves call it: fixed)");
	}
	label("Name");
	editText("##name", m.name, wide);
	label("Phase");
	int phase = int(m.state);
	if (pick("##phase", phase, { "solid", "liquid", "gas" }, narrow))
		m.state = materialInfo::phase(phase);
	label("Colour");
	float colour[3] = { m.colour.r / 255.f, m.colour.g / 255.f, m.colour.b / 255.f };
	ImGui::SetNextItemWidth(wide);
	if (ImGui::ColorEdit3("##colour", colour))
		m.colour = sf::Color(sf::Uint8(std::lround(colour[0] * 255.f)), sf::Uint8(std::lround(colour[1] * 255.f)), sf::Uint8(std::lround(colour[2] * 255.f)));
	label("Density");
	editNumber("##density", m.density, narrow, 1e-9);
	hint(m.state == materialInfo::phase::solid ? "kg/m3, as it packs (with the gaps)" : "kg/m3");
	label("Heat capacity");
	editNumber("##heatCapacity", m.heatCapacity, narrow, 1e-9);
	hint("J/(kg K)");
}

void contentEditor::drawItemDiagram(float _s)
{
	tileTypeInfo& t = item;
	const bool machine = footprint::isMachine(t);
	if (!machine)
		previewTurn = 0;
	if (machine)
	{
		if (ImGui::SmallButton("<"))
			previewTurn = (previewTurn + 3) % 4;
		ImGui::SameLine();
		if (ImGui::SmallButton(">"))
			previewTurn = (previewTurn + 1) % 4;
		ImGui::SameLine();
		ImGui::Text("turned %d", previewTurn);
		ImGui::SameLine();
	}
	ImGui::TextDisabled("new ports carry");
	for (const portInfo::kind kind : { portInfo::kind::power, portInfo::kind::liquid, portInfo::kind::items })
	{
		ImGui::SameLine();
		if (ImGui::RadioButton(portKindName(kind), newPortKind == kind))
			newPortKind = kind;
	}

	const int turns = previewTurn;
	const sf::Vector2i size = footprint::turnedSize(t, turns);
	//its picture for this turn, mirrored as the game does when it has none of its own
	const bool ownTurnArt = turns > 0 && t.turnArt.size() == 3 && !t.turnArt[turns - 1].empty();
	const sf::Texture* picture = art(ownTurnArt ? t.turnArt[turns - 1] : t.art);
	const bool mirrored = (turns & 1) && t.footprint.x != t.footprint.y && !ownTurnArt;

	//world bounds: the footprint (upper tile's centre at 0,0) and the art over it
	const sf::Vector2f bottom(32.f * float(size.x - size.y), 16.f * float(size.x + size.y) - 16.f);
	float left = -32.f * float(size.y), right = 32.f * float(size.x), top = -16.f, lowest = bottom.y;
	sf::Vector2f artTopLeft, artSize;
	if (picture)
	{
		artSize = sf::Vector2f(picture->getSize());
		artTopLeft = bottom + sf::Vector2f(-32.f * float(size.x), 1.5f - artSize.y);
		left = std::min(left, artTopLeft.x);
		right = std::max(right, artTopLeft.x + artSize.x);
		top = std::min(top, artTopLeft.y);
	}
	const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
	const ImVec2 canvasSize(std::max(ImGui::GetContentRegionAvail().x, 100.f), 300.f * _s);
	ImGui::InvisibleButton("diagram", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool hovered = ImGui::IsItemHovered();
	const bool leftClick = ImGui::IsItemClicked(ImGuiMouseButton_Left), rightClick = ImGui::IsItemClicked(ImGuiMouseButton_Right);
	ImDrawList* list = ImGui::GetWindowDrawList();
	list->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(22, 22, 28, 255));
	const float margin = 24.f * _s;
	const float zoom = std::min({ (canvasSize.x - 2.f * margin) / (right - left), (canvasSize.y - 2.f * margin) / (lowest - top), 3.f * _s });
	const ImVec2 corner(canvasPos.x + (canvasSize.x - (right - left) * zoom) * 0.5f - left * zoom, canvasPos.y + (canvasSize.y - (lowest - top) * zoom) * 0.5f - top * zoom);
	auto screen = [&](sf::Vector2f _world) { return ImVec2(corner.x + _world.x * zoom, corner.y + _world.y * zoom); };
	list->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

	//its floor tiles
	for (int y = 0; y < size.y; y++)
	{
		for (int x = 0; x < size.x; x++)
		{
			const sf::Vector2f c = cellCentre(sf::Vector2i(x, y));
			const ImVec2 a = screen(c + sf::Vector2f(0.f, -16.f)), b = screen(c + sf::Vector2f(32.f, 0.f)), d = screen(c + sf::Vector2f(0.f, 16.f)),
				e = screen(c + sf::Vector2f(-32.f, 0.f));
			list->AddQuadFilled(a, b, d, e, IM_COL32(70, 72, 80, 255));
			list->AddQuad(a, b, d, e, IM_COL32(110, 112, 122, 255), 1.f);
		}
	}
	if (picture)
	{
		const ImVec2 p0 = screen(artTopLeft), p1 = screen(artTopLeft + artSize);
		list->AddImage(textureId(*picture), p0, p1, ImVec2(mirrored ? 1.f : 0.f, 0.f), ImVec2(mirrored ? 0.f : 1.f, 1.f));
	}
	//its outline, over the art
	{
		const ImVec2 a = screen(sf::Vector2f(0.f, -16.f)), b = screen(cellCentre(sf::Vector2i(size.x - 1, 0)) + sf::Vector2f(32.f, 0.f)),
			d = screen(bottom), e = screen(cellCentre(sf::Vector2i(0, size.y - 1)) + sf::Vector2f(-32.f, 0.f));
		list->AddQuad(a, b, d, e, IM_COL32(0, 230, 255, 200), 2.f);
	}
	for (const portInfo& port : footprint::turnedPorts(t, turns))
	{
		const sf::Vector2f out = outOf(port.side);
		machineView::drawPortMarker(list, screen(cellCentre(port.cell) + sideMiddle(port.side)), ImVec2(out.x, out.y), 11.f * zoom, port.carries, port.flow);
	}

	//its outside faces: hover, click to add a port (or turn an existing one's way round), right click to remove one
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	int hoveredPort = -1;
	bool found = false;
	portInfo face;
	for (int y = 0; y < size.y && hovered; y++)
	{
		for (int x = 0; x < size.x; x++)
		{
			for (int side = 0; side < 4; side++)
			{
				const sf::Vector2i across = sf::Vector2i(x, y) + footprint::step(side);
				if (across.x >= 0 && across.y >= 0 && across.x < size.x && across.y < size.y)
					continue;
				const ImVec2 p = screen(cellCentre(sf::Vector2i(x, y)) + sideMiddle(side));
				if (std::hypot(mouse.x - p.x, mouse.y - p.y) > std::max(10.f * zoom, 8.f))
					continue;
				//back to the unturned footprint, where ports are kept
				const int back = (4 - turns) % 4;
				face.cell = footprint::turnCell(sf::Vector2i(x, y), size, back);
				face.side = footprint::turnSide(side, back);
				found = true;
				list->AddCircle(p, std::max(9.f * zoom, 7.f), IM_COL32(255, 255, 255, 220), 0, 2.f);
			}
		}
	}
	if (found)
	{
		for (int i = 0; i < int(t.ports.size()); i++)
			if (t.ports[i].cell == face.cell && t.ports[i].side == face.side)
				hoveredPort = i;
		if (leftClick)
		{
			if (hoveredPort < 0)
			{
				face.carries = newPortKind;
				face.flow = portInfo::direction::both;
				t.ports.push_back(face);
				pickedPort = int(t.ports.size()) - 1;
			}
			else
			{
				//both -> in -> out -> both
				portInfo& port = t.ports[hoveredPort];
				port.flow = port.flow == portInfo::direction::both ? portInfo::direction::in : port.flow == portInfo::direction::in ? portInfo::direction::out :
					portInfo::direction::both;
				pickedPort = hoveredPort;
			}
		}
		if (rightClick && hoveredPort >= 0)
		{
			t.ports.erase(t.ports.begin() + hoveredPort);
			pickedPort = -1;
		}
		ImGui::SetTooltip(hoveredPort >= 0 ? "click: change which way it goes (Any, In, Out in turn); right click: remove it" : "click: add a %s port here", portKindName(newPortKind));
	}
	list->PopClipRect();
	if (!picture)
		ImGui::TextColored(amber, "no picture: %s", t.art.empty() ? "give it art" : ("can't load " + t.art).c_str());
	else
		ImGui::TextDisabled("art %u x %u%s (a %d x %d footprint fits art %d wide)", picture->getSize().x, picture->getSize().y, mirrored ? ", mirrored" : "",
			t.footprint.x, t.footprint.y, 32 * (t.footprint.x + t.footprint.y));
}

void contentEditor::drawPreview(float _s)
{
	if (editing == mode::none)
	{
		ImGui::TextDisabled("The preview, the problems and the TOML it writes show here.");
		return;
	}
	if (editing == mode::item)
	{
		drawItemDiagram(_s);
		const tileTypeInfo& t = item;
		ImGui::PushTextWrapPos(0.f);
		const std::string lines[6] = { electrical::summary(t.electrical), fluid::summary(t.fluid), thermal::summary(t.thermal), storage::summary(t.storage),
			items::summary(t.items), recipes::summary(t) };
		const ImVec4 colours[6] = { ImVec4(1.f, 0.85f, 0.3f, 1.f), ImVec4(0.5f, 0.8f, 1.f, 1.f), ImVec4(0.75f, 0.85f, 0.95f, 1.f), ImVec4(0.9f, 0.75f, 0.55f, 1.f),
			ImVec4(0.95f, 0.7f, 0.45f, 1.f), ImVec4(0.95f, 0.65f, 0.4f, 1.f) };
		for (int i = 0; i < 6; i++)
			if (!lines[i].empty())
				ImGui::TextColored(colours[i], "%s", lines[i].c_str());
		if (t.thermal.heatCapacity > 0.0)
			ImGui::TextDisabled("adds %.1f kJ/K to its tiles' heat capacity", t.thermal.heatCapacity / 1000.0);
		ImGui::PopTextWrapPos();
	}
	else
	{
		const materialInfo& m = material;
		swatch(m.colour, ImGui::GetTextLineHeight() * 3.f);
		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::Text("%s, a %s", m.name.c_str(), phaseName(m.state));
		ImGui::TextDisabled("%.3g kg in a litre", m.kilogramsPerLitre());
		ImGui::TextDisabled("%.4g J/K to warm a litre by a degree", m.heatPerLitre());
		ImGui::EndGroup();
	}

	ImGui::Separator();
	int errors = 0;
	for (const content::message& m : problems)
		errors += m.error;
	if (problems.empty())
		ImGui::TextColored(green, "No problems: it can be saved.");
	ImGui::PushTextWrapPos(0.f);
	for (const content::message& m : problems)
		ImGui::TextColored(m.error ? red : amber, "%s", m.text.c_str());
	if (!afterSave.empty())
	{
		ImGui::TextDisabled("when it was last saved and reloaded:");
		for (const content::message& m : afterSave)
			ImGui::TextColored(m.error ? red : amber, "%s", m.text.c_str());
	}
	ImGui::PopTextWrapPos();
	(void)errors;

	if (ImGui::CollapsingHeader("The TOML it writes", ImGuiTreeNodeFlags_DefaultOpen))
	{
		std::vector<char> buffer(text.begin(), text.end());
		buffer.push_back('\0');
		const int lineCount = 1 + int(std::count(text.begin(), text.end(), '\n'));
		ImGui::InputTextMultiline("##toml", buffer.data(), buffer.size(),
			ImVec2(-1.f, std::min(float(lineCount) + 1.5f, 24.f) * ImGui::GetTextLineHeight()), ImGuiInputTextFlags_ReadOnly);
	}
}

void contentEditor::drawFooter(station& _station, textureManager& _textures, contentPanel& _panel, float _s)
{
	if (editing == mode::none)
		return;
	int errors = 0;
	for (const content::message& m : problems)
		errors += m.error;
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("File");
	ImGui::SameLine();
	if (isNew)
	{
		//any file the content is read from, or a new one
		std::vector<std::string> files = content::lastReport().files;
		const std::string custom = fs::path(folder).filename().generic_string() + "/custom.toml";
		if (std::find(files.begin(), files.end(), custom) == files.end())
			files.push_back(custom);
		if (std::find(files.begin(), files.end(), file) == files.end())
			files.push_back(file);
		ImGui::SetNextItemWidth(220.f * _s);
		if (ImGui::BeginCombo("##file", file.c_str()))
		{
			for (const std::string& f : files)
				if (ImGui::Selectable(f.c_str(), f == file))
					file = f;
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		editText("##newFile", newFile, 130.f * _s, "new file");
		ImGui::SameLine();
		ImGui::BeginDisabled(newFile.empty());
		if (ImGui::Button("Use"))
		{
			std::string name = newFile;
			if (name.size() < 5 || name.substr(name.size() - 5) != ".toml")
				name += ".toml";
			file = fs::path(folder).filename().generic_string() + "/" + name;
			newFile.clear();
		}
		ImGui::EndDisabled();
	}
	else
		ImGui::TextDisabled("%s", file.c_str());
	ImGui::SameLine(0.f, 24.f * _s);
	const bool changed = hasUnsavedChanges();
	ImGui::BeginDisabled(errors > 0 || !changed);
	if (ImGui::Button(isNew ? "Save (adds it)" : "Save"))
		save(_station, _textures, _panel);
	ImGui::EndDisabled();
	if (!isNew)
	{
		ImGui::SameLine();
		ImGui::BeginDisabled(!changed);
		if (ImGui::Button("Revert"))
		{
			if (editing == mode::item)
				loadItem(item.key);
			else
				loadMaterial(material.key);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Delete..."))
			askDelete = true;
	}
	ImGui::SameLine();
	if (errors > 0)
		ImGui::TextColored(red, "%s to fix before it can be saved", content::count(errors, "problem").c_str());
	else if (changed)
		ImGui::TextColored(amber, "unsaved changes");
	else if (!savedNote.empty())
		ImGui::TextColored(green, "%s", savedNote.c_str());
	if (!saveError.empty())
		ImGui::TextColored(red, "%s", saveError.c_str());
}

void contentEditor::save(station& _station, textureManager& _textures, contentPanel& _panel)
{
	const bool isItem = editing == mode::item;
	const std::string section = isItem ? "item" : "material";
	const std::string key = isItem ? item.key : material.key;
	std::string error;
	if (!contentWriter::writeEntry(diskPath(file), section, key, text, error))
	{
		saveError = error;
		return;
	}
	saveError.clear();
	const content::report& report = content::reload(folder, _station, _textures);
	_panel.notice(content::summaryLine(report), report.errors() > 0);
	afterSave.clear();
	const std::string tag = "[" + section + "." + key + "]";
	for (const content::message& m : report.messages)
		if (m.where.find(tag) != std::string::npos)
			afterSave.push_back(m);
	isNew = false;
	if (isItem)
		item.removed = false;
	else
		material.removed = false;
	savedText = text;
	savedNote = "saved to " + file + " and reloaded";
}

void contentEditor::remove(station& _station, textureManager& _textures, contentPanel& _panel)
{
	const std::string section = editing == mode::item ? "item" : "material";
	const std::string key = editing == mode::item ? item.key : material.key;
	std::string error;
	if (!contentWriter::removeEntry(diskPath(file), section, key, error))
	{
		saveError = error;
		return;
	}
	saveError.clear();
	const content::report& report = content::reload(folder, _station, _textures);
	_panel.notice(content::summaryLine(report), report.errors() > 0);
	editing = mode::none;
	text.clear();
	savedText.clear();
	problems.clear();
}
