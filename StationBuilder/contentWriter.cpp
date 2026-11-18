#include "contentWriter.h"
#include "tileCatalogue.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace
{
	const char* sideName(int _side)
	{
		static const char* names[4] = { "UL", "UR", "LL", "LR" };
		return names[_side & 3];
	}

	const char* layerName(tileLayer _layer)
	{
		switch (_layer)
		{
		case tileLayer::floor: return "floor";
		case tileLayer::wall: return "wall";
		case tileLayer::background: return "background";
		default: return "fixture";
		}
	}

	std::string lowerCase(std::string _text)
	{
		for (char& c : _text)
			c = char(std::tolower(static_cast<unsigned char>(c)));
		return _text;
	}

	//"a = 1, b = 2" from name/value pairs, leaving out the empty ones
	std::string fields(const std::vector<std::pair<std::string, std::string>>& _pairs)
	{
		std::string text;
		for (const auto& [name, value] : _pairs)
		{
			if (value.empty())
				continue;
			text += (text.empty() ? "" : ", ") + name + " = " + value;
		}
		return text;
	}
	std::string inlineTable(const std::vector<std::pair<std::string, std::string>>& _pairs)
	{
		return "{ " + fields(_pairs) + " }";
	}
	//a number, or "" when it is 0 (fields that are left out at 0)
	std::string unlessZero(double _value)
	{
		return _value != 0.0 ? contentWriter::number(_value) : "";
	}

	//settings the loader adds by itself: a line piece's shape, a machine's turn, a maker's recipe
	bool reservedSetting(const itemSetting& _setting)
	{
		return _setting.hidden && (_setting.key == connections::settingKey || _setting.key == footprint::rotationKey || _setting.key == recipes::settingKey);
	}

	std::string amounts(const std::vector<std::pair<std::string, double>>& _list)
	{
		std::vector<std::pair<std::string, std::string>> pairs;
		for (const auto& [key, amount] : _list)
			pairs.push_back({ key, contentWriter::number(amount) });
		return inlineTable(pairs);
	}

	//a file's lines, without their endings; whether it used \r\n; whether its last line ended with one
	struct fileText
	{
		std::vector<std::string> lines;
		bool crlf = false;
		bool exists = false;
	};
	fileText readLines(const std::string& _file)
	{
		fileText result;
		std::ifstream in(_file, std::ios::binary);
		if (!in)
			return result;
		result.exists = true;
		std::stringstream buffer;
		buffer << in.rdbuf();
		const std::string raw = buffer.str();
		result.crlf = raw.find("\r\n") != std::string::npos;
		std::string line;
		for (char c : raw)
		{
			if (c == '\n')
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				result.lines.push_back(line);
				line.clear();
			}
			else
				line += c;
		}
		if (!line.empty())
			result.lines.push_back(line);
		return result;
	}

	//the files already backed up this run: only their text from before the first change is kept
	std::set<std::string>& backedUp()
	{
		static std::set<std::string> files;
		return files;
	}

	bool writeLines(const std::string& _file, const fileText& _text, std::string& _error)
	{
		namespace fs = std::filesystem;
		std::error_code problem;
		const fs::path path(_file);
		if (path.has_parent_path())
			fs::create_directories(path.parent_path(), problem);
		if (_text.exists && !backedUp().count(fs::absolute(path, problem).string()))
		{
			fs::copy_file(path, fs::path(_file + ".bak"), fs::copy_options::overwrite_existing, problem);
			if (problem)
			{
				_error = "couldn't keep a copy of " + _file + " (" + problem.message() + "): nothing written";
				return false;
			}
			backedUp().insert(fs::absolute(path, problem).string());
		}
		std::ofstream out(_file, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			_error = "couldn't write " + _file;
			return false;
		}
		const char* ending = _text.crlf ? "\r\n" : "\n";
		for (const std::string& line : _text.lines)
			out << line << ending;
		if (!out)
		{
			_error = "couldn't finish writing " + _file;
			return false;
		}
		return true;
	}

	std::string trimmed(const std::string& _line)
	{
		const size_t first = _line.find_first_not_of(" \t");
		if (first == std::string::npos)
			return "";
		const size_t last = _line.find_last_not_of(" \t");
		return _line.substr(first, last - first + 1);
	}

	//a table header's name ("item.smelter.recipe" for "[[item.smelter.recipe]]  # ..."), "" if the line isn't one
	std::string headerName(const std::string& _line, bool* _array = nullptr)
	{
		std::string line = trimmed(_line);
		if (line.empty() || line[0] != '[')
			return "";
		const bool array = line.size() > 1 && line[1] == '[';
		const size_t close = line.find(']');
		if (close == std::string::npos)
			return "";
		std::string name;
		for (size_t i = array ? 2 : 1; i < close; i++)
			if (line[i] != ' ' && line[i] != '\t')
				name += line[i];
		if (_array)
			*_array = array;
		return name;
	}

	std::vector<std::string> splitLines(const std::string& _text)
	{
		std::vector<std::string> lines;
		std::string line;
		for (char c : _text)
		{
			if (c == '\n')
			{
				lines.push_back(line);
				line.clear();
			}
			else if (c != '\r')
				line += c;
		}
		if (!line.empty())
			lines.push_back(line);
		return lines;
	}
}

namespace contentWriter
{
	std::string number(double _value)
	{
		if (!std::isfinite(_value))
			return "0";
		char text[40];
		//whole numbers as they are (no 6e+01)
		if (_value == std::floor(_value) && std::abs(_value) < 1e15)
		{
			snprintf(text, sizeof(text), "%.0f", _value);
			return text;
		}
		//otherwise the fewest digits that read back as the same double
		for (int digits = 1; digits <= 17; digits++)
		{
			snprintf(text, sizeof(text), "%.*g", digits, _value);
			if (std::strtod(text, nullptr) == _value)
				break;
		}
		return text;
	}

	std::string quoted(const std::string& _text)
	{
		std::string text = "\"";
		for (char c : _text)
		{
			switch (c)
			{
			case '"': text += "\\\""; break;
			case '\\': text += "\\\\"; break;
			case '\n': text += "\\n"; break;
			case '\t': text += "\\t"; break;
			case '\r': text += "\\r"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20)
				{
					char escaped[8];
					snprintf(escaped, sizeof(escaped), "\\u%04x", c);
					text += escaped;
				}
				else
					text += c;
			}
		}
		return text + "\"";
	}

	std::string itemText(const tileTypeInfo& _t)
	{
		std::vector<std::string> lines;
		auto line = [&](const std::string& _name, const std::string& _value) {
			if (!_value.empty())
				lines.push_back(_name + " = " + _value);
		};
		lines.push_back("[item." + _t.key + "]");
		line("name", quoted(_t.name));
		if (!_t.category.empty())
			line("tab", quoted(_t.category));
		if (_t.layer != tileLayer::fixture)
			line("layer", quoted(layerName(_t.layer)));
		if (_t.layer == tileLayer::wall && _t.wallEdge >= 0)
			line("edge", quoted(sideName(_t.wallEdge)));
		if (_t.outside)
			line("mount", quoted("outside"));
		if (_t.turnArt.size() == 3)
			line("art", "[" + quoted(_t.art) + ", " + quoted(_t.turnArt[0]) + ", " + quoted(_t.turnArt[1]) + ", " + quoted(_t.turnArt[2]) + "]");
		else
			line("art", quoted(_t.art));
		if (!_t.blockedMessage.empty() && _t.blockedMessage != "cannot place " + lowerCase(_t.name) + " here")
			line("blocked", quoted(_t.blockedMessage));
		if (findSetting(_t, connections::settingKey))
			line("shape", "true");
		if (!_t.drawArms.empty())
			line("draw_arms", quoted(_t.drawArms));
		if (_t.footprint != sf::Vector2i(1, 1))
			line("footprint", "[" + std::to_string(_t.footprint.x) + ", " + std::to_string(_t.footprint.y) + "]");
		if (_t.rotates)
			line("rotates", "true");
		if (_t.maxTemperature > 0.0)
			line("max_temperature", number(_t.maxTemperature));

		//what it does
		const electricalProperties& e = _t.electrical;
		if (e.conductor)
			line(e.vertical ? "electrical.riser" : "electrical.conductor", inlineTable({ { "ohms", number(e.resistance) }, { "henries", unlessZero(e.inductance) },
				{ "amps", unlessZero(e.ratedCurrent) } }));
		if (e.source)
			line("electrical.generator", inlineTable({ { "volts", number(e.emf) }, { "hz", number(e.frequency) }, { "va", number(e.ratedPower) },
				{ "ohms", number(e.sourceResistance) }, { "henries", unlessZero(e.sourceInductance) }, { "efficiency", unlessZero(e.efficiency) } }));
		if (e.load)
		{
			//written as its watts at the station's voltage, the way the files give it (what it reads back to is the same ohms)
			const double volts = electrical::nominalVolts;
			char watts[40];
			snprintf(watts, sizeof(watts), "%.6g", e.loadResistance > 0.0 ? volts * volts / e.loadResistance : 0.0);
			line("electrical.load", inlineTable({ { "watts", watts }, { "henries", unlessZero(e.loadInductance) }, { "farads", unlessZero(e.loadCapacitance) } }));
		}
		const fluidProperties& f = _t.fluid;
		if (f.tank)
			line("fluid.tank", inlineTable({ { "litres", number(f.capacity) }, { "conductance", number(f.conductance) },
				{ "liquid", f.liquid.empty() ? "" : quoted(f.liquid) } }));
		else if (f.pump)
			line("fluid.pump", inlineTable({ { "pushes", f.pumpSide >= 0 ? quoted(sideName(f.pumpSide)) : "" }, { "kpa", number(f.pumpHead) },
				{ "flow", number(f.pumpFlow) }, { "conductance", number(f.conductance) }, { "litres", unlessZero(f.volume) } }));
		else if (f.exchange > 0.0)
			line("fluid.exchanger", inlineTable({ { "watts_per_kelvin", number(f.exchange) }, { "conductance", number(f.conductance) },
				{ "litres", unlessZero(f.volume) } }));
		else if (f.radiatingArea > 0.0)
			line("fluid.radiator", inlineTable({ { "area_m2", number(f.radiatingArea) }, { "emissivity", number(f.emissivity) },
				{ "conductance", number(f.conductance) }, { "litres", unlessZero(f.volume) } }));
		else if (f.carriesLiquid)
			line(f.vertical ? "fluid.riser" : "fluid.pipe", inlineTable({ { "conductance", number(f.conductance) }, { "litres", unlessZero(f.volume) } }));
		const thermalProperties& h = _t.thermal;
		if (h.heatCapacity != 0.0 || h.wallConductance != 0.0 || h.ductConductance != 0.0)
			line("thermal", inlineTable({ { "mass", unlessZero(h.heatCapacity) }, { "wall", unlessZero(h.wallConductance) }, { "duct", unlessZero(h.ductConductance) } }));
		if (_t.storage.isStore())
			line("storage", inlineTable({ { "litres", number(_t.storage.litres) }, { "kg", unlessZero(_t.storage.kg) } }));
		if (_t.items.tube)
			line(_t.items.vertical ? "items.riser" : "items.tube", inlineTable({ { "kg_per_s", number(_t.items.rate) } }));

		if (!_t.ports.empty())
		{
			lines.push_back("ports = [");
			for (const portInfo& port : _t.ports)
				lines.push_back("    " + inlineTable({ { "tile", "[" + std::to_string(port.cell.x) + ", " + std::to_string(port.cell.y) + "]" },
					{ "side", quoted(sideName(port.side)) }, { "carries", quoted(portKindName(port.carries)) },
					{ "flow", port.flow == portInfo::direction::both ? "" : quoted(portFlowName(port.flow)) } }) + ",");
			lines.push_back("]");
		}
		std::vector<std::string> settingLines;
		for (const itemSetting& s : _t.settings)
		{
			if (reservedSetting(s))
				continue;
			if (s.type == itemSetting::kind::toggle)
				settingLines.push_back("    " + inlineTable({ { "key", quoted(s.key) }, { "label", quoted(s.label) }, { "toggle", "true" },
					{ "default", s.defaultValue != 0.0 ? "true" : "false" }, { "hidden", s.hidden ? "true" : "" } }) + ",");
			else
				settingLines.push_back("    " + inlineTable({ { "key", quoted(s.key) }, { "label", quoted(s.label) }, { "default", number(s.defaultValue) },
					{ "min", number(s.minimum) }, { "max", number(s.maximum) }, { "step", s.step != 1.0 ? number(s.step) : "" },
					{ "unit", s.unit.empty() ? "" : quoted(s.unit) }, { "hidden", s.hidden ? "true" : "" } }) + ",");
		}
		if (!settingLines.empty())
		{
			lines.push_back("settings = [");
			lines.insert(lines.end(), settingLines.begin(), settingLines.end());
			lines.push_back("]");
		}

		//what it makes: a table each, after its own
		for (const recipeInfo& recipe : _t.recipes)
		{
			lines.push_back("");
			lines.push_back("[[item." + _t.key + ".recipe]]");
			line("name", quoted(recipe.name));
			line("time", number(recipe.seconds));
			if (!recipe.inputs.empty())
				line("in", amounts(recipe.inputs));
			if (!recipe.outputs.empty())
				line("out", amounts(recipe.outputs));
			if (recipe.heat != 0.0)
				line("heat", number(recipe.heat));
		}

		std::string text;
		for (size_t i = 0; i < lines.size(); i++)
			text += lines[i] + (i + 1 < lines.size() ? "\n" : "");
		return text;
	}

	std::string materialText(const materialInfo& _m)
	{
		char colour[16];
		snprintf(colour, sizeof(colour), "#%02x%02x%02x", _m.colour.r, _m.colour.g, _m.colour.b);
		return "[material." + _m.key + "]\n"
			"name = " + quoted(_m.name) + "\n"
			"phase = " + quoted(phaseName(_m.state)) + "\n"
			"colour = " + quoted(colour) + "\n"
			"density = " + number(_m.density) + "\n"
			"heat_capacity = " + number(_m.heatCapacity);
	}

	bool findEntry(const std::vector<std::string>& _lines, const std::string& _section, const std::string& _key, int& _first, int& _end)
	{
		const std::string name = _section + "." + _key;
		_first = -1;
		for (int i = 0; i < int(_lines.size()); i++)
		{
			bool array = false;
			if (headerName(_lines[i], &array) == name && !array)
			{
				_first = i;
				break;
			}
		}
		if (_first < 0)
			return false;
		//up to the next table that isn't one of its own sub-tables
		_end = int(_lines.size());
		for (int i = _first + 1; i < int(_lines.size()); i++)
		{
			const std::string header = headerName(_lines[i]);
			if (!header.empty() && header.rfind(name + ".", 0) != 0)
			{
				_end = i;
				break;
			}
		}
		//the comments and blank lines just before the next table describe that one: they stay
		while (_end - 1 > _first)
		{
			const std::string line = trimmed(_lines[_end - 1]);
			if (!line.empty() && line[0] != '#')
				break;
			_end--;
		}
		return true;
	}

	bool writeEntry(const std::string& _file, const std::string& _section, const std::string& _key, const std::string& _text, std::string& _error)
	{
		fileText file = readLines(_file);
		if (!file.exists)
			file.crlf = false;
		const std::vector<std::string> entry = splitLines(_text);
		int first = 0, end = 0;
		if (findEntry(file.lines, _section, _key, first, end))
		{
			file.lines.erase(file.lines.begin() + first, file.lines.begin() + end);
			file.lines.insert(file.lines.begin() + first, entry.begin(), entry.end());
			//a blank line between it and whatever follows
			const int after = first + int(entry.size());
			if (after < int(file.lines.size()) && !trimmed(file.lines[after]).empty())
				file.lines.insert(file.lines.begin() + after, "");
		}
		else
		{
			if (!file.lines.empty() && !trimmed(file.lines.back()).empty())
				file.lines.push_back("");
			file.lines.insert(file.lines.end(), entry.begin(), entry.end());
		}
		return writeLines(_file, file, _error);
	}

	bool removeEntry(const std::string& _file, const std::string& _section, const std::string& _key, std::string& _error)
	{
		fileText file = readLines(_file);
		int first = 0, end = 0;
		if (!file.exists || !findEntry(file.lines, _section, _key, first, end))
		{
			_error = "[" + _section + "." + _key + "] isn't in " + _file;
			return false;
		}
		//the comment just above it (no blank line between) is about it, so it goes; so do the blank lines after it, so
		//no gap is left
		while (first > 0 && trimmed(file.lines[first - 1]).rfind("#", 0) == 0)
			first--;
		while (end < int(file.lines.size()) && trimmed(file.lines[end]).empty())
			end++;
		file.lines.erase(file.lines.begin() + first, file.lines.begin() + end);
		return writeLines(_file, file, _error);
	}
}
