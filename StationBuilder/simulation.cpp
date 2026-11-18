#include "simulation.h"
#include <algorithm>

void simulationManager::addSystem(simulationSystem* _system)
{
	if (_system && std::find(systems.begin(), systems.end(), _system) == systems.end())
		systems.push_back(_system);
}

void simulationManager::removeSystem(simulationSystem* _system)
{
	systems.erase(std::remove(systems.begin(), systems.end(), _system), systems.end());
}

int simulationManager::update(double _realFrameSeconds)
{
	fallingBehind = false;
	if (paused)
		return 0;

	//a stalled frame (the window being dragged, a breakpoint) must not turn into a burst of ticks
	const double frameSeconds = std::clamp(_realFrameSeconds, 0.0, maxFrameSeconds);
	accumulator += frameSeconds * speed;

	int ticksRun = 0;
	while (accumulator >= tickLength)
	{
		if (ticksRun == maxTicksPerFrame)
		{
			//can't keep up: drop the backlog so the simulation slows down instead of snowballing
			fallingBehind = true;
			accumulator = 0.0;
			break;
		}
		runTick();
		accumulator -= tickLength;
		ticksRun++;
	}
	return ticksRun;
}

void simulationManager::step()
{
	runTick();
}

void simulationManager::runTick()
{
	for (simulationSystem* system : systems)
		system->tick(tickLength);
	tickCount++;
}

void simulationManager::setPaused(bool _paused)
{
	paused = _paused;
}

void simulationManager::togglePaused()
{
	paused = !paused;
}

bool simulationManager::isPaused() const
{
	return paused;
}

void simulationManager::setSpeed(int _multiplier)
{
	if (_multiplier == 1 || _multiplier == 2 || _multiplier == 4)
		speed = _multiplier;
}

int simulationManager::getSpeed() const
{
	return speed;
}

std::uint64_t simulationManager::getTickCount() const
{
	return tickCount;
}

double simulationManager::getSimTime() const
{
	return double(tickCount) * tickLength;
}

bool simulationManager::isFallingBehind() const
{
	return fallingBehind;
}
