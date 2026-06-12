#include <Windows.h>
#include <cstring>
#include <filesystem>
#include <system_error>
#include "app_paths.h"
#include "string_conv.h"


namespace fs = std::filesystem;


namespace AppPaths {

std::string profileDir() {

	static std::string cached;

	if (!cached.empty()) {
		return cached;
	}

	wchar_t profilePath[MAX_PATH] = {};
	const std::string envSubPath = std::string("%appdata%\\") + appName();

	if (!ExpandEnvironmentStrings(Utf8ToWide(envSubPath), profilePath, MAX_PATH)) {
		cached = std::string("C:\\") + appName();
	} else {
		cached = WideToUtf8(profilePath);
	}

	std::error_code ec;

	if (!fs::is_directory(cached, ec)) {
		if (!fs::create_directory(cached, ec)) {
			cached = std::string("C:\\") + appName();
			if (!fs::is_directory(cached, ec)) {
				fs::create_directory(cached, ec);
			}
		}
	}

	if (!fs::is_directory(cached, ec)) {
		cached.clear();
	}

	return cached;
}

std::string currentDir() {

	static std::string cached;

	if (!cached.empty()) {
		return cached;
	}

	wchar_t path[MAX_PATH] = {};
	GetModuleFileName(NULL, path, MAX_PATH);

	if (auto p = wcsrchr(path, L'\\')) {
		*p = L'\0';
		cached = WideToUtf8(path);
	}

	return cached;
}

std::string configFile() {
	return profileDir() + "\\config.toml";
}

std::string logFile() {
	return profileDir() + "\\log.txt";
}

} // namespace AppPaths
