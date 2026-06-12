#pragma once
#include <atomic>
#include "win32utility.h"  // result_t, unexpected_error


// ProxyManager module (sington)
class ProxyManager {

private:
	ProxyManager()   = default;
	~ProxyManager()  = default;

public:
	static ProxyManager& getInstance() {
		static ProxyManager instance;
		return instance;
	}

	ProxyManager(const ProxyManager&)              = delete;
	ProxyManager(ProxyManager&&)                   = delete;
	ProxyManager& operator= (const ProxyManager&)  = delete;
	ProxyManager& operator= (ProxyManager&&)       = delete;


	void      init();
	bool      load();

	void      mount();

	bool      checkFileInstalled();
	bool      checkFileHashMatch();
	bool      checkProxyLoaded();

	bool      uninstallProxy();
	bool      removeFileOnReboot();

	void      enable();
	void      disable();

private:
	result_t  _installDllFile(bool& needReboot);
	result_t  _hashDllFile(bool& needReboot);
	result_t  _getDllFileHash(UCHAR hash[32]);
	result_t  _cancelPendingDelete();
	bool      _fileExists(const wchar_t* path);
	bool      _filesContentEqual(const wchar_t* path1, const wchar_t* path2);

public:
	std::atomic<bool>      mountEnabled{ true };
	std::atomic<bool>      heartBleedEnabled{ false };
	std::atomic<DWORD>     appliedCount{ 0 };
	std::atomic<DWORD>     totalCount{ 5 };

private:
	std::atomic<DWORD>     mountPid{ 0 };
};