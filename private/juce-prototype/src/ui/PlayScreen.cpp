#include "app/App.h"
#include "ui/Keyboard.h"
#include "ui/Screens.h"
#include "ui/Theme.h"

#include <imgui.h>

#include <cmath>
#include <string>

namespace tos::ui
{

namespace
{

constexpr ImGuiWindowFlags screenFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                         | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
                                         | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

constexpr float margin = 20.0f;

ImU32 stateColor(SlotState s)
{
    switch (s)
    {
        case SlotState::Solved: return col::green;
        case SlotState::Wrong: return col::red;
        case SlotState::Entered: return col::text;
        case SlotState::Empty: return col::faint;
    }
    return col::faint;
}

void slotBox(App& app, ImDrawList* dl, const Slot& slot, ImVec2 a, ImVec2 b, bool cursor, float fontSize)
{
    const auto& q = app.puzzle->question();
    ImU32 bg = col::panelHi;
    if (slot.state == SlotState::Solved)
        bg = col::rgb(40, 84, 66);
    else if (slot.state == SlotState::Wrong)
        bg = col::rgb(92, 44, 52);
    dl->AddRectFilled(a, b, bg, 6.0f);

    std::string label = "?";
    ImU32 color = stateColor(slot.state);
    if (slot.state == SlotState::Solved)
        label = slot.kind == SlotKind::Melody ? pitchClassName(slot.targetPitch, q.flats()) : slot.targetChord.name(q.flats());
    else if (slot.state == SlotState::Entered || slot.state == SlotState::Wrong)
        label = slot.enteredLabel;
    else if (slot.revealed)
    {
        label = slot.kind == SlotKind::Melody ? pitchClassName(slot.targetPitch, q.flats()) : slot.targetChord.name(q.flats());
        color = col::goldDim;
    }

    textCentered(dl, app.fonts.bold, fontSize, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), color, label.c_str());
    if (slot.revealed && slot.state != SlotState::Solved)
        dl->AddRect(a, b, col::goldDim, 6.0f, 0, 1.0f);
    if (cursor)
    {
        const float pulse = 0.5f + 0.5f * std::sin((float) ImGui::GetTime() * 6.0f);
        dl->AddRect(a, b, col::rgb(236, 190, 96, 120 + (int) (135 * pulse)), 6.0f, 0, 2.5f);
    }
}

void topBar(App& app, ImDrawList* dl, ImVec2 size)
{
    auto& p = *app.puzzle;
    const auto& q = p.question();
    ImGui::SetCursorScreenPos(ImVec2(margin, 16));
    if (ImGui::Button("< Tower", ImVec2(96, 40)))
        app.backToTower();

    const std::string title = p.level().floor + "  \xC2\xB7  " + p.level().title;
    dl->AddText(app.fonts.title, 24.0f, ImVec2(margin + 116, 14), col::text, title.c_str());
    char info[256];
    std::snprintf(info, sizeof(info), "%s  \xC2\xB7  %.0f bpm  \xC2\xB7  %s", q.keyName().c_str(), q.bpm, q.source.c_str());
    dl->AddText(app.fonts.body, smallSize, ImVec2(margin + 117, 43), col::muted, info);

    char score[64], sub[128];
    std::snprintf(score, sizeof(score), "%d", p.score());
    std::snprintf(sub, sizeof(sub), "still available %d  \xC2\xB7  perfect %d", p.potentialRemaining(), p.perfectScore());
    ImGui::PushFont(app.fonts.title, 34.0f);
    const float sw = ImGui::CalcTextSize(score).x;
    ImGui::PopFont();
    ImGui::PushFont(app.fonts.body, smallSize);
    const float subW = ImGui::CalcTextSize(sub).x;
    ImGui::PopFont();
    dl->AddText(app.fonts.title, 34.0f, ImVec2(size.x - margin - sw, 8), col::gold, score);
    dl->AddText(app.fonts.body, smallSize, ImVec2(size.x - margin - subW, 46), col::muted, sub);
    dl->AddText(app.fonts.bold, smallSize, ImVec2(size.x - margin - sw - 52, 22), col::muted, "SCORE");
}

void transportBar(App& app, float y, float width)
{
    auto& p = *app.puzzle;
    ImGui::SetCursorScreenPos(ImVec2(margin, y));
    const ImVec2 bsize(118, 50);

    for (const auto& def : globalListenTable())
    {
        if (! p.globalAvailable(def.id))
            continue;
        char label[96];
        const char* name = def.id == GlobalListen::PlayAll ? "Play  (Space)" : def.label;
        std::snprintf(label, sizeof(label), "%s\n%s##g%d", name, costLabel(p.globalNextCost(def.id)), (int) def.id);
        const bool pressed = def.id == GlobalListen::PlayAll ? primaryButton(label, bsize) : ImGui::Button(label, bsize);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\nCost applies to every unsolved bar.", def.tip);
        if (pressed)
            app.play(p.useGlobal(def.id));
        ImGui::SameLine();
    }
    if (ImGui::Button("Stop\n(Esc)", ImVec2(80, 50)))
        app.stopPlayback();

    // Explore / Answer switch on the right.
    const float sw = 132.0f;
    ImGui::SetCursorScreenPos(ImVec2(margin + width - sw * 2 - 8, y));
    const bool answering = p.answerMode();
    auto modeButton = [&](const char* label, bool active, bool targetMode) {
        if (active)
        {
            if (primaryButton(label, ImVec2(sw, 50)) && answering != targetMode)
                app.toggleAnswerMode();
        }
        else if (ImGui::Button(label, ImVec2(sw, 50)) && answering != targetMode)
            app.toggleAnswerMode();
    };
    modeButton("Explore\nplay freely", ! answering, false);
    ImGui::SameLine(0, 8);
    modeButton("Answer  (Tab)\nfill the slots", answering, true);
}

float barCardHeight(const Puzzle& p)
{
    float h = 40.0f;
    if (p.level().hasChords())
        h += 92.0f;
    if (p.level().hasMelody())
        h += 58.0f + 64.0f;
    return h;
}

void barCards(App& app, ImDrawList* dl, float y, float width)
{
    auto& p = *app.puzzle;
    const auto& q = p.question();
    const int n = p.sectionCount();
    const float gap = 14.0f;
    const float cardW = (width - gap * (float) (n - 1)) / (float) n;
    const float cardH = barCardHeight(p);
    const int playing = app.playingSection();
    const int melodyCursor = p.melodyCursor(), chordCursor = p.chordCursor();

    for (int s = 0; s < n; ++s)
    {
        const ImVec2 a(margin + (float) s * (cardW + gap), y), b(a.x + cardW, y + cardH);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(s);
        if (ImGui::InvisibleButton("bar", ImVec2(cardW, cardH)))
            p.selectSection(s);
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        const bool selected = p.selectedSection() == s;
        const bool solved = p.sectionSolved(s);
        dl->AddRectFilled(a, b, hovered ? col::panelHi : col::panel, 10.0f);
        if (playing == s)
        {
            dl->AddRect(ImVec2(a.x - 3, a.y - 3), ImVec2(b.x + 3, b.y + 3), col::rgb(236, 190, 96, 90), 12.0f, 0, 5.0f);
            dl->AddRect(a, b, col::gold, 10.0f, 0, 2.0f);
        }
        else if (selected)
            dl->AddRect(a, b, col::blue, 10.0f, 0, 2.0f);

        // Header: bar number and how much of its value is left.
        char header[32];
        std::snprintf(header, sizeof(header), "BAR %d", s + 1);
        dl->AddText(app.fonts.bold, smallSize, ImVec2(a.x + 14, a.y + 12), selected ? col::blue : col::muted, header);
        if (solved)
            dl->AddText(app.fonts.bold, smallSize, ImVec2(a.x + 70, a.y + 12), col::green, "SOLVED");

        const float m = p.section(s).multiplier;
        char pct[16];
        std::snprintf(pct, sizeof(pct), "%.0f%%", m * 100.0f);
        const float meterW = 70.0f;
        const ImVec2 ma(b.x - 14 - meterW, a.y + 18);
        dl->AddRectFilled(ma, ImVec2(ma.x + meterW, ma.y + 5), col::stoneDark, 3.0f);
        dl->AddRectFilled(ma, ImVec2(ma.x + meterW * m, ma.y + 5), solved ? col::green : col::gold, 3.0f);
        dl->AddText(app.fonts.body, smallSize, ImVec2(ma.x - 40, a.y + 11), col::muted, pct);

        float cy = a.y + 40;
        const auto& sec = p.section(s);

        if (p.level().hasChords())
        {
            for (int idx : p.slotsIn(s, SlotKind::Chord))
                slotBox(app, dl, p.slots()[(size_t) idx], ImVec2(a.x + 14, cy), ImVec2(b.x - 14, cy + 54), idx == chordCursor,
                        26.0f);
            std::string reveal;
            const auto& chord = p.slots()[(size_t) p.slotsIn(s, SlotKind::Chord)[0]].targetChord;
            if (sec.showRoot)
                reveal += "root " + pitchClassName(chord.root, q.flats());
            if (sec.showQuality)
                reveal += std::string(reveal.empty() ? "" : "  \xC2\xB7  ") + qualityLongName(chord.quality);
            if (! reveal.empty())
                dl->AddText(app.fonts.body, smallSize, ImVec2(a.x + 16, cy + 60), col::gold, reveal.c_str());
            cy += 92;
        }

        if (p.level().hasMelody())
        {
            const auto idxs = p.slotsIn(s, SlotKind::Melody);
            const float inner = cardW - 28.0f;
            const float boxW = std::min(48.0f, (inner - 6.0f * (float) (idxs.size() - 1)) / (float) idxs.size());
            for (size_t k = 0; k < idxs.size(); ++k)
            {
                const ImVec2 ba(a.x + 14 + (float) k * (boxW + 6.0f), cy);
                slotBox(app, dl, p.slots()[(size_t) idxs[k]], ba, ImVec2(ba.x + boxW, ba.y + 46),
                        idxs[k] == melodyCursor, 17.0f);
            }
            cy += 58;
            const float wrap = cardW - 30.0f;
            auto line = [&](const std::string& text) {
                dl->AddText(app.fonts.body, smallSize, ImVec2(a.x + 16, cy), col::gold, text.c_str(), nullptr, wrap);
                cy += 19;
            };
            if (sec.showFirstNote)
                line("first note " + p.firstNoteText(s));
            if (sec.showContour)
                line("contour  " + p.contourText(s));
            if (sec.showIntervals)
                line("intervals  " + p.intervalsText(s));
        }
    }
}

void hintPanel(App& app, ImVec2 pos, ImVec2 size)
{
    auto& p = *app.puzzle;
    const int s = p.selectedSection();
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginChild("hints", size, ImGuiChildFlags_AlwaysUseWindowPadding);

    ImGui::PushFont(app.fonts.bold, headingSize - 3.0f);
    textColored(col::gold, "Help for bar %d", s + 1);
    ImGui::PopFont();
    ImGui::SameLine();
    textColored(col::muted, p.sectionSolved(s) ? " solved, help is free now" : " worth %.0f%% of its points",
                p.section(s).multiplier * 100.0f);

    const auto next = p.nextHint(s);
    const float nextW = 150.0f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - nextW);
    ImGui::BeginDisabled(! next.has_value());
    if (primaryButton("Next hint", ImVec2(nextW, 0)) && next)
        if (auto opts = p.useHint(*next, s))
            app.play(*opts);
    ImGui::EndDisabled();
    ImGui::Spacing();

    ImGui::BeginChild("hintlist", ImVec2(0, 0), ImGuiChildFlags_None);
    const float colW = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
    int column = 0;
    for (const auto& def : hintTable())
    {
        const bool available = p.hintAvailable(def.id, s);
        const int uses = p.section(s).uses[(size_t) def.id];
        if (! available && uses == 0)
            continue;

        ImGui::PushID((int) def.id);
        ImGui::BeginDisabled(! available);
        const ImVec2 bpos = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::Button("##hint", ImVec2(colW, 34));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", def.tip);
        ImGui::EndDisabled();

        auto* dl = ImGui::GetWindowDrawList();
        const ImU32 labelColor = available ? (def.reveal ? col::rgb(240, 214, 160) : col::text) : col::faint;
        dl->AddText(app.fonts.body, 14.5f, ImVec2(bpos.x + 10, bpos.y + 9), labelColor, def.label);
        const char* cost = available ? costLabel(p.hintNextCost(def.id, s)) : "used";
        ImGui::PushFont(app.fonts.bold, smallSize);
        const float cw = ImGui::CalcTextSize(cost).x;
        ImGui::PopFont();
        dl->AddText(app.fonts.bold, smallSize, ImVec2(bpos.x + colW - cw - 10, bpos.y + 10),
                    available ? col::gold : col::faint, cost);
        if (uses > 0)
        {
            char u[16];
            std::snprintf(u, sizeof(u), "x%d", uses);
            dl->AddText(app.fonts.body, 12.0f, ImVec2(bpos.x + colW - cw - 36, bpos.y + 11), col::muted, u);
        }
        if (pressed)
            if (auto opts = p.useHint(def.id, s))
                app.play(*opts);
        ImGui::PopID();

        if (++column % 2 == 1)
            ImGui::SameLine(0, 8);
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

void answerPanel(App& app, ImVec2 pos, ImVec2 size)
{
    auto& p = *app.puzzle;
    const auto& q = p.question();
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginChild("answer", size, ImGuiChildFlags_AlwaysUseWindowPadding);

    ImGui::PushFont(app.fonts.bold, headingSize - 3.0f);
    textColored(col::gold, p.answerMode() ? "Answer mode" : "Explore mode");
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0f);
    if (p.answerMode())
        textColored(col::muted, p.level().hasMelody() && p.level().hasChords()
                                    ? "Single notes fill the next melody slot. Hold 3+ notes together to enter the chord."
                                    : (p.level().hasMelody() ? "Each note you play fills the next melody slot."
                                                             : "Hold 3+ notes together to enter a chord, or pick one below."));
    else
        textColored(col::muted, "Play freely to find the notes. %d free notes per bar, then each costs 1%% of the selected bar.",
                    Puzzle::freeExploreNotes);
    ImGui::PopTextWrapPos();

    if (ImGui::Button("Undo  (Backspace)"))
        p.undoEntry();
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        p.clearEntries();
    ImGui::SameLine();
    if (primaryButton("Check  (Enter)", ImVec2(160, 0)))
        app.check();

    if (! p.message().empty())
    {
        ImGui::PushFont(app.fonts.bold, textSize);
        textColored(col::text, "%s", p.message().c_str());
        ImGui::PopFont();
    }

    if (p.level().hasChords())
    {
        ImGui::Spacing();
        textColored(col::muted, "Chord picker");
        const bool flats = q.flats();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 5));
        for (int r = 0; r < 12; ++r)
        {
            ImGui::PushID(r);
            const std::string name = pitchClassName(r, flats);
            const bool on = app.pickerRoot == r;
            if (on ? primaryButton(name.c_str(), ImVec2(38, 0)) : ImGui::Button(name.c_str(), ImVec2(38, 0)))
                app.pickerRoot = r;
            ImGui::PopID();
            if (r < 11)
                ImGui::SameLine();
        }
        static const ChordQuality qualities[] = { ChordQuality::Major, ChordQuality::Minor, ChordQuality::Diminished,
                                                  ChordQuality::Dom7, ChordQuality::Maj7, ChordQuality::Min7 };
        static const char* qualityNames[] = { "major", "minor", "dim", "7", "maj7", "m7" };
        for (int k = 0; k < 6; ++k)
        {
            ImGui::PushID(100 + k);
            const bool on = app.pickerQuality == k;
            if (on ? primaryButton(qualityNames[k], ImVec2(62, 0)) : ImGui::Button(qualityNames[k], ImVec2(62, 0)))
                app.pickerQuality = k;
            ImGui::PopID();
            ImGui::SameLine();
        }
        const Chord picked { app.pickerRoot, qualities[app.pickerQuality] };
        const std::string enter = "Enter " + picked.name(flats);
        if (ImGui::Button(enter.c_str(), ImVec2(-1, 0)))
            p.enterChord(picked);
        ImGui::PopStyleVar(2);
    }

    const auto& st = p.stats();
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 30);
    textColored(col::faint, "Listens %d  \xC2\xB7  bar listens %d  \xC2\xB7  explored %d notes  \xC2\xB7  hints %d  \xC2\xB7  wrong %d  \xC2\xB7  %d:%02d",
                st.globalListens, st.barListens, st.exploreNotes, st.hintsUsed, st.wrongEntries, (int) st.seconds / 60,
                (int) st.seconds % 60);
    ImGui::EndChild();
}

void completionPopup(App& app)
{
    auto& p = *app.puzzle;
    const auto& q = p.question();
    if (app.showCompletion && ! ImGui::IsPopupOpen("Solved"))
        ImGui::OpenPopup("Solved");

    const ImVec2 size = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(size.x * 0.5f, size.y * 0.42f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 24));
    if (ImGui::BeginPopupModal("Solved", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        ImGui::PushFont(app.fonts.title, 30.0f);
        textColored(col::gold, "Floor cleared!");
        ImGui::PopFont();
        ImGui::PushFont(app.fonts.title, 44.0f);
        ImGui::Text("%d", p.score());
        ImGui::PopFont();
        const auto prog = app.progress.level(p.level().id);
        if (app.completionWasBest)
            textColored(col::green, "New best for this floor!");
        else
            textColored(col::muted, "Best on this floor: %d", prog.best);
        ImGui::Spacing();

        std::string answer;
        if (! q.chords.empty())
            for (size_t i = 0; i < q.chords.size(); ++i)
                answer += (i ? "  -  " : "Chords  ") + q.chords[i].chord.name(q.flats());
        if (! answer.empty())
            ImGui::TextUnformatted(answer.c_str());
        if (! q.melody.empty())
        {
            std::string mel = "Melody  ";
            for (const auto& n : q.melody)
                mel += pitchClassName(n.pitch, q.flats()) + " ";
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(mel.c_str());
            ImGui::PopTextWrapPos();
        }
        const auto& st = p.stats();
        textColored(col::muted, "%d listens, %d bar listens, %d notes explored, %d hints, %d wrong, %d:%02d",
                    st.globalListens, st.barListens, st.exploreNotes, st.hintsUsed, st.wrongEntries,
                    (int) st.seconds / 60, (int) st.seconds % 60);
        ImGui::Spacing();
        ImGui::Spacing();
        if (primaryButton("Next puzzle  (Enter)", ImVec2(220, 44)))
        {
            ImGui::CloseCurrentPopup();
            app.startLevel(p.level().id);
        }
        ImGui::SameLine();
        if (ImGui::Button("Hear it again", ImVec2(110, 44)))
            app.play({});
        ImGui::SameLine();
        if (ImGui::Button("Tower", ImVec2(-1, 44)))
        {
            ImGui::CloseCurrentPopup();
            app.backToTower();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

} // namespace

void drawPlayScreen(App& app)
{
    if (! app.puzzle)
        return;

    const ImVec2 size = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("play", nullptr, screenFlags);
    ImGui::PopStyleVar();
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(ImVec2(0, 0), size, col::nightTop, col::nightTop, col::night, col::night);

    const float width = size.x - margin * 2;
    topBar(app, dl, size);
    transportBar(app, 76, width);

    const float barsY = 142;
    barCards(app, dl, barsY, width);

    const float keyboardH = 132.0f;
    const float keyboardY = size.y - keyboardH - 14.0f;
    const float panelsY = barsY + barCardHeight(*app.puzzle) + 14.0f;
    const float panelsH = keyboardY - panelsY - 12.0f;
    const float hintW = std::floor(width * 0.5f - 6.0f);
    hintPanel(app, ImVec2(margin, panelsY), ImVec2(hintW, panelsH));
    answerPanel(app, ImVec2(margin + hintW + 12, panelsY), ImVec2(width - hintW - 12, panelsH));

    ImGui::SetCursorScreenPos(ImVec2(margin, keyboardY));
    std::vector<KeyEvent> events;
    pianoKeyboard("##piano", ImVec2(width, keyboardH), 36, 96, app.heldDisplay, app.qwertyBase(), app.fonts.bold, events);
    for (const auto& e : events)
    {
        if (e.on)
            app.userNoteOn(e.pitch, 0.75f);
        else
            app.userNoteOff(e.pitch);
    }

    completionPopup(app);
    ImGui::End();
}

} // namespace tos::ui
