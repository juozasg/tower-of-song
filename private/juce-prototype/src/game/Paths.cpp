#include "game/Paths.h"

#include <juce_core/juce_core.h>

namespace tos::paths
{

std::string assetsDir()
{
    const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const juce::File candidates[] = {
        exe.getParentDirectory().getParentDirectory().getChildFile("Resources/assets"), // macOS bundle
        exe.getParentDirectory().getChildFile("assets"),
        juce::File(TOS_DEV_ASSETS_DIR),
    };
    for (const auto& dir : candidates)
        if (dir.isDirectory())
            return dir.getFullPathName().toStdString();
    return juce::File(TOS_DEV_ASSETS_DIR).getFullPathName().toStdString();
}

std::string userDataDir()
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");
#endif
    auto dir = base.getChildFile("TowerOfSong");
    dir.createDirectory();
    return dir.getFullPathName().toStdString();
}

std::vector<std::string> listFiles(const std::string& dir, const std::vector<std::string>& extensions)
{
    std::vector<std::string> result;
    const juce::File folder(dir);
    if (! folder.isDirectory())
        return result;

    for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFiles))
    {
        const auto ext = entry.getFile().getFileExtension().toLowerCase().toStdString();
        for (const auto& wanted : extensions)
            if (ext == wanted)
                result.push_back(entry.getFile().getFullPathName().toStdString());
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string fileStem(const std::string& path)
{
    return juce::File(path).getFileNameWithoutExtension().toStdString();
}

std::string join(const std::string& dir, const std::string& name)
{
    return juce::File(dir).getChildFile(name).getFullPathName().toStdString();
}

} // namespace tos::paths
