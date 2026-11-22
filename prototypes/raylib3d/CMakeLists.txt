# The StationBuilder 3D prototype: raylib and Dear ImGui, drawing the scene the game's SceneExport tool writes.
# A project of its own (it never touches the game's build). build.bat in this folder configures and builds it into
# build/raylib3d at the repository's root.
cmake_minimum_required(VERSION 3.21)
project(StationBuilder3DPrototype LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
# raylib: just the library
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(raylib)
# Dear ImGui, the same version as the game's
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

set(GAME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../StationBuilder)

add_executable(Raylib3DDemo
    demo.cpp
    imguiRaylib.cpp
    ${GAME_DIR}/uiTheme.cpp              # the game's amber theme (it only needs ImGui)
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(Raylib3DDemo PRIVATE ${imgui_SOURCE_DIR} ${GAME_DIR})
target_link_libraries(Raylib3DDemo PRIVATE raylib)
if(MSVC)
    target_compile_options(Raylib3DDemo PRIVATE /utf-8)
endif()
