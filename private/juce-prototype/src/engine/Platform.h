#pragma once

namespace tos
{

// Creates JUCE's message manager on the calling (main) thread. Call after SDL_Init and
// before creating the AudioEngine. SDL's event loop pumps the same OS queue, so JUCE's
// device notifications are delivered without a separate JUCE run loop.
void platformInit();
void platformShutdown();

} // namespace tos
