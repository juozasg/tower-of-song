#pragma once

#include <string>
#include <vector>

namespace tos::paths
{

// The bundled assets folder: inside the .app on macOS, next to the .exe on Windows,
// falling back to the source tree during development.
std::string assetsDir();

// Per-user folder for saves, logs and extra sound fonts / MIDI files. Created on demand.
std::string userDataDir();

// Files in `dir` (not recursive) whose extension matches one of `extensions` (e.g. ".sf2").
std::vector<std::string> listFiles(const std::string& dir, const std::vector<std::string>& extensions);

std::string fileStem(const std::string& path);
std::string join(const std::string& dir, const std::string& name);

} // namespace tos::paths
