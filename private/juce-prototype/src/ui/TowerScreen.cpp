#include "app/App.h"
#include "game/Paths.h"
#include "ui/Screens.h"
#include "ui/Theme.h"

#include <imgui.h>

#include <cctype>
#include <cmath>
#include <string>

namespace tos::ui
{

namespace
{

constexpr ImGuiWindowFlags screenFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                         | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
                                         | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

void drawSky(ImDrawList* dl, ImVec2 size)
{
    dl->AddRectFilledMultiColor(ImVec2(0, 0), size, col::nightTop, col::nightTop, col::night, col::night);
    // Fixed pseudo-random stars.
    uint32_t h = 2166136261u;
    for (int i = 0; i < 110; ++i)
    {
        h = (h ^ (uint32_t) i) * 16777619u;
        const float x = (float) (h % 10000) / 10000.0f * size.x;
        h = h * 1103515245u + 12345u;
        const float y = (float) (h % 10000) / 10000.0f * size.y * 0.75f;
        h = h * 1103515245u + 12345u;
        const float r = 0.6f + (float) (h % 100) / 100.0f * 1.1f;
        if (y < 130.0f && std::abs(x - size.x * 0.5f) < 430.0f)
            continue; // keep the title clear
        const int alpha = 70 + (int) (h % 120);
        dl->AddCircleFilled(ImVec2(x, y), r, col::rgb(255, 245, 220, alpha));
    }
    dl->AddCircleFilled(ImVec2(size.x * 0.83f, 92), 30, col::rgb(244, 236, 210, 230));
    dl->AddCircleFilled(ImVec2(size.x * 0.83f + 11, 84), 27, col::nightTop);
}

void heading(App& app, const char* text)
{
    ImGui::PushFont(app.fonts.bold, headingSize - 3.0f);
    textColored(col::gold, "%s", text);
    ImGui::PopFont();
}

void drawFloor(App& app, ImDrawList* dl, const LevelDef& level, ImVec2 a, ImVec2 b)
{
    const auto prog = app.progress.level(level.id);
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(level.id);
    const bool clicked = ImGui::InvisibleButton("floor", ImVec2(b.x - a.x, b.y - a.y));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    dl->AddRectFilled(a, b, hovered ? col::stoneHi : col::stone, 4.0f);
    // Brick courses.
    for (float y = a.y + 22.0f, row = 0; y < b.y - 4.0f; y += 22.0f, ++row)
    {
        dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), col::rgb(0, 0, 0, 40));
        for (float x = a.x + (std::fmod(row, 2.0f) == 0.0f ? 30.0f : 60.0f); x < b.x; x += 60.0f)
            dl->AddLine(ImVec2(x, y - 22.0f), ImVec2(x, y), col::rgb(0, 0, 0, 28));
    }
    if (hovered)
        dl->AddRect(a, b, col::gold, 4.0f, 0, 2.0f);

    // Arched window, lit once the floor has been cleared.
    const float ww = 50.0f, wh = 66.0f;
    const ImVec2 wa(b.x - ww - 26.0f, a.y + (b.y - a.y - wh) * 0.5f + 6.0f);
    const ImU32 glass = prog.best > 0 ? col::gold : col::rgb(26, 28, 46);
    if (prog.best > 0)
        dl->AddCircleFilled(ImVec2(wa.x + ww * 0.5f, wa.y + wh * 0.5f), 52.0f, col::rgb(236, 190, 96, 34));
    dl->AddRectFilled(ImVec2(wa.x, wa.y + ww * 0.5f), ImVec2(wa.x + ww, wa.y + wh), glass);
    dl->AddCircleFilled(ImVec2(wa.x + ww * 0.5f, wa.y + ww * 0.5f), ww * 0.5f, glass);
    dl->AddLine(ImVec2(wa.x + ww * 0.5f, wa.y), ImVec2(wa.x + ww * 0.5f, wa.y + wh), col::stoneDark, 3.0f);
    dl->AddLine(ImVec2(wa.x, wa.y + wh * 0.55f), ImVec2(wa.x + ww, wa.y + wh * 0.55f), col::stoneDark, 3.0f);

    const float tx = a.x + 22.0f;
    std::string floorLabel = level.floor;
    for (auto& ch : floorLabel)
        ch = (char) std::toupper((unsigned char) ch);
    dl->AddText(app.fonts.bold, smallSize, ImVec2(tx, a.y + 14), col::muted, floorLabel.c_str());
    dl->AddText(app.fonts.title, 25.0f, ImVec2(tx, a.y + 32), col::text, level.title.c_str());
    dl->AddText(app.fonts.body, 14.5f, ImVec2(tx, a.y + 66), col::muted, level.description.c_str(), nullptr,
                b.x - a.x - ww - 70.0f);

    char line[128];
    if (prog.best > 0)
        std::snprintf(line, sizeof(line), "Best %d   \xC2\xB7   cleared %d of %d", prog.best, prog.completions, prog.plays);
    else if (prog.plays > 0)
        std::snprintf(line, sizeof(line), "Not cleared yet   \xC2\xB7   tried %d", prog.plays);
    else
        std::snprintf(line, sizeof(line), "Not climbed yet");
    dl->AddText(app.fonts.bold, 14.5f, ImVec2(tx, b.y - 28), prog.best > 0 ? col::gold : col::faint, line);

    if (clicked)
        app.startLevel(level.id);
}

void drawTower(App& app, ImDrawList* dl, ImVec2 size)
{
    const auto& levels = allLevels();
    const float towerW = std::min(470.0f, size.x * 0.38f);
    const float floorH = 138.0f;
    const float groundY = size.y - 34.0f;
    const float cx = size.x * 0.5f;
    const float x0 = cx - towerW * 0.5f, x1 = cx + towerW * 0.5f;

    dl->AddRectFilled(ImVec2(0, groundY), size, col::rgb(24, 26, 40));
    dl->AddRectFilled(ImVec2(x0 - 30, groundY - 8), ImVec2(x1 + 30, groundY + 4), col::stoneDark, 3.0f);

    float topY = groundY - 8;
    for (size_t i = 0; i < levels.size(); ++i)
    {
        const float yb = groundY - 8 - (float) i * floorH;
        const float ya = yb - floorH + 8;
        drawFloor(app, dl, levels[i], ImVec2(x0, ya), ImVec2(x1, yb));
        dl->AddRectFilled(ImVec2(x0 - 10, ya - 8), ImVec2(x1 + 10, ya), col::stoneDark, 2.0f); // ledge
        topY = ya - 8;
    }

    // Roof and pennant.
    const float roofH = 82.0f;
    dl->AddTriangleFilled(ImVec2(x0 - 22, topY), ImVec2(x1 + 22, topY), ImVec2(cx, topY - roofH), col::rgb(122, 52, 64));
    dl->AddLine(ImVec2(cx, topY - roofH), ImVec2(cx, topY - roofH - 34), col::muted, 2.0f);
    dl->AddTriangleFilled(ImVec2(cx, topY - roofH - 34), ImVec2(cx + 30, topY - roofH - 27), ImVec2(cx, topY - roofH - 20),
                          col::gold);
}

void drawGuide(App& app, ImVec2 pos, ImVec2 size)
{
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec4(col::rgb(30, 35, 62, 225)));
    ImGui::BeginChild("guide", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    heading(app, "How to climb");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    const char* steps[] = {
        "Listen. The phrase plays when you enter a floor. Space plays it again.",
        "Explore on the keys to find the notes. The first 12 notes in each bar are free.",
        "Press Tab for Answer mode and play your answer. Hold three or more notes together to enter a chord.",
        "Press Enter to check. Correct answers lock in and score right away.",
    };
    for (int i = 0; i < 4; ++i)
    {
        textColored(col::gold, "%d", i + 1);
        ImGui::SameLine();
        ImGui::TextUnformatted(steps[i]);
        ImGui::Spacing();
    }
    ImGui::Separator();
    ImGui::Spacing();
    textColored(col::muted, "Stuck? Every bar has a ladder of help, from \xE2\x80\x9Chear this bar\xE2\x80\x9D up to \xE2\x80\x9Creveal the note\xE2\x80\x9D. "
                            "Help costs part of that bar's points, never all of them.");
    ImGui::Spacing();
    textColored(col::muted, "Small wins count: solve one bar at a time. Solving several at once earns a bonus.");
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void drawSetup(App& app, ImVec2 pos, ImVec2 size)
{
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec4(col::rgb(30, 35, 62, 225)));
    ImGui::BeginChild("setup", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushTextWrapPos(0.0f);

    heading(app, "MIDI keyboards");
    const auto devices = app.audio.midiDevices();
    if (devices.empty())
        textColored(col::faint, "None found. Plug one in at any time.");
    for (const auto& d : devices)
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5, p.y + 9), 4.5f, d.open ? col::green : col::red);
        ImGui::SetCursorScreenPos(ImVec2(p.x + 16, p.y));
        ImGui::TextUnformatted(d.name.c_str());
        if (! d.open)
            textColored(col::red, "%s", d.error.c_str());
    }
    textColored(col::muted, "Computer keys play too: A W S E D F T G Y H U J K. Z / X change octave (now C%d).",
                app.keyboardOctave);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    heading(app, "Sound");
    const auto st = app.audio.audioStatus();
    if (! st.deviceName.empty())
    {
        ImGui::TextUnformatted(st.deviceName.c_str());
        textColored(col::muted, "%.0f Hz  \xC2\xB7  %d samples  \xC2\xB7  about %.0f ms", st.sampleRate, st.bufferSize,
                    st.outputLatencyMs);
    }
    else
    {
        textColored(col::red, "No audio output. %s", st.error.c_str());
    }

    ImGui::PushItemWidth(-90.0f);
    if (ImGui::BeginCombo("Driver", st.deviceType.c_str()))
    {
        for (const auto& type : app.audio.deviceTypes())
            if (ImGui::Selectable(type.c_str(), type == st.deviceType))
                app.audio.setDeviceType(type);
        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("Output", st.deviceName.c_str()))
    {
        for (const auto& name : app.audio.outputDevices())
            if (ImGui::Selectable(name.c_str(), name == st.deviceName))
                app.audio.setOutputDevice(name);
        ImGui::EndCombo();
    }
    const std::string bufferLabel = std::to_string(st.bufferSize) + " samples";
    if (ImGui::BeginCombo("Buffer", bufferLabel.c_str()))
    {
        for (int size : app.audio.bufferSizes())
            if (ImGui::Selectable((std::to_string(size) + " samples").c_str(), size == st.bufferSize))
                app.audio.setBufferSize(size);
        ImGui::EndCombo();
    }
    const std::string piano = app.audio.pianoName();
    if (ImGui::BeginCombo("Piano", piano.c_str()))
    {
        if (ImGui::Selectable("Built-in modelled piano", app.progress.settings.soundFont.empty()))
            app.setSoundFont("");
        for (const auto& path : app.audio.availableSoundFonts())
            if (ImGui::Selectable(paths::fileStem(path).c_str(), path == app.progress.settings.soundFont))
                app.setSoundFont(path);
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Put .sf2 files in assets/soundfonts or the data folder's soundfonts folder.");
    float volume = app.audio.masterVolume();
    if (ImGui::SliderFloat("Volume", &volume, 0.0f, 1.5f, "%.2f"))
        app.audio.setMasterVolume(volume);
    if (ImGui::IsItemDeactivatedAfterEdit())
        app.saveSettings();
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    heading(app, "Song catalog");
    textColored(col::muted, "%zu MIDI songs, %zu two-bar phrases. The ground floor mixes these with generated melodies.",
                app.catalog.songCount(), app.catalog.phraseCount(2));
    if (ImGui::Button("Open data folder"))
        SDL_OpenURL(("file://" + paths::userDataDir()).c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Saves, the event log, and extra catalog/ and soundfonts/ folders live here.");

    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace

void drawTowerScreen(App& app)
{
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("tower", nullptr, screenFlags);
    ImGui::PopStyleVar();
    auto* dl = ImGui::GetWindowDrawList();

    drawSky(dl, size);
    textCentered(dl, app.fonts.title, 48.0f, ImVec2(size.x * 0.5f, 56), col::gold, "THE TOWER OF SONG");
    textCentered(dl, app.fonts.body, 16.0f, ImVec2(size.x * 0.5f, 96), col::muted,
                 "An ear-training climb. Listen, find it on the keys, name it.");
    if (! app.statusMessage.empty())
        textCentered(dl, app.fonts.bold, 14.0f, ImVec2(size.x * 0.5f, 122), col::red, app.statusMessage.c_str());

    drawTower(app, dl, size);

    const float panelW = std::min(330.0f, (size.x - std::min(470.0f, size.x * 0.38f)) * 0.5f - 44.0f);
    drawGuide(app, ImVec2(24, 150), ImVec2(panelW, size.y - 150 - 60));
    drawSetup(app, ImVec2(size.x - 24 - panelW, 150), ImVec2(panelW, size.y - 150 - 60));

    ImGui::End();
}

} // namespace tos::ui
