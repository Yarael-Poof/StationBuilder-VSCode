#include "navView.h"
#include "world.h"
#include "uiTheme.h"
#include "tileCatalogue.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	const double pi = 3.14159265358979323846;
	const double minPixelsPerMetre = 0.02;
	const double maxPixelsPerMetre = 40.0;
	const float pickRadius = 14.f;      //px: a click this close to a small vessel picks it
	const float symbolBelow = 7.f;      //px: a vessel smaller than this on screen is drawn as a symbol
	const double vectorSeconds = 10.0;  //the velocity line shows where it will be in this long

	ImU32 colour(const ImVec4& _c, float _alpha = 1.f) { return uiTheme::u32(ImVec4(_c.x, _c.y, _c.z, _c.w * _alpha)); }
	double length(vec2 _v) { return std::sqrt(_v.x * _v.x + _v.y * _v.y); }

	std::string metres(double _m)
	{
		char text[32];
		if (std::abs(_m) >= 10000.0)
			std::snprintf(text, sizeof(text), "%.1f km", _m / 1000.0);
		else if (std::abs(_m) >= 1000.0)
			std::snprintf(text, sizeof(text), "%.2f km", _m / 1000.0);
		else
			std::snprintf(text, sizeof(text), "%.0f m", _m);
		return text;
	}
	std::string tonnes(double _kg)
	{
		char text[32];
		std::snprintf(text, sizeof(text), _kg >= 100000.0 ? "%.0f t" : "%.1f t", _kg / 1000.0);
		return text;
	}
	std::string kilonewtons(double _n)
	{
		char text[32];
		std::snprintf(text, sizeof(text), "%.1f kN", _n / 1000.0);
		return text;
	}
	//a round grid step at least _minimum metres
	double gridStep(double _minimum)
	{
		for (double step : { 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0, 50000.0 })
			if (step >= _minimum)
				return step;
		return 100000.0;
	}
}

ImVec2 navMap::toScreen(vec2 _area) const
{
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	return ImVec2(float(display.x * 0.5 + (_area.x - centre.x) * pixelsPerMetre), float(display.y * 0.5 + (_area.y - centre.y) * pixelsPerMetre));
}

vec2 navMap::toArea(ImVec2 _screen) const
{
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	return vec2(centre.x + (_screen.x - display.x * 0.5) / pixelsPerMetre, centre.y + (_screen.y - display.y * 0.5) / pixelsPerMetre);
}

void navMap::fitTo(world& _world)
{
	//the station and every ship on screen, with room round them
	double reach = 50.0;
	for (int i = 0; i < _world.count(); i++)
	{
		const flightSystem& f = _world.at(i).flight;
		reach = std::max(reach, length(f.position) + f.radius());
	}
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	pixelsPerMetre = std::clamp(std::min(display.x, display.y) * 0.4 / reach, minPixelsPerMetre, maxPixelsPerMetre);
	centre = vec2(0.0, 0.0);
	fitted = true;
}

void navMap::draw(world& _world, float _scale, ImFont* _font)
{
	for (int i = 0; i < _world.count(); i++)
		_world.at(i).flight.update();
	if (!fitted)
		fitTo(_world);
	picked = std::clamp(picked, 0, _world.count() - 1);
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	if (_font)
		dl->PushTextureID(_font->ContainerAtlas->TexID);
	ImFont* font = _font ? _font : ImGui::GetFont();
	const float fontSize = font->FontSize;

	//the screen: dark, with a grid in round numbers of metres, and the scale
	dl->AddRectFilled(ImVec2(0.f, 0.f), display, IM_COL32(6, 5, 3, 255));
	const double step = gridStep(80.0 / pixelsPerMetre);
	const vec2 corner = toArea(ImVec2(0.f, 0.f)), far = toArea(display);
	for (double x = std::floor(corner.x / step) * step; x <= far.x; x += step)
	{
		const float sx = toScreen(vec2(x, 0.0)).x;
		dl->AddLine(ImVec2(sx, 0.f), ImVec2(sx, display.y), colour(uiTheme::faint(), std::abs(x) < 1e-6 ? 0.45f : 0.18f));
	}
	for (double y = std::floor(corner.y / step) * step; y <= far.y; y += step)
	{
		const float sy = toScreen(vec2(0.0, y)).y;
		dl->AddLine(ImVec2(0.f, sy), ImVec2(display.x, sy), colour(uiTheme::faint(), std::abs(y) < 1e-6 ? 0.45f : 0.18f));
	}
	//range rings round the station
	const ImVec2 home = toScreen(_world.home().flight.position);
	for (int ring = 1; ring <= 4; ring++)
		dl->AddCircle(home, float(step * 2.0 * ring * pixelsPerMetre), colour(uiTheme::dim(), 0.25f), 96, 1.f);
	{
		const float barPixels = float(step * pixelsPerMetre);
		const ImVec2 at(24.f * _scale, display.y - 30.f * _scale);
		dl->AddLine(at, ImVec2(at.x + barPixels, at.y), colour(uiTheme::text()), 2.f);
		dl->AddLine(at, ImVec2(at.x, at.y - 6.f), colour(uiTheme::text()), 2.f);
		dl->AddLine(ImVec2(at.x + barPixels, at.y), ImVec2(at.x + barPixels, at.y - 6.f), colour(uiTheme::text()), 2.f);
		dl->AddText(font, fontSize, ImVec2(at.x, at.y - fontSize - 6.f), colour(uiTheme::text()), metres(step).c_str());
	}

	const double T = shipTuning::tileMetres;
	//every docking point: a small open diamond on the face a ship docks against
	for (int i = 0; i < _world.count(); i++)
	{
		vessel& v = _world.at(i);
		for (const dockPoint& d : _world.docksOf(v))
		{
			const sf::Vector2i step = footprint::step(d.face);
			const ImVec2 at = toScreen(v.flight.toArea(v.flight.localOfTile(d.tile) + vec2(step.x, step.y) * (T * 0.5)));
			const float r = 4.f * _scale;
			dl->AddQuad(ImVec2(at.x, at.y - r), ImVec2(at.x + r, at.y), ImVec2(at.x, at.y + r), ImVec2(at.x - r, at.y), uiTheme::u32(uiTheme::info), 1.5f);
		}
	}
	for (int i = 0; i < _world.count(); i++)
	{
		vessel& v = _world.at(i);
		const flightSystem& f = v.flight;
		const bool isPicked = i == picked;
		const ImVec2 middle = toScreen(f.position);
		const float size = float(f.radius() * pixelsPerMetre);
		const ImVec4 tint = v.ship ? (isPicked ? uiTheme::bright() : uiTheme::text()) : uiTheme::mid();

		//its trail and velocity, and the autopilot's target
		if (v.ship)
		{
			for (const vec2& p : f.trail())
				dl->AddCircleFilled(toScreen(p), 1.5f, colour(uiTheme::dim(), isPicked ? 0.8f : 0.4f), 6);
			if (length(f.velocity) > 0.05)
				dl->AddLine(middle, toScreen(f.position + f.velocity * vectorSeconds), uiTheme::u32(uiTheme::info), 1.5f);
			if (!f.manual && f.order == flightOrder::dock)
			{
				//docking: the stand-off point, and the berth beyond it
				const ImU32 c = colour(uiTheme::good, isPicked ? 1.f : 0.45f);
				const ImVec2 standoff = toScreen(f.berth + f.berthOutwards * shipTuning::dockStandoff), berth = toScreen(f.berth);
				dl->AddLine(standoff, berth, c, 1.f);
				dl->AddCircle(standoff, 4.f * _scale, c, 12, 1.5f);
				dl->AddCircleFilled(berth, 3.f * _scale, c, 12);
			}
			if (!f.manual && (f.order == flightOrder::goTo || f.order == flightOrder::hold))
			{
				const ImVec2 t = toScreen(f.target);
				const float x = 6.f * _scale;
				const ImU32 c = colour(uiTheme::good, isPicked ? 1.f : 0.45f);
				dl->AddLine(ImVec2(t.x - x, t.y - x), ImVec2(t.x + x, t.y + x), c, 2.f);
				dl->AddLine(ImVec2(t.x - x, t.y + x), ImVec2(t.x + x, t.y - x), c, 2.f);
				if (isPicked && f.order == flightOrder::goTo)
				{
					//a dashed line to it
					const vec2 to = f.target - f.position;
					const double d = length(to) * pixelsPerMetre;
					for (double s = 0.0; s < d; s += 12.0)
					{
						const vec2 a = f.position + to * (s / d), b = f.position + to * (std::min(s + 6.0, d) / d);
						dl->AddLine(toScreen(a), toScreen(b), c, 1.f);
					}
				}
			}
		}

		if (size < symbolBelow)
		{
			//far off: a symbol, pointing the way it faces
			const vec2 fwd = f.directionToArea(length(f.forwards()) > 0.0 ? f.forwards() : vec2(0.0, -1.0));
			const vec2 side(-fwd.y, fwd.x);
			const float r = 7.f * _scale;
			auto at = [&](double _along, double _across) { return ImVec2(middle.x + float((fwd.x * _along + side.x * _across) * r), middle.y + float((fwd.y * _along + side.y * _across) * r)); };
			if (v.ship)
				dl->AddTriangle(at(1.2, 0.0), at(-0.8, 0.7), at(-0.8, -0.7), colour(tint), 1.5f);
			else
				dl->AddRect(ImVec2(middle.x - r, middle.y - r), ImVec2(middle.x + r, middle.y + r), colour(tint), 0.f, 0, 1.5f);
		}
		else
		{
			//its decks from above: each tile a square turned with it
			for (const outlineCell& c : f.outline())
			{
				const ImVec2 a = toScreen(f.toArea(c.local + vec2(-T / 2, -T / 2))), b = toScreen(f.toArea(c.local + vec2(T / 2, -T / 2)));
				const ImVec2 cc = toScreen(f.toArea(c.local + vec2(T / 2, T / 2))), d = toScreen(f.toArea(c.local + vec2(-T / 2, T / 2)));
				switch (c.what)
				{
				case outlineCell::kind::floor:
					dl->AddQuadFilled(a, b, cc, d, colour(tint, 0.28f));
					break;
				case outlineCell::kind::equipment:
					dl->AddQuadFilled(a, b, cc, d, colour(tint, 0.55f));
					break;
				case outlineCell::kind::frame:
					dl->AddQuad(a, b, cc, d, colour(tint, 0.35f), 1.f);
					break;
				}
			}
			//the way it faces, and the engines' flame while they fire
			if (v.ship && length(f.forwards()) > 0.0)
			{
				const vec2 fwd = f.directionToArea(f.forwards());
				dl->AddLine(middle, toScreen(f.position + fwd * (f.radius() + 6.0 / pixelsPerMetre)), colour(tint), 1.5f);
				if (f.status().throttle > 0.01)
					dl->AddLine(toScreen(f.position - fwd * f.radius() * 0.7), toScreen(f.position - fwd * (f.radius() * (0.7 + 0.6 * f.status().throttle))),
						uiTheme::u32(uiTheme::warning), 3.f);
			}
		}
		if (isPicked)
		{
			//brackets round it
			const float r = std::max(size, pickRadius * _scale) + 4.f;
			const float k = r * 0.35f;
			const ImU32 c = colour(uiTheme::bright());
			for (const ImVec2 sgn : { ImVec2(-1.f, -1.f), ImVec2(1.f, -1.f), ImVec2(1.f, 1.f), ImVec2(-1.f, 1.f) })
			{
				const ImVec2 corner(middle.x + sgn.x * r, middle.y + sgn.y * r);
				dl->AddLine(corner, ImVec2(corner.x - sgn.x * k, corner.y), c, 1.5f);
				dl->AddLine(corner, ImVec2(corner.x, corner.y - sgn.y * k), c, 1.5f);
			}
		}
		//its name, and its speed
		char label[96];
		if (v.ship)
			std::snprintf(label, sizeof(label), "%s  %.1f m/s", v.name.c_str(), length(f.velocity));
		else
			std::snprintf(label, sizeof(label), "%s", v.name.c_str());
		const float below = std::max(size, pickRadius * _scale) + 6.f;
		const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, label);
		dl->AddText(font, fontSize, ImVec2(middle.x - textSize.x * 0.5f, middle.y + below), colour(tint), label);
	}
	if (_font)
		dl->PopTextureID();
}

float navMap::drawPanel(world& _world, float _top, float _scale, ImFont* _font)
{
	const float s = _scale;
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	picked = std::clamp(picked, 0, _world.count() - 1);
	if (_font)
		ImGui::PushFont(_font);
	ImGui::SetNextWindowPos(ImVec2(display.x - 8.f * s, _top), ImGuiCond_Always, ImVec2(1.f, 0.f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(340.f * s, 0.f), ImVec2(FLT_MAX, FLT_MAX));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, uiTheme::panel());
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f * s, 8.f * s));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f * s, 4.f * s));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f * s, 2.f * s));
	ImGui::Begin("navPanel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::TextColored(uiTheme::bright(), "NAV MAP");
	ImGui::SameLine();
	ImGui::TextDisabled("(N to close)");
	ImGui::TextDisabled("wheel zooms, right drag moves the map");
	ImGui::Separator();

	//the vessels
	for (int i = 0; i < _world.count(); i++)
	{
		vessel& v = _world.at(i);
		ImGui::PushID(i);
		char row[96];
		std::snprintf(row, sizeof(row), "%s %s", v.ship ? ">" : "#", v.name.c_str());
		if (ImGui::Selectable(row, i == picked))
			picked = i;
		ImGui::PopID();
	}
	if (ImGui::Button("New ship"))
	{
		_world.newShip();
		picked = _world.count() - 1;
	}
	ImGui::Separator();

	vessel& v = _world.at(picked);
	flightSystem& f = v.flight;
	const flightStatus& st = f.status();
	ImGui::TextColored(uiTheme::bright(), "%s", v.name.c_str());
	if (!v.ship)
	{
		ImGui::TextDisabled("the station: it keeps still");
		ImGui::Text("mass %s", tonnes(st.mass).c_str());
	}
	else
	{
		const double speed = length(f.velocity);
		const double toStation = length(f.position - _world.home().flight.position);
		ImGui::Text("%.1f m/s   %s from the station", speed, metres(toStation).c_str());
		//the way it faces, as a bearing: 0 up the map, clockwise
		const vec2 facing = f.directionToArea(length(f.forwards()) > 0.0 ? f.forwards() : vec2(0.0, -1.0));
		const double bearing = std::fmod(std::atan2(facing.x, -facing.y) * 180.0 / pi + 360.0, 360.0);
		ImGui::Text("heading %03.0f deg   turning %.1f deg/s", bearing, f.spin * 180.0 / pi);
		ImGui::Text("mass %s (dry %s)", tonnes(st.mass).c_str(), tonnes(st.dryMass).c_str());
		if (std::isinf(st.deltaV))
			ImGui::Text("delta-v unlimited (electric engines)");
		else if (st.electricThrust > 0.0)
			ImGui::Text("fuel %.0f kg   delta-v %.0f m/s + electric", st.fuelKg, st.deltaV);
		else
			ImGui::Text("fuel %.0f kg   delta-v %.0f m/s", st.fuelKg, st.deltaV);
		const double m = std::max(st.mass, 1.0);
		ImGui::Text("engines %s (%.2f m/s2)", kilonewtons(st.mainThrust).c_str(), st.mainThrust / m);
		if (st.engineWatts > 0.0)
			ImGui::TextDisabled("  drawing %.1f kW at full thrust", st.engineWatts / 1000.0);
		ImGui::Text("thrusters %s (%.2f m/s2)", kilonewtons(st.thrusterThrust).c_str(), st.thrusterThrust / m);
		ImGui::Text("gyroscopes %.0f kN m", st.gyroTorque / 1000.0);
		//what is missing
		if (st.helms == 0)
			ImGui::TextColored(uiTheme::danger, "no flight console: it can't take orders");
		else if (!st.helmPowered)
			ImGui::TextColored(uiTheme::danger, "the flight console has no power");
		if (st.engines == 0)
			ImGui::TextColored(uiTheme::warning, "no main engine");
		if (st.gyroscopes == 0)
			ImGui::TextColored(uiTheme::warning, "no gyroscope: it can't turn");
		if (st.fuelKg <= 0.0 && st.needsFuel)
			ImGui::TextColored(uiTheme::warning, "no fuel");
		if (st.gyroTorque > 0.0 && std::abs(st.engineTorque) > st.gyroTorque * 0.8)
			ImGui::TextColored(uiTheme::warning, "the engines push off its middle: more turn than the gyroscopes hold");

		ImGui::Separator();
		ImGui::Text("now: %s", st.state.c_str());
		if (st.throttle > 0.0 || st.thrusterUse > 0.0 || st.turning != 0.0)
			ImGui::TextDisabled("engines %.0f%%  thrusters %.0f%%  gyros %.0f%%", st.throttle * 100.0, st.thrusterUse * 100.0, std::abs(st.turning) * 100.0);
		//(no flying while docked: Undock first)
		ImGui::BeginDisabled(f.docked);
		bool manual = f.manual;
		if (ImGui::RadioButton("Autopilot", !manual))
			f.setManual(false);
		ImGui::SameLine();
		if (ImGui::RadioButton("Manual", manual))
			f.setManual(true);
		if (!f.manual)
		{
			ImGui::TextDisabled("right click the map to send it there");
			if (ImGui::Button("Stop"))
				f.stopHere();
			ImGui::SameLine();
			if (ImGui::Button("Hold here"))
				f.holdHere();
			ImGui::SameLine();
			if (ImGui::Button("Drift"))
				f.order = flightOrder::none;
			float limit = float(f.maxSpeed);
			ImGui::SetNextItemWidth(160.f * s);
			if (ImGui::SliderFloat("max speed", &limit, 2.f, 200.f, "%.0f m/s"))
				f.maxSpeed = limit;
		}
		else
		{
			ImGui::TextDisabled("W thrust, S back, A and D turn, Q and E slide");
			ImGui::TextDisabled("(no turn key: the gyroscopes stop the spin)");
		}
		ImGui::EndDisabled();

		//docking
		ImGui::Separator();
		dockLink* link = _world.linkOf(v);
		if (link && link->docked)
		{
			ImGui::TextColored(uiTheme::good, "docked at %s", link->host->name.c_str());
			//a transfer through the dock: which way, and how it goes
			auto transfer = [&](const char* _what, int& _way, double _rate, const std::string& _note, const char* _unit) {
				ImGui::PushID(_what);
				ImGui::Text("%s", _what);
				ImGui::SameLine(70.f * s);
				const std::string toHost = "to " + link->host->name;
				if (ImGui::RadioButton(toHost.c_str(), _way < 0))
					_way = -1;
				ImGui::SameLine();
				if (ImGui::RadioButton("off", _way == 0))
					_way = 0;
				ImGui::SameLine();
				if (ImGui::RadioButton("to the ship", _way > 0))
					_way = 1;
				if (_way != 0)
				{
					if (std::abs(_rate) > 1e-6)
						ImGui::TextDisabled("  %.1f %s: %s", std::abs(_rate), _unit, _note.c_str());
					else
						ImGui::TextColored(uiTheme::warning, "  %s", _note.empty() ? "nothing moving" : _note.c_str());
				}
				ImGui::PopID();
			};
			transfer("cargo", link->cargo, link->cargoRate, link->cargoNote, "kg/s");
			transfer("liquid", link->liquid, link->liquidRate, link->liquidNote, "L/s");
			if (ImGui::Button("Undock"))
				_world.undock(v);
		}
		else
		{
			const std::vector<world::dockChoice> choices = _world.dockChoices(v);
			if (choices.empty())
				ImGui::TextDisabled("no docks to go to: build a docking jetty on the station");
			else
			{
				dockPick = std::clamp(dockPick, 0, int(choices.size()) - 1);
				ImGui::SetNextItemWidth(250.f * s);
				if (ImGui::BeginCombo("##dockAt", choices[size_t(dockPick)].label.c_str()))
				{
					for (int i = 0; i < int(choices.size()); i++)
					{
						const std::string text = choices[size_t(i)].label + (choices[size_t(i)].usable ? "" : " (no)");
						if (ImGui::Selectable(text.c_str(), i == dockPick))
							dockPick = i;
					}
					ImGui::EndCombo();
				}
				const world::dockChoice& choice = choices[size_t(dockPick)];
				if (!choice.usable)
					ImGui::TextColored(uiTheme::warning, "%s", choice.whyNot.c_str());
				ImGui::BeginDisabled(!choice.usable);
				if (ImGui::Button(link ? "Docking..." : "Dock"))
				{
					std::string why;
					dockMessage = _world.startDocking(v, choice.dock, &why) ? "" : why;
				}
				ImGui::SameLine();
				if (ImGui::Button("Dock now"))
				{
					std::string why;
					dockMessage = _world.dockNow(v, choice.dock, &why) ? "" : why;
				}
				ImGui::EndDisabled();
				if (link)
				{
					ImGui::SameLine();
					if (ImGui::Button("Cancel"))
						_world.undock(v);
				}
				ImGui::TextDisabled("Dock: the autopilot flies it in (it needs thrusters)");
				if (!dockMessage.empty())
					ImGui::TextColored(uiTheme::warning, "%s", dockMessage.c_str());
			}
		}
	}
	ImGui::Separator();
	if (ImGui::Button(v.ship ? "Go inside the ship" : "Go inside the station"))
	{
		_world.requestView(picked);
		goInside = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Centre"))
	{
		centre = f.position;
	}

	const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
	ImGui::End();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor();
	if (_font)
		ImGui::PopFont();
	return bottom;
}

bool navMap::handleEvent(const sf::Event& _event, world& _world, bool _guiWantsMouse)
{
	switch (_event.type)
	{
	case sf::Event::MouseWheelScrolled:
	{
		if (_guiWantsMouse)
			return false;
		//zoom round the mouse: the point under it stays put
		const ImVec2 mouse(float(_event.mouseWheelScroll.x), float(_event.mouseWheelScroll.y));
		const vec2 before = toArea(mouse);
		pixelsPerMetre = std::clamp(pixelsPerMetre * std::pow(1.15, double(_event.mouseWheelScroll.delta)), minPixelsPerMetre, maxPixelsPerMetre);
		const vec2 after = toArea(mouse);
		centre += before - after;
		return true;
	}
	case sf::Event::MouseButtonPressed:
		if (_guiWantsMouse)
			return false;
		if (_event.mouseButton.button == sf::Mouse::Right)
		{
			dragging = true;
			dragMoved = false;
			dragStart = dragLast = sf::Vector2i(_event.mouseButton.x, _event.mouseButton.y);
			return true;
		}
		if (_event.mouseButton.button == sf::Mouse::Left)
		{
			//the vessel under the mouse (or nearest it, within a little of a small one)
			const ImVec2 mouse(float(_event.mouseButton.x), float(_event.mouseButton.y));
			int best = -1;
			float bestDistance = 1e9f;
			for (int i = 0; i < _world.count(); i++)
			{
				const flightSystem& f = _world.at(i).flight;
				const ImVec2 at = toScreen(f.position);
				const float d = std::sqrt((at.x - mouse.x) * (at.x - mouse.x) + (at.y - mouse.y) * (at.y - mouse.y));
				const float reach = std::max(float(f.radius() * pixelsPerMetre), pickRadius);
				if (d <= reach && d < bestDistance)
				{
					best = i;
					bestDistance = d;
				}
			}
			if (best >= 0)
				picked = best;
			return true;
		}
		return false;
	case sf::Event::MouseMoved:
		if (!dragging)
			return false;
		{
			const sf::Vector2i at(_event.mouseMove.x, _event.mouseMove.y);
			centre.x -= (at.x - dragLast.x) / pixelsPerMetre;
			centre.y -= (at.y - dragLast.y) / pixelsPerMetre;
			dragLast = at;
			if (std::abs(at.x - dragStart.x) + std::abs(at.y - dragStart.y) > 5)
				dragMoved = true;
		}
		return true;
	case sf::Event::MouseButtonReleased:
		if (_event.mouseButton.button != sf::Mouse::Right || !dragging)
			return false;
		dragging = false;
		if (!dragMoved && !_guiWantsMouse && picked >= 0 && picked < _world.count() && _world.at(picked).ship && !_world.at(picked).flight.docked)
		{
			//a right click: the picked ship goes there
			flightSystem& f = _world.at(picked).flight;
			f.goTo(toArea(ImVec2(float(_event.mouseButton.x), float(_event.mouseButton.y))));
		}
		return true;
	default:
		return false;
	}
}

bool navMap::isFlyingKey(sf::Keyboard::Key _key) const
{
	return _key == sf::Keyboard::W || _key == sf::Keyboard::A || _key == sf::Keyboard::S || _key == sf::Keyboard::D ||
		_key == sf::Keyboard::Q || _key == sf::Keyboard::E;
}

void navMap::readControls(world& _world, bool _keysFree)
{
	if (picked < 0 || picked >= _world.count())
		return;
	flightSystem& f = _world.at(picked).flight;
	if (!f.manual)
		return;
	manualControls c;
	if (_keysFree)
	{
		auto down = [](sf::Keyboard::Key _key) { return sf::Keyboard::isKeyPressed(_key) ? 1.0 : 0.0; };
		c.thrust = down(sf::Keyboard::W);
		c.back = down(sf::Keyboard::S);
		c.turn = down(sf::Keyboard::D) - down(sf::Keyboard::A);
		c.slide = down(sf::Keyboard::E) - down(sf::Keyboard::Q);
	}
	f.controls = c;
}
