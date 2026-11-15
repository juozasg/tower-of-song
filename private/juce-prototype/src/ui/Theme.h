#pragma once

#include <imgui.h>

namespace tos::ui
{

struct Fonts
{
    ImFont* body = nullptr;
    ImFont* bold = nullptr;
    ImFont* title = nullptr; // serif display face
};

Fonts loadFonts();
void applyTheme();

// Font sizes (logical pixels; ImGui rasterises at the display's pixel density).
constexpr float textSize = 16.0f;
constexpr float smallSize = 13.5f;
constexpr float headingSize = 21.0f;

namespace col
{
constexpr ImU32 rgb(int r, int g, int b, int a = 255)
{
    return IM_COL32(r, g, b, a);
}

constexpr ImU32 night = rgb(17, 20, 38);
constexpr ImU32 nightTop = rgb(30, 28, 62);
constexpr ImU32 panel = rgb(30, 35, 62);
constexpr ImU32 panelHi = rgb(42, 49, 86);
constexpr ImU32 stone = rgb(66, 62, 84);
constexpr ImU32 stoneHi = rgb(86, 80, 108);
constexpr ImU32 stoneDark = rgb(46, 43, 60);
constexpr ImU32 gold = rgb(236, 190, 96);
constexpr ImU32 goldDim = rgb(150, 118, 62);
constexpr ImU32 text = rgb(238, 232, 218);
constexpr ImU32 muted = rgb(150, 156, 184);
constexpr ImU32 faint = rgb(95, 100, 130);
constexpr ImU32 green = rgb(98, 204, 140);
constexpr ImU32 red = rgb(228, 108, 108);
constexpr ImU32 blue = rgb(110, 170, 236);
constexpr ImU32 white = rgb(250, 248, 242);
constexpr ImU32 black = rgb(24, 22, 30);
} // namespace col

ImVec4 toVec4(ImU32 c);

// Small helpers used by the screens.
void textColored(ImU32 color, const char* fmt, ...);
void textCentered(ImDrawList* dl, ImFont* font, float size, ImVec2 center, ImU32 color, const char* text);
bool primaryButton(const char* label, ImVec2 size = ImVec2(0, 0));
// "free", "-4%", "-0.5%"
const char* costLabel(float factor);

} // namespace tos::ui
