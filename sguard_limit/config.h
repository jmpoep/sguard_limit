#pragma once
#include <Windows.h>
#include <string>
#include <cstdint>


// load & save config (sington)

class ConfigManager {

private:
	ConfigManager()   = default;
	~ConfigManager()  = default;

public:
	static ConfigManager& getInstance() {
		static ConfigManager instance;
		return instance;
	}

	ConfigManager(const ConfigManager&)                = delete;
	ConfigManager(ConfigManager&&)                     = delete;
	ConfigManager& operator= (const ConfigManager&)    = delete;
	ConfigManager& operator= (ConfigManager&&)         = delete;


public:
	void         init();

	int64_t      readInt(const char* section, const char* key, int64_t defaultValue) const;
	DWORD        readDword(const char* section, const char* key, DWORD defaultValue) const;
	std::string  readStr(const char* section, const char* key, const std::string& defaultValue) const;
	bool         readBool(const char* section, const char* key, bool defaultValue) const;

	void         writeInt(const char* section, const char* key, int64_t value);
	void         writeDword(const char* section, const char* key, DWORD value);
	void         writeStr(const char* section, const char* key, const std::string& value);
	void         writeBool(const char* section, const char* key, bool value);

private:
	void         _loadFromFile();
	void         _saveToFile();

private:
	std::string  _configFile{};
};
