#include "tileCatalogue.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace catalogueStore
{
	//deques: adding entries never moves the existing ones, so pointers to them (every placed object has one) stay good
	std::deque<tileTypeInfo>& types()
	{
		static std::deque<tileTypeInfo> list;
		return list;
	}

	std::deque<materialInfo>& materials()
	{
		static std::deque<materialInfo> list;
		return list;
	}

	std::vector<rotationFamily>& turns()
	{
		static std::vector<rotationFamily> list;
		return list;
	}

	std::vector<kindFamily>& kinds()
	{
		static std::vector<kindFamily> list;
		return list;
	}

	std::uint64_t& generation()
	{
		static std::uint64_t count = 0;
		return count;
	}

	std::vector<std::string>& tabOrder()
	{
		static std::vector<std::string> list;
		return list;
	}

	std::map<std::string, std::pair<std::string, std::string>>& aliases()
	{
		static std::map<std::string, std::pair<std::string, std::string>> map;
		return map;
	}
}

std::string resolveAlias(const std::string& _key, std::string& _settings)
{
	_settings.clear();
	const auto found = catalogueStore::aliases().find(_key);
	if (found == catalogueStore::aliases().end())
		return _key;
	_settings = found->second.second;
	return found->second.first;
}

const char* portKindName(portInfo::kind _kind)
{
	switch (_kind)
	{
	case portInfo::kind::power: return "power";
	case portInfo::kind::items: return "items";
	default: return "liquid";
	}
}

const char* portFlowName(portInfo::direction _flow)
{
	switch (_flow)
	{
	case portInfo::direction::in: return "in";
	case portInfo::direction::out: return "out";
	default: return "both ways";
	}
}

namespace footprint
{
	bool isMachine(const tileTypeInfo& _type)
	{
		return !_type.ports.empty() || isMultiTile(_type) || _type.rotates;
	}

	bool isMultiTile(const tileTypeInfo& _type)
	{
		return _type.footprint.x > 1 || _type.footprint.y > 1;
	}

	int cellCount(const tileTypeInfo& _type)
	{
		return std::max(1, _type.footprint.x * _type.footprint.y);
	}

	sf::Vector2i turnedSize(const tileTypeInfo& _type, int _turns)
	{
		return (_turns & 1) ? sf::Vector2i(_type.footprint.y, _type.footprint.x) : _type.footprint;
	}

	sf::Vector2i turnCell(sf::Vector2i _cell, sf::Vector2i _size, int _turns)
	{
		//a quarter turn clockwise on screen takes the x direction (LR) to y (LL), and y (LL) to -x (UL): (x, y) -> (-y, x),
		//shifted back into the footprint, which is now h x w
		for (int t = 0; t < (((_turns % 4) + 4) % 4); t++)
		{
			_cell = sf::Vector2i(_size.y - 1 - _cell.y, _cell.x);
			_size = sf::Vector2i(_size.y, _size.x);
		}
		return _cell;
	}

	int turnSide(int _side, int _turns)
	{
		static const int next[4] = { 1, 3, 0, 2 };   //UL -> UR, UR -> LR, LL -> UL, LR -> LL
		for (int t = 0; t < (((_turns % 4) + 4) % 4); t++)
			_side = next[_side & 3];
		return _side;
	}

	std::vector<portInfo> turnedPorts(const tileTypeInfo& _type, int _turns)
	{
		std::vector<portInfo> turned = _type.ports;
		for (portInfo& port : turned)
		{
			port.cell = turnCell(port.cell, _type.footprint, _turns);
			port.side = turnSide(port.side, _turns);
		}
		return turned;
	}

	sf::Vector2i originFor(sf::Vector2i _pointed, sf::Vector2i _turnedSize)
	{
		return _pointed - sf::Vector2i((_turnedSize.x - 1) / 2, (_turnedSize.y - 1) / 2);
	}

	sf::Vector2i mainCell(const tileTypeInfo& _type, int _turns, portInfo::kind _kind)
	{
		for (const portInfo& port : _type.ports)
		{
			if (port.carries == _kind)
				return turnCell(port.cell, _type.footprint, _turns);
		}
		return sf::Vector2i(-1, -1);
	}

	sf::Vector2i step(int _side)
	{
		static const sf::Vector2i steps[4] = { sf::Vector2i(-1, 0), sf::Vector2i(0, -1), sf::Vector2i(0, 1), sf::Vector2i(1, 0) };
		return steps[_side & 3];
	}

	int portSidesOnTile(const tileObject& _object, portInfo::kind _kind)
	{
		if (!_object.type || _object.type->ports.empty())
			return 0;
		int sides = 0;
		for (const portInfo& port : turnedPorts(*_object.type, _object.turns()))
		{
			if (port.carries == _kind && port.cell == _object.cell)
				sides |= 1 << port.side;
		}
		return sides;
	}

	bool isMainTile(const tileObject& _object, portInfo::kind _kind)
	{
		if (!_object.type || !isMultiTile(*_object.type))
			return true;
		return mainCell(*_object.type, _object.turns(), _kind) == _object.cell;
	}
}

std::uint64_t catalogueGeneration()
{
	return catalogueStore::generation();
}

const std::deque<tileTypeInfo>& tileCatalogue()
{
	return catalogueStore::types();
}

const tileTypeInfo* findTileType(const std::string& _key)
{
	for (const tileTypeInfo& type : tileCatalogue())
	{
		if (type.key == _key)
			return &type;
	}
	return nullptr;
}

std::string blockedMessageFor(const std::string& _key)
{
	const tileTypeInfo* type = findTileType(_key);
	return type ? type->blockedMessage : "cannot place that here";
}

const std::deque<materialInfo>& materialCatalogue()
{
	return catalogueStore::materials();
}

const materialInfo* findMaterial(const std::string& _key)
{
	for (const materialInfo& material : materialCatalogue())
	{
		if (material.key == _key)
			return &material;
	}
	return nullptr;
}

const char* phaseName(materialInfo::phase _phase)
{
	switch (_phase)
	{
	case materialInfo::phase::liquid: return "liquid";
	case materialInfo::phase::gas: return "gas";
	default: return "solid";
	}
}

namespace fluid
{
	fluidProperties pipe(double _conductance, double _litres)
	{
		fluidProperties f;
		f.carriesLiquid = true;
		f.conductance = _conductance;
		f.volume = _litres;
		return f;
	}

	fluidProperties riser(double _conductance, double _litres)
	{
		fluidProperties f = pipe(_conductance, _litres);
		f.vertical = true;
		return f;
	}

	fluidProperties pump(int _side, double _headKPa, double _flowLitresPerSecond, double _conductance, double _litres)
	{
		fluidProperties f = pipe(_conductance, _litres);
		f.pump = true;
		f.pumpSide = _side;
		f.pumpHead = _headKPa;
		f.pumpFlow = _flowLitresPerSecond;
		return f;
	}

	fluidProperties tank(double _capacityLitres, double _conductance)
	{
		fluidProperties f = pipe(_conductance, 0.0);
		f.tank = true;
		f.capacity = _capacityLitres;
		return f;
	}

	fluidProperties exchanger(double _wattsPerKelvin, double _conductance, double _litres)
	{
		fluidProperties f = pipe(_conductance, _litres);
		f.exchange = _wattsPerKelvin;
		return f;
	}

	fluidProperties radiator(double _areaM2, double _emissivity, double _conductance, double _litres)
	{
		fluidProperties f = pipe(_conductance, _litres);
		f.radiatingArea = _areaM2;
		f.emissivity = _emissivity;
		return f;
	}

	double radiatedWatts(const fluidProperties& _f, double _celsius)
	{
		const double t = std::max(_celsius + 273.15, 0.0), sink = spaceSinkCelsius + 273.15;
		return _f.emissivity * stefanBoltzmann * _f.radiatingArea * (t * t * t * t - sink * sink * sink * sink);
	}

	std::string summary(const fluidProperties& _f)
	{
		static const char* sides[4] = { "upper left", "upper right", "lower left", "lower right" };
		char text[160];
		if (!_f.carriesLiquid)
			return "";
		if (_f.tank)
			snprintf(text, sizeof(text), "Tank: holds %.0f L (placed full)", _f.capacity);
		else if (_f.pump)
			snprintf(text, sizeof(text), "Pump: %.0f kPa, up to %.1f L/s,\n%s%s", _f.pumpHead, _f.pumpFlow,
				_f.pumpSide >= 0 && _f.pumpSide < 4 ? "pushes " : "from its in port to its out port",
				_f.pumpSide >= 0 && _f.pumpSide < 4 ? sides[_f.pumpSide] : "");
		else if (_f.exchange > 0.0)
			snprintf(text, sizeof(text), "Heat exchanger: %.0f W/K\nbetween the air and the liquid", _f.exchange);
		else if (_f.radiatingArea > 0.0)
			snprintf(text, sizeof(text), "Radiator: %g m2, emissivity %g:\n%.1f kW to space at 20 C, %.1f kW at 60 C", _f.radiatingArea, _f.emissivity,
				radiatedWatts(_f, 20.0) / 1000.0, radiatedWatts(_f, 60.0) / 1000.0);
		else if (_f.vertical)
			snprintf(text, sizeof(text), "Pipe riser: links to a pipe riser\non the level above or below");
		else
			snprintf(text, sizeof(text), "Pipe: holds %.0f L of liquid", _f.volume);
		return text;
	}
}

namespace thermal
{
	thermalProperties mass(double _joulesPerKelvin)
	{
		thermalProperties t;
		t.heatCapacity = _joulesPerKelvin;
		return t;
	}

	thermalProperties wall(double _wattsPerKelvin)
	{
		thermalProperties t;
		t.wallConductance = _wattsPerKelvin;
		return t;
	}

	thermalProperties duct(double _wattsPerKelvin)
	{
		thermalProperties t;
		t.ductConductance = _wattsPerKelvin;
		return t;
	}

	std::string summary(const thermalProperties& _t)
	{
		if (_t.ductConductance <= 0.0)
			return "";
		char text[120];
		snprintf(text, sizeof(text), "Air duct: %.0f W/K to an air duct\nriser on the level above or below", _t.ductConductance);
		return text;
	}
}

namespace recipes
{
	namespace
	{
		double amountIn(const std::vector<tileObject::heldMaterial>& _list, const std::string& _material)
		{
			for (const tileObject::heldMaterial& held : _list)
				if (held.material == _material)
					return held.amount;
			return 0.0;
		}
		//adds (or with a negative _amount, takes) some of a material in a buffer, dropping it once it is all gone
		void change(std::vector<tileObject::heldMaterial>& _list, const std::string& _material, double _amount)
		{
			for (size_t i = 0; i < _list.size(); i++)
			{
				if (_list[i].material != _material)
					continue;
				_list[i].amount += _amount;
				if (_list[i].amount <= 1e-9)
					_list.erase(_list.begin() + i);
				return;
			}
			if (_amount > 1e-12)
				_list.push_back({ _material, _amount });
		}
		std::string nameOf(const std::string& _material)
		{
			const materialInfo* material = findMaterial(_material);
			return material ? material->name : _material;
		}
	}

	bool isProcessor(const tileTypeInfo& _type)
	{
		return !_type.recipes.empty();
	}

	int currentIndex(const tileObject& _machine)
	{
		const tileObject& m = _machine.machine();
		if (!m.type || m.type->recipes.empty())
			return -1;
		return std::clamp(int(m.getSetting(settingKey)), 0, int(m.type->recipes.size()) - 1);
	}

	const recipeInfo* current(const tileObject& _machine)
	{
		const int index = currentIndex(_machine);
		return index >= 0 ? &_machine.machine().type->recipes[index] : nullptr;
	}

	double inputPerCycle(const recipeInfo& _recipe, const std::string& _material)
	{
		for (const auto& [key, amount] : _recipe.inputs)
			if (key == _material)
				return amount;
		return 0.0;
	}

	double outputPerCycle(const recipeInfo& _recipe, const std::string& _material)
	{
		for (const auto& [key, amount] : _recipe.outputs)
			if (key == _material)
				return amount;
		return 0.0;
	}

	double inputRoom(const tileObject& _machine, const materialInfo& _material)
	{
		const recipeInfo* recipe = current(_machine);
		if (!recipe)
			return 0.0;
		return std::max(0.0, bufferCycles * inputPerCycle(*recipe, _material.key) - amountIn(_machine.contents(), _material.key));
	}

	double putInput(tileObject& _machine, const materialInfo& _material, double _amount)
	{
		const double amount = std::min(std::max(_amount, 0.0), inputRoom(_machine, _material));
		if (amount > 0.0)
			change(_machine.contentsToChange(), _material.key, amount);
		return amount;
	}

	double takeOutput(tileObject& _machine, const std::string& _material, double _amount)
	{
		const double amount = std::min(std::max(_amount, 0.0), amountIn(_machine.outputs(), _material));
		if (amount > 0.0)
			change(_machine.outputsToChange(), _material, -amount);
		return amount;
	}

	std::string missingInput(const tileObject& _machine, const recipeInfo& _recipe)
	{
		for (const auto& [key, amount] : _recipe.inputs)
			if (amountIn(_machine.contents(), key) + 1e-9 < amount)
				return key;
		return "";
	}

	std::string fullOutput(const tileObject& _machine, const recipeInfo& _recipe)
	{
		for (const auto& [key, amount] : _recipe.outputs)
			if (amountIn(_machine.outputs(), key) + amount > bufferCycles * amount + 1e-9)
				return key;
		return "";
	}

	void runCycle(tileObject& _machine, const recipeInfo& _recipe)
	{
		for (const auto& [key, amount] : _recipe.inputs)
			change(_machine.contentsToChange(), key, -amount);
		for (const auto& [key, amount] : _recipe.outputs)
			change(_machine.outputsToChange(), key, amount);
	}

	void choose(tileObject& _machine, int _recipe)
	{
		tileObject& m = _machine.machine();
		if (!m.type || _recipe < 0 || _recipe >= int(m.type->recipes.size()) || _recipe == currentIndex(m))
			return;
		m.setSetting(settingKey, double(_recipe));
		m.progress = 0.0;
		const recipeInfo& recipe = m.type->recipes[_recipe];
		std::vector<tileObject::heldMaterial>& inputs = m.contentsToChange();
		for (size_t i = 0; i < inputs.size();)
		{
			if (inputPerCycle(recipe, inputs[i].material) > 0.0)
			{
				i++;
				continue;
			}
			change(m.outputsToChange(), inputs[i].material, inputs[i].amount);   //not used now: it goes back out
			inputs.erase(inputs.begin() + i);
		}
	}

	std::string describe(const std::vector<std::pair<std::string, double>>& _amounts)
	{
		std::string text;
		for (const auto& [key, amount] : _amounts)
		{
			const materialInfo* material = findMaterial(key);
			char part[96];
			snprintf(part, sizeof(part), "%g %s %s", amount, material && material->state == materialInfo::phase::liquid ? "L" : "kg", nameOf(key).c_str());
			text += (text.empty() ? "" : " + ") + std::string(part);
		}
		return text.empty() ? "nothing" : text;
	}

	std::string summary(const tileTypeInfo& _type)
	{
		if (_type.recipes.empty())
			return "";
		std::string text = "Makes:";
		for (const recipeInfo& recipe : _type.recipes)
		{
			char part[64];
			snprintf(part, sizeof(part), "%s (%g s)", recipe.name.c_str(), recipe.seconds);
			text += (text == "Makes:" ? " " : ", ") + std::string(part);
		}
		return text;
	}
}

namespace items
{
	std::string summary(const itemsProperties& _i)
	{
		if (!_i.tube)
			return "";
		char text[120];
		snprintf(text, sizeof(text), _i.vertical ? "Tube riser: %.1f kg/s, links to a tube\nriser on the level above or below" : "Item tube: %.1f kg/s", _i.rate);
		return text;
	}
}

namespace storage
{
	std::string summary(const storageProperties& _s)
	{
		if (!_s.isStore())
			return "";
		char text[120];
		if (_s.kg > 0.0)
			snprintf(text, sizeof(text), "Storage: %.0f L (%.1f m3),\nup to %.0f kg", _s.litres, _s.litres / 1000.0, _s.kg);
		else
			snprintf(text, sizeof(text), "Storage: %.0f L (%.1f m3)", _s.litres, _s.litres / 1000.0);
		return text;
	}

	bool isStore(const tileObject& _object)
	{
		return _object.type && _object.type->storage.isStore();
	}

	double usedLitres(const tileObject& _store)
	{
		double litres = 0.0;
		for (const tileObject::heldMaterial& held : _store.contents())
		{
			const materialInfo* material = findMaterial(held.material);
			litres += material ? held.amount / material->kilogramsPerLitre() : 0.0;
		}
		return litres;
	}

	double usedKg(const tileObject& _store)
	{
		double kg = 0.0;
		for (const tileObject::heldMaterial& held : _store.contents())
			kg += held.amount;
		return kg;
	}

	double amountOf(const tileObject& _store, const std::string& _material)
	{
		return _store.amountOf(_material);
	}

	double room(const tileObject& _store, const materialInfo& _material)
	{
		if (!isStore(_store) || _material.state != materialInfo::phase::solid || _material.density <= 0.0)
			return 0.0;
		const storageProperties& s = _store.type->storage;
		double kg = std::max(0.0, s.litres - usedLitres(_store)) * _material.kilogramsPerLitre();
		if (s.kg > 0.0)
			kg = std::min(kg, std::max(0.0, s.kg - usedKg(_store)));
		return kg;
	}

	double put(tileObject& _store, const materialInfo& _material, double _kg)
	{
		const double amount = std::min(std::max(_kg, 0.0), room(_store, _material));
		if (amount <= 0.0)
			return 0.0;
		std::vector<tileObject::heldMaterial>& held = _store.contentsToChange();
		for (tileObject::heldMaterial& h : held)
		{
			if (h.material == _material.key)
			{
				h.amount += amount;
				return amount;
			}
		}
		held.push_back({ _material.key, amount });
		return amount;
	}

	double take(tileObject& _store, const std::string& _material, double _kg)
	{
		std::vector<tileObject::heldMaterial>& held = _store.contentsToChange();
		for (size_t i = 0; i < held.size(); i++)
		{
			if (held[i].material != _material)
				continue;
			const double amount = std::min(std::max(_kg, 0.0), held[i].amount);
			held[i].amount -= amount;
			if (held[i].amount <= 1e-9)
				held.erase(held.begin() + i);
			return amount;
		}
		return 0.0;
	}

	void empty(tileObject& _store)
	{
		_store.contentsToChange().clear();
	}
}

const itemSetting* findSetting(const tileTypeInfo& _type, const std::string& _key)
{
	for (const itemSetting& setting : _type.settings)
	{
		if (setting.key == _key)
			return &setting;
	}
	return nullptr;
}

namespace settings
{
	itemSetting toggle(const std::string& _key, const std::string& _label, bool _defaultOn)
	{
		itemSetting s;
		s.key = _key;
		s.label = _label;
		s.type = itemSetting::kind::toggle;
		s.defaultValue = _defaultOn ? 1.0 : 0.0;
		s.minimum = 0.0;
		s.maximum = 1.0;
		return s;
	}

	itemSetting number(const std::string& _key, const std::string& _label, double _default, double _minimum, double _maximum,
		double _step, const std::string& _unit)
	{
		itemSetting s;
		s.key = _key;
		s.label = _label;
		s.type = itemSetting::kind::number;
		s.defaultValue = _default;
		s.minimum = _minimum;
		s.maximum = _maximum;
		s.step = _step;
		s.unit = _unit;
		return s;
	}
}

namespace electrical
{
	electricalProperties conductor(double _ohmsPerTile, double _henriesPerTile, double _ratedAmps)
	{
		electricalProperties p;
		p.conductor = true;
		p.resistance = _ohmsPerTile;
		p.inductance = _henriesPerTile;
		p.ratedCurrent = _ratedAmps;
		return p;
	}

	electricalProperties riser(double _ohms, double _henries, double _ratedAmps)
	{
		electricalProperties p = conductor(_ohms, _henries, _ratedAmps);
		p.vertical = true;
		return p;
	}

	electricalProperties generator(double _volts, double _hertz, double _voltAmps, double _internalOhms, double _internalHenries,
		double _efficiency)
	{
		electricalProperties p;
		p.source = true;
		p.emf = _volts;
		p.frequency = _hertz;
		p.ratedPower = _voltAmps;
		p.sourceResistance = _internalOhms;
		p.sourceInductance = _internalHenries;
		p.efficiency = _efficiency;
		return p;
	}

	electricalProperties load(double _ohms, double _henries, double _farads)
	{
		electricalProperties p;
		p.load = true;
		p.loadResistance = _ohms;
		p.loadInductance = _henries;
		p.loadCapacitance = _farads;
		return p;
	}

	electricalProperties resistiveLoad(double _watts, double _volts)
	{
		//P = V^2 / R
		return load(_volts * _volts / _watts);
	}

	double ratedLoadWatts(const electricalProperties& _p)
	{
		if (!_p.load)
			return 0.0;
		//S = V^2 / conj(Z), so the real part P = V^2 R / |Z|^2
		const double w = 2.0 * 3.14159265358979 * nominalHertz;
		double x = w * _p.loadInductance;
		if (_p.loadCapacitance > 0.0)
			x -= 1.0 / (w * _p.loadCapacitance);
		const double z2 = _p.loadResistance * _p.loadResistance + x * x;
		return z2 > 0.0 ? nominalVolts * nominalVolts * _p.loadResistance / z2 : 0.0;
	}

	std::string summary(const electricalProperties& _p)
	{
		char text[160];
		std::string out;
		if (_p.source)
		{
			snprintf(text, sizeof(text), "Generator: %.0f V, %.0f Hz, %.1f kVA\n", _p.emf, _p.frequency, _p.ratedPower / 1000.0);
			out += text;
		}
		if (_p.load)
		{
			const double w = 2.0 * 3.14159265358979 * nominalHertz;
			double x = w * _p.loadInductance;
			if (_p.loadCapacitance > 0.0)
				x -= 1.0 / (w * _p.loadCapacitance);
			const double z2 = _p.loadResistance * _p.loadResistance + x * x;
			const double watts = ratedLoadWatts(_p);
			const double powerFactor = z2 > 0.0 ? _p.loadResistance / std::sqrt(z2) : 1.0;
			if (powerFactor > 0.999)
				snprintf(text, sizeof(text), "Load: %.0f W at %.0f V\n", watts, nominalVolts);
			else
				snprintf(text, sizeof(text), "Load: %.0f W at %.0f V, pf %.2f\n", watts, nominalVolts, powerFactor);
			out += text;
		}
		if (_p.conductor && _p.vertical)
		{
			snprintf(text, sizeof(text), "Cable riser: %.0f A, links to a cable\nriser on the level above or below\n", _p.ratedCurrent);
			out += text;
		}
		else if (_p.conductor)
		{
			if (_p.ratedCurrent > 0.0)
				snprintf(text, sizeof(text), "Cable: %.3f ohm per tile, %.0f A\n", _p.resistance, _p.ratedCurrent);
			else
				snprintf(text, sizeof(text), "Cable: %.3f ohm per tile\n", _p.resistance);
			out += text;
		}
		if (!out.empty())
			out.pop_back();   //no newline after the last line
		return out;
	}
}

namespace connections
{
	int sides(const tileTypeInfo& _type, const std::vector<double>& _settings)
	{
		for (size_t s = 0; s < _type.settings.size() && s < _settings.size(); s++)
		{
			if (_type.settings[s].key == settingKey)
				return int(_settings[s]) & all;
		}
		const bool line = _type.electrical.conductor || _type.items.tube || (_type.fluid.carriesLiquid && !_type.fluid.pump && !_type.fluid.tank && _type.fluid.exchange == 0.0);
		return line ? all : 0;
	}

	int opposite(int _side)
	{
		return 3 - _side;
	}

	int rotate(int _mask)
	{
		//clockwise on screen: UL (bit 0) -> UR (bit 1) -> LR (bit 3) -> LL (bit 2) -> UL
		int turned = 0;
		if (_mask & 1) turned |= 2;
		if (_mask & 2) turned |= 8;
		if (_mask & 8) turned |= 4;
		if (_mask & 4) turned |= 1;
		return turned;
	}

	std::string describe(int _mask)
	{
		if ((_mask & all) == all)
			return "all sides";
		if ((_mask & all) == 0)
			return "none";
		static const char* names[4] = { "upper left", "upper right", "lower left", "lower right" };
		std::string text;
		for (int side : { 0, 1, 3, 2 })   //clockwise, as R turns them
		{
			if (!(_mask & (1 << side)))
				continue;
			if (!text.empty())
				text += ", ";
			text += names[side];
		}
		return text;
	}

	const char* shapeName(shape _shape)
	{
		switch (_shape)
		{
		case shape::automatic: return "Auto";
		case shape::straight: return "Straight";
		case shape::corner: return "Corner";
		case shape::junction: return "T-junction";
		}
		return "?";
	}

	shape nextShape(shape _shape)
	{
		switch (_shape)
		{
		case shape::automatic: return shape::straight;
		case shape::straight: return shape::corner;
		case shape::corner: return shape::junction;
		default: return shape::automatic;
		}
	}

	int shapeMask(shape _shape, int _rotation)
	{
		//each shape's first rotation, turned clockwise _rotation times
		int mask = all;
		switch (_shape)
		{
		case shape::automatic: return all;
		case shape::straight: mask = 1 | 8; break;     //upper left to lower right
		case shape::corner: mask = 1 | 2; break;       //upper left and upper right
		case shape::junction: mask = all & ~8; break;  //every side but lower right
		}
		for (int r = 0; r < ((_rotation % 4) + 4) % 4; r++)
			mask = rotate(mask);
		return mask;
	}

	int rotations(shape _shape)
	{
		switch (_shape)
		{
		case shape::automatic: return 1;
		case shape::straight: return 2;
		default: return 4;
		}
	}
}

const rotationFamily* familyOf(const std::string& _key)
{
	for (const rotationFamily& family : catalogueStore::turns())
	{
		for (const std::string& key : family.keys)
		{
			if (key == _key)
				return &family;
		}
	}
	return nullptr;
}

std::string rotateKey(const std::string& _key, int _steps)
{
	const rotationFamily* family = familyOf(_key);
	if (!family)
		return _key;
	const int count = int(family->keys.size());
	for (int i = 0; i < count; i++)
	{
		if (family->keys[i] == _key)
			return family->keys[(((i + _steps) % count) + count) % count];
	}
	return _key;
}

const kindFamily* kindFamilyOf(const std::string& _key)
{
	for (const kindFamily& family : catalogueStore::kinds())
	{
		if (std::find(family.keys.begin(), family.keys.end(), _key) != family.keys.end())
			return &family;
	}
	return nullptr;
}

int kindIndex(const std::string& _key)
{
	const kindFamily* family = kindFamilyOf(_key);
	if (!family)
		return -1;
	return int(std::find(family->keys.begin(), family->keys.end(), _key) - family->keys.begin());
}
