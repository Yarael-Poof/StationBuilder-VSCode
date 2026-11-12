cmake_minimum_required(VERSION 3.21)
project(StationBuilder LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Prebuilt SFML/TGUI only ship Debug + Release; use Release libs for other optimized configs
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)

# Dependency locations come from CMAKE_PREFIX_PATH (see CMakePresets.json):
#   SFML 2.6.x, TGUI 0.9.x, Boost (serialization, filesystem)
find_package(SFML 2.6 COMPONENTS graphics window system audio network REQUIRED)
find_package(TGUI 0.9 REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS serialization filesystem)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/StationBuilder)

add_executable(StationBuilder
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/gameManager.cpp
    ${SRC_DIR}/isometricLevel.cpp
    ${SRC_DIR}/station.cpp
    ${SRC_DIR}/tile.cpp
    ${SRC_DIR}/tileObject.cpp
    ${SRC_DIR}/StationBuilder.rc
)

target_include_directories(StationBuilder PRIVATE ${SRC_DIR})

target_link_libraries(StationBuilder PRIVATE
    sfml-graphics sfml-window sfml-system sfml-audio sfml-network
    tgui
    Boost::serialization Boost::filesystem
)

if(MSVC)
    # Match the original .vcxproj settings
    target_compile_options(StationBuilder PRIVATE /W3 /sdl /wd4996 /utf-8)
    target_compile_definitions(StationBuilder PRIVATE _CONSOLE)
    # Boost auto-linking is handled by the imported CMake targets
    target_compile_definitions(StationBuilder PRIVATE BOOST_ALL_NO_LIB)
endif()

# Copy SFML/TGUI/Boost runtime DLLs next to the executable so it runs without PATH tweaks
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
