#pragma once
#include <string>
#include <vector>

class station;
class textureManager;
struct tileTypeInfo;
struct materialInfo;

//the content files: every item, material and item family the game knows, read from TOML files in a folder
//(StationBuilder/content). content/README.md describes everything a file can hold. nothing about the items is
//written in code any more: a new item is a new [item.<key>] table in a file
namespace content
{
	//one problem found in the files
	struct message
	{
		bool error = true;      //false: a warning (an unknown field, say); the item still loads
		std::string where;      //"content/power.toml:14 [item.generator60]"
		std::string text;       //"electrical.generator.hz: must be a number above 0"
	};
	struct report
	{
		std::vector<std::string> files;     //every file read, in the order read
		std::vector<message> messages;
		int items = 0;                       //items, materials and families in the files
		int materials = 0;
		int families = 0;
		int errors() const;
		int warnings() const;
	};

	//reads every .toml file in _folder and its subfolders (in path order) into the catalogue.
	//the first call fills it; a later call (a reload) updates the definitions in place, so everything already
	//placed follows them. an item with errors keeps its previous definition (a new one is left out); an item no
	//longer in the files stays for what is placed but leaves the menu and can't be placed again. an item's layer
	//and wall edge can't change while the game runs (that needs a restart)
	const report& load(const std::string& _folder);
	//what the last load found (plus any art that wouldn't load, see loadArt)
	const report& lastReport();
	//adds a problem to the last report (the art loader uses it)
	void addMessage(const message& _message);
	//"1 item", "3 items" (_plural if it isn't just _one + "s")
	std::string count(int _n, const std::string& _one, const std::string& _plural = "");
	//one line for the notice after a reload: "Content reloaded: 22 items, 1 material, no problems"
	std::string summaryLine(const report& _report);

	//(the in-game editor) reads one item's TOML text (its [item.<key>] table and recipes) the way load() reads an item,
	//without changing the catalogue: _into gets what was read, and the result is every problem found (_where as their
	//place). a material the same way
	std::vector<message> readItemText(const std::string& _text, const std::string& _key, const std::string& _where, tileTypeInfo& _into);
	std::vector<message> readMaterialText(const std::string& _text, const std::string& _key, const std::string& _where, materialInfo& _into);

	//(contentArt.cpp) registers every item's art under its key, and the line-art versions its draw_arms asks for.
	//art that won't load goes into the last report as an error
	void loadArt(textureManager& _textures);
	//(contentArt.cpp) F5: reads the files again, reloads the art, and gives everything placed in the station its new
	//definition (settings kept by key) and sprite. every level is marked changed, so the simulations rebuild
	const report& reload(const std::string& _folder, station& _station, textureManager& _textures);
	//the same for several stations (the station and every ship): the files are read once, then everything placed in
	//each of them takes its new definition
	const report& reload(const std::string& _folder, const std::vector<station*>& _stations, textureManager& _textures);
}
