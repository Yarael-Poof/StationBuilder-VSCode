#pragma once

//Dear ImGui on raylib: the input from raylib each frame, and the drawing through rlgl (raylib's immediate mode). call
//newFrame between BeginDrawing and the ImGui calls, render after EndMode3D (in 2D, on top of the scene)
namespace imguiRaylib
{
	void init();
	//after the fonts are added: makes the font texture
	void buildFonts();
	void newFrame();
	void render();
	void shutdown();
}
