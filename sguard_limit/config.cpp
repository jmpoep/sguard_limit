#include <Windows.h>
#include <fstream>
#include <filesystem>
#include <string>
#include <toml++/toml.hpp>
#include "config.h"
#include "app_paths.h"
#include "string_conv.h"


namespace fs = std::filesystem;

namespace {

toml::table  configRoot;


template<typename T>
T cfg_value(const toml::node_view<toml::node>& node, T defaultValue) {
	if (auto value = node.value<T>()) {
		return *value;
	}
	return defaultValue;
}

toml::table& ensureSection(const char* section) {
	if (auto* tbl = configRoot[section].as_table()) {
		return *tbl;
	}
	configRoot.insert_or_assign(section, toml::table{});
	return *configRoot[section].as_table();
}

} // namespace


// config manager

void ConfigManager::init() {
	_configFile = AppPaths::configFile();
	_loadFromFile();
}

void ConfigManager::_loadFromFile() {
	configRoot = toml::table{};
	const std::wstring configPathWide = Utf8ToWide(_configFile);
	if (GetFileAttributes(configPathWide.c_str()) != INVALID_FILE_ATTRIBUTES) {
		try {
			configRoot = toml::parse_file(configPathWide);
		} catch (const toml::parse_error&) {
			// malformed config - fall back to defaults
		}
	}
}

void ConfigManager::_saveToFile() {
	const std::wstring configPathWide = Utf8ToWide(_configFile);
	std::ofstream ofs(fs::path(configPathWide), std::ios::trunc);
	ofs << configRoot;
}

int64_t ConfigManager::readInt(const char* section, const char* key, int64_t defaultValue) const {
	return cfg_value(configRoot[section][key], defaultValue);
}

DWORD ConfigManager::readDword(const char* section, const char* key, DWORD defaultValue) const {
	return static_cast<DWORD>(readInt(section, key, static_cast<int64_t>(defaultValue)));
}

std::string ConfigManager::readStr(const char* section, const char* key, const std::string& defaultValue) const {
	return cfg_value(configRoot[section][key], defaultValue);
}

bool ConfigManager::readBool(const char* section, const char* key, bool defaultValue) const {
	return cfg_value(configRoot[section][key], defaultValue);
}

void ConfigManager::writeInt(const char* section, const char* key, int64_t value) {
	ensureSection(section).insert_or_assign(key, value);
	_saveToFile();
}

void ConfigManager::writeDword(const char* section, const char* key, DWORD value) {
	writeInt(section, key, static_cast<int64_t>(value));
}

void ConfigManager::writeStr(const char* section, const char* key, const std::string& value) {
	ensureSection(section).insert_or_assign(key, value);
	_saveToFile();
}

void ConfigManager::writeBool(const char* section, const char* key, bool value) {
	ensureSection(section).insert_or_assign(key, value);
	_saveToFile();
}
