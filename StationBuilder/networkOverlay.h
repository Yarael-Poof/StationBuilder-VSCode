#pragma once
#include <SFML/Graphics/RenderWindow.hpp>

class tileNetwork;
class isometricLevel;

//debug view of a tileNetwork on one level: a line along every link and a dot on every node, coloured by
//network (island) so separate networks are easy to tell apart. bigger dots are tiles with a device on them
//(generator, light...). a device with no cable to connect through gets a grey ring instead.
//draws with ImGui's background draw list (over the level, under the GUI): call between ImGui::SFML::Update and Render.
//works for any medium; the player-facing electrical view is in powerView.h
void drawNetworkDebug(const tileNetwork& _network, int _level, isometricLevel* _isoLevel, const sf::RenderWindow& _window);
