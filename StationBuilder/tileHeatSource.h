#pragma once
#include <SFML/System/Vector2.hpp>
#include <functional>

//anything that puts heat into tiles' air, or takes it out: the electrical network (devices, cables, generators'
//waste heat), the fluid network (heat exchangers)... the thermal simulation asks each of them every tick
class tileHeatSource
{
public:
	virtual ~tileHeatSource() = default;
	//calls _add(level, tile, watts) for every tile it is heating right now (negative watts: cooling it)
	virtual void forEachTileHeat(const std::function<void(int, sf::Vector2i, double)>& _add) const = 0;
};
