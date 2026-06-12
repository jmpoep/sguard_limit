#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include "win32utility.h"  // result_t, unexpected_error


// kernel driver module (sington)

class KernelDriver {

private:
	KernelDriver() = default;
	~KernelDriver();

public:
	static KernelDriver& getInstance() {
		static KernelDriver instance;
		return instance;
	}

	KernelDriver(const KernelDriver&)               = delete;
	KernelDriver(KernelDriver&&)                    = delete;
	KernelDriver& operator= (const KernelDriver&)   = delete;
	KernelDriver& operator= (KernelDriver&&)        = delete;


public:
	void       init();
	bool       checkLoadable();
	
	result_t   load();
	void       unload();

	result_t   readVM(DWORD pid, PVOID out, PVOID targetAddress);
	result_t   writeVM(DWORD pid, PVOID in, PVOID targetAddress);
	result_t   allocVM(DWORD pid, PVOID* pAllocatedAddress);
	result_t   suspend(DWORD pid);
	result_t   resume(DWORD pid);
	result_t   searchVad(DWORD pid, std::vector<ULONG64>& out, const wchar_t* moduleName);
	result_t   restoreVad(/* param in kernel */);
	result_t   patchAceBase();


public:
	void       loadConfig();
	void       writeConfig();


public:
	// [out] whether kdriver is ready to use.
	// flag returned from checkLoadable(); decide accessibility to some menu options.
	bool       driverReady{false};

	// [xref] assert use same kernel offset, despite the risk of bsod.
	// flag read from config; decide if win11 latest check is ignored.
	bool       win11ForceEnable{false};

	// [xref] current win11 build number.
	// num read from config; decide if win11 has updated.
	DWORD      win11CurrentBuild{0};


private:
	result_t     _runSystemCheck();
	void         _importCertKey();
	void         _removeCertKey();
	result_t     _checkSysVersionMatch();
	result_t     _extractResource();
	std::string  _strUserManual();
	result_t     _startService();
	void         _endService();

private:
	std::string         sysImagePath{};
	HANDLE              hDriver{INVALID_HANDLE_VALUE};

	std::atomic<DWORD>  loadCount{0};  // thread sync for: load/unload
	std::mutex          loadLock{};
};