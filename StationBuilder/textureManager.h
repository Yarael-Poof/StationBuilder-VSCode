#pragma once
#include <SFML/Graphics.hpp>
#include <iostream>
//class to hold sprite textures so that they are all kept in scope
class textureManager
{
public:
	//loads the image under _key; adding a key again replaces its picture in place, so sprites already using it change
	//too. false if the file wouldn't load: the key keeps the picture it had (a blank one if it had none)
	bool addTexture(std::string _key, std::string _pathToTexture)
	{
		sf::Texture texture;
		const bool loaded = texture.loadFromFile(_pathToTexture);
		if (!loaded)
		{
			std::cout << debugErrorString + __func__ << " Cannot load texture " << _pathToTexture << std::endl;
			if (hasTexture(_key))
				return false;
		}
		store(_key, texture);
		return loaded;
	}
	//a texture made in code (e.g. a cable with only some of its arms) rather than loaded from a file
	bool addTexture(std::string _key, const sf::Image& _image)
	{
		sf::Texture texture;
		const bool made = texture.loadFromImage(_image);
		if (!made)
		{
			std::cout << debugErrorString + __func__ << " Cannot make texture " << _key << std::endl;
			if (hasTexture(_key))
				return false;
		}
		store(_key, texture);
		return made;
	}
	sf::Texture* getTexture(std::string _key)
	{
		if (textureMap.count(_key) != 1)
		{
			std::cout << debugErrorString + __func__ << " fatal! the requested texture key does not exist in map" << std::endl;
			exit(EXIT_FAILURE);//replace this with a better recovery at some point
		}
		return &textureMap[_key];
	}
	std::string getTextureKey(int _index)
	{
		if (_index == -1)
		{
			return "empty";
		}
		return keyMap[_index];
	}

	void setTextureSmooth(std::string _key, bool _toggle)
	{
		textureMap[_key].setSmooth(_toggle);
	}

	bool hasTexture(std::string _key)
	{
		return textureMap.count(_key) == 1;
	}
	int getTextureMapSize()
	{
		return textureMap.size();
	}
private:
	void store(const std::string& _key, sf::Texture& _texture)
	{
		_texture.setSmooth(false);
		if (!hasTexture(_key))
			keyMap[int(textureMap.size()) + 1] = _key;
		textureMap[_key] = _texture;   //the map's entry stays where it is, so sprites pointing at it see the new picture
	}

	std::map<std::string, sf::Texture> textureMap;
	std::string debugErrorString = "[ERROR] from [Texture Manager].";
	std::string debuginfoString = "[Texture Manager] ";
	std::map<int, std::string> keyMap;
};

