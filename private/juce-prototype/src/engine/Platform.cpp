#include "engine/Platform.h"

#include <juce_events/juce_events.h>

namespace tos
{

void platformInit()
{
    juce::MessageManager::getInstance();
}

void platformShutdown()
{
    juce::DeletedAtShutdown::deleteAll();
    juce::MessageManager::deleteInstance();
}

} // namespace tos
