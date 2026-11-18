cmake_minimum_required(VERSION 3.21)
project(StationBuilder LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Prebuilt SFML only ships Debug + Release; use Release libs for other optimized configs
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release "") # "" = fall back to config-less targets (e.g. OpenGL::GL)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release "")

# Dependency locations come from CMAKE_PREFIX_PATH (see CMakePresets.json):
#   SFML 2.6.x, Boost (serialization, filesystem)
find_package(SFML 2.6 COMPONENTS graphics window system audio network REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS serialization filesystem)

# Dear ImGui + ImGui-SFML backend, fetched at configure time (pinned versions)
include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(IMGUI_SFML_FIND_SFML ON CACHE BOOL "" FORCE)
set(IMGUI_SFML_IMGUI_DEMO OFF CACHE BOOL "" FORCE)
FetchContent_Declare(imgui-sfml
    GIT_REPOSITORY https://github.com/SFML/imgui-sfml.git
    GIT_TAG        v2.6.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui-sfml)
find_package(OpenGL REQUIRED) # ImGui-SFML renders with legacy OpenGL calls

# Eigen (header-only linear algebra) for the simulation solvers. Only the headers are needed, so
# SOURCE_SUBDIR points at a folder with no CMakeLists.txt: the source is fetched but Eigen's own
# build (tests, docs, BLAS checks) is never configured.
FetchContent_Declare(eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        3.4.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  headers-only)
FetchContent_MakeAvailable(eigen)
add_library(eigen_headers INTERFACE)
target_include_directories(eigen_headers SYSTEM INTERFACE ${eigen_SOURCE_DIR})

# toml++ (header-only TOML parser) for the content files in StationBuilder/content. Headers only, the same way as Eigen
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  headers-only)
FetchContent_MakeAvailable(tomlplusplus)
add_library(toml_headers INTERFACE)
target_include_directories(toml_headers SYSTEM INTERFACE ${tomlplusplus_SOURCE_DIR}/include)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/StationBuilder)

# the simulation's matrix solver, built optimised even in Debug (see the comment in that file)
add_subdirectory(cmake/optimised)

# the game's code, everything but main(): compiled once, then linked into the game and into its tools
add_library(stationCode OBJECT
    ${SRC_DIR}/gameManager.cpp
    ${SRC_DIR}/isometricLevel.cpp
    ${SRC_DIR}/station.cpp
    ${SRC_DIR}/tile.cpp
    ${SRC_DIR}/tileObject.cpp
    ${SRC_DIR}/tileCatalogue.cpp
    ${SRC_DIR}/buildMenu.cpp
    ${SRC_DIR}/simulation.cpp
    ${SRC_DIR}/tileNetwork.cpp
    ${SRC_DIR}/networkOverlay.cpp
    ${SRC_DIR}/electricalNetwork.cpp
    ${SRC_DIR}/powerView.cpp
    ${SRC_DIR}/tilePanel.cpp
    ${SRC_DIR}/thermalNetwork.cpp
    ${SRC_DIR}/thermalView.cpp
    ${SRC_DIR}/fluidNetwork.cpp
    ${SRC_DIR}/fluidView.cpp
    ${SRC_DIR}/lineArt.cpp
    ${SRC_DIR}/content.cpp
    ${SRC_DIR}/contentArt.cpp
    ${SRC_DIR}/contentPanel.cpp
    ${SRC_DIR}/machineView.cpp
    ${SRC_DIR}/logistics.cpp
    ${SRC_DIR}/logisticsView.cpp
    ${SRC_DIR}/processing.cpp
    ${SRC_DIR}/contentWriter.cpp
    ${SRC_DIR}/contentEditor.cpp
    ${SRC_DIR}/blueprint.cpp
    ${SRC_DIR}/blueprintTool.cpp
    ${SRC_DIR}/decks.cpp
    ${SRC_DIR}/exteriorView.cpp
    ${SRC_DIR}/uiTheme.cpp
    ${SRC_DIR}/screenEffects.cpp
    ${SRC_DIR}/textDecode.cpp
)

# PUBLIC: what the game and its tools compile and link with too
target_include_directories(stationCode PUBLIC ${SRC_DIR})

target_link_libraries(stationCode PUBLIC
    sfml-graphics sfml-window sfml-system sfml-audio sfml-network
    ImGui-SFML::ImGui-SFML OpenGL::GL
    Boost::serialization Boost::filesystem
    nodalSolver
    toml_headers
)

if(MSVC)
    # Match the original .vcxproj settings
    target_compile_options(stationCode PUBLIC /W3 /sdl /wd4996 /utf-8)
    target_compile_definitions(stationCode PUBLIC _CONSOLE)
    # Boost auto-linking is handled by the imported CMake targets
    target_compile_definitions(stationCode PUBLIC BOOST_ALL_NO_LIB)
endif()

# the game
add_executable(StationBuilder
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/StationBuilder.rc
)
target_link_libraries(StationBuilder PRIVATE stationCode)

# Copy SFML runtime DLLs next to the executable so it runs without PATH tweaks
add_custom_command(TARGET StationBuilder POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:StationBuilder> $<TARGET_FILE_DIR:StationBuilder>
    COMMAND_EXPAND_LISTS
)

# SFML audio needs OpenAL, which isn't a CMake target
find_file(OPENAL_DLL openal32.dll PATHS ${CMAKE_PREFIX_PATH} PATH_SUFFIXES bin NO_DEFAULT_PATH)
if(OPENAL_DLL)
    add_custom_command(TARGET StationBuilder POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENAL_DLL} $<TARGET_FILE_DIR:StationBuilder>
    )
endif()

# The game loads assets (tiles/, gui/, *.level) relative to the working directory,
# so run/debug from the source folder. VS Code launch.json sets cwd to this too.
set_property(TARGET StationBuilder PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${SRC_DIR})

# Tools, built only when asked for (cmake --build build/debug --target ReadmeShots) and run from the source folder
# like the game. ReadmeShots draws the README's screenshots into docs/screenshots (see the top of the file).
add_executable(ReadmeShots EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/tools/readmeShots.cpp)
target_link_libraries(ReadmeShots PRIVATE stationCode)
set_property(TARGET ReadmeShots PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${SRC_DIR})
