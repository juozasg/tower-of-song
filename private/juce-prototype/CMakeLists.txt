cmake_minimum_required(VERSION 3.24)

project(TowerOfSong VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

if(APPLE)
    enable_language(OBJC OBJCXX)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# Dependencies are downloaded once into .deps/ and shared by every build folder.
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH "")
set(FETCHCONTENT_QUIET OFF)
include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    TRUE)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.18
    GIT_SHALLOW    TRUE)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.93.0
    GIT_SHALLOW    TRUE)

# Header-only SoundFont (SF2) player, used when a .sf2 piano is present in assets/soundfonts.
FetchContent_Declare(tinysoundfont
    URL https://github.com/schellingb/TinySoundFont/archive/853a0a171759f1ddba0de1442133a75912bbeffa.tar.gz)

FetchContent_MakeAvailable(JUCE SDL3 imgui tinysoundfont)

# ---------------------------------------------------------------- imgui
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# ---------------------------------------------------------------- core
# Everything except the window/UI. JUCE is linked privately: no public header includes JUCE,
# so the app and the tests only see plain C++ types.
add_library(tos_core STATIC
    src/engine/AudioEngine.cpp
    src/engine/Voices.cpp
    src/engine/Platform.cpp
    src/music/Theory.cpp
    src/music/Generator.cpp
    src/music/Arrange.cpp
    src/music/MidiCatalog.cpp
    src/game/Levels.cpp
    src/game/Puzzle.cpp
    src/game/Hints.cpp
    src/game/Progress.cpp
    src/game/Paths.cpp)
target_include_directories(tos_core PUBLIC src PRIVATE ${tinysoundfont_SOURCE_DIR})
target_compile_definitions(tos_core
    PUBLIC  TOS_DEV_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
            JUCE_STANDALONE_APPLICATION=1
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0
            JUCE_MODAL_LOOPS_PERMITTED=0
            JUCE_STRICT_REFCOUNTEDPOINTER=1
            $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>)
target_link_libraries(tos_core PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_recommended_config_flags)

# ---------------------------------------------------------------- app
add_executable(TowerOfSong MACOSX_BUNDLE WIN32
    src/app/main.cpp
    src/app/App.cpp
    src/ui/Theme.cpp
    src/ui/Keyboard.cpp
    src/ui/TowerScreen.cpp
    src/ui/PlayScreen.cpp)
target_link_libraries(TowerOfSong PRIVATE tos_core imgui SDL3::SDL3)
set_target_properties(TowerOfSong PROPERTIES
    OUTPUT_NAME "Tower of Song"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.towerofsong.game"
    MACOSX_BUNDLE_BUNDLE_NAME "The Tower of Song"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

# Ship assets next to the executable (Windows) or inside the bundle (macOS).
if(APPLE)
    set(TOS_ASSET_DEST "$<TARGET_BUNDLE_CONTENT_DIR:TowerOfSong>/Resources/assets")
else()
    set(TOS_ASSET_DEST "$<TARGET_FILE_DIR:TowerOfSong>/assets")
endif()
add_custom_command(TARGET TowerOfSong POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${CMAKE_SOURCE_DIR}/assets" "${TOS_ASSET_DEST}")

# ---------------------------------------------------------------- tests
enable_testing()
add_executable(tos_tests tests/tests.cpp)
target_link_libraries(tos_tests PRIVATE tos_core)
add_test(NAME tos_tests COMMAND tos_tests)
