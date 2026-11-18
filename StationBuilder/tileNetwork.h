#pragma once
#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <vector>

class station;
class tileObject;
struct tileTypeInfo;

//what a medium (electrical, thermal, liquid) needs to know about each placed object. the rules take the object, not
//just its type: a machine's ports depend on its turn and on which tile of it this is (each tile of a big machine is
//its own object: the machine on its front tile, a part on each other)
struct networkMedium
{
	const char* name;                           //for debug text, e.g. "electrical"
	bool (*hasNode)(const tileObject&);         //the object gives its tile a node (cable, generator, light...)
	bool (*conducts)(const tileObject&);        //the object links its tile to neighbouring tiles that also conduct
	//the object links its tile to the same tile on the level above and below, where that tile has one too.
	//nullptr: nothing in this medium crosses between levels
	bool (*linksVertically)(const tileObject&);
	//which sides of its tile a conducting object links on (mask: bit 0 UL, 1 UR, 2 LL, 3 LR), from its type, turn and
	//settings. two neighbouring tiles link only if each has something connecting on the side between them.
	//nullptr: every conducting object connects on all four sides
	int (*connectsOn)(const tileObject&) = nullptr;
	//a machine bigger than one tile: the pairs of its (turned) cells that are joined inside it, so its ports on different
	//tiles are one network (a big tank), or pushed between (a pump). nullptr: nothing is joined inside machines
	void (*internalLinks)(const tileObject&, std::vector<std::pair<sf::Vector2i, sf::Vector2i>>&) = nullptr;
};

//cables conduct; risers also link to a riser on the level above or below; generators, lights and other loads
//only sit on a node
extern const networkMedium electricalMedium;
//every tile with a floor holds air: it links to floored neighbours and to the floored tile directly above and
//below (through the deck). walls don't make or break links, they only slow the heat (see thermalNetwork)
extern const networkMedium thermalMedium;
//pipes, pumps, tanks and heat exchangers all hold liquid and link to neighbours that also do. pipe risers also link
//to a pipe riser on the same tile of the level above or below
extern const networkMedium fluidMedium;
//item tubes join containers: a tube links by its shape, a machine through its item ports, and a one-tile container
//with no ports on every side. tube risers link to a tube riser directly above or below
extern const networkMedium itemsMedium;

//one tile that takes part in a medium
struct networkNode
{
	int level = 0;                                //station level index
	sf::Vector2i tile;                            //1-based tile coords, as isometricLevel uses them
	int tileIndex = 0;                            //index into that level's levelTiles
	std::vector<const tileTypeInfo*> objects;     //every object on the tile that belongs to this medium
	std::vector<std::vector<double>> settings;    //each of those objects' setting values (tileObject::allSettingValues)
	int conductors = 0;                           //how many of those conduct (several act in parallel)
	int verticalConductors = 0;                   //how many of those also link between levels (risers)
	int linkSides = 0;                            //the sides its conducting objects connect on, together (bit per side)
	int island = -1;                              //index into islands()
	int edgeUp = -1;                              //the edge to the same tile on the level above, or -1
	int edgeDown = -1;                            //the edge to the same tile on the level below, or -1
};

//two tiles joined in the medium: neighbours on one level that both conduct, or (vertical) the same tile on
//two adjacent levels that both link vertically
struct networkEdge
{
	int a = 0;   //node indices, a < b. for a vertical edge, a is on the lower level
	int b = 0;
	bool vertical = false;
	bool internal = false;   //inside a big machine, between two of its tiles (they need not be neighbours)
};

//a group of nodes joined by edges; each island is simulated on its own
struct networkIsland
{
	std::vector<int> nodes;
	std::vector<int> edges;
};

//the nodes, edges and islands of one medium across every level of a station.
//a node exists on every tile holding an object of the medium; an edge joins two tiles next to each other
//(tile x +-1 or y +-1, same level) when both hold a conductor, and the same tile on two adjacent levels
//(level +-1) when both hold something that links vertically.
//it rebuilds itself from the station whenever the station's revision changes.
class tileNetwork
{
public:
	explicit tileNetwork(const networkMedium& _medium);

	//rebuilds if the station has changed since the last build. returns true if the network itself changed
	//(the first build always counts): placing a floor or a wall changes the station but not this network,
	//while changing a setting of one of its objects does
	bool update(station& _station);
	//rebuilds now, changed or not
	void build(station& _station);

	const std::vector<networkNode>& nodes() const { return nodeList; }
	const std::vector<networkEdge>& edges() const { return edgeList; }
	const std::vector<networkIsland>& islands() const { return islandList; }
	const networkMedium& medium() const { return *mediumRules; }
	//the node on a tile, or -1 if the tile has none (or is outside the level)
	int nodeAt(int _level, sf::Vector2i _tile) const;
	//the station revision this was built from
	std::uint64_t builtRevision() const { return revision; }
	//how many levels the station had when this was built
	int levelCount() const { return int(nodeOfTile.size()); }

private:
	const networkMedium* mediumRules;
	std::vector<networkNode> nodeList;
	std::vector<networkEdge> edgeList;
	std::vector<networkIsland> islandList;
	std::vector<std::vector<int>> nodeOfTile;   //per level, per tile index: node index or -1
	std::vector<sf::Vector2i> levelSize;        //per level: width, height in tiles
	std::uint64_t revision = 0;
	std::uint64_t builtGeneration = 0;   //the content generation (catalogueGeneration) it was built with
	bool built = false;
	double lastBuildMs = 0.0;
};
