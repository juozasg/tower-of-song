#include "ui/Theme.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>

namespace tos::ui
{

namespace
{
ImFont* tryFont(const char* const* candidates, int count)
{
    for (int i = 0; i < count; ++i)
        if (std::filesystem::exists(candidates[i]))
            if (auto* f = ImGui::GetIO().Fonts->AddFontFromFileTTF(candidates[i]))
                return f;
    return nullptr;
}
} // namespace

Fonts loadFonts()
{
    static const char* body[] = { "/System/Library/Fonts/Supplemental/Arial.ttf", "C:/Windows/Fonts/segoeui.ttf",
                                  "C:/Windows/Fonts/arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf" };
    static const char* bold[] = { "/System/Library/Fonts/Supplemental/Arial Bold.ttf", "C:/Windows/Fonts/segoeuib.ttf",
                                  "C:/Windows/Fonts/arialbd.ttf",
                                  "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" };
    static const char* title[] = { "/System/Library/Fonts/Supplemental/Georgia Bold.ttf", "C:/Windows/Fonts/georgiab.ttf",
                                   "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf" };

    Fonts f;
    f.body = tryFont(body, IM_ARRAYSIZE(body));
    if (f.body == nullptr)
        f.body = ImGui::GetIO().Fonts->AddFontDefault();
    f.bold = tryFont(bold, IM_ARRAYSIZE(bold));
    if (f.bold == nullptr)
        f.bold = f.body;
    f.title = tryFont(title, IM_ARRAYSIZE(title));
    if (f.title == nullptr)
        f.title = f.bold;
    return f;
}

ImVec4 toVec4(ImU32 c)
{
    return ImGui::ColorConvertU32ToFloat4(c);
}

void applyTheme()
{
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.FontSizeBase = textSize;
    s.WindowRounding = 0.0f;
    s.ChildRounding = 10.0f;
    s.FrameRounding = 7.0f;
    s.GrabRounding = 7.0f;
    s.PopupRounding = 8.0f;
    s.WindowPadding = ImVec2(16, 14);
    s.FramePadding = ImVec2(12, 7);
    s.ItemSpacing = ImVec2(10, 8);
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;

    auto* c = s.Colors;
    c[ImGuiCol_Text] = toVec4(col::text);
    c[ImGuiCol_TextDisabled] = toVec4(col::faint);
    c[ImGuiCol_WindowBg] = toVec4(col::night);
    c[ImGuiCol_ChildBg] = toVec4(col::panel);
    c[ImGuiCol_PopupBg] = toVec4(col::panelHi);
    c[ImGuiCol_Border] = toVec4(col::panelHi);
    c[ImGuiCol_FrameBg] = toVec4(col::panelHi);
    c[ImGuiCol_FrameBgHovered] = toVec4(col::stoneHi);
    c[ImGuiCol_FrameBgActive] = toVec4(col::stoneHi);
    c[ImGuiCol_Button] = toVec4(col::panelHi);
    c[ImGuiCol_ButtonHovered] = toVec4(col::stoneHi);
    c[ImGuiCol_ButtonActive] = toVec4(col::goldDim);
    c[ImGuiCol_Header] = toVec4(col::panelHi);
    c[ImGuiCol_HeaderHovered] = toVec4(col::stoneHi);
    c[ImGuiCol_HeaderActive] = toVec4(col::goldDim);
    c[ImGuiCol_SliderGrab] = toVec4(col::gold);
    c[ImGuiCol_SliderGrabActive] = toVec4(col::gold);
    c[ImGuiCol_CheckMark] = toVec4(col::gold);
    c[ImGuiCol_Separator] = toVec4(col::panelHi);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.03f, 0.03f, 0.08f, 0.7f);
    c[ImGuiCol_PlotHistogram] = toVec4(col::gold);
}

void textColored(ImU32 color, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}

void textCentered(ImDrawList* dl, ImFont* font, float size, ImVec2 center, ImU32 color, const char* text)
{
    ImGui::PushFont(font, size);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    ImGui::PopFont();
    dl->AddText(font, size, ImVec2(std::floor(center.x - ts.x * 0.5f), std::floor(center.y - ts.y * 0.5f)), color, text);
}

bool primaryButton(const char* label, ImVec2 size)
{
    ImGui::PushStyleColor(ImGuiCol_Button, toVec4(col::gold));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toVec4(col::rgb(248, 210, 130)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, toVec4(col::goldDim));
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(col::black));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

const char* costLabel(float factor)
{
    static char buf[32];
    if (factor >= 0.9999f)
        return "free";
    const float pct = (1.0f - factor) * 100.0f;
    if (pct < 0.95f)
        std::snprintf(buf, sizeof(buf), "-%.1f%%", pct);
    else
        std::snprintf(buf, sizeof(buf), "-%.0f%%", pct);
    return buf;
}

} // namespace tos::ui
