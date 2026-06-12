#pragma once
#include <string>


namespace AppPaths {

inline constexpr const char* appName() { return "Hutao"; }

// Returns profile directory (%appdata%\Hutao). Creates if missing.
std::string profileDir();

// Returns exe directory.
std::string currentDir();

std::string configFile();
std::string logFile();

} // namespace AppPaths
