#pragma once

#include <imgui.h>

#include <array>
#include <vector>

namespace tos::ui
{

struct KeyEvent
{
    int pitch;
    bool on;
};

// Clickable piano drawn at the cursor. `held` lights keys; `qwertyBase` (or -1) labels
// the computer-keyboard mapping. Mouse presses, releases and glissandos are appended
// to `events`.
void pianoKeyboard(const char* id, ImVec2 size, int lowNote, int highNote, const std::array<bool, 128>& held,
                   int qwertyBase, ImFont* labelFont, std::vector<KeyEvent>& events);

// Computer keys mapped to semitones above the base C (A = C, W = C#, S = D ...).
const char* qwertyLabel(int semitone);

} // namespace tos::ui
