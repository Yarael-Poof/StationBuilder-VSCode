#include "tileObject.h"
#include "tileCatalogue.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>

tileObject::tileObject()
{

}
tileObject::tileObject(textureManager* _textureMaster, std::string _key, const sf::Vector2f& _screenPosition,
	 sf::Vector2f& _scale, float _rotation, const sf::Color& _colour)
{
	//an old key from an older save loads as the item it became ("pumpLR" -> a pump turned twice)
	std::string aliasSettings;
	const std::string key = resolveAlias(_key, aliasSettings);
	worldPosition = _screenPosition;
	setPosition(_screenPosition);//this will needed to be converted to screen coords
	tileTextureKey = key;
	setTypeFromKey(key);
	if (!aliasSettings.empty())
		applySettingsText(aliasSettings);
	//its picture: for its turn, else its own, else whatever was registered under the key it was asked for (none: it draws
	//nothing, rather than stopping the game)
	for (const std::string& art : { artKey(), key, _key })
	{
		if (_textureMaster->hasTexture(art))
		{
			setTexture(*(_textureMaster->getTexture(art)));
			break;
		}
	}
	setColor(_colour);
	setRotation(_rotation);
	setScale(_scale);
}

int tileObject::turns() const
{
	const tileObject& m = machine();
	if (!m.type || !footprint::isMachine(*m.type))
		return 0;
	return ((int(m.getSetting(footprint::rotationKey)) % 4) + 4) % 4;
}

tileObject* tileObject::makePart(tileObject* _machine, sf::Vector2i _cell)
{
	tileObject* part = new tileObject();
	part->owner = _machine;
	part->type = _machine->type;
	part->layer = _machine->layer;
	part->tileTextureKey = _machine->tileTextureKey;
	part->cell = _cell;
	part->origin = _machine->origin;
	return part;
}

std::string tileObject::artKey() const
{
	const int t = turns();
	if (type && t > 0 && t <= int(type->turnArt.size()) && !type->turnArt[t - 1].empty())
		return tileTextureKey + "@" + std::to_string(t);
	return tileTextureKey;
}

void tileObject::fitSprite(textureManager* _textures, sf::Vector2f _frontRoot)
{
	if (isPart() || !type)
		return;
	const std::string art = _textures->hasTexture(artKey()) ? artKey() : tileTextureKey;
	if (!_textures->hasTexture(art))
		return;
	const sf::Texture* texture = _textures->getTexture(art);
	setTexture(*texture, true);
	//the art's lower corner (the footprint's, at 32 x its turned width from the art's left, at its bottom) sits on the
	//front tile's lower corner: (32, 62.5) in a tile's 64 x 64 sprite
	const sf::Vector2i size = footprint::turnedSize(*type, turns());
	const sf::Vector2u pixels = texture->getSize();
	setPosition(_frontRoot + sf::Vector2f(32.f - 32.f * float(size.x), 64.f - float(pixels.y)));
	//no art of its own for a quarter turn of a non-square footprint: its art mirrored left to right fits it
	const bool mirrored = (turns() & 1) && type->footprint.x != type->footprint.y && art == tileTextureKey;
	setOrigin(mirrored ? float(pixels.x) : 0.f, 0.f);
	setScale(mirrored ? -1.f : 1.f, 1.f);
}

//the slot and wall edge come from the catalogue (tileCatalogue.cpp). an unknown key has no layer,
//so it can't be placed on a tile.
void tileObject::setTypeFromKey(const std::string& _key)
{
	type = findTileType(_key);
	layer = type ? type->layer : tileLayer::none;
	wallEdge = type ? type->wallEdge : -1;
	settingValues.clear();
	if (type)
	{
		for (const itemSetting& setting : type->settings)
			settingValues.push_back(setting.defaultValue);
	}
	held = defaultContents();
	acceptsOnly.clear();
	produced.clear();
	progress = 0.0;
}

bool tileObject::holdsContents() const
{
	return type && (type->fluid.tank || type->storage.isStore() || !type->recipes.empty()) && !isPart();   //a part's contents are its machine's
}

std::vector<tileObject::heldMaterial> tileObject::defaultContents() const
{
	//a tank is placed full of its liquid
	if (holdsContents() && findMaterial(type->fluid.liquid) && type->fluid.capacity > 0.0)
		return { heldMaterial{ type->fluid.liquid, type->fluid.capacity } };
	return {};
}

double tileObject::amountOf(const std::string& _material) const
{
	for (const heldMaterial& h : contents())
	{
		if (h.material == _material)
			return h.amount;
	}
	return 0.0;
}

void tileObject::setContents(const std::string& _material, double _amount)
{
	held.clear();
	if (_amount > 0.0 && std::isfinite(_amount) && !_material.empty())
		held.push_back(heldMaterial{ _material, _amount });
}

namespace
{
	//the index of a setting in the type's list, or -1
	int settingIndex(const tileTypeInfo* _type, const std::string& _key)
	{
		if (!_type)
			return -1;
		for (int i = 0; i < int(_type->settings.size()); i++)
		{
			if (_type->settings[i].key == _key)
				return i;
		}
		return -1;
	}

	double clampToSetting(const itemSetting& _setting, double _value)
	{
		if (_setting.type == itemSetting::kind::toggle)
			return _value != 0.0 ? 1.0 : 0.0;
		return std::clamp(_value, _setting.minimum, _setting.maximum);
	}
}

double tileObject::getSetting(const std::string& _key) const
{
	if (owner)
		return owner->getSetting(_key);   //a part reads its machine's settings
	const int i = settingIndex(type, _key);
	return i >= 0 && i < int(settingValues.size()) ? settingValues[i] : 0.0;
}

bool tileObject::setSetting(const std::string& _key, double _value)
{
	const int i = settingIndex(type, _key);
	if (i < 0 || i >= int(settingValues.size()) || !std::isfinite(_value))
		return false;
	settingValues[i] = clampToSetting(type->settings[i], _value);
	return true;
}

std::string tileObject::settingsText(bool _withContents) const
{
	std::string text;
	if (!type)
		return text;
	for (int i = 0; i < int(type->settings.size()) && i < int(settingValues.size()); i++)
	{
		if (settingValues[i] == type->settings[i].defaultValue)
			continue;
		std::ostringstream value;   //"C" formatting (always a '.'), up to 15 digits: 240, 232.5
		value.imbue(std::locale::classic());
		value << std::setprecision(15) << settingValues[i];
		if (!text.empty())
			text += ";";
		text += type->settings[i].key + "=" + value.str();
	}
	//what it holds, when that isn't what a new one starts with: "@water=850", or "@empty"
	if (_withContents && holdsContents() && held != defaultContents())
	{
		if (held.empty())
			text += std::string(text.empty() ? "" : ";") + "@empty";
		for (const heldMaterial& h : held)
		{
			std::ostringstream value;
			value.imbue(std::locale::classic());
			value << std::setprecision(15) << h.amount;
			text += std::string(text.empty() ? "" : ";") + "@" + h.material + "=" + value.str();
		}
	}
	//a machine's output buffer: "@>iron=6"
	for (const heldMaterial& h : (_withContents && holdsContents() ? produced : std::vector<heldMaterial>()))
	{
		std::ostringstream value;
		value.imbue(std::locale::classic());
		value << std::setprecision(15) << h.amount;
		text += std::string(text.empty() ? "" : ";") + "@>" + h.material + "=" + value.str();
	}
	//a container's filter: the one solid it takes from the tubes
	if (holdsContents() && type->storage.isStore() && !acceptsOnly.empty())
		text += std::string(text.empty() ? "" : ";") + "accept=" + acceptsOnly;
	return text;
}

bool tileObject::applySettingsText(const std::string& _text)
{
	bool allGood = true;
	bool contentsGiven = false;   //the first "@" entry replaces what a new one starts with
	std::istringstream entries(_text);
	std::string entry;
	while (std::getline(entries, entry, ';'))
	{
		if (entry.empty())
			continue;
		if (entry[0] == '@')
		{
			//what it holds: "@empty", or "@<material>=<amount>" (a tank takes only liquids); a machine's output buffer "@>..."
			const bool output = entry.size() > 1 && entry[1] == '>';
			if (output)
				entry.erase(1, 1);
			if (!contentsGiven && !output)
				held.clear();
			contentsGiven = contentsGiven || !output;
			if (entry == "@empty")
				continue;
			const size_t equals = entry.find('=');
			const std::string material = entry.substr(1, equals == std::string::npos ? std::string::npos : equals - 1);
			const materialInfo* known = findMaterial(material);
			double amount = 0.0;
			bool parsed = false;
			if (equals != std::string::npos)
			{
				std::istringstream number(entry.substr(equals + 1));
				number.imbue(std::locale::classic());
				parsed = bool(number >> amount) && number.peek() == std::char_traits<char>::eof() && std::isfinite(amount) && amount >= 0.0;
			}
			//a machine's buffers: any material, kg or L
			if (holdsContents() && !type->recipes.empty() && known && parsed)
			{
				if (amount > 0.0)
					(output ? produced : held).push_back(heldMaterial{ material, amount });
				continue;
			}
			//a tank holds one liquid (litres); storage holds solids (kg)
			const bool tank = holdsContents() && type->fluid.tank;
			const bool store = holdsContents() && type->storage.isStore();
			const materialInfo::phase wanted = tank ? materialInfo::phase::liquid : materialInfo::phase::solid;
			if (!(tank || store) || !known || known->state != wanted || !parsed)
			{
				std::cout << "[WARN] " << tileTextureKey << ": ignored what it held, '" << entry << "'\n";
				allGood = false;
				continue;
			}
			if (tank && amount > 0.0)
				held = { heldMaterial{ material, std::min(amount, type->fluid.capacity) } };
			if (store && amount > 0.0 && storage::put(*this, *known, amount) < amount - 1e-9)
			{
				std::cout << "[WARN] " << tileTextureKey << ": not all of '" << entry << "' fits in it any more\n";
				allGood = false;
			}
			continue;
		}
		if (entry.rfind("accept=", 0) == 0)
		{
			//a container's filter: a solid's key
			const materialInfo* known = findMaterial(entry.substr(7));
			if (holdsContents() && type->storage.isStore() && known && known->state == materialInfo::phase::solid)
				acceptsOnly = known->key;
			else
			{
				std::cout << "[WARN] " << tileTextureKey << ": ignored the filter '" << entry << "'\n";
				allGood = false;
			}
			continue;
		}
		const size_t equals = entry.find('=');
		const std::string key = entry.substr(0, equals);
		double value = 0.0;
		bool parsed = false;
		if (equals != std::string::npos)
		{
			std::istringstream number(entry.substr(equals + 1));
			number.imbue(std::locale::classic());
			parsed = bool(number >> value) && number.peek() == std::char_traits<char>::eof();
		}
		if (!parsed || !setSetting(key, value))
		{
			std::cout << "[WARN] " << tileTextureKey << ": ignored setting '" << entry << "'\n";
			allGood = false;
		}
	}
	return allGood;
}

void tileObject::SetWorldPosition(float _x, float _y){}
void tileObject::SetWorldPosition(const sf::Vector2f& _position){}
sf::Vector2f tileObject::GetWorldPosition() { return worldPosition; }

void tileObject::SetWorldZ(float _z)
{
	sf::Vector2f currentPosition = getPosition();
	setPosition(currentPosition.x, currentPosition.y + _z);
	worldZ = _z;
}

void tileObject::resetWorldZ()
{
	sf::Vector2f currentPosition = getPosition();
	setPosition(currentPosition.x, currentPosition.y - worldZ);
	worldZ = 0;
}
float tileObject::GetWorldZ() { return worldZ; }
