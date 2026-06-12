#pragma once
#include <Windows.h>
#include <atomic>


// limit module (sington)

class LimitManager {

private:
	LimitManager()   = default;
	~LimitManager()  = default;

public:
	static LimitManager& getInstance() {
		static LimitManager instance;
		return instance;
	}

	LimitManager(const LimitManager&)                = delete;
	LimitManager(LimitManager&&)                     = delete;
	LimitManager& operator= (const LimitManager&)    = delete;
	LimitManager& operator= (LimitManager&&)         = delete;


public:
	std::atomic<bool>      limitEnabled{true};
	std::atomic<DWORD>     limitPercent{90};
	std::atomic<bool>      useKernelMode{true};

public:
	void     init();
	void     loadConfig();
	void     writeConfig();

public:
	void     hijack();
	void     enable();                      // [if & only if] systemMgr.mode choose that mode, enable switch works.
	void     disable();
	void     setPercent(DWORD percent);     // setxxx is designed to trigger enable.
};