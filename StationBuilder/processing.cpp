#include "processing.h"
#include "electricalNetwork.h"
#include "fluidNetwork.h"
#include "station.h"
#include "thermalNetwork.h"
#include "tileCatalogue.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <tuple>

const char* machineStatusName(machineStatus _status)
{
	switch (_status)
	{
	case machineStatus::off: return "off";
	case machineStatus::noRecipe: return "no recipe";
	case machineStatus::waiting: return "waiting";
	case machineStatus::outputFull: return "output full";
	case machineStatus::tooHot: return "too hot";
	case machineStatus::starting: return "starting";
	case machineStatus::noPower: return "no power";
	case machineStatus::lowPower: return "low power";
	case machineStatus::working: return "working";
	}
	return "?";
}

sf::Color machineStatusColour(machineStatus _status)
{
	switch (_status)
	{
	case machineStatus::working: return sf::Color(115, 230, 128);
	case machineStatus::lowPower:
	case machineStatus::starting:
	case machineStatus::outputFull: return sf::Color(255, 190, 50);
	case machineStatus::noPower:
	case machineStatus::tooHot: return sf::Color(255, 90, 90);
	case machineStatus::waiting: return sf::Color(150, 175, 215);
	default: return sf::Color(150, 150, 150);
	}
}

processingSystem::processingSystem(station& _station, electricalNetwork* _power, fluidNetwork* _liquid, const thermalNetwork* _heat)
	: stationRef(&_station), power(_power), liquids(_liquid), temperatures(_heat)
{
}

void processingSystem::update()
{
	refresh();
}

void processingSystem::refresh()
{
	if (built && stationRef->getRevision() == seenRevision)
		return;
	seenRevision = stationRef->getRevision();
	built = true;
	//what each machine was doing, by where it is, to carry over a rebuild (too hot, idle)
	std::map<std::tuple<int, int, int>, processorState> previous;
	for (const processorState& state : list)
		previous[{ state.level, state.origin.x, state.origin.y }] = state;
	list.clear();
	for (int l = 0; l < stationRef->getLayerListSize(); l++)
	{
		isometricLevel* level = stationRef->getIsoLevel(float(l));
		for (tile& t : level->levelTiles)
		{
			t.forEachObject([&](tileObject* _object) {
				if (!_object->type || _object->isPart() || !recipes::isProcessor(*_object->type))
					return;
				processorState state;
				state.machine = _object;
				state.level = l;
				state.origin = _object->origin;
				state.tiles = level->machineTiles(*_object);
				state.electrical = _object->type->electrical.load;
				const auto found = previous.find({ l, state.origin.x, state.origin.y });
				if (found != previous.end() && found->second.machine && found->second.machine->type == _object->type)
				{
					state.status = found->second.status;
					state.hot = found->second.hot;
					state.idle = found->second.idle;
				}
				list.push_back(state);
			});
		}
	}
}

const processorState* processingSystem::stateOf(const tileObject& _machine) const
{
	const tileObject* machine = &_machine.machine();
	for (const processorState& state : list)
		if (state.machine == machine)
			return &state;
	return nullptr;
}

void processingSystem::moveLiquids(processorState& _state, double _dt)
{
	_state.liquidIn = 0.0;
	_state.liquidOut = 0.0;
	if (!liquids)
		return;
	tileObject& machine = *_state.machine;
	const recipeInfo* recipe = recipes::current(machine);
	const double perPort = processingTuning::liquidRate * _dt;
	for (const portInfo& port : footprint::turnedPorts(*machine.type, machine.turns()))
	{
		if (port.carries != portInfo::kind::liquid)
			continue;
		const int node = liquids->network().nodeAt(_state.level, machine.origin + port.cell);
		if (node < 0)
			continue;
		//in: the liquids its recipe takes, as far as its input buffer has room
		if (port.flow != portInfo::direction::out && recipe)
		{
			double budget = perPort;
			for (const auto& [key, amount] : recipe->inputs)
			{
				const materialInfo* material = findMaterial(key);
				if (!material || material->state != materialInfo::phase::liquid || budget <= 0.0)
					continue;
				const double want = std::min(budget, recipes::inputRoom(machine, *material));
				const double got = want > 1e-9 ? liquids->takeLiquid(node, *material, want) : 0.0;
				recipes::putInput(machine, *material, got);
				budget -= got;
				_state.liquidIn += got / _dt;
			}
		}
		//out: any liquid in its output buffer, as far as the network's tanks take it
		if (port.flow != portInfo::direction::in)
		{
			double budget = perPort;
			std::vector<std::pair<std::string, double>> waiting;
			for (const tileObject::heldMaterial& held : machine.outputs())
				waiting.push_back({ held.material, held.amount });
			for (const auto& [key, amount] : waiting)
			{
				const materialInfo* material = findMaterial(key);
				if (!material || material->state != materialInfo::phase::liquid || budget <= 0.0)
					continue;
				const double put = liquids->putLiquid(node, *material, std::min(budget, amount));
				recipes::takeOutput(machine, key, put);
				budget -= put;
				_state.liquidOut += put / _dt;
			}
		}
	}
}

int processingSystem::findDevice(const processorState& _state) const
{
	if (!power || !_state.electrical)
		return -1;
	const tileObject& machine = *_state.machine;
	const tileTypeInfo& type = *machine.type;
	//its power comes in at its power port's tile (a one-tile machine: its own tile)
	sf::Vector2i at = machine.origin;
	if (footprint::isMultiTile(type))
		at = machine.origin + footprint::mainCell(type, machine.turns(), portInfo::kind::power);
	const int node = power->network().nodeAt(_state.level, at);
	if (node < 0)
		return -1;
	//its load is the same-numbered one among the loads of its type on that tile (both list a tile's objects in order)
	int before = 0;
	bool seen = false;
	isometricLevel* level = stationRef->getIsoLevel(float(_state.level));
	if (!level->isInLevel(at))
		return -1;
	level->levelTiles[level->coords2TileIndex(at.x, at.y)].forEachObject([&](tileObject* _object) {
		if (seen || _object->type != &type)
			return;
		if (&_object->machine() == &machine)
			seen = true;
		else
			before++;
	});
	for (int d : power->devicesOnNode(node))
	{
		const electricalDevice& device = power->devices()[d];
		if (!device.isSource && device.type == &type && before-- == 0)
			return d;
	}
	return -1;
}

void processingSystem::run(processorState& _state, double _dt)
{
	tileObject& machine = *_state.machine;
	const tileTypeInfo& type = *machine.type;
	const recipeInfo* recipe = recipes::current(machine);
	const bool wasIdle = _state.idle;
	_state.about.clear();
	_state.speed = 0.0;
	_state.heat = 0.0;

	//its air: the hottest of its tiles
	_state.temperature = std::numeric_limits<double>::quiet_NaN();
	if (temperatures)
	{
		for (const sf::Vector2i& at : _state.tiles)
		{
			const double t = temperatures->temperatureAt(_state.level, at);
			if (!std::isnan(t) && (std::isnan(_state.temperature) || t > _state.temperature))
				_state.temperature = t;
		}
	}
	if (type.maxTemperature > 0.0 && !std::isnan(_state.temperature))
	{
		if (_state.temperature > type.maxTemperature)
			_state.hot = true;
		else if (_state.temperature < type.maxTemperature - processingTuning::hotBand)
			_state.hot = false;
	}
	else
		_state.hot = false;

	//its power: the share of its rating its load is getting
	const int device = findDevice(_state);
	_state.powerFraction = 1.0;
	if (_state.electrical && power)
	{
		_state.powerFraction = 0.0;
		const double rated = electrical::ratedLoadWatts(type.electrical);
		if (device >= 0 && rated > 0.0)
			_state.powerFraction = std::clamp(power->devices()[device].power.real() / rated, 0.0, 1.0);
	}

	const bool switchedOff = findSetting(type, "on") && machine.getSetting("on") == 0.0;
	std::string missing = recipe ? recipes::missingInput(machine, *recipe) : "";
	std::string full = recipe ? recipes::fullOutput(machine, *recipe) : "";
	if (switchedOff)
		_state.status = machineStatus::off;
	else if (!recipe)
		_state.status = machineStatus::noRecipe;
	else if (_state.hot)
		_state.status = machineStatus::tooHot;
	else if (!missing.empty())
		_state.status = machineStatus::waiting;
	else if (!full.empty())
		_state.status = machineStatus::outputFull;
	else if (_state.electrical && power && wasIdle)
		_state.status = machineStatus::starting;   //its power comes back on from the next tick
	else if (_state.powerFraction < processingTuning::minimumPower)
		_state.status = machineStatus::noPower;
	else
	{
		_state.speed = _state.powerFraction;
		_state.status = _state.speed >= processingTuning::fullPower ? machineStatus::working : machineStatus::lowPower;
	}
	if (_state.status == machineStatus::waiting)
		_state.about = missing;
	if (_state.status == machineStatus::outputFull)
		_state.about = full;

	//work on the cycle; each finished one turns a cycle of inputs into a cycle of outputs
	if (_state.speed > 0.0 && recipe)
	{
		_state.heat = recipe->heat * _state.speed;
		machine.progress += _state.speed * _dt / std::max(recipe->seconds, 1e-6);
		while (machine.progress >= 1.0)
		{
			recipes::runCycle(machine, *recipe);
			machine.progress -= 1.0;
			missing = recipes::missingInput(machine, *recipe);
			full = recipes::fullOutput(machine, *recipe);
			if (!missing.empty() || !full.empty())
			{
				machine.progress = 0.0;
				break;
			}
		}
	}

	//nothing to do: its power goes off until it has
	_state.idle = _state.status == machineStatus::off || _state.status == machineStatus::noRecipe || _state.status == machineStatus::tooHot ||
		_state.status == machineStatus::waiting || _state.status == machineStatus::outputFull;
	if (power && device >= 0)
		power->setLoadIdle(power->devices()[device].key, _state.idle);
}

void processingSystem::tick(double _dt)
{
	refresh();
	if (_dt <= 0.0)
		return;
	if (liquids)
		liquids->update();
	for (processorState& state : list)
	{
		if (!state.machine || !state.machine->type)
			continue;
		moveLiquids(state, _dt);
		run(state, _dt);
	}
}

std::string processingSystem::describe(const processorState& _state)
{
	auto nameOf = [](const std::string& _key) {
		const materialInfo* material = findMaterial(_key);
		return material ? material->name : _key;
	};
	char text[160];
	switch (_state.status)
	{
	case machineStatus::waiting:
		return "Waiting for " + nameOf(_state.about);
	case machineStatus::outputFull:
		return "Output full: " + nameOf(_state.about) + " has to go somewhere";
	case machineStatus::tooHot:
		snprintf(text, sizeof(text), "Too hot (%.0f C, stops above %.0f C)", _state.temperature,
			_state.machine && _state.machine->type ? _state.machine->type->maxTemperature : 0.0);
		return text;
	case machineStatus::lowPower:
		snprintf(text, sizeof(text), "Low power: working at %.0f%%", _state.speed * 100.0);
		return text;
	case machineStatus::off: return "Switched off";
	case machineStatus::noRecipe: return "Nothing to make";
	case machineStatus::starting: return "Starting";
	case machineStatus::noPower: return "No power";
	case machineStatus::working: return "Working";
	}
	return "";
}

void processingSystem::forEachTileHeat(const std::function<void(int, sf::Vector2i, double)>& _add) const
{
	for (const processorState& state : list)
	{
		if (state.heat == 0.0 || state.tiles.empty())
			continue;
		for (const sf::Vector2i& at : state.tiles)
			_add(state.level, at, state.heat / double(state.tiles.size()));
	}
}
