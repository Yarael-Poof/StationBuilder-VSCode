#include "blueprintTool.h"
#include "isometricLevel.h"
#include "lineArt.h"
#include "machineView.h"
#include "station.h"
#include "textureManager.h"
#include "tileCatalogue.h"
#include <imgui.h>
#include "uiTheme.h"
#include <imgui-SFML.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace
{
	const ImVec4 red = uiTheme::danger;
	const ImVec4 amber = uiTheme::warning;
	const ImVec4 green = uiTheme::good;
	const float messageSeconds = 5.f;

	ImTextureID textureId(const sf::Texture& _texture)
	{
		ImTextureID id{};
		const unsigned int handle = _texture.getNativeHandle();
		std::memcpy(&id, &handle, sizeof(handle));
		return id;
	}

	int settingIn(const std::string& _settings, const std::string& _key, int _fallback)
	{
		std::istringstream entries(_settings);
		std::string entry;
		while (std::getline(entries, entry, ';'))
			if (entry.rfind(_key + "=", 0) == 0)
				return std::atoi(entry.c_str() + _key.size() + 1);
		return _fallback;
	}

	//one object drawn: its texture, where its picture's top left goes (in world units, from the sprite corner of the
	//blueprint's tile (0, 0)), and whether it is mirrored
	struct drawn
	{
		const sf::Texture* texture = nullptr;
		sf::Vector2f corner;
		bool mirrored = false;
		int object = 0;
		float depth = 0.f;
	};
	std::vector<drawn> layout(const blueprint& _blueprint, textureManager& _textures)
	{
		std::vector<drawn> list;
		//a tile's sprite corner, from tile (0, 0)'s: half a tile across and a quarter down per x, the mirror per y
		auto root = [](sf::Vector2i _at) { return sf::Vector2f(32.f * float(_at.x - _at.y), 16.f * float(_at.x + _at.y)); };
		for (int i = 0; i < int(_blueprint.objects.size()); i++)
		{
			const blueprintObject& object = _blueprint.objects[i];
			const std::string art = blueprintTool::artFor(object, _textures);
			if (!_textures.hasTexture(art))
				continue;
			drawn d;
			d.texture = _textures.getTexture(art);
			d.object = i;
			d.corner = root(object.at);
			sf::Vector2i front = object.at;
			const tileTypeInfo* type = findTileType(object.key);
			if (type && footprint::isMachine(*type))
			{
				//as tileObject::fitSprite: the art's lower corner on its front tile's, mirrored on a quarter turn when it isn't
				//square and has no art of its own for the turn
				const int t = ((settingIn(object.settings, footprint::rotationKey, 0) % 4) + 4) % 4;
				const sf::Vector2i size = footprint::turnedSize(*type, t);
				front = object.at + size - sf::Vector2i(1, 1);
				d.corner = root(front) + sf::Vector2f(32.f - 32.f * float(size.x), 64.f - float(d.texture->getSize().y));
				d.mirrored = (t & 1) && type->footprint.x != type->footprint.y && !(t > 0 && _textures.hasTexture(object.key + "@" + std::to_string(t)));
			}
			//floors under everything, then walls and fixtures from the back to the front
			const int layer = type && type->layer == tileLayer::floor ? 0 : 1;
			d.depth = float(layer) * 10000.f + float(front.x + front.y) + float(i) * 1e-4f;
			list.push_back(d);
		}
		std::stable_sort(list.begin(), list.end(), [](const drawn& _a, const drawn& _b) { return _a.depth < _b.depth; });
		return list;
	}
}

blueprintTool::blueprintTool(const std::string& _folder)
	: folder(_folder)
{
}

std::string blueprintTool::artFor(const blueprintObject& _object, textureManager& _textures)
{
	const tileTypeInfo* type = findTileType(_object.key);
	if (!type)
		return _object.key;
	//a line piece: its shape's art
	if (findSetting(*type, connections::settingKey))
	{
		const std::string variant = lineArt::variantKey(_object.key, settingIn(_object.settings, connections::settingKey, connections::all));
		return _textures.hasTexture(variant) ? variant : _object.key;
	}
	//a machine: its art for its turn (if it has its own), on the stubs it connects on, turned (as the build menu's ghost)
	if (footprint::isMachine(*type))
	{
		const int t = ((settingIn(_object.settings, footprint::rotationKey, 0) % 4) + 4) % 4;
		const std::string turned = _object.key + "@" + std::to_string(t);
		const std::string art = t > 0 && _textures.hasTexture(turned) ? turned : _object.key;
		int sides = lineArt::ownSides(*type);
		for (int i = 0; i < t; i++)
			sides = connections::rotate(sides);
		const std::string variant = lineArt::variantKey(art, sides);
		return _textures.hasTexture(variant) ? variant : art;
	}
	return _object.key;
}

void blueprintTool::say(const std::string& _text, bool _problem)
{
	message = _text;
	messageProblem = _problem;
	messageTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
}

void blueprintTool::reloadLibrary()
{
	libraryProblems.clear();
	library = blueprints::loadAll(folder, libraryProblems);
	libraryRead = true;
}

void blueprintTool::startSelecting()
{
	state = mode::selecting;
	dragging = false;
	selected = false;
}

void blueprintTool::startPlacing(const blueprint& _blueprint)
{
	if (_blueprint.empty())
		return;
	placing = _blueprint;
	turns = 0;
	state = mode::placing;
	dragging = false;
	selected = false;
}

void blueprintTool::stop()
{
	state = mode::none;
	dragging = false;
	selected = false;
}

bool blueprintTool::copySelection(station& _station)
{
	if (!selected || selectionLevel < 0 || selectionLevel >= _station.getLayerListSize())
		return false;
	int leftOut = 0;
	blueprint copied = blueprints::copy(*_station.getIsoLevel(float(selectionLevel)), dragFrom, dragTo, &leftOut);
	if (copied.empty())
	{
		say(leftOut > 0 ? "nothing copied: the machines there reach outside the selection" : "nothing there to copy", true);
		return false;
	}
	copied.name = "Copy";
	clipboard = copied;
	startPlacing(clipboard);
	std::string text = "copied " + std::to_string(copied.objects.size()) + " things (" + std::to_string(copied.size.x) + " x " +
		std::to_string(copied.size.y) + " tiles)";
	if (leftOut > 0)
		text += "; " + std::to_string(leftOut) + (leftOut == 1 ? " machine reaches" : " machines reach") + " outside the selection, left out";
	say(text, leftOut > 0);
	return true;
}

bool blueprintTool::pasteCopy()
{
	if (clipboard.empty())
	{
		say("nothing copied yet: C, drag over some tiles, Ctrl+C", true);
		return false;
	}
	startPlacing(clipboard);
	return true;
}

void blueprintTool::turn()
{
	if (state != mode::placing)
		return;
	placing = placing.turned();
	turns = (turns + 1) % 4;
}

bool blueprintTool::escape()
{
	if (state == mode::selecting && (selected || dragging))
	{
		selected = false;
		dragging = false;
		return true;
	}
	if (state != mode::none)
	{
		stop();
		return true;
	}
	return false;
}

void blueprintTool::update(station& _station, sf::Vector2i _hovered, bool _inLevel, bool _leftPressed, bool _leftDown, bool _guiWantsMouse)
{
	if (state == mode::selecting)
	{
		if (_leftPressed && !_guiWantsMouse && _inLevel)
		{
			dragging = true;
			selected = false;
			dragFrom = dragTo = _hovered;
			selectionLevel = _station.getCurrentLevel();
		}
		if (dragging)
		{
			if (_inLevel)
				dragTo = _hovered;
			if (!_leftDown)
			{
				dragging = false;
				selected = true;
			}
		}
	}
	else if (state == mode::placing && _leftPressed && !_guiWantsMouse && _inLevel)
	{
		const blueprints::pasteResult result = blueprints::paste(*_station.getCurrentIsoLevel(), placing, placing.originFor(_hovered));
		std::string text = "placed " + std::to_string(result.placed);
		if (result.already > 0)
			text += ", " + std::to_string(result.already) + " already there";
		if (result.refused > 0)
			text += ", " + std::to_string(result.refused) + " didn't fit (first: " + result.firstRefused + ")";
		say(text, result.refused > 0);
	}
}

void blueprintTool::highlight(station& _station, sf::Vector2i _hovered, bool _inLevel)
{
	if (state != mode::selecting)
		return;
	if ((selected || dragging) && selectionLevel == _station.getCurrentLevel())
	{
		const sf::Vector2i lo(std::min(dragFrom.x, dragTo.x), std::min(dragFrom.y, dragTo.y)), hi(std::max(dragFrom.x, dragTo.x), std::max(dragFrom.y, dragTo.y));
		for (int y = lo.y; y <= hi.y; y++)
			for (int x = lo.x; x <= hi.x; x++)
				_station.highlightTile(sf::Vector2f(float(x), float(y)), sf::Color(120, 220, 255));
	}
	else if (_inLevel)
		_station.highlightTile(sf::Vector2f(_hovered), sf::Color(120, 220, 255));
}

void blueprintTool::drawGhost(sf::RenderWindow& _window, station& _station, textureManager& _textures, sf::Vector2i _hovered, bool _inLevel)
{
	if (state != mode::placing || !_inLevel)
		return;
	isometricLevel* level = _station.getCurrentIsoLevel();
	const sf::Vector2i origin = placing.originFor(_hovered);
	const std::vector<blueprints::objectCheck> checks = blueprints::check(*level, placing, origin);
	//everything relative to the pointed tile's sprite, as the build menu's ghost is
	const sf::Vector2f anchor = level->tileWorldPosition(sf::Vector2f(_hovered)) +
		(level->tileCentreWorld(sf::Vector2f(origin)) - level->tileCentreWorld(sf::Vector2f(_hovered)));
	bool allFit = true;
	for (const blueprints::objectCheck& c : checks)
		allFit = allFit && c.fits;
	for (const drawn& d : layout(placing, _textures))
	{
		sf::Sprite ghost(*d.texture);
		ghost.setPosition(anchor + d.corner);
		if (d.mirrored)
		{
			ghost.setOrigin(float(d.texture->getSize().x), 0.f);
			ghost.setScale(-1.f, 1.f);
		}
		const blueprints::objectCheck& c = checks[d.object];
		//see-through where it goes, fainter where the same is there already, a dark silhouette where it won't go
		ghost.setColor(c.already ? sf::Color(255, 255, 255, 80) : c.fits ? sf::Color(255, 255, 255, 170) : sf::Color(25, 25, 25, 200));
		_window.draw(ghost);
	}
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	machineView::drawFootprint(drawList, _window, *level, origin, placing.size, allFit ? IM_COL32(120, 220, 255, 220) : IM_COL32(230, 60, 60, 230), 2.f);
	//its machines' ports, so it can be turned to line up with what is already there
	for (const blueprintObject& object : placing.objects)
	{
		const tileTypeInfo* type = findTileType(object.key);
		if (type && !type->ports.empty())
			machineView::drawPorts(drawList, _window, *level, *type, ((settingIn(object.settings, footprint::rotationKey, 0) % 4) + 4) % 4, origin + object.at,
				7, 220);
	}
}

void blueprintTool::drawPreview(ImDrawList* _drawList, textureManager& _textures, const blueprint& _blueprint, const ImVec2& _corner, const ImVec2& _size)
{
	const std::vector<drawn> list = layout(_blueprint, _textures);
	if (list.empty())
		return;
	sf::Vector2f least(FLT_MAX, FLT_MAX), most(-FLT_MAX, -FLT_MAX);
	for (const drawn& d : list)
	{
		const sf::Vector2f size(d.texture->getSize());
		least = sf::Vector2f(std::min(least.x, d.corner.x), std::min(least.y, d.corner.y));
		most = sf::Vector2f(std::max(most.x, d.corner.x + size.x), std::max(most.y, d.corner.y + size.y));
	}
	const float scale = std::min({ _size.x / std::max(most.x - least.x, 1.f), _size.y / std::max(most.y - least.y, 1.f), 1.5f });
	const ImVec2 offset(_corner.x + (_size.x - (most.x - least.x) * scale) * 0.5f - least.x * scale, _corner.y + (_size.y - (most.y - least.y) * scale) * 0.5f - least.y * scale);
	for (const drawn& d : list)
	{
		const sf::Vector2f size(d.texture->getSize());
		const ImVec2 p0(offset.x + d.corner.x * scale, offset.y + d.corner.y * scale), p1(p0.x + size.x * scale, p0.y + size.y * scale);
		_drawList->AddImage(textureId(*d.texture), p0, p1, ImVec2(d.mirrored ? 1.f : 0.f, 0.f), ImVec2(d.mirrored ? 0.f : 1.f, 1.f));
	}
}

void blueprintTool::drawPanels(station& _station, textureManager& _textures, float _scale, ImFont* _font)
{
	const float s = _scale;
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	if (_font)
		ImGui::PushFont(_font);
	const std::pair<ImGuiCol, ImVec4> colours[] = { { ImGuiCol_WindowBg, uiTheme::panel() } };
	for (const auto& [which, colour] : colours)
		ImGui::PushStyleColor(which, colour);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f * s, 8.f * s));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f * s, 5.f * s));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f * s, 3.f * s));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);

	//the Blueprints menu
	float menuRight = 0.f;
	if (panelOpen)
	{
		if (!libraryRead)
			reloadLibrary();
		ImGui::SetNextWindowPos(ImVec2(170.f, 60.f * s), ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(560.f * s, std::min(720.f * s, display.y - 80.f * s)), ImGuiCond_Appearing);
		if (ImGui::Begin("Blueprints (B closes)", &panelOpen, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse))
		{
			ImGui::TextDisabled("C: copy tool (drag over tiles). Ctrl+C copies, Ctrl+V places the copy again.");
			ImGui::TextDisabled("While placing: click to place, R turns it, Esc stops.");
			const float picture = 110.f * s;
			//one row: its picture, name, size and description, and buttons
			auto row = [&](const blueprint& _b, int _id, bool _deletable) {
				ImGui::PushID(_id);
				const ImVec2 corner = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddRectFilled(corner, ImVec2(corner.x + picture * 1.4f, corner.y + picture), IM_COL32(18, 18, 22, 255));
				drawPreview(ImGui::GetWindowDrawList(), _textures, _b, ImVec2(corner.x + 4.f, corner.y + 4.f), ImVec2(picture * 1.4f - 8.f, picture - 8.f));
				ImGui::Dummy(ImVec2(picture * 1.4f, picture));
				ImGui::SameLine();
				ImGui::BeginGroup();
				ImGui::TextUnformatted(_b.name.c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("%d x %d tiles, %d things", _b.size.x, _b.size.y, int(_b.objects.size()));
				if (!_b.description.empty())
				{
					ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x);
					ImGui::TextDisabled("%s", _b.description.c_str());
					ImGui::PopTextWrapPos();
				}
				if (ImGui::Button("Place"))
					startPlacing(_b);
				if (_deletable)
				{
					ImGui::SameLine();
					if (confirmDelete == _b.file)
					{
						if (ImGui::Button("Really delete it?"))
						{
							std::error_code problem;
							std::filesystem::remove(_b.file, problem);
							say(problem ? "couldn't delete " + _b.file : "deleted " + _b.file, bool(problem));
							confirmDelete.clear();
							libraryRead = false;
						}
						ImGui::SameLine();
						if (ImGui::Button("Keep it"))
							confirmDelete.clear();
					}
					else if (ImGui::Button("Delete"))
						confirmDelete = _b.file;
				}
				ImGui::EndGroup();
				ImGui::PopID();
				ImGui::Spacing();
			};

			if (!clipboard.empty() && ImGui::CollapsingHeader("Your copy", ImGuiTreeNodeFlags_DefaultOpen))
			{
				row(clipboard, -1, false);
				char name[128], description[256];
				snprintf(name, sizeof(name), "%s", saveName.c_str());
				snprintf(description, sizeof(description), "%s", saveDescription.c_str());
				ImGui::SetNextItemWidth(200.f * s);
				if (ImGui::InputTextWithHint("##name", "its name", name, sizeof(name)))
					saveName = name;
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1.f);
				if (ImGui::InputTextWithHint("##description", "what it is for (optional)", description, sizeof(description)))
					saveDescription = description;
				const std::string path = folder + "/" + blueprints::fileName(saveName);
				const bool exists = !saveName.empty() && std::filesystem::exists(path);
				ImGui::BeginDisabled(saveName.empty());
				if (ImGui::Button(exists ? "Save as a blueprint (replaces the one with that name)" : "Save as a blueprint"))
				{
					blueprint b = clipboard;
					b.name = saveName;
					b.description = saveDescription;
					std::string error;
					if (blueprints::save(b, path, error))
					{
						say("saved " + b.file, false);
						saveName.clear();
						saveDescription.clear();
						libraryRead = false;
					}
					else
						say(error, true);
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::TextDisabled("as %s", path.c_str());
			}
			if (!libraryRead)
				reloadLibrary();
			int id = 0;
			const bool anyExample = std::any_of(library.begin(), library.end(), [](const blueprint& _b) { return _b.example; });
			if (anyExample && ImGui::CollapsingHeader("Examples: starting production chains", ImGuiTreeNodeFlags_DefaultOpen))
				for (const blueprint& b : library)
					if (b.example)
						row(b, id++, false);
			if (ImGui::CollapsingHeader("Yours", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool any = false;
				for (const blueprint& b : library)
				{
					if (b.example)
						continue;
					any = true;
					row(b, 1000 + id++, true);
				}
				if (!any)
					ImGui::TextDisabled("none yet: copy some tiles (C), then save the copy here");
			}
			for (const std::string& problem : libraryProblems)
				ImGui::TextColored(amber, "%s", problem.c_str());
			if (ImGui::Button("Read the folder again"))
				libraryRead = false;
			ImGui::SameLine();
			ImGui::TextDisabled("%s/ (a .toml file each; copy them to share)", folder.c_str());
		}
		menuRight = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;
		ImGui::End();
	}
	//the tool's note along the top: what it is doing, and what it did last
	const bool recent = ImGui::GetTime() - messageTime < messageSeconds && !message.empty();
	if (state != mode::none || recent)
	{
		//along the top, beside the menu when it is open (so the menu doesn't hide it)
		if (menuRight > 0.f)
			ImGui::SetNextWindowPos(ImVec2(menuRight + 12.f * s, 60.f * s), ImGuiCond_Always, ImVec2(0.f, 0.f));
		else
			ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, 60.f * s), ImGuiCond_Always, ImVec2(0.5f, 0.f));
		ImGui::Begin("blueprintNote", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
		if (state == mode::selecting)
		{
			if (!selected)
				ImGui::TextUnformatted(dragging ? "Copy: let go to select these tiles" : "Copy: drag over the tiles to copy (Esc stops)");
			else
			{
				const sf::Vector2i size(std::abs(dragTo.x - dragFrom.x) + 1, std::abs(dragTo.y - dragFrom.y) + 1);
				ImGui::Text("Selected %d x %d tiles", size.x, size.y);
				ImGui::SameLine();
				if (ImGui::Button("Copy (Ctrl+C)"))
					copySelection(_station);
				ImGui::SameLine();
				if (ImGui::Button("Copy and save as a blueprint..."))
				{
					if (copySelection(_station))
					{
						panelOpen = true;
						if (!libraryRead)
							reloadLibrary();
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Clear"))
					selected = false;
				ImGui::TextDisabled("drag again to select other tiles; Esc clears it");
			}
		}
		else if (state == mode::placing)
		{
			ImGui::Text("Placing %s (%d x %d)", placing.name.c_str(), placing.size.x, placing.size.y);
			ImGui::SameLine();
			ImGui::TextDisabled("click to place, R turns it (turned %d), Esc stops", turns);
		}
		if (recent)
			ImGui::TextColored(messageProblem ? amber : green, "%s", message.c_str());
		ImGui::End();
	}

	ImGui::PopStyleVar(5);
	ImGui::PopStyleColor(int(sizeof(colours) / sizeof(colours[0])));
	if (_font)
		ImGui::PopFont();
}
