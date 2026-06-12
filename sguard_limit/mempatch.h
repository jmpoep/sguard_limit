#pragma once
#include <Windows.h>
#include <string>
#include <atomic>
#include <unordered_map>


// mempatch module (sington)
class PatchManager {

private:
	PatchManager();
	~PatchManager() = default;

public:
	static PatchManager& getInstance() {
		static PatchManager instance;
		return instance;
	}

	PatchManager(const PatchManager&)                = delete;
	PatchManager(PatchManager&&)                     = delete;
	PatchManager& operator= (const PatchManager&)    = delete;
	PatchManager& operator= (PatchManager&&)         = delete;


public:
	typedef struct tagPatchSwitches_t {
		std::atomic<bool>   NtQueryVirtualMemory     = false;
		std::atomic<bool>   NtReadVirtualMemory      = false; // no delay
		std::atomic<bool>   GetAsyncKeyState         = false;
		std::atomic<bool>   NtWaitForSingleObject    = false;
		std::atomic<bool>   NtDelayExecution         = false;
		std::atomic<bool>   DeviceIoControl_1        = false; // no delay
		std::atomic<bool>   DeviceIoControl_1x       = false;
		std::atomic<bool>   DeviceIoControl_2        = false; // no delay
	} patchSwitches_t, patchStatus_t;

	struct patchDelayRange_t {
		DWORD low, def, high;
	};

	std::atomic<bool>             patchEnabled{true};

	patchSwitches_t               patchSwitches{};
	std::atomic<DWORD>            patchDelay[5]{};
	const patchDelayRange_t       patchDelayRange[5];

	std::atomic<DWORD>            patchPid{0};
	patchStatus_t                 patchStatus{};

	std::atomic<DWORD>            patchDelayBeforeNtdlletc{20};


public:
	void      loadConfig();
	void      writeConfig();

public:
	bool      init();
	void      patch();
	bool      patch_r0();

	void      enable(bool forceRecover = false);
	void      disable(bool forceRecover = false);


private:
	void      _applyDefaultConfig();
	DWORD     _getSyscallNumber(const char* funcName, LPCWSTR libName);

	bool      _patch_ntdll(DWORD pid, patchSwitches_t& switches);
	bool      _patch_user32(DWORD pid, patchSwitches_t& switches);
	
	bool      _fixThreadContext(ULONG64 pOrginalStart, ULONG64 patchSize, ULONG64 pDetourStart);

private:
	std::unordered_map<std::string, DWORD>   syscallTable{};  // func name -> native syscall num
};