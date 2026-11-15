// The Tower of Song: SDL3 window + Dear ImGui UI, JUCE audio/MIDI engine (see engine/).

#include "app/App.h"
#include "engine/Platform.h"
#include "ui/Theme.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{

struct Args
{
    tos::AppOptions app;
    std::string screenshot; // write a BMP after a few frames, then quit
    int frames = 30;
};

Args parseArgs(int argc, char** argv)
{
    Args a;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--screenshot")
            a.screenshot = next();
        else if (arg == "--frames")
            a.frames = std::atoi(next());
        else if (arg == "--level")
            a.app.startLevel = std::atoi(next());
        else if (arg == "--seed")
            a.app.seed = (uint32_t) std::strtoul(next(), nullptr, 10);
        else if (arg == "--demo")
            a.app.demo = true;
        else if (arg == "--data-dir")
            a.app.dataDir = next();
        else if (arg == "--no-audio")
            a.app.audio = false;
    }
    return a;
}

bool saveScreenshot(SDL_Renderer* renderer, const std::string& path)
{
    SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
    if (surface == nullptr)
        return false;
    const bool ok = SDL_SaveBMP(surface, path.c_str());
    SDL_DestroySurface(surface);
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    const Args args = parseArgs(argc, argv);

    if (! SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    tos::platformInit();

    const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow("The Tower of Song", (int) (1280 * scale), (int) (860 * scale), flags);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(window, (int) (1180 * scale), (int) (820 * scale));
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    tos::ui::applyTheme();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    int exitCode = 0;
    {
        tos::App app(args.app);
        app.fonts = tos::ui::loadFonts();

        bool done = false;
        int frame = 0;
        Uint64 last = SDL_GetTicksNS();
        while (! done)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL3_ProcessEvent(&event);
                app.handleEvent(event);
                if (event.type == SDL_EVENT_QUIT)
                    done = true;
                if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                    done = true;
            }
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
            {
                app.audio.pollMidiDevices();
                SDL_Delay(10);
                continue;
            }

            const Uint64 now = SDL_GetTicksNS();
            const double dt = (double) (now - last) * 1.0e-9;
            last = now;

            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            ImGui::PushFont(app.fonts.body, tos::ui::textSize);
            app.frame(dt);
            ImGui::PopFont();
            ImGui::Render();

            SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            SDL_SetRenderDrawColor(renderer, 17, 20, 38, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

            if (! args.screenshot.empty() && ++frame >= args.frames)
            {
                if (! saveScreenshot(renderer, args.screenshot))
                {
                    std::fprintf(stderr, "Screenshot failed: %s\n", SDL_GetError());
                    exitCode = 1;
                }
                done = true;
            }
            SDL_RenderPresent(renderer);
        }
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    tos::platformShutdown();
    SDL_Quit();
    return exitCode;
}
