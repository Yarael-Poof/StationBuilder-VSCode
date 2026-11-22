#pragma once
#include <SFML/Graphics/Texture.hpp>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "content.h"
#include "tileCatalogue.h"

class station;
class textureManager;
class contentPanel;
struct ImFont;

//F7: the in-game editor for the content files. it edits one item or material at a time in a form (every field the files
//can give, see content/README.md), with a preview (the art fitted to its footprint, its ports, which can be added by
//clicking the footprint's faces, its summaries), the problems the loader would find, and the TOML it will write. Save
//writes that table into its file (in place of the old one; the rest of the file is left alone, and the file's text from
//before the first change is kept as <file>.bak), then reloads the content as F5 does
class contentEditor
{
public:
	explicit contentEditor(const std::string& _contentFolder);
	//every station the content goes into when it is saved and reloaded (the station and the ships); unset: just the
	//one draw is given
	std::function<std::vector<station*>()> allStations;
	void toggle() { visible = !visible; }
	void open() { visible = true; }
	bool isOpen() const { return visible; }
	//opens on an existing item or material, or a new one (asking first if there are unsaved changes)
	void editItem(const std::string& _key);
	void editMaterial(const std::string& _key);
	void newItem();
	void newMaterial();
	bool hasUnsavedChanges() const;
	//opens (or closes) every section of the form on the next frame, as its "open all" / "close all" buttons do
	void setSectionsOpen(bool _open) { openSections = _open ? 1 : 0; }
	//call every frame between ImGui::SFML::Update and Render. a save reloads the content into _station and _textures,
	//and shows the reload's notice on _panel
	void draw(station& _station, textureManager& _textures, contentPanel& _panel, float _scale, ImFont* _font);

private:
	enum class mode { none, item, material };

	//switches to another item or material now, or after asking when there are unsaved changes
	void whenSafe(const std::function<void()>& _action);
	void loadItem(const std::string& _key);
	void loadMaterial(const std::string& _key);
	//the file a new entry goes in, or an existing one is in ("content/power.toml"), and where that is on disk
	std::string fileOf(const std::string& _origin) const;
	std::string diskPath(const std::string& _file) const;
	std::string uniqueKey(const std::string& _base, bool _material) const;

	void drawList(float _s);
	void drawItemForm(float _s);
	void drawMaterialForm(float _s);
	void drawPortsTable(float _s);
	void drawShipParts(float _s);
	void drawLighting(float _s);
	void drawSettings(float _s);
	void drawRecipes(float _s);
	void drawPreview(float _s);
	void drawItemDiagram(float _s);
	void drawFooter(station& _station, textureManager& _textures, contentPanel& _panel, float _s);
	void save(station& _station, textureManager& _textures, contentPanel& _panel);
	void remove(station& _station, textureManager& _textures, contentPanel& _panel);
	//the live check: the loader's problems with the text as it is now, and the editor's own (a key already used...)
	void check();

	//a picture for the preview, loaded from its file (again when the file changes); nullptr if it won't load
	const sf::Texture* art(const std::string& _path);
	const std::vector<std::string>& artFiles();

	std::string folder;
	bool visible = false;
	mode editing = mode::none;
	tileTypeInfo item;
	materialInfo material;
	bool isNew = false;
	std::string mapMessage;      //what the last "write the normal map" did
	std::string file;             //"content/power.toml"
	std::string text;             //the TOML for it as it is now
	std::string savedText;        //and as it was when opened or last saved
	std::string checkedText;      //the text the problems are for
	std::vector<content::message> problems;
	std::vector<content::message> afterSave;   //what the reload said about it after the last save
	std::string saveError;
	std::string savedNote;        //"saved to content/power.toml" for a moment

	//the list
	bool listMaterials = false;
	std::string filter;
	//the preview: its turn, what a port added by clicking carries, the port picked
	int previewTurn = 0;
	int openSections = -1;        //1 or 0: open or close every section of the form this frame; -1 leave them
	portInfo::kind newPortKind = portInfo::kind::liquid;
	int pickedPort = -1;
	//a new tab's name, a new file's name, being typed
	std::string newTab;
	std::string newFile;
	//waiting for "discard changes?"
	std::function<void()> pending;
	bool askDiscard = false;
	bool askDelete = false;

	struct artEntry
	{
		std::unique_ptr<sf::Texture> texture;
		std::filesystem::file_time_type time;
		bool failed = false;
	};
	std::map<std::string, artEntry> artCache;
	std::vector<std::string> artList;
	double artListTime = -100.0;
};
