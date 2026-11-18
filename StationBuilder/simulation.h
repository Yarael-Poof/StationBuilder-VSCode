#pragma once
#include <cstdint>
#include <vector>

//something that advances with the simulation clock: the electrical network now, thermal and liquid later.
//each system is stepped once per tick with the same fixed amount of game time.
class simulationSystem
{
public:
	virtual ~simulationSystem() = default;
	//advance this system by one tick of _dt seconds of game time
	virtual void tick(double _dt) = 0;
};

//the fixed-step simulation clock. the frame rate can change from moment to moment, but the simulation
//always advances in equal ticks (10 per second of game time), so its results don't depend on the frame rate.
//call update() once per frame with the real time that frame took; it runs as many ticks as are due.
//at 2x or 4x speed, game time runs that much faster than real time, so more ticks run per real second.
class simulationManager
{
public:
	static constexpr int ticksPerSecond = 10;                 //ticks per second of game time
	static constexpr double tickLength = 1.0 / ticksPerSecond; //seconds of game time per tick
	static constexpr int maxTicksPerFrame = 8;                 //catch-up cap, so a slow frame can't snowball
	static constexpr double maxFrameSeconds = 0.25;            //longer frames (window dragged, breakpoint) count as this

	//systems are stepped in the order they were added. the manager does not own them
	void addSystem(simulationSystem* _system);
	void removeSystem(simulationSystem* _system);

	//call once per frame with the real seconds since the last frame. returns how many ticks ran
	int update(double _realFrameSeconds);
	//run exactly one tick now, even while paused (for stepping through while debugging)
	void step();

	void setPaused(bool _paused);
	void togglePaused();
	bool isPaused() const;
	//game seconds per real second: 1, 2 or 4. anything else is ignored
	void setSpeed(int _multiplier);
	int getSpeed() const;

	std::uint64_t getTickCount() const;
	//seconds of game time simulated so far (tick count x tick length)
	double getSimTime() const;
	//true if the last update hit the catch-up cap, so the simulation is running slower than asked
	bool isFallingBehind() const;

private:
	void runTick();

	std::vector<simulationSystem*> systems;
	double accumulator = 0.0;   //game time waiting to be simulated, always less than one tick after update()
	std::uint64_t tickCount = 0;
	int speed = 1;
	bool paused = false;
	bool fallingBehind = false;
};
