#pragma once
#include <SFML/System/Vector2.hpp>
#include <string>

class station;
class textureManager;
class tileObject;
struct tileTypeInfo;

//cables, risers and pipes are drawn with arms only towards the neighbours they actually link to. their art is a
//four-way cross; makeVariants cuts it into the 16 versions (one per set of arms) and update gives every piece the
//version for its links, worked out with the same rule as the networks (both pieces must connect on the side between).
//pumps and heat exchangers sit on pipe stubs the same way (makeDeviceVariants)
namespace lineArt
{
	//makes "key#0" .. "key#15" from the four-way art in _file: each keeps the middle and only the arms in its mask
	//(bit 0 UL, 1 UR, 2 LL, 3 LR). _keepConduit also keeps a riser's upright conduit
	void makeVariants(textureManager& _textures, const std::string& _key, const std::string& _file, bool _keepConduit);
	//makes "key#0" .. "key#15" for a device that joins pipes: the device art in _file drawn over the pipe arms in
	//the mask (cut from the pipe art in _pipeFile)
	void makeDeviceVariants(textureManager& _textures, const std::string& _key, const std::string& _file, const std::string& _pipeFile);
	//the sides an item connects on as it comes from the menu (a cable on Auto: all four, a pump: front and back,
	//an exchanger: all four). 0 for items that carry neither power nor liquid
	int ownSides(const tileTypeInfo& _type);
	//the texture key for a piece showing just the arms in _mask
	std::string variantKey(const std::string& _key, int _mask);
	//true for the pieces drawn this way: cables, risers and plain pipes
	bool isLinePiece(const tileObject& _object);
	//the sides a piece on a tile links on: those it connects on where the neighbour across has something of the
	//same kind (electrical or liquid) connecting back. 0 for anything that carries neither
	int linkedSides(station& _station, int _level, sf::Vector2i _tile, const tileObject& _object);
	//gives every piece with variants on every level the texture for the links it has now. call when the station changes
	void update(station& _station, textureManager& _textures);
}
