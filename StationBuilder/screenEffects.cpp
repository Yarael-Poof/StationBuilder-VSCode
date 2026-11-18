#include "screenEffects.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
	//a 9-tap gaussian along one direction, in 5 lookups (the texture is smooth, so each lookup between two texels
	//averages them)
	const char* const blurShader = R"(
uniform sampler2D texture;
uniform vec2 direction;
void main()
{
	vec2 uv = gl_TexCoord[0].xy;
	vec4 sum = texture2D(texture, uv) * 0.2270270270;
	sum += texture2D(texture, uv + direction * 1.3846153846) * 0.3162162162;
	sum += texture2D(texture, uv - direction * 1.3846153846) * 0.3162162162;
	sum += texture2D(texture, uv + direction * 3.2307692308) * 0.0702702703;
	sum += texture2D(texture, uv - direction * 3.2307692308) * 0.0702702703;
	gl_FragColor = sum;
}
)";

	//the GUI over the scene: its colours are premultiplied (ImGui draws onto clear black), its alpha squared (ImGui
	//blends alpha as it does colour), so the square root brings back what it covers. the glow adds light without
	//covering anything. every other line (in screen pixels) is a little darker. curve bends the picture like old glass,
	//fade brings it up from black
	const char* const compositeShader = R"(
uniform sampler2D texture;
uniform sampler2D glowTexture;
uniform float glow;
uniform float scan;
uniform float linePeriod;
uniform float curve;
uniform float fade;
void main()
{
	vec2 c = gl_TexCoord[0].xy - vec2(0.5);
	vec2 uv = vec2(0.5) + c * (1.0 + curve * dot(c, c));
	if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0)
	{
		gl_FragColor = vec4(0.0);
		return;
	}
	vec4 g = texture2D(texture, uv);
	vec4 light = texture2D(glowTexture, uv);
	vec3 colour = g.rgb + light.rgb * glow;
	float line = mod(floor(gl_FragCoord.y / linePeriod), 2.0);
	colour *= 1.0 - scan * line;
	gl_FragColor = vec4(colour, sqrt(clamp(g.a, 0.0, 1.0))) * fade;
}
)";

	const float powerOnSeconds = 1.5f;
	const float startCurve = 0.32f;
}

bool screenEffects::available()
{
	return sf::Shader::isAvailable() && !failed;
}

bool screenEffects::load()
{
	if (loaded || failed)
		return loaded;
	if (!sf::Shader::isAvailable() || !blur.loadFromMemory(blurShader, sf::Shader::Fragment) ||
		!composite.loadFromMemory(compositeShader, sf::Shader::Fragment))
	{
		std::cout << "[GUI] screen effects unavailable: the GUI is drawn plain\n";
		failed = true;
		return false;
	}
	loaded = true;
	return true;
}

sf::RenderTarget& screenEffects::begin(const sf::RenderWindow& _window)
{
	load();
	const sf::Vector2u windowSize = _window.getSize();
	if (windowSize != size)
	{
		size = windowSize;
		gui.create(std::max(size.x, 1u), std::max(size.y, 1u));
		gui.setSmooth(true);
		glowA.create(std::max(size.x / 2, 1u), std::max(size.y / 2, 1u));
		glowA.setSmooth(true);
		glowB.create(std::max(size.x / 2, 1u), std::max(size.y / 2, 1u));
		glowB.setSmooth(true);
	}
	gui.clear(sf::Color::Transparent);
	return gui;
}

void screenEffects::present(sf::RenderWindow& _window, float _scale)
{
	gui.display();
	if (!started)
	{
		started = true;
		sincePowerOn.restart();
	}
	const sf::BlendMode premultiplied(sf::BlendMode::One, sf::BlendMode::OneMinusSrcAlpha);

	//the glow: the GUI at half size, blurred across and down, twice
	sf::Sprite small(gui.getTexture());
	small.setScale(0.5f, 0.5f);
	glowA.clear(sf::Color::Transparent);
	glowA.draw(small, sf::RenderStates(sf::BlendNone));
	glowA.display();
	const sf::Vector2f texel(1.f / float(glowA.getSize().x), 1.f / float(glowA.getSize().y));
	blur.setUniform("texture", sf::Shader::CurrentTexture);
	for (int pass = 0; pass < 2; pass++)
	{
		sf::RenderStates across(sf::BlendNone);
		across.shader = &blur;
		blur.setUniform("direction", sf::Glsl::Vec2(texel.x * (1.f + pass), 0.f));
		glowB.clear(sf::Color::Transparent);
		glowB.draw(sf::Sprite(glowA.getTexture()), across);
		glowB.display();
		blur.setUniform("direction", sf::Glsl::Vec2(0.f, texel.y * (1.f + pass)));
		glowA.clear(sf::Color::Transparent);
		glowA.draw(sf::Sprite(glowB.getTexture()), across);
		glowA.display();
	}

	//the power-on: curved and dim at first, flat and full within a second and a half
	const float t = std::clamp(sincePowerOn.getElapsedTime().asSeconds() / powerOnSeconds, 0.f, 1.f);
	const float settle = 1.f - (1.f - t) * (1.f - t) * (1.f - t);   //eases out
	composite.setUniform("texture", sf::Shader::CurrentTexture);
	composite.setUniform("glowTexture", glowA.getTexture());
	composite.setUniform("glow", glow);
	composite.setUniform("scan", scanlines);
	composite.setUniform("linePeriod", std::max(1.f, std::round(_scale)));
	composite.setUniform("curve", startCurve * (1.f - settle));
	composite.setUniform("fade", std::clamp(t * 3.f, 0.f, 1.f));

	//over the window, in window pixels whatever the game's view is
	const sf::View view = _window.getView();
	_window.setView(sf::View(sf::FloatRect(0.f, 0.f, float(size.x), float(size.y))));
	sf::RenderStates states(premultiplied);
	states.shader = &composite;
	_window.draw(sf::Sprite(gui.getTexture()), states);
	_window.setView(view);
}
