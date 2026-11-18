#pragma once
#include <SFML/Graphics.hpp>

//the GUI as a glowing terminal screen: it is drawn into a texture of its own instead of straight onto the window, then
//laid over the game with a soft glow (a blurred copy added on top, so it lights the scene a little round its edges) and
//faint scanlines. the game world itself is untouched. when it first comes on, the picture is curved like old glass and
//fades up, flattening out over about a second and a half; after that it stays flat (so clicks land where they look
//to). a machine without shaders just draws the GUI as it is
class screenEffects
{
public:
	bool available();             //shaders work here
	bool enabled = true;          //F8 in the game
	float glow = 0.55f;           //how strong the glow is
	float scanlines = 0.10f;      //how much every other line is darkened

	//where to draw the GUI this frame (ImGui::SFML::Render(target)), cleared: sized to the window
	sf::RenderTarget& begin(const sf::RenderWindow& _window);
	//lays it over the window. _scale is the GUI scale (thicker scanlines on a bigger screen)
	void present(sf::RenderWindow& _window, float _scale);
	//starts the power-on (it also runs on the first frame)
	void powerOn() { started = false; }

private:
	bool load();

	sf::RenderTexture gui;        //the GUI, full size
	sf::RenderTexture glowA;      //half size: the glow, blurred back and forth between these two
	sf::RenderTexture glowB;
	sf::Shader blur;
	sf::Shader composite;
	bool loaded = false;
	bool failed = false;
	bool started = false;
	sf::Clock sincePowerOn;
	sf::Vector2u size;
};
