#include <Windows.h>
#include <Shlwapi.h>
#include <filesystem>
#include "app_paths.h"
#include "string_conv.h"


namespace fs = std::filesystem;


namespace AppPaths {

std::string profileDir() {

	static std::string cached;

	if (!cached.empty()) {
		return cached;
	}

	const std::wstring envSubPathWide    = L"%appdata%\\" + std::wstring(appNameWide());
	const std::wstring fallBackPathWide  = L"C:\\" + std::wstring(appNameWide());

	std::wstring profilePathWide;

	wchar_t buffer[MAX_PATH] = {};
	if (ExpandEnvironmentStrings(envSubPathWide.c_str(), buffer, MAX_PATH)) {
		profilePathWide = buffer;
	} else {
		profilePathWide = fallBackPathWide;
	}

	std::error_code ec;
	if (!fs::is_directory(profilePathWide, ec)) {
		if (!fs::create_directory(profilePathWide, ec)) {
			profilePathWide = fallBackPathWide;
			if (!fs::is_directory(profilePathWide, ec)) {
				fs::create_directory(profilePathWide, ec);
			}
		}
	}

	if (!fs::is_directory(profilePathWide, ec)) {
		return {};
	}

	cached = WideToUtf8(profilePathWide);
	return cached;
}

std::string currentDir() {

	static std::string cached;

	if (!cached.empty()) {
		return cached;
	}

	wchar_t path[MAX_PATH] = {};
	if (GetModuleFileName(NULL, path, MAX_PATH) == 0) {
		return {};
	}

	if (!PathRemoveFileSpec(path)) {
		return {};
	}

	cached = WideToUtf8(path);
	return cached;
}

std::string configFile() {
	return profileDir() + "\\config.toml";
}

std::string logFile() {
	return profileDir() + "\\log.txt";
}

} // namespace AppPaths
