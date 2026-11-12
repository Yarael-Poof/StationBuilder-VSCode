#include <SFML/Graphics.hpp>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui-SFML.h>
#include "isometricLevel.h"
#include "textureManager.h"
#include "gameManager.h"
#include "station.h"
#include "fps.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>


const float zoomAmount{ 1.1f };
sf::Vector2f oldPos;
bool moving = false;

void zoomViewAt(sf::Vector2i pixel, sf::RenderWindow& window, float zoom)
{
    const sf::Vector2f beforeCoord{ window.mapPixelToCoords(pixel) };
    sf::View view{ window.getView() };
    view.zoom(zoom);
    window.setView(view);
    const sf::Vector2f afterCoord{ window.mapPixelToCoords(pixel) };
    const sf::Vector2f offsetCoords{ beforeCoord - afterCoord };
    view.move(offsetCoords);
    window.setView(view);

}
template <typename T>
std::string to_string_with_precision(const T a_value, const int n = 6)
{
    std::ostringstream out;
    out.precision(n);
    out << std::fixed << a_value;
    return out.str();
}

//----------------------------------------------------------------------------------------
// GUI (Dear ImGui). Recreates the layout that used to live in gui/stationGUI.txt (TGUI):
// a column of 48x48 icon buttons on the left, black labels beside them, and the
// build menu list box. Positions/colours are taken from that file.
//----------------------------------------------------------------------------------------
namespace gui
{
    const ImU32 black = IM_COL32(0, 0, 0, 255);
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    const ImVec4 blackV = ImVec4(0.f, 0.f, 0.f, 1.f);
    const ImVec4 whiteV = ImVec4(1.f, 1.f, 1.f, 1.f);
    const ImVec4 grey40 = ImVec4(0x40 / 255.f, 0x40 / 255.f, 0x40 / 255.f, 1.f);   // #404040
    const ImVec4 grey66 = ImVec4(0x66 / 255.f, 0x66 / 255.f, 0x66 / 255.f, 1.f);   // #666666

    // Each interactive widget lives in its own tiny borderless window, so ImGui only
    // claims the mouse when the cursor is actually over a widget (not the empty
    // space between them), letting clicks/zoom/drag reach the level everywhere else.
    const ImGuiWindowFlags overlayFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_AlwaysAutoResize;

    void applyStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(0, 0);
        style.WindowBorderSize = 0.f;
        style.WindowMinSize = ImVec2(1, 1);
        style.FramePadding = ImVec2(0, 0);
        style.FrameBorderSize = 0.f;
        style.ItemSpacing = ImVec2(0, 0);
        style.ScrollbarSize = 14.f;
        style.ScrollbarRounding = 0.f;
        style.FrameRounding = 0.f;

        ImVec4* c = style.Colors;
        c[ImGuiCol_Button] = blackV;          // BackgroundColor
        c[ImGuiCol_ButtonHovered] = grey40;   // BackgroundColorHover
        c[ImGuiCol_ButtonActive] = grey66;    // BackgroundColorDown
        c[ImGuiCol_Text] = whiteV;
        c[ImGuiCol_FrameBg] = blackV;         // list box background
        c[ImGuiCol_Border] = blackV;
        c[ImGuiCol_ScrollbarBg] = ImVec4(0xF5 / 255.f, 0xF5 / 255.f, 0xF5 / 255.f, 1.f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0xDC / 255.f, 0xDC / 255.f, 0xDC / 255.f, 1.f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0xE6 / 255.f, 0xE6 / 255.f, 0xE6 / 255.f, 1.f);
        c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0xE6 / 255.f, 0xE6 / 255.f, 0xE6 / 255.f, 1.f);
    }

    // TGUI BitmapButton equivalent: 48x48 image button at a fixed screen position.
    bool iconButton(const char* id, const sf::Texture& icon, ImVec2 pos)
    {
        ImGui::SetNextWindowPos(pos);
        ImGui::Begin(id, nullptr, overlayFlags);
        bool clicked = ImGui::ImageButton(id, icon, sf::Vector2f(48.f, 48.f));
        ImGui::End();
        return clicked;
    }

    // 48x48 text button that toggles a flag; stays highlighted (#666666) while on.
    bool toggleButton(const char* id, const char* text, ImVec2 pos, bool& on)
    {
        ImGui::SetNextWindowPos(pos);
        ImGui::Begin(id, nullptr, overlayFlags);
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, grey66);
        bool clicked = ImGui::Button(text, ImVec2(48.f, 48.f));
        if (on)
            ImGui::PopStyleColor();
        ImGui::End();
        if (clicked)
            on = !on;
        return clicked;
    }

    // TGUI Label equivalent: non-interactive text on a black box at a fixed screen position.
    void label(ImVec2 pos, ImVec2 size, const std::string& text, bool centred = false)
    {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const ImVec2 max = pos + size;
        dl->AddRectFilled(pos, max, black);
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const float x = centred ? pos.x + (size.x - textSize.x) * 0.5f : pos.x + 2.f;
        const float y = pos.y + (size.y - textSize.y) * 0.5f;
        dl->PushClipRect(pos, max, true);
        dl->AddText(ImVec2(x, y), white, text.c_str());
        dl->PopClipRect();
    }

    // TGUI ListBox equivalent (ItemHeight 20, 160x142). Returns true when the selection changes.
    // Colours match the TGUI renderer: white highlight with black text for selected/hovered rows.
    bool listBox(const char* id, ImVec2 pos, ImVec2 size, const std::vector<std::string>& items, int& selected)
    {
        bool changed = false;
        ImGui::SetNextWindowPos(pos);
        ImGui::Begin(id, nullptr, overlayFlags);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
        if (ImGui::BeginListBox("##list", size))
        {
            for (int i = 0; i < (int)items.size(); i++)
            {
                const bool isSelected = (i == selected);
                ImGui::PushID(i);
                ImGui::PushStyleColor(ImGuiCol_Header, whiteV);                                   // SelectedBackgroundColor
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, isSelected ? grey66 : whiteV);      // (Selected)BackgroundColorHover
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, isSelected ? grey66 : whiteV);
                if (ImGui::Selectable("##item", isSelected, 0, ImVec2(0.f, 20.f)) && !isSelected)
                {
                    selected = i;
                    changed = true;
                }
                ImGui::PopStyleColor(3);

                // draw the row text ourselves so it can switch to black on a white highlight
                const bool hovered = ImGui::IsItemHovered();
                const ImU32 textCol = (isSelected || hovered) && !(isSelected && hovered) ? black : white;
                const ImVec2 rowMin = ImGui::GetItemRectMin();
                const float textY = rowMin.y + (20.f - ImGui::GetTextLineHeight()) * 0.5f;
                ImGui::GetWindowDrawList()->AddText(ImVec2(rowMin.x + 3.f, textY), textCol, items[i].c_str());
                ImGui::PopID();
            }
            ImGui::EndListBox();
        }
        ImGui::PopStyleVar();
        ImGui::End();
        return changed;
    }

    sf::Texture loadIcon(const std::string& path)
    {
        sf::Texture texture;
        if (!texture.loadFromFile(path))
            std::cout << "[GUI] Cannot load icon " << path << std::endl;
        texture.setSmooth(true);
        return texture;
    }
}

int main()
{
    int screenWidth = 1920;
    int screenHeight = 1080;


    sf::ContextSettings settings;
    settings.antialiasingLevel = 8;
    sf::RenderWindow window(sf::VideoMode(screenWidth, screenHeight), "SFML works!", sf::Style::Default, settings);
    sf::Clock clock;
    sf::Clock guiClock;


    FPS fps;

    // ---- Dear ImGui setup ----
    if (!ImGui::SFML::Init(window, false))
        return EXIT_FAILURE;
    ImGui::GetIO().IniFilename = nullptr; // layout is fixed, don't write imgui.ini
    if (!ImGui::GetIO().Fonts->AddFontFromFileTTF("arial.ttf", 14.f))
        ImGui::GetIO().Fonts->AddFontDefault();
    if (!ImGui::SFML::UpdateFontTexture())
        return EXIT_FAILURE;
    gui::applyStyle();

    // white arrows (the copies in gui/ are black and would be invisible on the black buttons)
    const sf::Texture upIcon = gui::loadIcon("up.png");
    const sf::Texture downIcon = gui::loadIcon("down.png");
    const sf::Texture buildIcon = gui::loadIcon("gui/build.png");
    const sf::Texture bugIcon = gui::loadIcon("gui/bug.png");
    const sf::Texture saveIcon = gui::loadIcon("gui/save.png");

    std::vector<std::string> buildMenuItems;
    int buildMenuSelected = -1; // -1 = nothing selected (textureManager maps this to "empty")
    bool showTilePositions = false; // debug option: overlay each tile's index + world position



    gameManager gameMaster;
    //this is a master global scope object to hold all our textures for all levels,
    //we must pass a pointer of this to all isometricLevels so they can get their textures.
    //add new textures using addTexture("name", "filename"), get them by using getTexture("name")
    //returns a pointer to the texture, this keeps them always in scope for any sprite of any level
    textureManager textureMaster;
    textureMaster.addTexture("empty", "tiles/empty64white.png");
    textureMaster.addTexture("floor", "tiles/floor64.png");
    textureMaster.addTexture("wallUL", "tiles/wallUL.png");
    textureMaster.addTexture("wallUR", "tiles/wallUR.png");

    // textureManager's key indices start at 1 (index 0 is unused)
    for (int i = 1; i <= textureMaster.getTextureMapSize(); i++)
    {
        buildMenuItems.push_back(textureMaster.getTextureKey(i));
    }



    station HaxelPort(&textureMaster);
    HaxelPort.appendNewIsoLevel(40, 40, 64);
    HaxelPort.appendNewIsoLevel(40, 40, 64, "floor");

    //isometricLevel layerOne(&textureMaster,40,40,64);
    //layerOne.fillFloors();

    //gameMaster.saveLevelToDisc(HaxelPort.getCurrentIsoLevel(), "test2");
    //gameMaster.loadLevelFromDisc(layerTwo, "test2", &textureMaster);
    //HaxelPort.addIsoLevel(layerTwo, 2);


    sf::View view(window.getDefaultView());
    window.clear(sf::Color::White);
    while (window.isOpen())
    {
        // true while the cursor is over a GUI widget: world input is ignored then
        const bool guiWantsMouse = ImGui::GetIO().WantCaptureMouse;

        sf::Event event;
        while (window.pollEvent(event))
        {
            // Pass the event to the GUI
            ImGui::SFML::ProcessEvent(window, event);
            if (event.type == sf::Event::Closed)
            {
                window.close();
            }

            else if (event.type == sf::Event::MouseWheelScrolled)
            {
                if (guiWantsMouse)
                    continue;

                if (event.mouseWheelScroll.delta > 0)
                    zoomViewAt({ event.mouseWheelScroll.x, event.mouseWheelScroll.y }, window, (1.f / zoomAmount));

                else if (event.mouseWheelScroll.delta < 0)
                    zoomViewAt({ event.mouseWheelScroll.x, event.mouseWheelScroll.y }, window, zoomAmount);
            }

            else if (event.type == sf::Event::MouseButtonPressed)
            {
                if (event.mouseButton.button == 1 && !guiWantsMouse) {
                    moving = true;
                    oldPos = window.mapPixelToCoords(sf::Vector2i(event.mouseButton.x, event.mouseButton.y));
                }
            }
            else if (event.type == sf::Event::MouseButtonReleased)
            {
                if (event.mouseButton.button == 1) {
                    moving = false;
                }
            }
            else if (event.type == sf::Event::MouseMoved)
            {
                if (!moving)
                    break;
                // Determine the new position in world coordinates
                const sf::Vector2f newPos = window.mapPixelToCoords(sf::Vector2i(event.mouseMove.x, event.mouseMove.y));
                // Determine how the cursor has moved
                // Swap these to invert the movement direction
                const sf::Vector2f deltaPos = oldPos - newPos;
                // Move our view accordingly and update the window
                sf::View movedView{ window.getView() };
                movedView.move(deltaPos);

                window.setView(movedView);
                //window.setView(window.getDefaultView());
                // Save the new position as the old one
                // We're recalculating this, since we've changed the view
                oldPos = window.mapPixelToCoords(sf::Vector2i(event.mouseMove.x, event.mouseMove.y));
            }
            else if (event.type == sf::Event::Resized)
            {
                sf::Vector2u size = window.getSize();

                view.setSize((float)size.x, (float)size.y);
                window.setView(view);
                // ImGui draws in window pixels, so the GUI no longer stretches on resize
            }
            else if (event.type == sf::Event::KeyPressed)
            {
                if (event.key.code == sf::Keyboard::PageUp)
                {
                    HaxelPort.incCurrentLevel();
                }
                else if (event.key.code == sf::Keyboard::PageDown)
                {
                    HaxelPort.decCurrentLevel();
                }
            }

        }

        ImGui::SFML::Update(window, guiClock.restart());

        // ---- GUI: build the widgets for this frame (positions from the old stationGUI.txt) ----
        if (gui::iconButton("upLayer", upIcon, ImVec2(4, 20)))
            HaxelPort.incCurrentLevel();
        if (gui::iconButton("downLayer", downIcon, ImVec2(4, 90)))
            HaxelPort.decCurrentLevel();
        if (gui::iconButton("build", buildIcon, ImVec2(4, 160)))
            HaxelPort.toggleBuildMode();
        if (gui::iconButton("bug", bugIcon, ImVec2(4, 230)))
            HaxelPort.toggleDebugMode();
        if (gui::iconButton("save", saveIcon, ImVec2(4, 480)))
            gameMaster.saveStationToDisc(&HaxelPort, "Haxel_Port");

        window.clear(sf::Color::Black);
        HaxelPort.highlightAllTiles(sf::Color::White);
        if (HaxelPort.getBuildMode() == true)
        {
            sf::Mouse mouse;
            gui::listBox("buildMenu", ImVec2(0, 310), ImVec2(160, 142), buildMenuItems, buildMenuSelected);
            sf::Vector2i mousePositionRel = mouse.getPosition(window);
            sf::Vector2f worldPositionCart = HaxelPort.isoToCart(window.mapPixelToCoords(mousePositionRel,window.getView()));
            sf::Vector2f tileCoords = sf::Vector2f(floor(worldPositionCart.x / 32.f), floor(worldPositionCart.y / 32.f));
            tileCoords.x = std::clamp(tileCoords.x, 1.f, HaxelPort.getCurrentIsoLevel()->getSizeW());
            tileCoords.y = std::clamp(tileCoords.y, 1.f, HaxelPort.getCurrentIsoLevel()->getSizeH());
            std::string screenPos = std::to_string(mousePositionRel.x) + ","+ std::to_string(mousePositionRel.y);
            gui::label(ImVec2(60, 164), ImVec2(67.2f, 18), screenPos);
            gui::label(ImVec2(60, 184), ImVec2(67.2f, 18), to_string_with_precision(tileCoords.x, 0) + "," + to_string_with_precision(tileCoords.y, 0));
            HaxelPort.highlightTile(tileCoords, sf::Color::Red);
            if (!guiWantsMouse && mouse.isButtonPressed(sf::Mouse::Button::Left) && floor(worldPositionCart.x / 32.f) < HaxelPort.getCurrentIsoLevel()->getSizeW() && floor(worldPositionCart.y / 32.f) < HaxelPort.getCurrentIsoLevel()->getSizeH())
            {
                //HaxelPort.getCurrentIsoLevel()->printTileInfo(tileCoords);
                const std::string key = buildMenuSelected >= 0 ? buildMenuItems[buildMenuSelected] : textureMaster.getTextureKey(-1);
                HaxelPort.getCurrentIsoLevel()->addObjectToTile(tileCoords, key);
            }
        }
        if (HaxelPort.getDebugMode() == true)
        {
            gui::label(ImVec2(60, 234), ImVec2(67.2f, 18), "FPS: " + std::to_string(fps.getFPS()));
            // debug options (only shown in debug mode)
            gui::toggleButton("tilePosDebug", "Tile\nPos", ImVec2(132, 230), showTilePositions);
            if (showTilePositions)
                HaxelPort.getCurrentIsoLevel()->drawTilePositionDebug(window);
        }
        gui::label(ImVec2(13, 70), ImVec2(30, 18), std::to_string(HaxelPort.getCurrentLevel()), true);

        HaxelPort.drawLayer(window,HaxelPort.getCurrentLevel());
        ImGui::SFML::Render(window);//draw this last so it is always ontop
        //layerOne.draw(window);
        window.display();
        fps.update();
        std::ostringstream ss;
        ss << "FPS: " << fps.getFPS();

        window.setTitle(ss.str());
    }

    ImGui::SFML::Shutdown();
    return 0;
}
