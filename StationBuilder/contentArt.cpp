#include "content.h"
#include "tileCatalogue.h"
#include "textureManager.h"
#include "lineArt.h"
#include "station.h"
#include <map>

namespace
{
	//calls _visit(tileObject*) for every object on every tile of every level
	template <typename F>
	void forEachPlaced(station& _station, F&& _visit)
	{
		for (int lv = 0; lv < _station.getLayerListSize(); lv++)
		{
			for (tile& t : _station.getIsoLevel(float(lv))->levelTiles)
				t.forEachObject(_visit);
		}
	}
}

namespace content
{
	void loadArt(textureManager& _textures)
	{
		//each item's art under its key, and a machine's art for each quarter turn (if it has its own) as "key@1".."key@3"
		auto pictures = [](const tileTypeInfo& _type) {
			std::vector<std::pair<std::string, std::string>> list{ { _type.key, _type.art } };
			for (size_t t = 0; t < _type.turnArt.size(); t++)
				list.push_back({ _type.key + "@" + std::to_string(t + 1), _type.turnArt[t] });
			return list;
		};
		for (const tileTypeInfo& type : tileCatalogue())
		{
			if (type.removed)
				continue;
			for (const auto& [key, file] : pictures(type))
			{
				if (!_textures.addTexture(key, file))
					addMessage({ true, type.origin, "art: can't load \"" + file + "\" (paths are from the game folder, e.g. \"tiles/cable.png\")" });
			}
		}
		//the versions with only some arms: cut from four-way art, or the item drawn over another item's arms
		for (const tileTypeInfo& type : tileCatalogue())
		{
			if (type.removed || type.drawArms.empty())
				continue;
			for (const auto& [key, file] : pictures(type))
			{
				if (type.drawArms == "cross" || type.drawArms == "cross_upright")
					lineArt::makeVariants(_textures, key, file, type.drawArms == "cross_upright");
				else if (const tileTypeInfo* under = findTileType(type.drawArms))
					lineArt::makeDeviceVariants(_textures, key, file, under->art);
			}
		}
	}

	const report& reload(const std::string& _folder, station& _station, textureManager& _textures)
	{
		return reload(_folder, std::vector<station*>{ &_station }, _textures);
	}

	const report& reload(const std::string& _folder, const std::vector<station*>& _stations, textureManager& _textures)
	{
		//every placed object's settings, read while the definitions they were made with are still in place (parts of big
		//machines keep nothing of their own: they read their machine's)
		std::map<tileObject*, std::string> saved;
		for (station* s : _stations)
		{
			forEachPlaced(*s, [&](tileObject* _object) {
				if (!_object->isPart())
					saved[_object] = _object->settingsText();
			});
		}

		load(_folder);
		loadArt(_textures);

		//each object takes its (possibly changed) definition: its settings put back by key (ones the item no longer
		//has are dropped), then every picture is put back (line pieces get their arms back when lineArt::update next runs)
		for (station* s : _stations)
		{
			forEachPlaced(*s, [&](tileObject* _object) {
				if (_object->isPart())
					return;
				_object->setTypeFromKey(_object->tileTextureKey);
				_object->applySettingsText(saved[_object]);
			});
			forEachPlaced(*s, [&](tileObject* _object) {
				if (_object->isPart())
					_object->type = _object->owner->type;
			});
			for (int lv = 0; lv < s->getLayerListSize(); lv++)
			{
				s->getIsoLevel(float(lv))->refreshSprites();
				s->getIsoLevel(float(lv))->markChanged();
			}
		}
		return lastReport();
	}
}
