#pragma once
#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include "simulation.h"
#include "tileHeatSource.h"

class station;
class tileObject;
class electricalNetwork;
class fluidNetwork;
class thermalNetwork;

namespace processingTuning
{
	const double liquidRate = 2.0;     //L/s each of a machine's liquid ports can take in or give out
	const double hotBand = 2.0;        //C: a machine stopped for being too hot starts again this far below its limit
	const double minimumPower = 0.05;  //below this share of its rated power a machine makes no progress at all
	const double fullPower = 0.9;      //from this share up it counts as working normally (below: low power)
}

//what a machine that makes things is doing
enum class machineStatus
{
	off,          //switched off (its "on" setting)
	noRecipe,     //it has nothing to make
	waiting,      //it hasn't a whole cycle of one of its inputs
	outputFull,   //no room in its output buffer for another cycle: something has to take its products
	tooHot,       //its air is hotter than its max_temperature
	starting,     //it has just asked for power again after being idle
	noPower,      //it wants power and gets (next to) none
	lowPower,     //working, slowly: it gets only part of its power
	working
};
const char* machineStatusName(machineStatus _status);
//green working, amber slowed or stuck on its output, red no power or too hot, blue-grey waiting, grey off
sf::Color machineStatusColour(machineStatus _status);

//one placed machine that makes things, as of the last tick
struct processorState
{
	tileObject* machine = nullptr;
	int level = 0;
	sf::Vector2i origin;                //its upper tile
	std::vector<sf::Vector2i> tiles;    //every tile it covers
	machineStatus status = machineStatus::noRecipe;
	std::string about;                  //the material it waits for (waiting) or has no room for (outputFull), else ""
	double powerFraction = 1.0;         //share of its rated power it gets (1 for a machine that needs none)
	double speed = 0.0;                 //how fast its cycle runs, 0..1 of full speed
	double heat = 0.0;                  //W its recipe gives off right now (its power's heat is the electrical side's)
	double temperature = 0.0;           //C, the hottest air on its tiles (NaN with no air)
	double liquidIn = 0.0;              //L/s taken in through its liquid ports this tick
	double liquidOut = 0.0;             //L/s given out through them
	bool hot = false;                   //stopped for heat (it starts again hotBand below its limit)
	bool idle = false;                  //its power is switched off while it has nothing to do
	bool electrical = false;            //it uses power
};

//machines that make things, as a simulation system. each tick, for each placed machine with recipes: liquids come in
//and go out through its liquid ports (from and to the tanks on that pipe network), then it works on its recipe if it has
//a cycle of every input and room for a cycle of every output, at the speed its power allows. solids reach its buffers
//through the item tubes (logistics.h). an idle machine switches its power off, so it costs nothing while it waits.
//add it to the simulation after the electrical, liquid and item systems (it reads this tick's power), and to the
//thermal simulation as a heat source
class processingSystem : public simulationSystem, public tileHeatSource
{
public:
	//any of the networks may be nullptr: no power network means full speed, no liquids means no liquid ports work, no
	//heat means it never gets too hot
	processingSystem(station& _station, electricalNetwork* _power, fluidNetwork* _liquid, const thermalNetwork* _heat);

	void tick(double _dt) override;
	//catch up with station changes now, without any time passing
	void update();

	const std::vector<processorState>& machines() const { return list; }
	//the state of a placed machine (or any of its parts), nullptr if it isn't one that makes things
	const processorState* stateOf(const tileObject& _machine) const;
	//"Waiting for Iron ore", "Working", "Low power (40%)"...
	static std::string describe(const processorState& _state);

	void forEachTileHeat(const std::function<void(int, sf::Vector2i, double)>& _add) const override;

private:
	void refresh();
	void moveLiquids(processorState& _state, double _dt);
	//which of the electrical network's devices is its load, -1 if none
	int findDevice(const processorState& _state) const;
	void run(processorState& _state, double _dt);

	station* stationRef;
	electricalNetwork* power;
	fluidNetwork* liquids;
	const thermalNetwork* temperatures;
	std::uint64_t seenRevision = 0;
	bool built = false;
	std::vector<processorState> list;
};
