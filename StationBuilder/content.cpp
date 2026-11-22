#include "content.h"
#include "tileCatalogue.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <tuple>

namespace
{
	content::report current;
	const double unlimited = std::numeric_limits<double>::infinity();

	int countErrors(const std::vector<content::message>& _messages)
	{
		return int(std::count_if(_messages.begin(), _messages.end(), [](const content::message& _m) { return _m.error; }));
	}

	//keys end up in save files between ',' '{' ';' '=' and texture keys use '#': letters, digits and _ only
	bool validKey(const std::string& _key, bool _lowerCaseOnly = false)
	{
		if (_key.empty() || !std::isalpha(static_cast<unsigned char>(_key[0])))
			return false;
		for (char c : _key)
		{
			const unsigned char u = static_cast<unsigned char>(c);
			if (!(std::isalnum(u) || c == '_') || (_lowerCaseOnly && std::isupper(u)))
				return false;
		}
		return true;
	}

	//"UL" -> 0 ... "LR" -> 3 (the wall slot / side numbering), -1 if it isn't one
	int sideFromName(const std::string& _name)
	{
		static const char* names[4] = { "UL", "UR", "LL", "LR" };
		for (int s = 0; s < 4; s++)
		{
			if (_name == names[s])
				return s;
		}
		return -1;
	}

	std::string lowerCase(std::string _text)
	{
		for (char& c : _text)
			c = char(std::tolower(static_cast<unsigned char>(c)));
		return _text;
	}

	//reads the fields of one TOML table, with a message for every field that is missing, the wrong type or out of
	//range. it remembers which fields were asked for, so finish() can warn about the ones nobody knows (typos)
	class fieldReader
	{
	public:
		fieldReader(const toml::table& _table, const std::string& _where, const std::string& _path, std::vector<content::message>& _out)
			: table(_table), where(_where), path(_path), out(_out)
		{
		}

		void error(const std::string& _text) { out.push_back({ true, where, _text }); }
		void warn(const std::string& _text) { out.push_back({ false, where, _text }); }
		void fieldError(const std::string& _field, const std::string& _text) { error(path + _field + ": " + _text); }

		bool has(const std::string& _field)
		{
			asked.insert(_field);
			return table.contains(_field);
		}

		//a number: _fallback when it's left out (an error instead if _required). it has to lie in _min.._max, and be
		//above _min rather than equal to it if _aboveMin
		double number(const std::string& _field, double _fallback, bool _required = false, double _min = -unlimited, double _max = unlimited,
			bool _aboveMin = false)
		{
			const toml::node* node = get(_field);
			if (!node)
			{
				if (_required)
					fieldError(_field, "missing (a number)");
				return _fallback;
			}
			if (!node->is_number())
			{
				fieldError(_field, "must be a number");
				return _fallback;
			}
			const double value = node->value<double>().value_or(_fallback);
			if (!std::isfinite(value) || value < _min || value > _max || (_aboveMin && value <= _min))
			{
				std::string range;
				if (_aboveMin && _max == unlimited)
					range = "above " + shortNumber(_min);
				else if (_max == unlimited)
					range = shortNumber(_min) + " or more";
				else
					range = (_aboveMin ? "above " : "from ") + shortNumber(_min) + " to " + shortNumber(_max);
				fieldError(_field, "must be a number " + range + " (it is " + shortNumber(value) + ")");
				return _fallback;
			}
			return value;
		}

		std::string text(const std::string& _field, const std::string& _fallback, bool _required = false)
		{
			const toml::node* node = get(_field);
			if (!node)
			{
				if (_required)
					fieldError(_field, "missing (text in quotes)");
				return _fallback;
			}
			if (!node->is_string())
			{
				fieldError(_field, "must be text in quotes");
				return _fallback;
			}
			return node->value<std::string>().value_or(_fallback);
		}

		bool flag(const std::string& _field, bool _fallback)
		{
			const toml::node* node = get(_field);
			if (!node)
				return _fallback;
			if (!node->is_boolean())
			{
				fieldError(_field, "must be true or false");
				return _fallback;
			}
			return node->value<bool>().value_or(_fallback);
		}

		//a table inside this one, or nullptr if it isn't there (an error if it is there but isn't a table)
		const toml::table* subTable(const std::string& _field)
		{
			const toml::node* node = get(_field);
			if (!node)
				return nullptr;
			if (!node->is_table())
			{
				fieldError(_field, "must be a table, e.g. " + _field + " = { ... }");
				return nullptr;
			}
			return node->as_table();
		}

		const toml::array* list(const std::string& _field)
		{
			const toml::node* node = get(_field);
			if (!node)
				return nullptr;
			if (!node->is_array())
			{
				fieldError(_field, "must be a list, e.g. " + _field + " = [ ... ]");
				return nullptr;
			}
			return node->as_array();
		}

		//a list of item keys or names
		std::vector<std::string> textList(const std::string& _field, bool _required)
		{
			std::vector<std::string> values;
			const toml::array* items = list(_field);
			if (!items)
			{
				if (_required && !table.contains(_field))
					fieldError(_field, "missing (a list of text in quotes)");
				return values;
			}
			for (const toml::node& element : *items)
			{
				if (!element.is_string())
				{
					fieldError(_field, "every entry must be text in quotes");
					return {};
				}
				values.push_back(element.value<std::string>().value_or(""));
			}
			return values;
		}

		//warns about every field that wasn't asked for: a typo, or something this version doesn't know
		void finish()
		{
			for (auto&& [key, value] : table)
			{
				const std::string field(key.str());
				if (!asked.count(field))
					warn(path + field + ": not a field this game knows (a typo?), ignored");
			}
		}

		const std::string& location() const { return where; }
		const std::string& prefix() const { return path; }
		std::vector<content::message>& messages() { return out; }

		static std::string shortNumber(double _value)
		{
			char text[32];
			snprintf(text, sizeof(text), "%g", _value);
			return text;
		}

	private:
		const toml::node* get(const std::string& _field)
		{
			asked.insert(_field);
			return table.get(_field);
		}

		const toml::table& table;
		std::string where;
		std::string path;
		std::vector<content::message>& out;
		std::set<std::string> asked;
	};

	//electrical = { conductor | riser = {...}, generator = {...}, load = {...} }
	void readElectrical(fieldReader& _item, const toml::table& _table, electricalProperties& _p)
	{
		fieldReader r(_table, _item.location(), "electrical.", _item.messages());
		const toml::table* conductor = r.subTable("conductor");
		const toml::table* riser = r.subTable("riser");
		if (conductor && riser)
			r.error("electrical: an item is a conductor or a riser, not both");
		for (const bool vertical : { false, true })
		{
			const toml::table* t = vertical ? riser : conductor;
			if (!t || (conductor && riser && vertical))
				continue;
			fieldReader q(*t, r.location(), vertical ? "electrical.riser." : "electrical.conductor.", r.messages());
			_p.conductor = true;
			_p.vertical = vertical;
			_p.resistance = q.number("ohms", 0.01, true, 0.0, unlimited, true);
			_p.inductance = q.number("henries", 0.0, false, 0.0);
			_p.ratedCurrent = q.number("amps", 0.0, false, 0.0);
			q.finish();
		}
		if (const toml::table* t = r.subTable("generator"))
		{
			fieldReader q(*t, r.location(), "electrical.generator.", r.messages());
			_p.source = true;
			_p.emf = q.number("volts", electrical::nominalVolts, true, 0.0, unlimited, true);
			_p.frequency = q.number("hz", electrical::nominalHertz, true, 0.0, unlimited, true);
			_p.ratedPower = q.number("va", 1000.0, true, 0.0, unlimited, true);
			_p.sourceResistance = q.number("ohms", 0.1, true, 0.0, unlimited, true);
			_p.sourceInductance = q.number("henries", 0.0, false, 0.0);
			_p.efficiency = q.number("efficiency", 0.0, false, 0.0, 1.0);
			q.finish();
		}
		if (const toml::table* t = r.subTable("load"))
		{
			fieldReader q(*t, r.location(), "electrical.load.", r.messages());
			_p.load = true;
			if (q.has("watts"))
			{
				if (q.has("ohms"))
					q.error("electrical.load: give watts or ohms, not both");
				const double watts = q.number("watts", 100.0, true, 0.0, unlimited, true);
				const double volts = q.number("volts", electrical::nominalVolts, false, 0.0, unlimited, true);
				_p.loadResistance = volts * volts / watts;   //P = V^2 / R
			}
			else
				_p.loadResistance = q.number("ohms", 100.0, true, 0.0, unlimited, true);
			_p.loadInductance = q.number("henries", 0.0, false, 0.0);
			_p.loadCapacitance = q.number("farads", 0.0, false, 0.0);
			q.finish();
		}
		if (!conductor && !riser && !_p.source && !_p.load)
			r.error("electrical: give one of conductor, riser, generator or load");
		r.finish();
	}

	//fluid = { pipe | riser | pump | tank | exchanger = {...} }
	void readFluid(fieldReader& _item, const toml::table& _table, fluidProperties& _f)
	{
		fieldReader r(_table, _item.location(), "fluid.", _item.messages());
		const char* kinds[6] = { "pipe", "riser", "pump", "tank", "exchanger", "radiator" };
		int found = 0;
		for (const char* kind : kinds)
		{
			const toml::table* t = r.subTable(kind);
			if (!t)
				continue;
			if (++found > 1)
			{
				r.error(std::string("fluid: an item is one of pipe, riser, pump, tank, exchanger or radiator (") + kind + " is a second)");
				continue;
			}
			const std::string name(kind);
			fieldReader q(*t, r.location(), "fluid." + name + ".", r.messages());
			if (name == "pipe" || name == "riser")
			{
				const double g = q.number("conductance", 0.5, true, 0.0, unlimited, true);
				const double litres = q.number("litres", 0.0, false, 0.0);
				_f = name == "pipe" ? fluid::pipe(g, litres) : fluid::riser(g, litres);
			}
			else if (name == "pump")
			{
				//which way it pushes: from its liquid "in" port to its "out" port (see readMachine), or for a pump
				//without ports, pushes = the side it pushes towards
				int side = -1;
				if (q.has("pushes"))
				{
					side = sideFromName(q.text("pushes", ""));
					if (side < 0)
						q.fieldError("pushes", "must be \"UL\", \"UR\", \"LL\" or \"LR\" (the side it pushes towards)");
				}
				const double head = q.number("kpa", 100.0, true, 0.0, unlimited, true);
				const double flow = q.number("flow", 1.0, true, 0.0, unlimited, true);
				const double g = q.number("conductance", 0.5, true, 0.0, unlimited, true);
				const double litres = q.number("litres", 0.0, false, 0.0);
				_f = fluid::pump(side, head, flow, g, litres);
			}
			else if (name == "tank")
			{
				const double capacity = q.number("litres", 100.0, true, 0.0, unlimited, true);
				const double g = q.number("conductance", 1.0, true, 0.0, unlimited, true);
				_f = fluid::tank(capacity, g);
				_f.liquid = q.text("liquid", "");   //checked against the materials once they are all read
			}
			else if (name == "exchanger")
			{
				const double wk = q.number("watts_per_kelvin", 100.0, true, 0.0, unlimited, true);
				const double g = q.number("conductance", 0.5, true, 0.0, unlimited, true);
				const double litres = q.number("litres", 0.0, false, 0.0);
				_f = fluid::exchanger(wk, g, litres);
			}
			else
			{
				//a radiator: sheds the liquid's heat to space, e sigma A (T^4 - Ts^4)
				const double area = q.number("area_m2", 4.0, true, 0.0, unlimited, true);
				const double emissivity = q.number("emissivity", 0.9, false, 0.0, 1.0, true);
				const double g = q.number("conductance", 0.5, true, 0.0, unlimited, true);
				const double litres = q.number("litres", 0.0, false, 0.0);
				_f = fluid::radiator(area, emissivity, g, litres);
			}
			q.finish();
		}
		if (found == 0)
			r.error("fluid: give one of pipe, riser, pump, tank, exchanger or radiator");
		r.finish();
	}

	//thermal = { mass = J/K, wall = W/K, duct = W/K }
	void readThermal(fieldReader& _item, const toml::table& _table, thermalProperties& _t)
	{
		fieldReader r(_table, _item.location(), "thermal.", _item.messages());
		_t.heatCapacity = r.number("mass", 0.0, false, 0.0);
		_t.wallConductance = r.has("wall") ? r.number("wall", 0.5, false, 0.0, unlimited, true) : 0.0;
		_t.ductConductance = r.has("duct") ? r.number("duct", 100.0, false, 0.0, unlimited, true) : 0.0;
		r.finish();
	}

	//settings = [ { key, label, toggle = true, default } or { key, label, default, min, max, step, unit } ]
	void readSettings(fieldReader& _item, const toml::array& _list, std::vector<itemSetting>& _settings)
	{
		int index = 0;
		for (const toml::node& element : _list)
		{
			const std::string path = "settings[" + std::to_string(index++) + "].";
			if (!element.is_table())
			{
				_item.error(path + ": every setting is a table, e.g. { key = \"on\", label = \"Running\", toggle = true, default = true }");
				continue;
			}
			fieldReader q(*element.as_table(), _item.location(), path, _item.messages());
			itemSetting s;
			s.key = q.text("key", "", true);
			if (!s.key.empty() && !validKey(s.key, true))
				q.fieldError("key", "lower case letters, digits and _ only (it goes in save files)");
			if (s.key == connections::settingKey)
				q.fieldError("key", "\"c\" is kept for a piece's shape (use shape = true)");
			for (const itemSetting& other : _settings)
			{
				if (!s.key.empty() && other.key == s.key)
					q.fieldError("key", "\"" + s.key + "\" is used by another setting of this item");
			}
			s.label = q.text("label", s.key);
			s.hidden = q.flag("hidden", false);
			if (q.flag("toggle", false))
			{
				s.type = itemSetting::kind::toggle;
				s.minimum = 0.0;
				s.maximum = 1.0;
				if (q.has("default"))
				{
					//true / false, or 1 / 0
					const toml::node* node = element.as_table()->get("default");
					if (node->is_boolean())
						s.defaultValue = node->value<bool>().value_or(true) ? 1.0 : 0.0;
					else
						s.defaultValue = q.number("default", 1.0, false, 0.0, 1.0) != 0.0 ? 1.0 : 0.0;
				}
				else
					s.defaultValue = 1.0;
			}
			else
			{
				s.type = itemSetting::kind::number;
				s.minimum = q.number("min", 0.0, true);
				s.maximum = q.number("max", unlimited, true, s.minimum);
				const bool maximumOk = s.maximum != unlimited;   //not missing or below min (that is already reported)
				if (!maximumOk)
					s.maximum = s.minimum;
				s.defaultValue = q.number("default", s.minimum, true, s.minimum, maximumOk ? s.maximum : unlimited);
				s.step = q.number("step", 1.0, false, 0.0, unlimited, true);
				s.unit = q.text("unit", "");
			}
			q.finish();
			_settings.push_back(s);
		}
	}

	//footprint = [w, h], ports = [ { tile = [x, y], side, carries, flow } ], rotates: what makes an item a machine, and
	//the checks that it can work (a big machine reaches the networks only through its ports)
	void readMachine(fieldReader& _r, const toml::table& _table, tileTypeInfo& _type)
	{
		if (const toml::array* size = _r.list("footprint"))
		{
			if (size->size() == 2 && (*size)[0].is_integer() && (*size)[1].is_integer())
				_type.footprint = sf::Vector2i(int((*size)[0].value<int64_t>().value_or(1)), int((*size)[1].value<int64_t>().value_or(1)));
			if (size->size() != 2 || _type.footprint.x < 1 || _type.footprint.y < 1 || _type.footprint.x > 8 || _type.footprint.y > 8)
			{
				_r.fieldError("footprint", "must be [w, h]: whole numbers of tiles from 1 to 8 (w along the lower right, h along the lower left)");
				_type.footprint = sf::Vector2i(1, 1);
			}
		}
		_type.rotates = _r.flag("rotates", false);
		if (const toml::array* ports = _r.list("ports"))
		{
			int index = 0;
			for (const toml::node& element : *ports)
			{
				const std::string path = "ports[" + std::to_string(index++) + "].";
				if (!element.is_table())
				{
					_r.error(path + ": every port is a table, e.g. { tile = [0, 0], side = \"LR\", carries = \"liquid\", flow = \"out\" }");
					continue;
				}
				fieldReader q(*element.as_table(), _r.location(), path, _r.messages());
				portInfo port;
				if (const toml::array* cell = q.list("tile"))
				{
					if (cell->size() == 2 && (*cell)[0].is_integer() && (*cell)[1].is_integer())
						port.cell = sf::Vector2i(int((*cell)[0].value<int64_t>().value_or(0)), int((*cell)[1].value<int64_t>().value_or(0)));
					else
						q.fieldError("tile", "must be [x, y], a tile of the footprint ([0, 0] is its upper tile)");
				}
				if (port.cell.x < 0 || port.cell.y < 0 || port.cell.x >= _type.footprint.x || port.cell.y >= _type.footprint.y)
					q.fieldError("tile", "[" + std::to_string(port.cell.x) + ", " + std::to_string(port.cell.y) + "] isn't a tile of its " +
						std::to_string(_type.footprint.x) + " x " + std::to_string(_type.footprint.y) + " footprint");
				port.side = sideFromName(q.text("side", "", true));
				if (port.side < 0)
					q.fieldError("side", "must be \"UL\", \"UR\", \"LL\" or \"LR\"");
				else
				{
					const sf::Vector2i across = port.cell + footprint::step(port.side);
					if (across.x >= 0 && across.y >= 0 && across.x < _type.footprint.x && across.y < _type.footprint.y)
						q.fieldError("side", "faces another tile of the machine: a port has to be on an outside face");
				}
				const std::string carries = q.text("carries", "", true);
				if (carries == "power")
					port.carries = portInfo::kind::power;
				else if (carries == "liquid")
					port.carries = portInfo::kind::liquid;
				else if (carries == "items")
					port.carries = portInfo::kind::items;
				else if (!carries.empty())
					q.fieldError("carries", "must be \"power\", \"liquid\" or \"items\"");
				const std::string flow = q.text("flow", "both");
				if (flow == "in")
					port.flow = portInfo::direction::in;
				else if (flow == "out")
					port.flow = portInfo::direction::out;
				else if (flow == "both" || flow == "any")
					port.flow = portInfo::direction::both;
				else
					q.fieldError("flow", "must be \"in\", \"out\" or \"both\" (or \"any\", the same)");
				q.finish();
				_type.ports.push_back(port);
			}
		}

		if (!footprint::isMachine(_type))
			return;
		auto count = [&](portInfo::kind _kind, int _flow = -1) {
			return int(std::count_if(_type.ports.begin(), _type.ports.end(), [&](const portInfo& _p) {
				return _p.carries == _kind && (_flow < 0 || int(_p.flow) == _flow);
			}));
		};
		const bool multiTile = footprint::isMultiTile(_type);
		if (_type.layer != tileLayer::fixture)
			_r.error("only fixtures can be machines (with a footprint, ports or rotates)");
		if (findSetting(_type, connections::settingKey))
			_r.error("shape = true is for cables and pipes: a machine connects through its ports instead");
		if (_type.electrical.conductor)
			_r.error("cables and risers connect by their shape, not ports or a footprint");
		if (count(portInfo::kind::power) > 1)
			_r.fieldError("ports", "a machine has one power port at most");
		if (count(portInfo::kind::power) == 1 && !_type.electrical.load && !_type.electrical.source)
			_r.fieldError("ports", "a power port needs electrical.load or electrical.generator to use it");
		if (multiTile && (_type.electrical.load || _type.electrical.source) && count(portInfo::kind::power) == 0)
			_r.fieldError("ports", "a machine bigger than one tile uses power through a power port: add one");
		if (multiTile && _type.fluid.carriesLiquid && count(portInfo::kind::liquid) == 0)
			_r.fieldError("ports", "a machine bigger than one tile holds liquid through liquid ports: add one");
		if (_type.fluid.pump && !_type.ports.empty() &&
			(count(portInfo::kind::liquid, int(portInfo::direction::in)) != 1 || count(portInfo::kind::liquid, int(portInfo::direction::out)) != 1))
			_r.fieldError("ports", "a pump needs one liquid port with flow = \"in\" and one with flow = \"out\": it pushes from one to the other");
		if (_type.fluid.pump && _type.ports.empty() && _type.fluid.pumpSide < 0)
			_r.error("fluid.pump: give it liquid ports (flow \"in\" and \"out\"), or pushes = \"UL\" / \"UR\" / \"LL\" / \"LR\"");
		//its turn, set by R when it is placed
		itemSetting turn = settings::number(footprint::rotationKey, "Turn", 0.0, 0.0, 3.0, 1.0, "");
		turn.hidden = true;
		_type.settings.push_back(turn);
		for (const itemSetting& setting : _type.settings)
		{
			if (setting.key == footprint::rotationKey && &setting != &_type.settings.back())
				_r.fieldError("settings", "\"r\" is kept for a machine's turn");
		}
		(void)_table;
	}

	//[[item.<key>.recipe]] tables: name, time, in = { material = amount }, out = { ... }, heat
	void readRecipes(fieldReader& _r, const toml::array& _list, tileTypeInfo& _type)
	{
		int index = 0;
		for (const toml::node& element : _list)
		{
			const std::string path = "recipe[" + std::to_string(index++) + "].";
			if (!element.is_table())
			{
				_r.error("recipe: every recipe is a table: [[item." + _type.key + ".recipe]] followed by name, time, in and out");
				continue;
			}
			fieldReader q(*element.as_table(), _r.location(), path, _r.messages());
			recipeInfo recipe;
			recipe.name = q.text("name", "Recipe " + std::to_string(index));
			recipe.seconds = q.number("time", 1.0, true, 0.0, unlimited, true);
			recipe.heat = q.number("heat", 0.0);
			for (const bool output : { false, true })
			{
				const std::string field = output ? "out" : "in";
				const toml::table* amounts = q.subTable(field);
				if (!amounts)
					continue;
				//in the order the file gives them (a TOML table keeps its keys sorted)
				std::vector<std::pair<std::string, const toml::node*>> entries;
				for (auto&& [materialKey, value] : *amounts)
					entries.push_back({ std::string(materialKey.str()), &value });
				std::stable_sort(entries.begin(), entries.end(), [](const auto& _a, const auto& _b) {
					const toml::source_position a = _a.second->source().begin, b = _b.second->source().begin;
					return a.line != b.line ? a.line < b.line : a.column < b.column;
				});
				for (const auto& [key, node] : entries)
				{
					const toml::node& value = *node;
					const materialInfo* material = findMaterial(key);
					if (!material || material->removed)
					{
						q.fieldError(field, "\"" + key + "\" isn't a material");
						continue;
					}
					if (material->state == materialInfo::phase::gas)
					{
						q.fieldError(field, "\"" + key + "\" is a gas: machines take and give solids and liquids only");
						continue;
					}
					const double amount = value.value<double>().value_or(0.0);
					if (!value.is_number() || !std::isfinite(amount) || amount <= 0.0)
					{
						q.fieldError(field + "." + key, "must be a number above 0 (kg of a solid, L of a liquid, each cycle)");
						continue;
					}
					(output ? recipe.outputs : recipe.inputs).push_back({ key, amount });
				}
			}
			if (!q.has("in") && !q.has("out"))
				q.error(path + "in/out: a recipe takes something, gives something, or both, e.g. in = { iron_ore = 10 }, out = { iron = 6 }");
			q.finish();
			_type.recipes.push_back(recipe);
		}
	}

	//a machine that makes things has to be able to get its inputs and give its outputs: a solid through an item port, a
	//liquid through a liquid port, each facing the right way
	void checkRecipes(fieldReader& _r, const tileTypeInfo& _type)
	{
		if (_type.recipes.empty())
			return;
		if (_type.storage.isStore())
			_r.error("recipe: an item makes things or stores them, not both (its buffers hold its inputs and outputs)");
		if (_type.fluid.tank || _type.fluid.pump)
			_r.error("recipe: a tank or pump can't make things too: give the machine liquid ports instead");
		auto hasPort = [&](portInfo::kind _kind, bool _output) {
			return std::any_of(_type.ports.begin(), _type.ports.end(), [&](const portInfo& _p) {
				return _p.carries == _kind && _p.flow != (_output ? portInfo::direction::in : portInfo::direction::out);
			});
		};
		std::set<std::string> reported;
		for (int i = 0; i < int(_type.recipes.size()); i++)
		{
			for (const bool output : { false, true })
			{
				for (const auto& [key, amount] : output ? _type.recipes[i].outputs : _type.recipes[i].inputs)
				{
					const materialInfo* material = findMaterial(key);
					if (!material)
						continue;
					const bool solid = material->state == materialInfo::phase::solid;
					const portInfo::kind needs = solid ? portInfo::kind::items : portInfo::kind::liquid;
					const std::string what = std::string(solid ? "an item" : "a liquid") + " port with flow = \"" + (output ? "out" : "in") + "\" or \"both\"";
					if (!hasPort(needs, output) && reported.insert(what).second)
						_r.error("recipe[" + std::to_string(i) + "]." + (output ? "out" : "in") + ": " + material->name + " is a " +
							(solid ? "solid" : "liquid") + ": the machine needs " + what + " to " + (output ? "give" : "take") + " it");
				}
			}
		}
	}

	//one [item.<key>] table into _type. true if it had no errors
	bool readItem(const std::string& _key, const toml::table& _table, const std::string& _where, tileTypeInfo& _type,
		std::vector<content::message>& _messages)
	{
		const int errorsBefore = countErrors(_messages);
		fieldReader r(_table, _where, "", _messages);
		_type = tileTypeInfo();
		_type.key = _key;
		_type.origin = _where;
		if (!validKey(_key))
			r.error("the key \"" + _key + "\" can only have letters, digits and _ (it goes in save files), starting with a letter");
		_type.name = r.text("name", _key);
		_type.category = r.text("tab", "");

		//mount = "outside": it goes on a bare tile beside the hull or on a roof (see tileTypeInfo::outside)
		const std::string mount = r.text("mount", "inside");
		if (mount == "outside")
			_type.outside = true;
		else if (mount != "inside")
			r.fieldError("mount", "must be \"inside\" (on a floor or wall, the default) or \"outside\" (on a bare tile beside the hull)");
		const std::string layer = r.text("layer", "fixture");
		if (layer == "fixture")
			_type.layer = tileLayer::fixture;
		else if (layer == "floor")
			_type.layer = tileLayer::floor;
		else if (layer == "wall")
			_type.layer = tileLayer::wall;
		else if (layer == "background")
			_type.layer = tileLayer::background;
		else
			r.fieldError("layer", "must be \"fixture\", \"floor\", \"wall\" or \"background\"");
		//frame = true: open structure, a floor that isn't hull (see tileTypeInfo::frame)
		_type.frame = r.flag("frame", false);
		if (_type.frame && _type.layer != tileLayer::floor)
			r.fieldError("frame", "only a floor can be a frame (layer = \"floor\")");
		//kg: its own mass, for a ship's mass (left out: a default for its layer)
		if (r.has("kg"))
			_type.kg = r.number("kg", 0.0, false, 0.0, unlimited);
		if (_type.layer == tileLayer::wall)
		{
			_type.wallEdge = sideFromName(r.text("edge", ""));
			if (_type.wallEdge < 0)
				r.fieldError("edge", "a wall needs edge = \"UL\", \"UR\", \"LL\" or \"LR\"");
		}
		else if (r.has("edge"))
			r.fieldError("edge", "only walls have an edge");

		//art: one picture, or one for each quarter turn (unturned first)
		if (r.has("art") && _table.get("art")->is_array())
		{
			const std::vector<std::string> pictures = r.textList("art", true);
			if (pictures.size() == 4)
			{
				_type.art = pictures[0];
				_type.turnArt.assign(pictures.begin() + 1, pictures.end());
			}
			else
				r.fieldError("art", "give one picture, or a list of 4: unturned, then turned 1, 2 and 3 quarter turns clockwise");
		}
		else
			_type.art = r.text("art", "", true);
		_type.blockedMessage = r.text("blocked", "cannot place " + lowerCase(_type.name) + " here");
		_type.drawArms = r.text("draw_arms", "");
		if (r.flag("shape", false))
		{
			itemSetting shape = settings::number(connections::settingKey, "Connects on", connections::all, 0.0, 15.0, 1.0, "");
			shape.hidden = true;
			_type.settings.push_back(shape);
		}

		if (const toml::table* t = r.subTable("electrical"))
			readElectrical(r, *t, _type.electrical);
		if (const toml::table* t = r.subTable("fluid"))
			readFluid(r, *t, _type.fluid);
		if (const toml::table* t = r.subTable("thermal"))
			readThermal(r, *t, _type.thermal);
		//items = { tube | riser = { kg_per_s } }: an item tube
		if (const toml::table* t = r.subTable("items"))
		{
			fieldReader q(*t, r.location(), "items.", r.messages());
			const toml::table* tube = q.subTable("tube");
			const toml::table* riser = q.subTable("riser");
			if (tube && riser)
				q.error("items: an item is a tube or a tube riser, not both");
			else if (!tube && !riser)
				q.error("items: give tube = { kg_per_s = ... } or riser = { kg_per_s = ... }");
			else
			{
				fieldReader w(tube ? *tube : *riser, r.location(), tube ? "items.tube." : "items.riser.", r.messages());
				_type.items.tube = true;
				_type.items.vertical = riser != nullptr;
				_type.items.rate = w.number("kg_per_s", 1.0, true, 0.0, unlimited, true);
				w.finish();
			}
			q.finish();
			if (_type.layer != tileLayer::fixture)
				r.error("items: only fixtures can be tubes");
		}
		//storage = { litres, kg }: a container of solids
		if (const toml::table* t = r.subTable("storage"))
		{
			fieldReader q(*t, r.location(), "storage.", r.messages());
			_type.storage.litres = q.number("litres", 1000.0, true, 0.0, unlimited, true);
			_type.storage.kg = q.has("kg") ? q.number("kg", 1000.0, false, 0.0, unlimited, true) : 0.0;
			q.finish();
			if (_type.fluid.tank)
				r.error("storage: an item is a tank (liquids) or storage (solids), not both");
			if (_type.layer != tileLayer::fixture)
				r.error("storage: only fixtures can be storage");
		}
		//engine = { newtons, exhaust_velocity, fuel, pushes }: a ship's main engine. no fuel: electric (power alone)
		if (const toml::table* t = r.subTable("engine"))
		{
			fieldReader q(*t, r.location(), "engine.", r.messages());
			_type.engine.newtons = q.number("newtons", 0.0, true, 0.0, unlimited, true);
			_type.engine.fuel = q.text("fuel", "");
			const bool fuelled = !_type.engine.fuel.empty();
			_type.engine.exhaustVelocity = q.number("exhaust_velocity", 0.0, fuelled, 0.0, unlimited, fuelled);
			const int side = sideFromName(q.text("pushes", "UL"));
			if (side < 0)
				q.fieldError("pushes", "must be \"UL\", \"UR\", \"LL\" or \"LR\": the way it pushes the ship, unturned");
			_type.engine.pushes = std::max(side, 0);
			q.finish();
		}
		//thrusters = { newtons, exhaust_velocity, fuel }: manoeuvring thrusters. no fuel: electric
		if (const toml::table* t = r.subTable("thrusters"))
		{
			fieldReader q(*t, r.location(), "thrusters.", r.messages());
			_type.thrusters.newtons = q.number("newtons", 0.0, true, 0.0, unlimited, true);
			_type.thrusters.fuel = q.text("fuel", "");
			const bool fuelled = !_type.thrusters.fuel.empty();
			_type.thrusters.exhaustVelocity = q.number("exhaust_velocity", 0.0, fuelled, 0.0, unlimited, fuelled);
			q.finish();
		}
		//gyroscope = { newton_metres }: turns its ship
		if (const toml::table* t = r.subTable("gyroscope"))
		{
			fieldReader q(*t, r.location(), "gyroscope.", r.messages());
			_type.gyroTorque = q.number("newton_metres", 0.0, true, 0.0, unlimited, true);
			q.finish();
		}
		//helm = true: a flight console
		_type.helm = r.flag("helm", false);
		//dock = { tile = [x, y], face = "LR" }: a docking point
		if (const toml::table* t = r.subTable("dock"))
		{
			fieldReader q(*t, r.location(), "dock.", r.messages());
			_type.dock.isDock = true;
			if (const toml::array* cell = q.list("tile"))
			{
				if (cell->size() == 2 && (*cell)[0].is_integer() && (*cell)[1].is_integer())
					_type.dock.cell = sf::Vector2i(int((*cell)[0].value<int64_t>().value_or(0)), int((*cell)[1].value<int64_t>().value_or(0)));
				else
					q.fieldError("tile", "must be [x, y], a tile of the footprint ([0, 0] is its upper tile)");
			}
			const int face = sideFromName(q.text("face", "", true));
			if (face < 0)
				q.fieldError("face", "must be \"UL\", \"UR\", \"LL\" or \"LR\": the face a ship docks against");
			_type.dock.face = std::max(face, 0);
			q.finish();
		}
		//light = { colour, brightness, reach, height, follows, flicker, emergency, glow }: the light it gives (lighting.h)
		auto readDriver = [](fieldReader& _q, lightDriver _fallback) {
			const std::string name = _q.text("follows", lightDriverName(_fallback));
			const int driver = lightDriverFromName(name);
			if (driver < 0)
				_q.fieldError("follows", "must be \"power\", \"running\", \"working\", \"always\" or \"blink\"");
			return driver < 0 ? _fallback : lightDriver(driver);
		};
		if (const toml::table* t = r.subTable("light"))
		{
			fieldReader q(*t, r.location(), "light.", r.messages());
			lightProperties& l = _type.light;
			l.gives = true;
			const std::string colour = q.text("colour", "#ffffff");
			unsigned int rgb = 0;
			if (colour.size() == 7 && colour[0] == '#' && sscanf(colour.c_str() + 1, "%6x", &rgb) == 1)
				l.colour = sf::Color(sf::Uint8(rgb >> 16), sf::Uint8(rgb >> 8), sf::Uint8(rgb));
			else
				q.fieldError("colour", "must be \"#rrggbb\", e.g. \"#ffd69e\"");
			l.brightness = q.number("brightness", 1.0, false, 0.0, 20.0);
			l.reach = q.number("reach", 6.0, false, 0.0, 100.0, true);
			l.height = q.number("height", 20.0, false, 0.0, 255.0);
			l.follows = readDriver(q, _type.electrical.load ? lightDriver::power : lightDriver::always);
			l.flicker = q.number("flicker", 0.0, false, 0.0, 1.0);
			l.emergency = q.flag("emergency", false);
			l.glow = q.number("glow", 0.8, false, 0.0, 1.0);
			q.finish();
		}
		//emits = { kind, at, follows, rate }: steam or smoke it gives off
		if (const toml::table* t = r.subTable("emits"))
		{
			fieldReader q(*t, r.location(), "emits.", r.messages());
			emitProperties& e = _type.emits;
			e.kind = q.text("kind", "", true);
			if (e.kind != "steam" && e.kind != "smoke")
			{
				q.fieldError("kind", "must be \"steam\" or \"smoke\"");
				e.kind.clear();
			}
			if (const toml::array* at = q.list("at"))
			{
				if (at->size() == 2 && (*at)[0].is_number() && (*at)[1].is_number())
					e.at = sf::Vector2f(float((*at)[0].value<double>().value_or(0.0)), float((*at)[1].value<double>().value_or(0.0)));
				else
					q.fieldError("at", "must be [x, y]: where in its art it comes from, in pixels from the art's top left");
			}
			e.follows = readDriver(q, lightDriver::always);
			e.rate = q.number("rate", 1.0, false, 0.0, 10.0);
			q.finish();
		}
		if (const toml::array* list = r.list("settings"))
			readSettings(r, *list, _type.settings);
		//[[item.<key>.recipe]]: what a machine makes, picked from its tile panel (its hidden "rc" setting)
		if (const toml::array* list = r.list("recipe"))
		{
			readRecipes(r, *list, _type);
			for (const itemSetting& setting : _type.settings)
			{
				if (setting.key == recipes::settingKey)
					r.fieldError("settings", "\"rc\" is kept for a machine's recipe");
			}
			itemSetting chosen = settings::number(recipes::settingKey, "Recipe", 0.0, 0.0, double(std::max(int(_type.recipes.size()) - 1, 0)), 1.0, "");
			chosen.hidden = true;
			_type.settings.push_back(chosen);
		}
		if (r.has("max_temperature"))
		{
			_type.maxTemperature = r.number("max_temperature", 0.0, false, 0.0, unlimited, true);
			if (_type.recipes.empty())
				r.warn("max_temperature: only machines that make things (with a [[item." + _key + ".recipe]]) stop when hot: ignored");
		}
		readMachine(r, _table, _type);
		checkRecipes(r, _type);
		//engines and thrusters burn a liquid that comes in through a liquid port; electric ones (no fuel) run on power
		for (const auto& [field, fuel, used] : { std::tuple<std::string, std::string, bool>("engine", _type.engine.fuel, _type.engine.isEngine()),
			std::tuple<std::string, std::string, bool>("thrusters", _type.thrusters.fuel, _type.thrusters.isThruster()) })
		{
			if (!used)
				continue;
			if (fuel.empty())
			{
				if (!_type.electrical.load)
					r.fieldError(field, "with no fuel it is electric and runs on power alone: give it electrical.load = { watts = ... }");
				if (_type.layer != tileLayer::fixture)
					r.error(field + ": only fixtures can be engines or thrusters");
				continue;
			}
			const materialInfo* material = findMaterial(fuel);
			if (!material)
				r.fieldError(field + ".fuel", "\"" + fuel + "\" is not a material in the content files");
			else if (material->state != materialInfo::phase::liquid)
				r.fieldError(field + ".fuel", material->name + " is a solid: fuel has to be a liquid (it comes through a pipe)");
			const bool hasLiquidIn = std::any_of(_type.ports.begin(), _type.ports.end(), [](const portInfo& _p) {
				return _p.carries == portInfo::kind::liquid && _p.flow != portInfo::direction::out;
			});
			if (!hasLiquidIn)
				r.fieldError("ports", field + ": it needs a liquid port (flow \"in\" or \"both\") to take its fuel through");
			if (_type.layer != tileLayer::fixture)
				r.error(field + ": only fixtures can be engines or thrusters");
		}
		if ((_type.gyroTorque > 0.0 || _type.helm) && _type.layer != tileLayer::fixture)
			r.error("gyroscope, helm: only fixtures can be these");
		if (_type.dock.isDock)
		{
			const sf::Vector2i cell = _type.dock.cell;
			if (_type.layer != tileLayer::fixture)
				r.error("dock: only fixtures can be docking points");
			else if (cell.x < 0 || cell.y < 0 || cell.x >= _type.footprint.x || cell.y >= _type.footprint.y)
				r.fieldError("dock.tile", "[" + std::to_string(cell.x) + ", " + std::to_string(cell.y) + "] isn't a tile of its footprint");
			else
			{
				const sf::Vector2i across = cell + footprint::step(_type.dock.face);
				if (across.x >= 0 && across.y >= 0 && across.x < _type.footprint.x && across.y < _type.footprint.y)
					r.fieldError("dock.face", "faces another tile of it: a ship docks against an outside face");
			}
			if (!_type.outside)
				r.warn("dock: a docking point is usually mounted outside (mount = \"outside\"), where ships can reach it");
		}
		//what drives a light, or steam and smoke, has to be something the item has
		auto checkDriver = [&](const std::string& _field, lightDriver _driver) {
			if (_driver == lightDriver::power && !_type.electrical.load)
				r.warn(_field + ": follows \"power\", but it uses no power (electrical.load), so it stays dark");
			else if (_driver == lightDriver::running && !_type.electrical.source)
				r.warn(_field + ": follows \"running\", which is for generators (electrical.generator)");
			else if (_driver == lightDriver::working && _type.recipes.empty())
				r.warn(_field + ": follows \"working\", which is for machines that make things (a recipe)");
		};
		if (_type.light.gives)
			checkDriver("light.follows", _type.light.follows);
		if (_type.emits.emits())
			checkDriver("emits.follows", _type.emits.follows);
		if (_type.outside)
		{
			//outside, on a bare tile: no cable, pipe or tube can share its tile, so it reaches the networks through ports
			auto hasPort = [&](portInfo::kind _kind) {
				return std::any_of(_type.ports.begin(), _type.ports.end(), [&](const portInfo& _p) { return _p.carries == _kind; });
			};
			if (_type.layer != tileLayer::fixture)
				r.error("mount: only fixtures go outside (floors and walls are the hull itself)");
			if (_type.electrical.conductor || _type.fluid.vertical || (_type.fluid.carriesLiquid && !_type.fluid.tank && _type.ports.empty()) || _type.items.tube)
				r.error("mount: cables, pipes, risers and tubes go inside, on floors");
			if ((_type.electrical.load || _type.electrical.source) && !hasPort(portInfo::kind::power))
				r.fieldError("ports", "outside, it takes or gives power only through a power port facing the hull (no cable can be on its bare tile)");
			if (_type.fluid.carriesLiquid && !hasPort(portInfo::kind::liquid))
				r.fieldError("ports", "outside, it joins the pipes only through liquid ports facing the hull");
			if (_type.storage.isStore() && !hasPort(portInfo::kind::items))
				r.fieldError("ports", "outside, its storage is reached only through item ports facing the hull");
		}
		r.finish();
		return countErrors(_messages) == errorsBefore;
	}

	//one [material.<key>] table
	bool readMaterial(const std::string& _key, const toml::table& _table, const std::string& _where, materialInfo& _material,
		std::vector<content::message>& _messages)
	{
		const int errorsBefore = countErrors(_messages);
		fieldReader r(_table, _where, "", _messages);
		_material = materialInfo();
		_material.key = _key;
		_material.origin = _where;
		if (!validKey(_key))
			r.error("the key \"" + _key + "\" can only have letters, digits and _, starting with a letter");
		_material.name = r.text("name", _key);
		const std::string phase = r.text("phase", "", true);
		if (phase == "solid")
			_material.state = materialInfo::phase::solid;
		else if (phase == "liquid")
			_material.state = materialInfo::phase::liquid;
		else if (phase == "gas")
			_material.state = materialInfo::phase::gas;
		else if (!phase.empty())
			r.fieldError("phase", "must be \"solid\", \"liquid\" or \"gas\"");
		const std::string colour = r.text("colour", "#ffffff");
		unsigned int rgb = 0;
		if (colour.size() == 7 && colour[0] == '#' && sscanf(colour.c_str() + 1, "%6x", &rgb) == 1)
			_material.colour = sf::Color(sf::Uint8(rgb >> 16), sf::Uint8(rgb >> 8), sf::Uint8(rgb));
		else
			r.fieldError("colour", "must be \"#rrggbb\", e.g. \"#3a8cff\"");
		_material.density = r.number("density", 1000.0, true, 0.0, unlimited, true);
		_material.heatCapacity = r.number("heat_capacity", 1000.0, true, 0.0, unlimited, true);
		r.finish();
		return countErrors(_messages) == errorsBefore;
	}

	//an entry found in a file, to be read in file then line order
	struct found
	{
		int file = 0;
		int line = 0;
		std::string key;
		const toml::table* table = nullptr;
		std::string where;
	};

	//"content/power.toml" from the full path, for messages
	std::string displayPath(const std::filesystem::path& _file, const std::filesystem::path& _folder)
	{
		std::error_code ignored;
		const std::filesystem::path relative = std::filesystem::relative(_file, _folder.parent_path(), ignored);
		return (relative.empty() ? _file : relative).generic_string();
	}

	bool lessByPlace(const found& _a, const found& _b)
	{
		return _a.file != _b.file ? _a.file < _b.file : _a.line < _b.line;
	}
}

namespace content
{
	int report::errors() const { return countErrors(messages); }
	int report::warnings() const { return int(messages.size()) - errors(); }

	const report& lastReport() { return current; }

	std::vector<message> readItemText(const std::string& _text, const std::string& _key, const std::string& _where, tileTypeInfo& _into)
	{
		std::vector<message> messages;
		_into = tileTypeInfo();
		_into.key = _key;
		try
		{
			const toml::table file = toml::parse(_text);
			const toml::node* entry = file["item"][_key].node();
			if (!entry || !entry->is_table())
			{
				messages.push_back({ true, _where, "there is no [item." + _key + "] table" });
				return messages;
			}
			readItem(_key, *entry->as_table(), _where, _into, messages);
		}
		catch (const toml::parse_error& error)
		{
			messages.push_back({ true, _where + ":" + std::to_string(error.source().begin.line), "can't read it: " + std::string(error.description()) });
			return messages;
		}
		//what load() checks once every item is read: another item's arms, and a tank's liquid
		const std::string& arms = _into.drawArms;
		if (!arms.empty() && arms != "cross" && arms != "cross_upright" && (arms == _key || !findTileType(arms) || findTileType(arms)->art.empty()))
			messages.push_back({ true, _where, "draw_arms: must be \"cross\", \"cross_upright\" or another item's key (\"" + arms + "\" isn't one)" });
		if (_into.fluid.tank && !_into.fluid.liquid.empty())
		{
			const materialInfo* liquid = findMaterial(_into.fluid.liquid);
			if (!liquid || liquid->removed || liquid->state != materialInfo::phase::liquid)
				messages.push_back({ true, _where, "fluid.tank.liquid: \"" + _into.fluid.liquid + "\" isn't a liquid material" });
		}
		return messages;
	}

	std::vector<message> readMaterialText(const std::string& _text, const std::string& _key, const std::string& _where, materialInfo& _into)
	{
		std::vector<message> messages;
		_into = materialInfo();
		_into.key = _key;
		try
		{
			const toml::table file = toml::parse(_text);
			const toml::node* entry = file["material"][_key].node();
			if (!entry || !entry->is_table())
			{
				messages.push_back({ true, _where, "there is no [material." + _key + "] table" });
				return messages;
			}
			readMaterial(_key, *entry->as_table(), _where, _into, messages);
		}
		catch (const toml::parse_error& error)
		{
			messages.push_back({ true, _where + ":" + std::to_string(error.source().begin.line), "can't read it: " + std::string(error.description()) });
		}
		return messages;
	}

	void addMessage(const message& _message) { current.messages.push_back(_message); }

	std::string count(int _n, const std::string& _one, const std::string& _plural)
	{
		return std::to_string(_n) + " " + (_n == 1 ? _one : (_plural.empty() ? _one + "s" : _plural));
	}

	std::string summaryLine(const report& _report)
	{
		std::string line = "Content reloaded: " + count(_report.items, "item") + ", " + count(_report.materials, "material") + ", ";
		if (_report.errors() > 0)
			line += count(_report.errors(), "error") + " (F6 for details)";
		else if (_report.warnings() > 0)
			line += count(_report.warnings(), "warning") + " (F6 for details)";
		else
			line += "no problems";
		return line;
	}

	const report& load(const std::string& _folder)
	{
		namespace fs = std::filesystem;
		report result;
		std::error_code problem;
		const fs::path folder = fs::absolute(fs::path(_folder), problem);
		if (!fs::is_directory(folder, problem))
		{
			result.messages.push_back({ true, _folder, "the content folder isn't there: nothing loaded" });
			current = result;
			return current;
		}

		//every .toml under the folder, in path order
		std::vector<fs::path> paths;
		for (fs::recursive_directory_iterator it(folder, problem), end; !problem && it != end; it.increment(problem))
		{
			if (it->is_regular_file() && it->path().extension() == ".toml")
				paths.push_back(it->path());
		}
		std::sort(paths.begin(), paths.end());

		//parse them all (kept alive until everything is read)
		std::vector<toml::table> files;
		std::vector<std::string> names;
		for (const fs::path& path : paths)
		{
			const std::string name = displayPath(path, folder);
			try
			{
				files.push_back(toml::parse_file(path.string()));
				names.push_back(name);
				result.files.push_back(name);
			}
			catch (const toml::parse_error& error)
			{
				result.messages.push_back({ true, name + ":" + std::to_string(error.source().begin.line),
					"can't read the file: " + std::string(error.description()) + " (nothing in this file was loaded)" });
			}
		}

		//find every entry; the sections a file can have
		std::vector<found> items, materials, turns, kinds;
		std::vector<std::string> tabs;
		struct aliasText { std::string key, text, where; };
		std::vector<aliasText> aliasTexts;
		for (int f = 0; f < int(files.size()); f++)
		{
			for (auto&& [sectionKey, sectionNode] : files[f])
			{
				const std::string section(sectionKey.str());
				const std::string at = names[f] + ":" + std::to_string(sectionNode.source().begin.line);
				if (section == "menu")
				{
					if (!sectionNode.is_table())
						continue;
					fieldReader menu(*sectionNode.as_table(), at + " [menu]", "", result.messages);
					for (const std::string& tab : menu.textList("tabs", false))
						tabs.push_back(tab);
					menu.finish();
					continue;
				}
				if (section == "aliases")
				{
					//old key = "item{settings}": checked once the items are all read
					if (!sectionNode.is_table())
						continue;
					for (auto&& [aliasKey, aliasNode] : *sectionNode.as_table())
					{
						const std::string where = names[f] + ":" + std::to_string(aliasNode.source().begin.line) + " [aliases]";
						if (!aliasNode.is_string())
						{
							result.messages.push_back({ true, where, std::string(aliasKey.str()) + ": must be text, e.g. \"pump{r=2}\"" });
							continue;
						}
						aliasTexts.push_back({ std::string(aliasKey.str()), aliasNode.value<std::string>().value_or(""), where });
					}
					continue;
				}
				std::vector<found>* into = section == "item" ? &items : section == "material" ? &materials :
					section == "turns" ? &turns : section == "kinds" ? &kinds : nullptr;
				if (!into || !sectionNode.is_table())
				{
					result.messages.push_back({ false, at + " [" + section + "]", "not a section this game knows (item, material, turns, kinds, menu, aliases), ignored" });
					continue;
				}
				for (auto&& [entryKey, entryNode] : *sectionNode.as_table())
				{
					found entry;
					entry.file = f;
					entry.line = int(entryNode.source().begin.line);
					entry.key = std::string(entryKey.str());
					entry.where = names[f] + ":" + std::to_string(entry.line) + " [" + section + "." + entry.key + "]";
					if (!entryNode.is_table())
					{
						result.messages.push_back({ true, entry.where, "must be a table: [" + section + "." + entry.key + "] followed by its fields" });
						continue;
					}
					entry.table = entryNode.as_table();
					into->push_back(entry);
				}
			}
		}
		for (std::vector<found>* list : { &items, &materials, &turns, &kinds })
			std::sort(list->begin(), list->end(), lessByPlace);

		//materials first: items name them (a tank's liquid). a key defined twice: the first one counts
		std::deque<materialInfo>& materialStore = catalogueStore::materials();
		std::set<std::string> materialsInFiles;
		for (const found& entry : materials)
		{
			if (materialsInFiles.count(entry.key))
			{
				result.messages.push_back({ true, entry.where, "material \"" + entry.key + "\" is defined twice (this one is ignored)" });
				continue;
			}
			materialsInFiles.insert(entry.key);
			materialInfo material;
			const bool ok = readMaterial(entry.key, *entry.table, entry.where, material, result.messages);
			auto existing = std::find_if(materialStore.begin(), materialStore.end(), [&](const materialInfo& _m) { return _m.key == entry.key; });
			if (!ok)
				continue;
			if (existing == materialStore.end())
				materialStore.push_back(material);
			else
				*existing = material;
		}
		for (materialInfo& material : materialStore)
		{
			if (!materialsInFiles.count(material.key))
				material.removed = true;
		}
		result.materials = int(materialsInFiles.size());

		//read the items. a key defined twice: the first one counts
		std::vector<tileTypeInfo> readItems;
		std::vector<bool> readOk;
		std::map<std::string, std::string> firstPlace;
		for (const found& entry : items)
		{
			if (firstPlace.count(entry.key))
			{
				result.messages.push_back({ true, entry.where, "\"" + entry.key + "\" is already defined at " + firstPlace[entry.key] + " (this one is ignored)" });
				continue;
			}
			firstPlace[entry.key] = entry.where;
			tileTypeInfo type;
			readOk.push_back(readItem(entry.key, *entry.table, entry.where, type, result.messages));
			readItems.push_back(type);
		}
		result.items = int(readItems.size());

		//draw_arms names another item, so it is checked once they are all read
		for (size_t i = 0; i < readItems.size(); i++)
		{
			const std::string& arms = readItems[i].drawArms;
			if (arms.empty() || arms == "cross" || arms == "cross_upright")
				continue;
			const bool known = std::any_of(readItems.begin(), readItems.end(), [&](const tileTypeInfo& _t) { return _t.key == arms && !_t.art.empty(); });
			if (!known)
			{
				result.messages.push_back({ true, readItems[i].origin, "draw_arms: must be \"cross\", \"cross_upright\" or another item's key (\"" + arms + "\" isn't one)" });
				readOk[i] = false;
			}
		}

		//a tank's liquid: a liquid material (water when it doesn't say, or the first liquid there is)
		const materialInfo* defaultLiquid = nullptr;
		for (const materialInfo& material : materialStore)
		{
			if (material.state == materialInfo::phase::liquid && !material.removed && (!defaultLiquid || material.key == "water"))
				defaultLiquid = &material;
		}
		for (size_t i = 0; i < readItems.size(); i++)
		{
			fluidProperties& f = readItems[i].fluid;
			if (!f.tank)
				continue;
			if (f.liquid.empty())
			{
				if (defaultLiquid)
					f.liquid = defaultLiquid->key;
				else
				{
					result.messages.push_back({ true, readItems[i].origin, "fluid.tank: there are no liquid materials to fill it with (add a [material.<key>] with phase = \"liquid\")" });
					readOk[i] = false;
				}
				continue;
			}
			const materialInfo* chosen = findMaterial(f.liquid);
			if (!chosen || chosen->removed || chosen->state != materialInfo::phase::liquid)
			{
				result.messages.push_back({ true, readItems[i].origin, "fluid.tank.liquid: \"" + f.liquid + "\" isn't a liquid material" });
				readOk[i] = false;
			}
		}

		//into the catalogue: in place for a key it has (pointers to it stay good), added for a new one
		std::deque<tileTypeInfo>& store = catalogueStore::types();
		std::set<std::string> inFiles;
		for (size_t i = 0; i < readItems.size(); i++)
		{
			tileTypeInfo& fresh = readItems[i];
			inFiles.insert(fresh.key);
			auto existing = std::find_if(store.begin(), store.end(), [&](const tileTypeInfo& _t) { return _t.key == fresh.key; });
			if (!readOk[i])
			{
				if (existing != store.end())
					result.messages.push_back({ true, fresh.origin, "kept its previous definition until this is fixed" });
				continue;
			}
			if (existing == store.end())
			{
				store.push_back(fresh);
				continue;
			}
			if (!existing->removed && (existing->layer != fresh.layer || existing->wallEdge != fresh.wallEdge || existing->footprint != fresh.footprint ||
				footprint::isMachine(*existing) != footprint::isMachine(fresh)))
			{
				result.messages.push_back({ true, fresh.origin, "its layer, wall edge, footprint or being a machine changed: that needs a restart (kept its previous definition)" });
				continue;
			}
			*existing = fresh;
		}
		for (tileTypeInfo& type : store)
		{
			if (!inFiles.count(type.key) && !type.removed)
			{
				type.removed = true;
				result.messages.push_back({ false, type.origin, "\"" + type.key + "\" is no longer in the files: what is placed stays, but it can't be placed again" });
			}
		}

		//families: turns (R steps through the items) and kinds (one menu item, its kind picked). replaced each load
		std::vector<rotationFamily> newTurns;
		std::vector<kindFamily> newKinds;
		std::set<std::string> inFamily;
		auto checkMembers = [&](fieldReader& _r, const std::vector<std::string>& _keys) {
			bool ok = true;
			for (const std::string& key : _keys)
			{
				const tileTypeInfo* type = findTileType(key);
				if (!type || type->removed)
				{
					_r.fieldError("items", "\"" + key + "\" isn't an item");
					ok = false;
				}
				else if (inFamily.count(key) || std::count(_keys.begin(), _keys.end(), key) > 1)
				{
					_r.fieldError("items", "\"" + key + "\" is in a family twice");
					ok = false;
				}
			}
			return ok;
		};
		auto claim = [&](const std::vector<std::string>& _keys) { inFamily.insert(_keys.begin(), _keys.end()); };
		for (const found& entry : turns)
		{
			fieldReader r(*entry.table, entry.where, "", result.messages);
			rotationFamily family{ entry.key, r.textList("items", true) };
			bool ok = !family.keys.empty();
			if (family.keys.size() != 4)
			{
				r.fieldError("items", "needs 4 items, clockwise from the upper left: UL, UR, LR, LL");
				ok = false;
			}
			ok = checkMembers(r, family.keys) && ok;
			r.finish();
			if (ok)
			{
				newTurns.push_back(family);
				claim(family.keys);
			}
		}
		for (const found& entry : kinds)
		{
			fieldReader r(*entry.table, entry.where, "", result.messages);
			kindFamily family{ entry.key, r.textList("items", true), r.textList("labels", true) };
			bool ok = family.keys.size() >= 2;
			if (family.keys.size() < 2)
				r.fieldError("items", "needs at least 2 items");
			if (family.labels.size() != family.keys.size())
			{
				r.fieldError("labels", "needs one label for each item");
				ok = false;
			}
			ok = checkMembers(r, family.keys) && ok;
			r.finish();
			if (ok)
			{
				newKinds.push_back(family);
				claim(family.keys);
			}
		}
		catalogueStore::turns() = newTurns;
		catalogueStore::kinds() = newKinds;
		catalogueStore::tabOrder() = tabs;

		//aliases: an old key and what it loads as now ("pumpLR" = "pump{r=2}")
		std::map<std::string, std::pair<std::string, std::string>> newAliases;
		for (const aliasText& alias : aliasTexts)
		{
			const size_t brace = alias.text.find('{');
			const std::string target = alias.text.substr(0, brace);
			std::string settingsText;
			if (brace != std::string::npos)
			{
				const size_t close = alias.text.find('}', brace);
				if (close == std::string::npos || close != alias.text.size() - 1)
				{
					result.messages.push_back({ true, alias.where, alias.key + ": \"" + alias.text + "\" should be \"item\" or \"item{settings}\"" });
					continue;
				}
				settingsText = alias.text.substr(brace + 1, close - brace - 1);
			}
			const tileTypeInfo* type = findTileType(target);
			if (!type || type->removed)
				result.messages.push_back({ true, alias.where, alias.key + ": \"" + target + "\" isn't an item" });
			else if (findTileType(alias.key) && !findTileType(alias.key)->removed)
				result.messages.push_back({ true, alias.where, alias.key + ": is an item of its own, so it can't also be an alias" });
			else
				newAliases[alias.key] = { target, settingsText };
		}
		catalogueStore::aliases() = newAliases;
		result.families = int(newTurns.size() + newKinds.size());

		catalogueStore::generation()++;
		current = result;
		for (const message& m : current.messages)
			std::cout << (m.error ? "[CONTENT ERROR] " : "[CONTENT] ") << m.where << ": " << m.text << "\n";
		std::cout << "[CONTENT] " << current.files.size() << " files: " << current.items << " items, " << current.materials << " materials, "
			<< current.families << " families, " << current.errors() << " errors, " << current.warnings() << " warnings\n";
		return current;
	}
}
