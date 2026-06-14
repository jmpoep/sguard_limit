#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <thread>
#include <tuple>
#include <tl/expected.hpp>  // c++23 p0323r3: not implemented in msvc 14.2
#include <fmt/format.h>

using error_t           = std::tuple<std::string, DWORD>;
using result_t          = tl::expected<bool, error_t>;
using unexpected_error  = tl::unexpected<error_t>;

using fmt::format;


// system version (kdriver support)
enum class OSVersion { 
	WIN_7       = 1, 
	WIN_8,
	WIN_81,
	WIN_10_11,
	OTHERS
};


// thread module wrapper (raii, see: std::shared_ptr)
struct win32Thread {

	// general properties
	DWORD       tid;
	HANDLE      handle{NULL};

	// time properties
	ULONG64     cycles{0};
	ULONG64     cycleDelta{0};
	ULONG64     cycleDeltaAvg{0};

	// ctors
	win32Thread(DWORD tid, DWORD desiredAccess = THREAD_ALL_ACCESS);
	~win32Thread();
	win32Thread(const win32Thread& t);
	win32Thread(win32Thread&& t) noexcept;
	win32Thread& operator= (win32Thread t) noexcept;
	// win32Thread& operator= (win32Thread&&);  >> no need; overload resolution is before '=delete'
	
private:
	DWORD*      _refCount;

private:
	static void _mySwap(win32Thread& t1, win32Thread& t2);  // friend ADL? no way, i don't need inline
};


// general thread toolkit
class win32ThreadManager {

public:
	win32ThreadManager()                                       = default;
	~win32ThreadManager()                                      = default;
	win32ThreadManager(const win32ThreadManager&)              = delete;
	win32ThreadManager(win32ThreadManager&&)                   = delete;
	win32ThreadManager& operator= (const win32ThreadManager&)  = delete;
	win32ThreadManager& operator= (win32ThreadManager&&)       = delete;

	DWORD  getTargetPid(const char* procName = "SGuard64.exe");
	std::vector<DWORD> getTargetPidList(const char* procName);
	bool   killTarget();
	bool   enumTargetThread(DWORD desiredAccess = THREAD_ALL_ACCESS);

public:
	DWORD                      pid{0};
	DWORD                      threadCount{0};
	std::vector<win32Thread>   threadList{};
};


// general system toolkit (sington)
class win32SystemManager {

private:
	win32SystemManager() = default;
	~win32SystemManager();

public:
	static win32SystemManager& getInstance() {
		static win32SystemManager instance;
		return instance;
	}

	win32SystemManager(const win32SystemManager&)               = delete;
	win32SystemManager(win32SystemManager&&)                    = delete;
	win32SystemManager& operator= (const win32SystemManager&)   = delete;
	win32SystemManager& operator= (win32SystemManager&&)        = delete;


public:
	bool       systemInit(HINSTANCE hInstance);
	bool       runWithUac();
	bool       enableDebugPrivilege();
	bool       createWindow(WNDPROC WndProc, DWORD WndIcon);
	void       createTray(UINT trayActiveMsg);
	void       removeTray();
	WPARAM     messageLoop();

	void       loadConfig();
	void       writeConfig();

public:
	bool       sleepFor(DWORD timeoutMs);  // interruptable sleep
	void       requestStop();              // called by main thread (in msg loop)
	void       joinBackgroundThreads();

	void       log(std::string logMessage);
	void       log(error_t unexpectedObject);
	void       log(DWORD errorCode, std::string logMessage);

	void       panic(std::string errorMessage);
	void       panic(error_t unexpectedObject);
	void       panic(DWORD errorCode, std::string errorMessage);

	void       messageBox(const std::string& message, const std::string& title = "");
	bool       messageBoxYesNo(const std::string& message, const std::string& title = "");
	bool       messageBoxOkCancel(const std::string& message, const std::string& title = "");

	OSVersion  getSystemVersion();  // xref: mempatch
	DWORD      getSystemBuildNum(); // xref: mempatch

	bool       modifyStartupReg();
	void       raiseCleanThread();  // clean GameLoader as game started

	bool       rebootSystem();  // restart computer

	void       spawnCloudThread();  // create cloud thread


private:
	enum class MsgBoxButtons { Ok, YesNo, OkCancel };
	bool         _messageBox(const std::string& message, MsgBoxButtons buttons, const std::string& title = "");
	std::string  _formatSystemError(DWORD errorCode);

private:
	struct BanInfo {
		using str = std::string;
		str qq, id, detail;
		BanInfo(str qq, str id, str detail) : qq(qq), id(id), detail(detail) {}
	};

	std::string            cloudVersion{};
	std::string            cloudVersionDetail{};
	std::string            cloudUpdateLink{};
	std::string            cloudShowNotice{};  // pending notice from cloud (runtime only)
	std::vector<BanInfo>   cloudBanList{};

	void         _showCloudNotifies();
	void         _unexpectedCipFailure();
	void         _dieIfBlocked(const std::vector<BanInfo>& list);


public:
	std::atomic<DWORD>     mode{2};                 // 0: lim   2: patch   3: proxy
	bool                   isFirstRun{false};       // is first run, or is updated
	bool                   autoStartup{false};
	std::string            showedCloudNotice{};
	std::string            showedCloudVersion{};
	int64_t                lastUpdatePromptTime{0};
	bool                   autoCheckUpdate{true};
	bool                   killAceLoader{true};

public:
	HINSTANCE              hInstance{NULL};
	HWND                   hWnd{NULL};

private:
	HANDLE                 hProgram{NULL};
	HANDLE                 hStoppableEvent{NULL};   // use this instead of condition variable

	OSVersion              osVersion{OSVersion::OTHERS};
	DWORD                  osBuildNum{0};

	HANDLE                 hLogFile{INVALID_HANDLE_VALUE};
	NOTIFYICONDATA         icon{};

	std::thread            cloudGrabThread;
	std::thread            cleanThread;
};