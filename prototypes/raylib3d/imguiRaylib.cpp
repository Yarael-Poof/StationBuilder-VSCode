#include "imguiRaylib.h"
#include "imgui.h"
#include "raylib.h"
#include "rlgl.h"
#include <cstdint>

namespace
{
	Texture2D fontTexture{};

	//the keys ImGui's text boxes and shortcuts use
	struct keyPair { int raylib; ImGuiKey imgui; };
	const keyPair keys[] = {
		{ KEY_TAB, ImGuiKey_Tab }, { KEY_LEFT, ImGuiKey_LeftArrow }, { KEY_RIGHT, ImGuiKey_RightArrow }, { KEY_UP, ImGuiKey_UpArrow },
		{ KEY_DOWN, ImGuiKey_DownArrow }, { KEY_PAGE_UP, ImGuiKey_PageUp }, { KEY_PAGE_DOWN, ImGuiKey_PageDown }, { KEY_HOME, ImGuiKey_Home },
		{ KEY_END, ImGuiKey_End }, { KEY_INSERT, ImGuiKey_Insert }, { KEY_DELETE, ImGuiKey_Delete }, { KEY_BACKSPACE, ImGuiKey_Backspace },
		{ KEY_SPACE, ImGuiKey_Space }, { KEY_ENTER, ImGuiKey_Enter }, { KEY_ESCAPE, ImGuiKey_Escape }, { KEY_A, ImGuiKey_A },
		{ KEY_C, ImGuiKey_C }, { KEY_V, ImGuiKey_V }, { KEY_X, ImGuiKey_X }, { KEY_Y, ImGuiKey_Y }, { KEY_Z, ImGuiKey_Z },
	};
}

namespace imguiRaylib
{
	void init()
	{
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.BackendPlatformName = "raylib";
		io.BackendRendererName = "rlgl";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
		io.IniFilename = nullptr;
	}

	void buildFonts()
	{
		ImGuiIO& io = ImGui::GetIO();
		unsigned char* pixels = nullptr;
		int width = 0, height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		Image image{ pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
		if (fontTexture.id != 0)
			UnloadTexture(fontTexture);
		fontTexture = LoadTextureFromImage(image);
		SetTextureFilter(fontTexture, TEXTURE_FILTER_POINT);   //VT323 is pixel art
		io.Fonts->SetTexID((ImTextureID)(std::uintptr_t)fontTexture.id);
	}

	void newFrame()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(float(GetScreenWidth()), float(GetScreenHeight()));
		const Vector2 dpi = GetWindowScaleDPI();   //a scaled display: the framebuffer has more pixels than the window
		io.DisplayFramebufferScale = ImVec2(dpi.x, dpi.y);
		io.DeltaTime = GetFrameTime() > 0.f ? GetFrameTime() : 1.f / 60.f;
		const Vector2 mouse = GetMousePosition();
		io.AddMousePosEvent(mouse.x, mouse.y);
		io.AddMouseButtonEvent(0, IsMouseButtonDown(MOUSE_BUTTON_LEFT));
		io.AddMouseButtonEvent(1, IsMouseButtonDown(MOUSE_BUTTON_RIGHT));
		io.AddMouseButtonEvent(2, IsMouseButtonDown(MOUSE_BUTTON_MIDDLE));
		const Vector2 wheel = GetMouseWheelMoveV();
		if (wheel.x != 0.f || wheel.y != 0.f)
			io.AddMouseWheelEvent(wheel.x, wheel.y);
		io.AddKeyEvent(ImGuiMod_Ctrl, IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
		io.AddKeyEvent(ImGuiMod_Shift, IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
		io.AddKeyEvent(ImGuiMod_Alt, IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT));
		for (const keyPair& k : keys)
			io.AddKeyEvent(k.imgui, IsKeyDown(k.raylib));
		for (int c = GetCharPressed(); c != 0; c = GetCharPressed())
			io.AddInputCharacter(unsigned(c));
		ImGui::NewFrame();
	}

	void render()
	{
		ImGui::Render();
		const ImDrawData* data = ImGui::GetDrawData();
		rlDrawRenderBatchActive();
		rlDisableBackfaceCulling();
		const int screenHeight = GetScreenHeight();
		const ImVec2 scale = data->FramebufferScale;   //raylib draws in window units; the clipping is in framebuffer pixels
		for (int n = 0; n < data->CmdListsCount; n++)
		{
			const ImDrawList* list = data->CmdLists[n];
			for (const ImDrawCmd& cmd : list->CmdBuffer)
			{
				if (cmd.UserCallback)
				{
					cmd.UserCallback(list, &cmd);
					continue;
				}
				const ImVec4 clip = cmd.ClipRect;
				rlEnableScissorTest();
				rlScissor(int(clip.x * scale.x), int((float(screenHeight) - clip.w) * scale.y), int((clip.z - clip.x) * scale.x), int((clip.w - clip.y) * scale.y));
				//(rlBegin starts a new batch entry with the blank texture when the mode changes: set the texture after it)
				rlBegin(RL_TRIANGLES);
				rlSetTexture((unsigned int)(std::uintptr_t)cmd.GetTexID());
				for (unsigned i = 0; i < cmd.ElemCount; i++)
				{
					const ImDrawVert& v = list->VtxBuffer[int(list->IdxBuffer[int(cmd.IdxOffset + i)] + cmd.VtxOffset)];
					rlColor4ub((unsigned char)(v.col >> IM_COL32_R_SHIFT), (unsigned char)(v.col >> IM_COL32_G_SHIFT),
						(unsigned char)(v.col >> IM_COL32_B_SHIFT), (unsigned char)(v.col >> IM_COL32_A_SHIFT));
					rlTexCoord2f(v.uv.x, v.uv.y);
					rlVertex2f(v.pos.x, v.pos.y);
				}
				rlEnd();
				rlDrawRenderBatchActive();
			}
		}
		rlSetTexture(0);
		rlDisableScissorTest();
		rlEnableBackfaceCulling();
	}

	void shutdown()
	{
		if (fontTexture.id != 0)
			UnloadTexture(fontTexture);
		ImGui::DestroyContext();
	}
}
