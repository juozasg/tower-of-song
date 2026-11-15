#include "ui/Keyboard.h"

#include "music/Theory.h"
#include "ui/Theme.h"

#include <map>
#include <string>

namespace tos::ui
{

namespace
{
bool isBlack(int pitch)
{
    switch (pitchClass(pitch))
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

std::map<ImGuiID, int> mouseNotes; // note held by the mouse, per keyboard widget
} // namespace

const char* qwertyLabel(int semitone)
{
    static const char* labels[] = { "A", "W", "S", "E", "D", "F", "T", "G", "Y", "H", "U", "J", "K", "O", "L", "P", ";", "'" };
    return semitone >= 0 && semitone < (int) IM_ARRAYSIZE(labels) ? labels[semitone] : nullptr;
}

void pianoKeyboard(const char* id, ImVec2 size, int lowNote, int highNote, const std::array<bool, 128>& held,
                   int qwertyBase, ImFont* labelFont, std::vector<KeyEvent>& events)
{
    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const ImGuiID wid = ImGui::GetItemID();

    int whiteCount = 0;
    for (int p = lowNote; p <= highNote; ++p)
        whiteCount += ! isBlack(p);
    const float whiteW = size.x / (float) whiteCount;
    const float blackW = whiteW * 0.62f, blackH = size.y * 0.62f;

    struct KeyRect
    {
        int pitch;
        ImVec2 a, b;
    };
    std::vector<KeyRect> whites, blacks;
    int wi = 0;
    for (int p = lowNote; p <= highNote; ++p)
    {
        if (! isBlack(p))
        {
            const float x = origin.x + wi * whiteW;
            whites.push_back({ p, ImVec2(x, origin.y), ImVec2(x + whiteW, origin.y + size.y) });
            ++wi;
        }
        else
        {
            const float x = origin.x + wi * whiteW - blackW * 0.5f;
            blacks.push_back({ p, ImVec2(x, origin.y), ImVec2(x + blackW, origin.y + blackH) });
        }
    }

    // Mouse: blacks sit on top, so test them first.
    int hovered = -1;
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
        const ImVec2 m = ImGui::GetIO().MousePos;
        auto inside = [&](const KeyRect& k) { return m.x >= k.a.x && m.x < k.b.x && m.y >= k.a.y && m.y < k.b.y; };
        for (const auto& k : blacks)
            if (inside(k))
                hovered = k.pitch;
        if (hovered < 0)
            for (const auto& k : whites)
                if (inside(k))
                    hovered = k.pitch;
    }

    int& mouseNote = mouseNotes.try_emplace(wid, -1).first->second;
    const int wanted = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) ? hovered : -1;
    if (wanted != mouseNote)
    {
        if (mouseNote >= 0)
            events.push_back({ mouseNote, false });
        if (wanted >= 0)
            events.push_back({ wanted, true });
        mouseNote = wanted;
    }

    auto lit = [&](int p) { return held[(size_t) p] || p == mouseNote; };

    for (const auto& k : whites)
    {
        const ImU32 fill = lit(k.pitch) ? col::blue : (k.pitch == hovered ? col::rgb(232, 230, 222) : col::white);
        dl->AddRectFilled(ImVec2(k.a.x + 1, k.a.y), ImVec2(k.b.x - 1, k.b.y), fill, 4.0f, ImDrawFlags_RoundCornersBottom);
        if (pitchClass(k.pitch) == 0)
        {
            const std::string name = noteName(k.pitch);
            dl->AddText(labelFont, 11.0f, ImVec2(k.a.x + 4, k.b.y - 16), col::faint, name.c_str());
        }
        if (qwertyBase >= 0)
            if (const char* q = qwertyLabel(k.pitch - qwertyBase))
                textCentered(dl, labelFont, 12.0f, ImVec2((k.a.x + k.b.x) * 0.5f, k.b.y - 32), col::goldDim, q);
    }
    for (const auto& k : blacks)
    {
        const ImU32 fill = lit(k.pitch) ? col::blue : (k.pitch == hovered ? col::rgb(70, 66, 84) : col::black);
        dl->AddRectFilled(k.a, k.b, fill, 3.0f, ImDrawFlags_RoundCornersBottom);
        if (qwertyBase >= 0)
            if (const char* q = qwertyLabel(k.pitch - qwertyBase))
                textCentered(dl, labelFont, 11.0f, ImVec2((k.a.x + k.b.x) * 0.5f, k.b.y - 12), col::gold, q);
    }
    // Felt strip along the top edge.
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + 4), col::rgb(120, 40, 50));
}

} // namespace tos::ui
