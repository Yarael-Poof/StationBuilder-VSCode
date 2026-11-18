#pragma once
#include <string>
#include <vector>

struct tileTypeInfo;
struct materialInfo;

//writes items and materials back out as content-file TOML (the in-game editor's side of content.h): the text the loader
//reads back to the same definition, and the edits to a file that put it there without disturbing the rest of the file
namespace contentWriter
{
	//the TOML for one item: its [item.<key>] table with every field that isn't at its default, in the order
	//content/README.md lists them, then a [[item.<key>.recipe]] table per recipe. no blank line at the end
	std::string itemText(const tileTypeInfo& _type);
	//the TOML for one material: its [material.<key>] table
	std::string materialText(const materialInfo& _material);
	//a number as short as it reads back exactly: 230, 0.5, 0.00015, 1e+20
	std::string number(double _value);
	//text in TOML quotes, with \ and " (and control characters) escaped
	std::string quoted(const std::string& _text);

	//where the table for [<_section>.<_key>] is among a file's lines: its header line (_first) to just past its last
	//line (_end), including its sub-tables ([[item.x.recipe]]). comments and blank lines straight before the next table
	//are left out (they describe that one). false if the file has no such table
	bool findEntry(const std::vector<std::string>& _lines, const std::string& _section, const std::string& _key, int& _first, int& _end);
	//puts _text in _file as the table for [<_section>.<_key>]: in place of the one there, or added at the end (a new
	//file is made if needed). the rest of the file is left exactly as it was, line endings included. the first time a
	//file is changed while the game runs, its old text is kept beside it as <file>.bak. false (and _error says why) if
	//it couldn't be written
	bool writeEntry(const std::string& _file, const std::string& _section, const std::string& _key, const std::string& _text, std::string& _error);
	//takes the table for [<_section>.<_key>] out of _file (backed up the same way). false if it isn't there
	bool removeEntry(const std::string& _file, const std::string& _section, const std::string& _key, std::string& _error);
}
