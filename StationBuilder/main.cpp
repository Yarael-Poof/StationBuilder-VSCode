#include <SFML/Graphics.hpp>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui-SFML.h>
#include "isometricLevel.h"
#include "textureManager.h"
#include "gameManager.h"
#include "station.h"
#include "buildMenu.h"
#include "tileCatalogue.h"
#include "simulation.h"
#include "tileNetwork.h"
#include "networkOverlay.h"
#include "electricalNetwork.h"
#include "powerView.h"
#include "tilePanel.h"
#include "thermalNetwork.h"
#include "thermalView.h"
#include "fluidNetwork.h"
#include "fluidView.h"
#include "logistics.h"
#include "logisticsView.h"
#include "processing.h"
#include "blueprintTool.h"
#include "decks.h"
#include "exteriorView.h"
#include "uiTheme.h"
#include "screenEffects.h"
#include "textDecode.h"
#include "lineArt.h"
#include "content.h"
#include "contentPanel.h"
#include "contentEditor.h"
#include "fps.h"
#include <iostream>
#include <cstdio>
#include <cmath>
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
// a column of 48x48 icon buttons on the left, black labels beside them. The build panel
// along the bottom lives in buildMenu.cpp.
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

    // the terminal look (uiTheme.h), with the old layout's spacing: no padding or spacing by default (the icon column
    // and labels are placed exactly), each panel setting its own
    void applyStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(0, 0);
        style.WindowMinSize = ImVec2(1, 1);
        style.FramePadding = ImVec2(0, 0);
        style.ItemSpacing = ImVec2(0, 0);
        style.ScrollbarSize = 14.f;
        uiTheme::apply();
    }

    // an icon in the theme's hue on its button (the icons are drawn white)
    sf::Color iconTint()
    {
        const ImVec4 c = uiTheme::text();
        return sf::Color(sf::Uint8(c.x * 255.f), sf::Uint8(c.y * 255.f), sf::Uint8(c.z * 255.f));
    }

    // TGUI BitmapButton equivalent: 48x48 image button at a fixed screen position.
    bool iconButton(const char* id, const sf::Texture& icon, ImVec2 pos)
    {
        ImGui::SetNextWindowPos(pos);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::Begin(id, nullptr, overlayFlags);
        bool clicked = ImGui::ImageButton(id, icon, sf::Vector2f(48.f, 48.f), sf::Color::Transparent, iconTint());
        ImGui::End();
        ImGui::PopStyleVar();
        return clicked;
    }

    // 48x48 icon button that stays lit while its mode is on. returns true when clicked.
    bool iconToggle(const char* id, const sf::Texture& icon, ImVec2 pos, bool on)
    {
        ImGui::SetNextWindowPos(pos);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::Begin(id, nullptr, overlayFlags);
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, uiTheme::fillActive());
        bool clicked = ImGui::ImageButton(id, icon, sf::Vector2f(48.f, 48.f), sf::Color::Transparent, iconTint());
        if (on)
            ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleVar();
        return clicked;
    }

    // 48x48 text button that toggles a flag; stays lit while on.
    bool toggleButton(const char* id, const char* text, ImVec2 pos, bool& on)
    {
        ImGui::SetNextWindowPos(pos);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::Begin(id, nullptr, overlayFlags);
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, uiTheme::fillActive());
        bool clicked = ImGui::Button(text, ImVec2(48.f, 48.f));
        if (on)
            ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleVar();
        if (clicked)
            on = !on;
        return clicked;
    }

    // TGUI Label equivalent: non-interactive text on a panel-coloured box at a fixed screen position.
    void label(ImVec2 pos, ImVec2 size, const std::string& text, bool centred = false)
    {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const ImVec2 max = pos + size;
        dl->AddRectFilled(pos, max, uiTheme::u32(uiTheme::panel()));
        dl->AddRect(pos, max, uiTheme::u32(uiTheme::faint()));
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const float x = centred ? pos.x + (size.x - textSize.x) * 0.5f : pos.x + 3.f;
        const float y = pos.y + (size.y - textSize.y) * 0.5f;
        dl->PushClipRect(pos, max, true);
        dl->AddText(ImVec2(x, y), uiTheme::u32(uiTheme::text()), text.c_str());
        dl->PopClipRect();
    }

    sf::Texture loadIcon(const std::string& path)
    {
        sf::Texture texture;
        if (!texture.loadFromFile(path))
            std::cout << "[GUI] Cannot load icon " << path << std::endl;
        texture.setSmooth(true);
        return texture;
    }

    // The build panel and the time controls can be drawn bigger than the rest of the GUI (for 4K screens).
    // ImGui fonts are bitmaps, so rather than stretching the 14 px font, a second copy is rasterised at
    // the panel size; changing the scale reloads both.
    const float guiBaseFontSize = uiTheme::baseFontSize;
    const float minPanelScale = 1.f;
    const float maxPanelScale = 3.f;
    const float panelScaleStep = 0.25f;

    struct guiFonts
    {
        ImFont* normal = nullptr;   // everything else
        ImFont* panel = nullptr;    // build panel and time controls
    };

    // call outside a frame (before ImGui::SFML::Update), never between Update and Render
    bool loadFonts(guiFonts& fonts, float panelScale)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();
        // VT323, a terminal font: crisp at whole pixel sizes, so no oversampling. the degree sign is in Latin-1
        ImFontConfig crisp;
        crisp.OversampleH = 1;
        crisp.OversampleV = 1;
        crisp.PixelSnapH = true;
        fonts.normal = io.Fonts->AddFontFromFileTTF(uiTheme::fontFile, uiTheme::fontSize(1.f), &crisp, io.Fonts->GetGlyphRangesDefault());   // the first font added is the default
        if (!fonts.normal)
            fonts.normal = io.Fonts->AddFontDefault();
        fonts.panel = io.Fonts->AddFontFromFileTTF(uiTheme::fontFile, uiTheme::fontSize(panelScale), &crisp, io.Fonts->GetGlyphRangesDefault());
        if (!fonts.panel)
        {
            ImFontConfig config;
            config.SizePixels = std::round(13.f * panelScale);
            fonts.panel = io.Fonts->AddFontDefault(&config);
        }
        return ImGui::SFML::UpdateFontTexture();
    }

    // starting scale from the desktop height: 1 at 1080p, 1.25 at 1440p, 2 at 4K
    float defaultPanelScale()
    {
        const float byHeight = sf::VideoMode::getDesktopMode().height / 1080.f;
        return std::clamp(std::round(byHeight / panelScaleStep) * panelScaleStep, minPanelScale, maxPanelScale);
    }

    // game time as h:mm:ss
    std::string clockText(double seconds)
    {
        const long long total = (long long)seconds;
        char text[32];
        snprintf(text, sizeof(text), "%lld:%02lld:%02lld", total / 3600, (total / 60) % 60, total % 60);
        return text;
    }

    // top right: pause/play, the speed buttons, and the simulation clock. Space and 1/2/3 do the same.
    // drawn at the panel scale with the panel font. returns the screen y of its bottom edge
    float timeControls(simulationManager& sim, bool showDebug, std::uint64_t stationRevision, float scale, ImFont* font)
    {
        const float s = scale;
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        if (font)
            ImGui::PushFont(font);
        ImGui::SetNextWindowPos(ImVec2(display.x - 8.f * s, 8.f * s), ImGuiCond_Always, ImVec2(1.f, 0.f)); // pinned by its top right corner
        ImGui::PushStyleColor(ImGuiCol_WindowBg, uiTheme::panel());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * s, 8 * s));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4 * s, 6 * s));
        ImGui::Begin("timeControls", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);

        const bool paused = sim.isPaused();
        if (paused)
            ImGui::PushStyleColor(ImGuiCol_Button, uiTheme::fillActive());
        if (ImGui::Button(paused ? "Play" : "Pause", ImVec2(56 * s, 28 * s)))
            sim.togglePaused();
        if (paused)
            ImGui::PopStyleColor();

        for (int speed : { 1, 2, 4 })
        {
            ImGui::SameLine();
            const bool current = sim.getSpeed() == speed;
            if (current)
                ImGui::PushStyleColor(ImGuiCol_Button, uiTheme::fillActive());
            const std::string text = std::to_string(speed) + "x";
            if (ImGui::Button(text.c_str(), ImVec2(36 * s, 28 * s)))
                sim.setSpeed(speed);
            if (current)
                ImGui::PopStyleColor();
        }

        ImGui::Text("%s   tick %llu", clockText(sim.getSimTime()).c_str(), (unsigned long long)sim.getTickCount());
        if (paused)
            ImGui::TextColored(uiTheme::warning, "PAUSED");
        if (showDebug)
            ImGui::TextDisabled("station rev %llu", (unsigned long long)stationRevision);

        const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        if (font)
            ImGui::PopFont();
        return bottom;
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
    float panelScale = gui::defaultPanelScale();   // size of the build panel and time controls; Ctrl + / Ctrl - change it
    bool panelScaleChanged = false;
    gui::guiFonts fonts;
    screenEffects screen;           // the GUI's glow and scanlines (F8), and its power-on as the game starts
    textDecode decoder;             // new text types itself in, flickering through random characters (with the effects)
    if (!gui::loadFonts(fonts, panelScale))
        return EXIT_FAILURE;
    gui::applyStyle();

    // white arrows (the copies in gui/ are black and would be invisible on the black buttons)
    const sf::Texture upIcon = gui::loadIcon("up.png");
    const sf::Texture downIcon = gui::loadIcon("down.png");
    const sf::Texture buildIcon = gui::loadIcon("gui/build.png");
    const sf::Texture bugIcon = gui::loadIcon("gui/bug.png");
    const sf::Texture saveIcon = gui::loadIcon("gui/save.png");
    const sf::Texture bulldozerIcon = gui::loadIcon("gui/bulldozer.png");
    const sf::Texture loadStationIcon = gui::loadIcon("gui/load.png");
    const sf::Texture powerIcon = gui::loadIcon("gui/power.png");
    const sf::Texture heatIcon = gui::loadIcon("gui/thermal.png");
    const sf::Texture liquidIcon = gui::loadIcon("gui/liquid.png");
    const sf::Texture itemsIcon = gui::loadIcon("gui/items.png");
    const sf::Texture copyIcon = gui::loadIcon("gui/copy.png");
    const sf::Texture blueprintIcon = gui::loadIcon("gui/blueprint.png");
    const sf::Texture decksIcon = gui::loadIcon("gui/decks.png");
    const sf::Texture exteriorIcon = gui::loadIcon("gui/exterior.png");

    simulationManager simulation;   // fixed 10 Hz tick for the electrical (later thermal, liquid) simulations
    buildMenu buildBar;             // the panel along the bottom: tabs, item icons and a preview
    // copy and paste, and blueprints: C selects tiles to copy, B opens the Blueprints menu (blueprints/ folder)
    blueprintTool blueprintMaker;
    // the decks: each one's area (any shape) and adding or taking them away (D); the station seen from outside (O)
    deckPanel deckTools;
    bool showExterior = false;
    buildBar.setScale(panelScale, fonts.panel);
    bool showTilePositions = false; // debug option: overlay each tile's index + world position
    bool showPowerNetwork = false;  // debug option: draw the electrical network's links, coloured by network
    bool showPowerView = false;     // the power view: voltages, currents, flow, generator load (P key or the bolt button)
    bool showHeatView = false;      // the heat view: temperatures and leaks to space (T key or the thermometer button)
    // the two views both tint the tiles, so only one is shown at a time
    bool showLiquidView = false;    // the liquid view: pipes, flow, tanks, pumps, heat exchangers (L key or the droplet button)
    bool showItemsView = false;     // the logistics view: item tubes, what they move, blocked outputs (I key or the crate button)
    // the views all tint the tiles, so only one is shown at a time
    auto setPowerView = [&](bool _on) { showPowerView = _on; if (_on) { showHeatView = false; showLiquidView = false; showItemsView = false; } };
    auto setHeatView = [&](bool _on) { showHeatView = _on; if (_on) { showPowerView = false; showLiquidView = false; showItemsView = false; } };
    auto setLiquidView = [&](bool _on) { showLiquidView = _on; if (_on) { showPowerView = false; showHeatView = false; showItemsView = false; } };
    auto setItemsView = [&](bool _on) { showItemsView = _on; if (_on) { showPowerView = false; showHeatView = false; showLiquidView = false; } };
    tilePanel selection;            // the selected tile's panel: its objects, readings and settings (left click, Esc closes)
    double powerAnimationSeconds = 0.0;   // moves the power view's flow dots; runs with the simulation's speed, stops when paused
    bool deleteMode = false;        // left click removes the top object of a tile (Delete key or bulldozer button)



    gameManager gameMaster;
    //this is a master global scope object to hold all our textures for all levels,
    //we must pass a pointer of this to all isometricLevels so they can get their textures.
    //get them by using getTexture("name"), which returns a pointer to the texture, this keeps them always in scope
    //for any sprite of any level.
    textureManager textureMaster;
    // every item, material and family comes from the content files (content/*.toml, see content/README.md), and
    // each item's art is registered under its key from there. F5 reads them again while the game runs, F6 shows them
    const std::string contentFolder = "content";
    contentPanel contentWindow;
    // F7: the content editor, a form for an item's or material's definition that writes it into its file and reloads.
    // the content panel (F6) and the tile panel's "edit" buttons open it on an item
    contentEditor editor(contentFolder);
    contentWindow.onEditItem = [&](const std::string& _key) { editor.editItem(_key); };
    contentWindow.onEditMaterial = [&](const std::string& _key) { editor.editMaterial(_key); };
    contentWindow.onNewItem = [&]() { editor.newItem(); };
    selection.onEditDefinition = [&](const std::string& _key) { editor.editItem(_key); };
    content::load(contentFolder);
    content::loadArt(textureMaster);
    if (content::lastReport().errors() > 0)
        contentWindow.open();
    if (!findTileType("empty") || !textureMaster.hasTexture("empty"))
    {
        std::cout << "[ERROR] the content files have no usable \"empty\" item (the blank tile every level is made of)\n";
        return EXIT_FAILURE;
    }
    std::uint64_t lineArtRevision = 0;   // the station revision the line pieces were last drawn for



    station HaxelPort(&textureMaster);
    HaxelPort.setStationName("Haxel_Port");   // save and load use this name; a future dialog can change it
    // a new game: a small walled room on the lowest deck, an octagonal deck above it to build on (decks.h)
    decks::startingStation(HaxelPort);

    // the AC electrical simulation: rebuilds its circuits when the station changes, solves every tick
    electricalNetwork power(HaxelPort);
    simulation.addSystem(&power);
    // the heat simulation: every floored tile's temperature. added after the electrical network so each tick it
    // uses that tick's heat from cables, lights, heaters and generators
    // the liquid simulation: pipes, pumps (powered by the electrical network), tanks and heat exchangers
    fluidNetwork liquid(HaxelPort, &power);
    simulation.addSystem(&liquid);
    thermalNetwork heat(HaxelPort, &power);
    heat.addHeatSource(&liquid);    // heat exchangers warm or cool their tiles' air
    liquid.setTemperatureSource(&heat);
    simulation.addSystem(&heat);
    // and back the other way: heaters' thermostats, hot generators and cable temperatures read the tile temperatures
    power.setTemperatureSource(&heat);
    // the item tubes: move solids between containers' item ports
    logisticsNetwork items(HaxelPort);
    simulation.addSystem(&items);
    // machines that make things: after the others, so each tick they work with that tick's power, liquids and items.
    // an idle one switches its power off; a working one may give off heat
    processingSystem machines(HaxelPort, &power, &liquid, &heat);
    simulation.addSystem(&machines);
    heat.addHeatSource(&machines);

    //isometricLevel layerOne(&textureMaster,40,40,64);
    //layerOne.fillFloors();

    //gameMaster.saveLevelToDisc(HaxelPort.getCurrentIsoLevel(), "test2");
    //gameMaster.loadLevelFromDisc(layerTwo, "test2", &textureMaster);
    //HaxelPort.addIsoLevel(layerTwo, 2);


    // build and delete are mutually exclusive: turning delete on switches build off
    auto setDeleteMode = [&](bool _on) {
        deleteMode = _on;
        if (_on && HaxelPort.getBuildMode())
            HaxelPort.toggleBuildMode();
        if (_on)
        {
            blueprintMaker.stop();
            deckTools.stopTool();
        }
    };
    // the copy tool and placing a copy or blueprint switch building, bulldozing and the deck area tools off
    auto leaveBuildModes = [&]() {
        if (HaxelPort.getBuildMode())
            HaxelPort.toggleBuildMode();
        deleteMode = false;
        deckTools.stopTool();
    };
    // the outside view: every tool off, and no tile selected
    auto setExterior = [&](bool _on) {
        showExterior = _on;
        if (_on)
        {
            leaveBuildModes();
            blueprintMaker.stop();
            selection.clear();
        }
    };

    sf::View view(window.getDefaultView());
    window.clear(sf::Color::White);
    while (window.isOpen())
    {
        // true while the cursor is over a GUI widget: world input is ignored then
        const bool guiWantsMouse = ImGui::GetIO().WantCaptureMouse;
        // true for one frame when the left button goes down (a press event, not a held button),
        // so a single click places a single object no matter how long the button is held
        bool leftClicked = false;

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
                if (event.mouseButton.button == sf::Mouse::Left) {
                    leftClicked = true;   // place or delete, one per press
                }
                else if (event.mouseButton.button == sf::Mouse::Right && !guiWantsMouse) {
                    moving = true;        // right button drags the view
                    oldPos = window.mapPixelToCoords(sf::Vector2i(event.mouseButton.x, event.mouseButton.y));
                }
            }
            else if (event.type == sf::Event::MouseButtonReleased)
            {
                if (event.mouseButton.button == sf::Mouse::Right) {
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
                if (event.key.code == sf::Keyboard::Escape && !ImGui::GetIO().WantTextInput)
                {
                    // Esc steps back: out of copying or placing a copy, building or bulldozing first, then the selected tile's
                    // panel closes
                    if (deckTools.escape() || blueprintMaker.escape())
                        ;
                    else if (showExterior)
                        setExterior(false);
                    else if (HaxelPort.getBuildMode())
                        HaxelPort.toggleBuildMode();
                    else if (deleteMode)
                        setDeleteMode(false);
                    else
                        selection.clear();
                }
                else if (event.key.code == sf::Keyboard::F5)
                {
                    // the content files again: every item's numbers, art and settings, live
                    const content::report& report = content::reload(contentFolder, HaxelPort, textureMaster);
                    contentWindow.notice(content::summaryLine(report), report.errors() > 0);
                    if (report.errors() > 0)
                        contentWindow.open();
                }
                else if (event.key.code == sf::Keyboard::F6)
                {
                    contentWindow.toggle();
                }
                else if (event.key.code == sf::Keyboard::F7)
                {
                    editor.toggle();
                }
                else if (event.key.code == sf::Keyboard::F8)
                {
                    // F8: the GUI's glow and scanlines on or off; Shift+F8: amber or orange
                    if (event.key.shift)
                    {
                        uiTheme::setHue(uiTheme::currentHue() == uiTheme::hue::amber ? uiTheme::hue::orange : uiTheme::hue::amber);
                        contentWindow.notice(std::string("GUI colour: ") + uiTheme::hueName(uiTheme::currentHue()), false);
                    }
                    else
                    {
                        screen.enabled = !screen.enabled;
                        if (screen.enabled)
                            screen.powerOn();
                        contentWindow.notice(screen.enabled ? "screen effects on (F8)" : "screen effects off (F8)", false);
                    }
                }
                else if (!ImGui::GetIO().WantTextInput)   // leave these keys alone while typing into a text box
                {
                    const bool zoomIn = event.key.code == sf::Keyboard::Equal || event.key.code == sf::Keyboard::Add;
                    const bool zoomOut = event.key.code == sf::Keyboard::Hyphen || event.key.code == sf::Keyboard::Subtract;
                    if (event.key.control && (zoomIn || zoomOut))
                    {
                        // GUI scale: the fonts are reloaded before the next frame starts
                        const float newScale = std::clamp(panelScale + (zoomIn ? gui::panelScaleStep : -gui::panelScaleStep),
                            gui::minPanelScale, gui::maxPanelScale);
                        panelScaleChanged = panelScaleChanged || newScale != panelScale;
                        panelScale = newScale;
                    }
                    else if (event.key.code == sf::Keyboard::Delete)
                        setDeleteMode(!deleteMode);
                    else if (event.key.code == sf::Keyboard::PageUp)
                        HaxelPort.incCurrentLevel();
                    else if (event.key.code == sf::Keyboard::PageDown)
                        HaxelPort.decCurrentLevel();
                    else if (event.key.code == sf::Keyboard::P)
                        setPowerView(!showPowerView);
                    else if (event.key.code == sf::Keyboard::T)
                        setHeatView(!showHeatView);
                    else if (event.key.code == sf::Keyboard::C && event.key.control)
                        blueprintMaker.copySelection(HaxelPort);
                    else if (event.key.code == sf::Keyboard::V && event.key.control)
                    {
                        leaveBuildModes();
                        blueprintMaker.pasteCopy();
                    }
                    else if (event.key.code == sf::Keyboard::C)
                    {
                        if (blueprintMaker.current() == blueprintTool::mode::selecting)
                            blueprintMaker.stop();
                        else
                        {
                            leaveBuildModes();
                            blueprintMaker.startSelecting();
                        }
                    }
                    else if (event.key.code == sf::Keyboard::O)
                        setExterior(!showExterior);
                    else if (event.key.code == sf::Keyboard::D)
                        deckTools.toggle();
                    else if (event.key.code == sf::Keyboard::B)
                        blueprintMaker.togglePanel();
                    else if (event.key.code == sf::Keyboard::R && blueprintMaker.current() == blueprintTool::mode::placing)
                        blueprintMaker.turn();
                    else if (event.key.code == sf::Keyboard::R && HaxelPort.getBuildMode())
                    {
                        // turn what is about to be placed; with Shift, a cable's or pipe's next shape
                        if (event.key.shift)
                            buildBar.nextShape();
                        else
                            buildBar.rotate();
                    }
                    else if (event.key.code == sf::Keyboard::L)
                        setLiquidView(!showLiquidView);
                    else if (event.key.code == sf::Keyboard::I)
                        setItemsView(!showItemsView);
                    else if (event.key.code == sf::Keyboard::Space)
                        simulation.togglePaused();
                    else if (event.key.code == sf::Keyboard::Num1)
                        simulation.setSpeed(1);
                    else if (event.key.code == sf::Keyboard::Num2)
                        simulation.setSpeed(2);
                    else if (event.key.code == sf::Keyboard::Num3)
                        simulation.setSpeed(4);
                }
            }

        }

        if (panelScaleChanged)
        {
            // fonts can only be rebuilt between frames, i.e. here, before ImGui::SFML::Update
            gui::loadFonts(fonts, panelScale);
            buildBar.setScale(panelScale, fonts.panel);
            panelScaleChanged = false;
            std::cout << "[GUI] panel scale " << panelScale << std::endl;
        }

        // one clock for the GUI and the simulation: the simulation runs however many ticks this frame's time is worth
        const sf::Time frameTime = guiClock.restart();
        ImGui::SFML::Update(window, frameTime);
        simulation.update(frameTime.asSeconds());
        if (!simulation.isPaused())
            powerAnimationSeconds += frameTime.asSeconds() * simulation.getSpeed();

        // ---- GUI: build the widgets for this frame (positions from the old stationGUI.txt) ----
        if (gui::iconButton("upLayer", upIcon, ImVec2(4, 20)))
            HaxelPort.incCurrentLevel();
        if (gui::iconButton("downLayer", downIcon, ImVec2(4, 90)))
            HaxelPort.decCurrentLevel();
        if (gui::iconButton("build", buildIcon, ImVec2(4, 160))) {
            HaxelPort.toggleBuildMode();
            if (HaxelPort.getBuildMode())
            {
                deleteMode = false;
                blueprintMaker.stop();
                deckTools.stopTool();
            }
        }
        if (gui::iconToggle("delete", bulldozerIcon, ImVec2(56, 160), deleteMode))
            setDeleteMode(!deleteMode);
        if (gui::iconToggle("powerView", powerIcon, ImVec2(4, 300), showPowerView))
            setPowerView(!showPowerView);
        if (gui::iconToggle("heatView", heatIcon, ImVec2(56, 300), showHeatView))
            setHeatView(!showHeatView);
        if (gui::iconToggle("liquidView", liquidIcon, ImVec2(108, 300), showLiquidView))
            setLiquidView(!showLiquidView);
        if (gui::iconToggle("itemsView", itemsIcon, ImVec2(4, 352), showItemsView))
            setItemsView(!showItemsView);
        if (gui::iconToggle("copyTool", copyIcon, ImVec2(4, 416), blueprintMaker.current() == blueprintTool::mode::selecting))
        {
            if (blueprintMaker.current() == blueprintTool::mode::selecting)
                blueprintMaker.stop();
            else
            {
                leaveBuildModes();
                blueprintMaker.startSelecting();
            }
        }
        if (gui::iconToggle("blueprints", blueprintIcon, ImVec2(56, 416), blueprintMaker.isPanelOpen()))
            blueprintMaker.togglePanel();
        if (gui::iconToggle("decks", decksIcon, ImVec2(108, 416), deckTools.isOpen()))
            deckTools.toggle();
        if (gui::iconToggle("exterior", exteriorIcon, ImVec2(56, 352), showExterior))
            setExterior(!showExterior);
        if (gui::iconButton("bug", bugIcon, ImVec2(4, 230)))
            HaxelPort.toggleDebugMode();
        if (gui::iconButton("save", saveIcon, ImVec2(4, 480)))
            gameMaster.saveStationToDisc(&HaxelPort, HaxelPort.getStationName());
        if (gui::iconButton("load", loadStationIcon, ImVec2(56, 480)))
            ImGui::OpenPopup("Load station?");
        // the right-hand column: time controls, then the power view's key, then the selected tile's panel
        float rightColumnBottom = gui::timeControls(simulation, HaxelPort.getDebugMode(), HaxelPort.getRevision(), panelScale, fonts.panel);
        // outside: the deck under the mouse
        const int exteriorHover = showExterior && !guiWantsMouse ?
            exteriorView::deckAt(HaxelPort, window.mapPixelToCoords(sf::Mouse::getPosition(window), window.getView())) : -1;
        if (showExterior)
            rightColumnBottom = exteriorView::drawLegend(HaxelPort, exteriorHover, rightColumnBottom + 8.f * panelScale, panelScale, fonts.panel);
        else if (showPowerView)
            rightColumnBottom = powerView::drawLegend(power, rightColumnBottom + 8.f * panelScale, panelScale, fonts.panel);
        else if (showHeatView)
            rightColumnBottom = thermalView::drawLegend(heat, rightColumnBottom + 8.f * panelScale, panelScale, fonts.panel);
        else if (showLiquidView)
            rightColumnBottom = fluidView::drawLegend(liquid, rightColumnBottom + 8.f * panelScale, panelScale, fonts.panel);
        else if (showItemsView)
            rightColumnBottom = logisticsView::drawLegend(items, rightColumnBottom + 8.f * panelScale, panelScale, fonts.panel, &machines);
        selection.draw(HaxelPort, power, &heat, &liquid, rightColumnBottom + 8.f * panelScale, panelScale, fonts.panel, &items, &machines);
        // the Blueprints menu, and what the copy tool or placing is doing
        blueprintMaker.drawPanels(HaxelPort, textureMaster, panelScale, fonts.panel);
        if (blueprintMaker.current() == blueprintTool::mode::placing && (HaxelPort.getBuildMode() || deleteMode))
            leaveBuildModes();   // a blueprint picked from the menu while building: placing it wins
        // the Decks panel; a deck added or taken away renumbers them, so the selected tile goes
        if (deckTools.draw(HaxelPort, panelScale, fonts.panel))
            selection.clear();
        if (deckTools.active() && (HaxelPort.getBuildMode() || deleteMode || blueprintMaker.active()))
        {
            // an area tool just picked: it wins over building, bulldozing and copying
            if (HaxelPort.getBuildMode())
                HaxelPort.toggleBuildMode();
            deleteMode = false;
            blueprintMaker.stop();
        }
        // any tool picked while outside goes back inside to use it
        if (showExterior && (HaxelPort.getBuildMode() || deleteMode || blueprintMaker.active() || deckTools.active()))
            showExterior = false;

        // confirmation before loading, since loading replaces everything on screen
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 10));
        if (ImGui::BeginPopupModal("Load station?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Load \"%s\"?", HaxelPort.getStationName().c_str());
            ImGui::Text("This replaces the current station. Unsaved changes will be lost.");
            if (ImGui::Button("Load", ImVec2(120, 0)))
            {
                gameMaster.loadStationFromDisc(&HaxelPort, HaxelPort.getStationName(), &textureMaster);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);

        window.clear(sf::Color::Black);
        HaxelPort.highlightAllTiles(sf::Color::White);
        // lights, heaters and other loads look as bright as the power they are getting (after the colour reset above,
        // before the cursor highlight below)
        power.update();
        powerView::applyDeviceShading(power, HaxelPort, HaxelPort.getCurrentLevel());

        // the tile under the mouse cursor: the one whose floor diamond the cursor is in (may be off the map)
        const sf::Vector2i mousePixel = sf::Mouse::getPosition(window);
        const sf::Vector2i hoveredTile = HaxelPort.getCurrentIsoLevel()->tileAtWorld(window.mapPixelToCoords(mousePixel, window.getView()));
        const bool hoveredInLevel = HaxelPort.getCurrentIsoLevel()->isInLevel(hoveredTile);
        // the copy tool's drag, or a click placing a copy or blueprint; the tiles it has selected tinted
        blueprintMaker.update(HaxelPort, hoveredTile, hoveredInLevel, leftClicked, sf::Mouse::isButtonPressed(sf::Mouse::Left), guiWantsMouse);
        blueprintMaker.highlight(HaxelPort, hoveredTile, hoveredInLevel);
        // the deck area tools' drag
        deckTools.update(HaxelPort, hoveredTile, hoveredInLevel, leftClicked, sf::Mouse::isButtonPressed(sf::Mouse::Left), guiWantsMouse);
        deckTools.highlight(HaxelPort, hoveredTile, hoveredInLevel);

        if (HaxelPort.getBuildMode() == true || deleteMode)
        {
            const sf::Vector2f tileCoords(hoveredTile);
            if (HaxelPort.getBuildMode() == true)
            {
                buildBar.draw(&textureMaster);
                std::string screenPos = std::to_string(mousePixel.x) + ","+ std::to_string(mousePixel.y);
                gui::label(ImVec2(108, 164), ImVec2(67.2f, 18), screenPos);
                gui::label(ImVec2(108, 184), ImVec2(67.2f, 18), hoveredInLevel ? std::to_string(hoveredTile.x) + "," + std::to_string(hoveredTile.y) : "-");
            }
            if (hoveredInLevel)
                HaxelPort.highlightTile(tileCoords, deleteMode ? sf::Color::Yellow : sf::Color::Red);
            if (!guiWantsMouse && leftClicked && hoveredInLevel)
            {
                if (deleteMode)
                {
                    HaxelPort.getCurrentIsoLevel()->removeTopObjectFromTile(tileCoords);
                }
                else
                {
                    //HaxelPort.getCurrentIsoLevel()->printTileInfo(tileCoords);
                    // the selected item as turned by R, with its shape if it is a cable or pipe
                    const std::string key = buildBar.placementKey();
                    if (!key.empty() && !HaxelPort.getCurrentIsoLevel()->addObjectToTile(tileCoords, key, buildBar.placementSettings()))
                        buildBar.warnAt(tileCoords, blockedMessageFor(key));   // refused: say why next to the tile
                }
            }
        }
        buildBar.drawWarning(window, HaxelPort.getCurrentIsoLevel());
        // F6: the content files' items, materials and problems; and the notice after an F5 reload
        contentWindow.draw(&textureMaster, panelScale, fonts.panel);
        editor.draw(HaxelPort, textureMaster, contentWindow, panelScale, fonts.panel);

        // the electrical network: catch up straight away if anything was built, bulldozed or set this frame (the
        // tick may be paused)
        power.update();
        liquid.update();
        heat.update();
        items.update();
        machines.update();
        // when not building or bulldozing, a click selects a tile (and opens its panel); a click off the map clears it
        // outside, a click on a deck goes inside to it
        if (showExterior && leftClicked && !guiWantsMouse && exteriorHover >= 0)
        {
            HaxelPort.setCurrentLevel(exteriorHover);
            showExterior = false;
        }
        else if (leftClicked && !guiWantsMouse && !HaxelPort.getBuildMode() && !deleteMode && !blueprintMaker.active() && !deckTools.active() && !showExterior)
        {
            if (hoveredInLevel)
                selection.select(HaxelPort.getCurrentLevel(), hoveredTile);
            else
                selection.clear();
        }
        if (showExterior)
            ;   // the views are of the deck shown, not the outside
        else if (showPowerView)
            powerView::drawOverlay(power, HaxelPort.getCurrentLevel(), HaxelPort.getCurrentIsoLevel(), window, powerAnimationSeconds);
        else if (showHeatView)
            thermalView::drawOverlay(heat, HaxelPort.getCurrentLevel(), HaxelPort.getCurrentIsoLevel(), window);
        else if (showLiquidView)
            fluidView::drawOverlay(liquid, HaxelPort.getCurrentLevel(), HaxelPort.getCurrentIsoLevel(), window, powerAnimationSeconds, fonts.panel);
        else if (showItemsView)
            logisticsView::drawOverlay(items, HaxelPort.getCurrentLevel(), HaxelPort.getCurrentIsoLevel(), window, powerAnimationSeconds, fonts.panel,
                &machines);
        if (!showExterior)
        {
            selection.drawOutline(HaxelPort, HaxelPort.getCurrentLevel(), window);
            // where the deck can be built, and the hull of the deck below
            decks::drawGuides(window, HaxelPort, deckTools.current() == deckPanel::tool::grow);
        }

        if (HaxelPort.getDebugMode() == true)
        {
            gui::label(ImVec2(60, 234), ImVec2(67.2f, 18), "FPS: " + std::to_string(fps.getFPS()));
            // debug options (only shown in debug mode)
            gui::toggleButton("tilePosDebug", "Tile\nPos", ImVec2(132, 230), showTilePositions);
            if (showTilePositions)
                HaxelPort.getCurrentIsoLevel()->drawTilePositionDebug(window);
            gui::toggleButton("powerNetDebug", "Power\nNet", ImVec2(184, 230), showPowerNetwork);
            if (showPowerNetwork)
            {
                const tileNetwork& wiring = power.network();
                drawNetworkDebug(wiring, HaxelPort.getCurrentLevel(), HaxelPort.getCurrentIsoLevel(), window);
                gui::label(ImVec2(236, 234), ImVec2(400, 18), "electrical: " + std::to_string(wiring.nodes().size()) + " nodes, " +
                    std::to_string(wiring.edges().size()) + " links, " + std::to_string(wiring.islands().size()) + " networks, " +
                    std::to_string(powerView::countLiveNetworks(power)) + " live (all levels)");
                const double joulesPerKWh = 3.6e6;
                gui::label(ImVec2(236, 254), ImVec2(400, 18), "energy so far: generated " +
                    to_string_with_precision(power.energy().generatedJoules / joulesPerKWh, 3) + " kWh, used " +
                    to_string_with_precision(power.energy().consumedJoules / joulesPerKWh, 3) + " kWh, cables " +
                    to_string_with_precision(power.energy().lostJoules / joulesPerKWh, 3) + " kWh");
            }
        }
        // generator warnings show all the time, on top of the power view; the inspector follows the mouse in the
        // power view (or the debug wiring view)
        powerView::drawAlerts(power, HaxelPort.getCurrentLevel(), HaxelPort.getCurrentIsoLevel(), window, fonts.panel);
        if (showExterior)
            ;
        else if ((showPowerView || (HaxelPort.getDebugMode() && showPowerNetwork)) && !guiWantsMouse && hoveredInLevel)
            powerView::drawInspector(power, HaxelPort.getCurrentLevel(), hoveredTile, fonts.panel);
        else if (showHeatView && !guiWantsMouse && hoveredInLevel)
            thermalView::drawInspector(heat, HaxelPort.getCurrentLevel(), hoveredTile, fonts.panel);
        else if (showLiquidView && !guiWantsMouse && hoveredInLevel)
            fluidView::drawInspector(liquid, HaxelPort.getCurrentLevel(), hoveredTile, fonts.panel);
        else if (showItemsView && !guiWantsMouse && hoveredInLevel)
            logisticsView::drawInspector(items, HaxelPort.getCurrentLevel(), hoveredTile, fonts.panel);
        gui::label(ImVec2(13, 70), ImVec2(30, 18), std::to_string(HaxelPort.getCurrentLevel()), true);

        // cables and pipes show arms only towards what they link to: redo their art whenever the station changes
        if (HaxelPort.getRevision() != lineArtRevision)
        {
            lineArt::update(HaxelPort, textureMaster);
            lineArtRevision = HaxelPort.getRevision();
        }
        if (showExterior)
            exteriorView::draw(window, HaxelPort, exteriorHover);
        else
            HaxelPort.drawLayer(window,HaxelPort.getCurrentLevel());
        // in build mode, a see-through copy of what a click would place, turned as R has set it
        if (HaxelPort.getBuildMode() && hoveredInLevel && !guiWantsMouse)
            buildBar.drawGhost(window, HaxelPort.getCurrentIsoLevel(), hoveredTile, &textureMaster);
        // placing a copy or blueprint: a see-through copy of it under the mouse
        blueprintMaker.drawGhost(window, HaxelPort, textureMaster, hoveredTile, hoveredInLevel && !guiWantsMouse);
        // the GUI last, on top: glowing with faint scanlines when the screen effects are on, else straight onto the window
        if (screen.enabled && screen.available())
        {
            ImGui::Render();   // the finished frame, so new text can type itself in; drawing it below doesn't build it again
            decoder.apply(ImGui::GetDrawData(), frameTime.asSeconds());
            ImGui::SFML::Render(screen.begin(window));
            screen.present(window, panelScale);
        }
        else
            ImGui::SFML::Render(window);
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
