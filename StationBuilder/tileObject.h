#pragma once
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
//this is a Sprite that is an objet within/ontop of a worldtile
#include "textureManager.h"

//which slot of a tile an object goes in (see tile.h)
enum class tileLayer { none, background, floor, wall, fixture };

struct tileTypeInfo;

class tileObject : public sf::Sprite
{
public:
	tileObject();
	tileObject(textureManager* _textureMaster,std::string _key, const sf::Vector2f& _screenPosition =sf::Vector2f(0,0),
    sf::Vector2f& _scale=sf::Vector2f(1,1), float _rotation=0.f, const sf::Color& _colour=sf::Color::White);
	void SetWorldPosition(float _x, float _y);
	void SetWorldPosition(const sf::Vector2f& _position);
	sf::Vector2f GetWorldPosition();
	std::string tileTextureKey;
	void SetWorldZ(float _z);
	void resetWorldZ();
	float GetWorldZ();

	//which slot this object occupies on a tile. set from the texture key, see tileObject.cpp
	void setTypeFromKey(const std::string& _key);
	//this object's catalogue entry (name, electrical properties...), nullptr for an unknown key.
	//catalogue entries live for the whole program, so the pointer never goes stale
	const tileTypeInfo* type = nullptr;

	//this object's own settings (a generator's switch and voltage...), declared by its catalogue entry.
	//a setting the type doesn't have reads as 0 and can't be set
	double getSetting(const std::string& _key) const;
	//sets a setting, clamped to its range (toggles become 0 or 1). false if the type has no such setting
	bool setSetting(const std::string& _key, double _value);
	//the settings that differ from their defaults, as saved: "on=0;v=240". "" when all are at their defaults.
	//_withContents false leaves out what it holds (a blueprint's copy: "@water=850", "@>iron=6"), keeping its filter
	std::string settingsText(bool _withContents = true) const;
	//reads settingsText() back. unknown or malformed entries are skipped with a message; false if any were
	bool applySettingsText(const std::string& _text);
	//every setting's value, in the order the catalogue declares them (a part: its machine's)
	const std::vector<double>& allSettingValues() const { return owner ? owner->settingValues : settingValues; }

	//machines (items with ports, or bigger than one tile; see footprint:: in tileCatalogue.h). a machine bigger than one
	//tile is this object on its front tile, and a part on each other tile: a part draws nothing, is never saved, and
	//reads its settings, turn and contents from the machine
	//its quarter turns, from its hidden "r" setting (0 for anything that doesn't turn)
	int turns() const;
	tileObject* owner = nullptr;              //a part: the machine it belongs to
	sf::Vector2i cell = sf::Vector2i(0, 0);   //which tile of its machine's turned footprint it is on
	sf::Vector2i origin = sf::Vector2i(0, 0); //its machine's upper tile (level tile coordinates)
	bool isPart() const { return owner != nullptr; }
	//the machine itself: its owner for a part, otherwise this object
	tileObject& machine() { return owner ? *owner : *this; }
	const tileObject& machine() const { return owner ? *owner : *this; }
	//a part of _machine for one of its other tiles
	static tileObject* makePart(tileObject* _machine, sf::Vector2i _cell);
	//the texture key of its art at its turn: "pump", or "pump@2" when the item has art of its own for that turn
	std::string artKey() const;
	//puts its picture on its front tile (whose sprite's top left is _frontRoot): a big machine's art reaches back over
	//its other tiles, and a non-square one with no art of its own for the turn is drawn mirrored on a quarter turn
	void fitSprite(textureManager* _textures, sf::Vector2f _frontRoot);

	//what this object holds: a tank's liquid, in litres. one entry per material (by its key). it starts with its
	//type's default (a tank: full of its liquid) and is saved with the settings when it differs: "tank{@coolant=850}",
	//"tank{@empty}". the simulations keep it up to date, so it survives rebuilds and saves
	struct heldMaterial
	{
		std::string material;
		double amount = 0.0;
		bool operator==(const heldMaterial& _other) const { return material == _other.material && amount == _other.amount; }
	};
	const std::vector<heldMaterial>& contents() const { return owner ? owner->held : held; }
	//the same, to change (storage:: in tileCatalogue.h does that for containers)
	std::vector<heldMaterial>& contentsToChange() { return owner ? owner->held : held; }
	//a machine that makes things: contents() is its input buffer, and this its output buffer (saved as "@>iron=6").
	//progress is how far through its current cycle it is, 0..1 (not saved: a load starts the cycle again)
	const std::vector<heldMaterial>& outputs() const { return owner ? owner->produced : produced; }
	std::vector<heldMaterial>& outputsToChange() { return owner ? owner->produced : produced; }
	double progress = 0.0;
	double amountOf(const std::string& _material) const;
	//replaces what it holds with _amount of one material (nothing if _amount is 0 or less)
	void setContents(const std::string& _material, double _amount);
	void clearContents() { held.clear(); }
	//true if its type holds materials (a tank: one liquid, in litres; storage: solids, in kg; a machine that makes
	//things: its buffers, kg of solids and L of liquids)
	bool holdsContents() const;
	//a container's filter: the one solid it takes from the item tubes, "" for anything (saved as "accept=iron_ore").
	//a part's is its machine's
	const std::string& accepts() const { return owner ? owner->acceptsOnly : acceptsOnly; }
	void setAccepts(const std::string& _material) { (owner ? owner->acceptsOnly : acceptsOnly) = _material; }
	//what a newly placed one holds
	std::vector<heldMaterial> defaultContents() const;
	tileLayer layer = tileLayer::none;
	//for walls only: which edge of the tile the wall stands on.
	//0 = UL, 1 = UR, 2 = LL, 3 = LR (same direction convention as isometricLevel.h)
	int wallEdge = -1;

protected:
	std::vector<double> settingValues;   //one per type->settings entry, in the same order
	std::vector<heldMaterial> held;      //see contents()
	std::string acceptsOnly;             //see accepts()
	std::vector<heldMaterial> produced;  //see outputs()
	sf::Vector2f worldPosition;//where in the world is the object, when we create the SFML sprite we must convert this to screen domain
	float worldZ = 0;//how high of the floor is it

};

