#pragma once
#include <functional>
#include <string>

class textureManager;
struct ImFont;

//F6: what the content files hold, and what is wrong with them. the problems from the last load (errors red,
//warnings amber), every item and material, and a preview of the one picked: its art, numbers, settings and where it
//is defined. it opens by itself when a load finds errors. it also shows the short notice after an F5 reload
class contentPanel
{
public:
	void toggle() { visible = !visible; }
	void open() { visible = true; }
	//opens the panel on one item's preview
	void showItem(const std::string& _key) { visible = true; selectedItem = _key; }
	bool isOpen() const { return visible; }
	//a line at the top of the screen for a few seconds, e.g. "Content reloaded: 23 items, no problems"
	void notice(const std::string& _text, bool _problem);
	//call every frame between ImGui::SFML::Update and Render. _scale: 1 = the 1080p layout, 2 = 4K
	void draw(textureManager* _textures, float _scale, ImFont* _font);
	//set by the game: open the content editor (F7) on an item or material, or on a new item
	std::function<void(const std::string&)> onEditItem;
	std::function<void(const std::string&)> onEditMaterial;
	std::function<void()> onNewItem;

private:
	bool visible = false;
	std::string selectedItem;
	std::string noticeText;
	bool noticeProblem = false;
	double noticeStart = -100.0;
};
