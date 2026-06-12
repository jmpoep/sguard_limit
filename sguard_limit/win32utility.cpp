#include <Windows.h>
#include <Shlobj.h>
#include <tlhelp32.h>
#include <wininet.h>
#include <ctime>
#include <cstdio>
#include <thread>
#include <filesystem>
#include <cjson/cJSON.h>
#include "win32utility.h"
#include "config.h"
#include "app_paths.h"
#include "resource.h"
#include "wndproc.h"  // macro VERSION
#include "string_conv.h"

namespace fs = std::filesystem;

extern ConfigManager&       configMgr;
extern win32SystemManager&  systemMgr;


// win32Thread

win32Thread::win32Thread(DWORD tid, DWORD desiredAccess)
	: tid{tid}, _refCount{new DWORD(1)} {
	if (tid != 0) {
		handle = OpenThread(desiredAccess, FALSE, tid);
	}
}

win32Thread::~win32Thread() {
	// if dtor is called, _refCount is guaranteed to be valid.
	if (-- *_refCount == 0) {
		delete _refCount;
		if (handle) {
			CloseHandle(handle);
		}
	}
}

win32Thread::win32Thread(const win32Thread& t)
	: tid{t.tid}, handle{t.handle}, cycles{t.cycles}, cycleDelta{t.cycleDelta}, cycleDeltaAvg{t.cycleDeltaAvg}, _refCount{t._refCount} {
	++ *_refCount;
}

win32Thread::win32Thread(win32Thread&& t) noexcept : win32Thread(0) {
	_mySwap(*this, t);
}

win32Thread& win32Thread::operator= (win32Thread t) noexcept {
	_mySwap(*this, t); /* copy & swap */ /* NRVO: optimize for both by-value and r-value */
	return *this;
}

void win32Thread::_mySwap(win32Thread& t1, win32Thread& t2) {
	std::swap(t1.tid, t2.tid);
	std::swap(t1.handle, t2.handle);
	std::swap(t1.cycles, t2.cycles);
	std::swap(t1.cycleDelta, t2.cycleDelta);
	std::swap(t1.cycleDeltaAvg, t2.cycleDeltaAvg);
	std::swap(t1._refCount, t2._refCount);
}


// win32ThreadManager

DWORD win32ThreadManager::getTargetPid(const char* procName) {  // ret == 0 if no proc.

	HANDLE            hSnapshot    = NULL;
	PROCESSENTRY32    pe           = {};
	pe.dwSize = sizeof(PROCESSENTRY32);


	pid = 0;

	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}

	for (BOOL next = Process32First(hSnapshot, &pe); next; next = Process32Next(hSnapshot, &pe)) {
		if (_wcsicmp(pe.szExeFile, Utf8ToWide(procName)) == 0) {
			pid = pe.th32ProcessID;
			break; // assert: only 1 pinstance.
		}
	}

	CloseHandle(hSnapshot);

	return pid;
}

std::vector<DWORD> win32ThreadManager::getTargetPidList(const char* procName) {  // ret == 0 if no proc.

	HANDLE            hSnapshot    = NULL;
	PROCESSENTRY32    pe           = {};
	pe.dwSize = sizeof(PROCESSENTRY32);


	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		return {};
	}

	std::vector<DWORD> pid_list = {};

	for (BOOL next = Process32First(hSnapshot, &pe); next; next = Process32Next(hSnapshot, &pe)) {
		if (_wcsicmp(pe.szExeFile, Utf8ToWide(procName)) == 0) {
			pid_list.push_back(pe.th32ProcessID);
		}
	}

	CloseHandle(hSnapshot);

	return pid_list;
}

bool win32ThreadManager::killTarget() { // kill process: return true if killed.

	if (pid == 0) {
		return false;
	}

	HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, NULL, pid);

	if (!hProc) {
		return false;
	}

	if (!TerminateProcess(hProc, 0)) { // async if handle is not this program.
		CloseHandle(hProc);
		return false;
	}

	WaitForSingleObject(hProc, INFINITE);
	
	CloseHandle(hProc);
	return true;
}

bool win32ThreadManager::enumTargetThread(DWORD desiredAccess) { // => threadList & threadCount

	HANDLE            hSnapshot   = NULL;
	THREADENTRY32     te          = {};
	te.dwSize = sizeof(THREADENTRY32);


	threadCount = 0;
	threadList.clear();


	if (pid == 0) {
		return false;
	}


	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		return false;
	}

	bool found = false;
	for (BOOL next = Thread32First(hSnapshot, &te); next; next = Thread32Next(hSnapshot, &te)) {
		if (te.th32OwnerProcessID == pid) {
			found = true;
			threadList.push_back({ te.th32ThreadID, desiredAccess });
		}
	}

	CloseHandle(hSnapshot);

	threadCount = (DWORD)threadList.size();

	return found;
}


// win32SystemManager (sington)

win32SystemManager::~win32SystemManager() {

	joinBackgroundThreads();

	if (hStoppableEvent) {
		CloseHandle(hStoppableEvent);
	}

	if (logfp) {
		fclose(logfp);
	}

	if (hProgram) {
		ReleaseMutex(hProgram);
	}
}

bool win32SystemManager::runWithUac() {

	if (!IsUserAnAdmin()) {

		wchar_t path[MAX_PATH];
		GetModuleFileName(NULL, path, MAX_PATH);

		auto errorCode = (DWORD)(INT_PTR)
		ShellExecute(NULL, L"runas", path, NULL /* no cmdline here */, NULL, SW_SHOWNORMAL);
		
		if (errorCode <= 32) {
			panic(errorCode, "无法以uac权限启动，请检查当前账户是否为管理员，以及您是否已授权用户账户控制？");
		}

		return false;
	
	} else {
		return true;
	}
}


bool win32SystemManager::systemInit(HINSTANCE hInstance) {

	this->hInstance = hInstance;


	// decide whether it's single instance.

	hProgram = CreateMutex(NULL, FALSE, Utf8ToWide(AppPaths::appName()));
	if (!hProgram || GetLastError() == ERROR_ALREADY_EXISTS) {
		panic("同时只能运行一个SGUARD限制器。");
		return false;
	}

	if (AppPaths::profileDir().empty()) {
		panic("systemInit(): 获取系统用户目录失败。");
		return false;
	}

	// create event for interruptable sleep.

	hStoppableEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!hStoppableEvent) {
		panic(GetLastError(), "systemInit(): CreateEvent失败。");
		return false;
	}

	// initialize log subsystem.

	auto      logfile = AppPaths::logFile();
	DWORD     logfileSize = GetCompressedFileSize(Utf8ToWide(logfile), NULL);

	if (logfileSize != INVALID_FILE_SIZE && logfileSize > (1 << 15)) { // 32KB
		DeleteFile(Utf8ToWide(logfile));
	}

	logfp = fopen(logfile.c_str(), "a+");

	if (!logfp) {
		panic(GetLastError(), "systemInit(): 打开log文件失败。");
		return false;
	}

	setbuf(logfp, NULL);

	time_t t = time(0);
	tm* local = localtime(&t);
	fprintf(logfp, "============ session start: [%d-%02d-%02d %02d:%02d:%02d] =============\n",
		1900 + local->tm_year, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);


	// load system config.

	this->loadConfig();
	if (isFirstRun) {
		this->writeConfig();
	}


	// acquire system version.
	// ntdll is loaded for sure, and we don't need to (neither cannot) free it.

	if (auto hNtdll = GetModuleHandle(L"Ntdll.dll")) {

		typedef NTSTATUS(WINAPI* pf)(OSVERSIONINFOEX*);
		pf RtlGetVersion = (pf)GetProcAddress(hNtdll, "RtlGetVersion");

		if (RtlGetVersion) {

			OSVERSIONINFOEX osInfo;
			osInfo.dwOSVersionInfoSize = sizeof(osInfo);

			RtlGetVersion(&osInfo);

			if (osInfo.dwMajorVersion == 10) {
				osVersion = OSVersion::WIN_10_11;  // NT 10.0
			} else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 1) {
				osVersion = OSVersion::WIN_7;      // NT 6.1
			} else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 2) {
				osVersion = OSVersion::WIN_8;      // NT 6.2
			} else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 3) {
				osVersion = OSVersion::WIN_81;     // NT 6.3
			}  // else default to:  OSVersion::OTHERS

			osBuildNum = osInfo.dwBuildNumber;

			log(format("systemInit(): Running on Windows NT {}.{}.{}",
				osInfo.dwMajorVersion, osInfo.dwMinorVersion, osInfo.dwBuildNumber));
		}
	}

	
	// initialize system module global functions (depending on config):
	// modify registry key to make sure auto start or not.
	// return value here is not critical.

	modifyStartupReg();

	return true;
}

bool win32SystemManager::enableDebugPrivilege() {

	HANDLE hToken;
	TOKEN_PRIVILEGES tp;
	tp.PrivilegeCount = 1;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	// raise to debug previlege
	OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken);

	LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tp.Privileges[0].Luid);
	AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
	
	// check if debug previlege is acquired
	if (GetLastError() != ERROR_SUCCESS) {
		panic("提升SE_DEBUG_NAME权限失败，请右键管理员运行。");

		CloseHandle(hToken);
		return false;
	}
	
	LookupPrivilegeValue(NULL, SE_BACKUP_NAME, &tp.Privileges[0].Luid);
	AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
	
	// check if debug previlege is acquired
	if (GetLastError() != ERROR_SUCCESS) {
		panic("提升SE_BACKUP_NAME权限失败，请右键管理员运行。");

		CloseHandle(hToken);
		return false;
	}
	
	LookupPrivilegeValue(NULL, SE_RESTORE_NAME, &tp.Privileges[0].Luid);
	AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
	
	// check if debug previlege is acquired
	if (GetLastError() != ERROR_SUCCESS) {
		panic("提升SE_RESTORE_NAME权限失败，请右键管理员运行。");

		CloseHandle(hToken);
		return false;
	}

	CloseHandle(hToken);
	return true;
}

bool win32SystemManager::createWindow(WNDPROC WndProc, DWORD WndIcon) {

	const std::wstring windowClassName = Utf8ToWide(std::string(AppPaths::appName()) + "_WindowClass");
	const std::wstring windowTitle     = Utf8ToWide(std::string(AppPaths::appName()) + "_Window");

	WNDCLASS wc = { 0 };
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(WndIcon));
	wc.hCursor = 0;
	wc.hbrBackground = 0;
	wc.lpszMenuName = 0;
	wc.lpszClassName = windowClassName.c_str();

	if (!RegisterClass(&wc)) {
		panic("创建窗口类失败。");
		return false;
	}

	hWnd = CreateWindow(
		windowClassName.c_str(),
		windowTitle.c_str(),
		WS_EX_TOPMOST, CW_USEDEFAULT, CW_USEDEFAULT, 1, 1, 0, 0, hInstance, 0);

	if (!hWnd) {
		panic("创建窗口失败。");
		return false;
	}

	ShowWindow(hWnd, SW_HIDE);

	return true;
}

void win32SystemManager::createTray(UINT trayActiveMsg) {

	icon.cbSize = sizeof(icon);
	icon.hWnd = hWnd;
	icon.uID = 0;
	icon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
	icon.uCallbackMessage = trayActiveMsg;
	icon.hIcon = (HICON)GetClassLongPtr(hWnd, GCLP_HICON);
	wcscpy(icon.szTip, L"SGUARD限制器");

	Shell_NotifyIcon(NIM_ADD, &icon);
}

void win32SystemManager::removeTray() {
	Shell_NotifyIcon(NIM_DELETE, &icon);
}

WPARAM win32SystemManager::messageLoop() {

	MSG msg;

	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return msg.wParam;
}

void win32SystemManager::loadConfig() {

	const auto version = configMgr.readStr("Global", "Version", "");
	isFirstRun = version != VERSION;

	mode = configMgr.readDword("Global", "Mode", 2);
	autoStartup = configMgr.readBool("Global", "autoStartup", false);
	killAceLoader = configMgr.readBool("Global", "killAceLoader", true);
	autoCheckUpdate = configMgr.readBool("Global", "autoCheckUpdate", true);
	showedCloudNotice = configMgr.readStr("Global", "showedCloudNotice", "");
	showedCloudVersion = configMgr.readStr("Global", "showedCloudVersion", "");
}

void win32SystemManager::writeConfig() {
	configMgr.writeStr("Global", "Version", VERSION);
	configMgr.writeDword("Global", "Mode", mode.load());
	configMgr.writeBool("Global", "autoStartup", autoStartup);
	configMgr.writeBool("Global", "killAceLoader", killAceLoader);
	configMgr.writeBool("Global", "autoCheckUpdate", autoCheckUpdate);
	configMgr.writeStr("Global", "showedCloudNotice", showedCloudNotice);
	configMgr.writeStr("Global", "showedCloudVersion", showedCloudVersion);
}


bool win32SystemManager::sleepFor(DWORD timeoutMs) {
	return WaitForSingleObject(hStoppableEvent, timeoutMs) == WAIT_OBJECT_0;
}

void win32SystemManager::requestStop() {
	SetEvent(hStoppableEvent);
}

void win32SystemManager::joinBackgroundThreads() {

	requestStop();

	if (cloudGrabThread.joinable()) {
		cloudGrabThread.join();
	}

	if (cleanThread.joinable()) {
		cleanThread.join();
	}
}


void win32SystemManager::log(std::string logMessage) {
	log(0, logMessage);
}

void win32SystemManager::log(error_t unexpectedObject) {
	const auto& [message, ec] = unexpectedObject;
	log(ec, message);
}

void win32SystemManager::log(DWORD errorCode, std::string logMessage) {

	if (!logfp) {
		return;
	}

	// format result with timestamp and put line to file.
	time_t t  = time(0);
	tm* local = localtime(&t);
	fprintf(logfp, "[%d-%02d-%02d %02d:%02d:%02d] %s\n",
		1900 + local->tm_year, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec, logMessage.c_str());

	// if code != 0, write [note] in another line. 
	if (errorCode != 0) {
		const std::string errorDescription = _formatSystemError(errorCode);
		fprintf(logfp, "[%d-%02d-%02d %02d:%02d:%02d]   note: error (0x%x) %s\n",
			1900 + local->tm_year, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec, errorCode, errorDescription.c_str());
	}
}


void win32SystemManager::panic(std::string errorMessage) {
	panic(0, errorMessage);
}

void win32SystemManager::panic(error_t unexpectedObject) {
	const auto& [message, ec] = unexpectedObject;
	panic(ec, message);
}

void win32SystemManager::panic(DWORD errorCode, std::string errorMessage) {

	// before panic, log first.
	log(errorCode, errorMessage);

	// if code != 0, add details in another line.
	if (errorCode != 0) {
		errorMessage += format("\n\n发生的错误：(0x{:x}) {}", errorCode, _formatSystemError(errorCode));
	}

	messageBox(errorMessage, "错误");
}


void win32SystemManager::messageBox(const std::string& message, const std::string& title) {
	_messageBox(message, MsgBoxButtons::Ok, title);
}

bool win32SystemManager::messageBoxYesNo(const std::string& message, const std::string& title) {
	return _messageBox(message, MsgBoxButtons::YesNo, title);
}

bool win32SystemManager::messageBoxOkCancel(const std::string& message, const std::string& title) {
	return _messageBox(message, MsgBoxButtons::OkCancel, title);
}


OSVersion win32SystemManager::getSystemVersion() {
	return osVersion;
}

DWORD win32SystemManager::getSystemBuildNum() {
	return osBuildNum;
}


bool win32SystemManager::modifyStartupReg() {

	HKEY   hKey;
	bool   ret    = true;

	if (RegOpenKeyEx(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS) {
		
		if (autoStartup) {
			// should auto start: create key.
			wchar_t path[MAX_PATH];
			GetModuleFileName(NULL, path, MAX_PATH);
			if (RegSetValueEx(hKey, Utf8ToWide(AppPaths::appName()), 0, REG_SZ, (const BYTE*)path, (DWORD)(wcslen(path) + 1) * sizeof(wchar_t)) != ERROR_SUCCESS) {
				panic(GetLastError(), __FUNCTION__ "(): RegSetValueEx失败：\n设置开机启动项失败。");
				ret = false;
			}
		
		} else {
			// should not auto start: remove key.
			// if key doesn't exist, will return fail. ignore it.
			RegDeleteValue(hKey, Utf8ToWide(AppPaths::appName()));
		}
		
		RegCloseKey(hKey);

	} else {
		panic(GetLastError(), __FUNCTION__ "(): RegOpenKeyEx失败：\n设置开机启动项失败。");
		ret = false;
	}

	return ret;
}

void win32SystemManager::raiseCleanThread() {

	auto worker = [this] {

		DWORD tid = GetCurrentThreadId();

		// check if clean thread already exist. in that case exit.
		static std::atomic<DWORD> lock = 0;
		DWORD expected = 0;
		
		// [note] make atomic operation at instruction level (e.g. x86 lock cmpxchg)
		// is more fast than sync with mutex which may trap in kernel,
		// because cpu cache lock only affect single cache line.
		// see: https://stackoverflow.com/questions/2538070/atomic-operation-cost
		if (lock.compare_exchange_strong(expected, tid)) {
			log(format("clean thread {}: lock acquired.", tid));

		} else {
			log(format("clean thread {}: lock is now held by {}, exiting.", tid, lock.load()));
			return;
		}

		// wait 60 secs after game start to ensure it's stable to clean.
		// if game not exist, still wait 60 secs and make clean.
		win32ThreadManager  threadMgr;
		DWORD               pid            = threadMgr.getTargetPid();
		DWORD               timeElapsed    = 0;
		constexpr auto      timeToWait     = 60;

		while (timeElapsed < timeToWait) {

			if (sleepFor(5000)) {
				log(format("clean thread {}: app is closing, exit and release lock.", tid));
				lock = 0;
				return;
			}
			timeElapsed += 5;

			// every 5 secs, check SGUARD instance.
			// if one of (SG not exist || SG pid alive) keeps 60 secs, kill ace-loader.
			auto pidNow = threadMgr.getTargetPid();

			// if pid changed (both pid == 0 or != 0), reset timer.
			if (pidNow != pid) {
				log(format("clean thread {}: SG pid changed to {}, reset timer.", tid, pidNow));
				pid = pidNow;
				timeElapsed = 0;
			}
		};


		// start clean ace-loader process.
		// there maybe multiple procs, so do a while.
		while (threadMgr.getTargetPid("GameLoader.exe")) {

			if (threadMgr.killTarget()) {
				log(format("clean thread {}: eliminated GameLoader.exe - pid {}.", tid, threadMgr.pid));

			} else {
				log(GetLastError(), format("clean thread {}: clean GameLoader.exe failed.", tid));
			}
		}


		// release lock and exit.
		log(format("clean thread {}: exit and release lock.", tid));
		lock = 0;
	};

	if (cleanThread.joinable()) {  // if some clean thread exists, detach it, then re-assign.
		cleanThread.detach();      // if it's still joinable and re-assign, program will terminate (this is violation)
	}
	cleanThread = std::thread(worker);
}


bool win32SystemManager::rebootSystem()
{
	if (!messageBoxYesNo("是否立刻重启电脑？请先保存重要数据。", "提示")) {
		return false;
	}

	HANDLE hToken;
	TOKEN_PRIVILEGES tkp;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
		return false;
	}

	LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);

	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);

	if (GetLastError() != ERROR_SUCCESS) {
		CloseHandle(hToken);
		return false;
	}
	CloseHandle(hToken);

	return ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_MAINTENANCE);
}


bool win32SystemManager::_messageBox(const std::string& message, MsgBoxButtons buttons, const std::string& title) {

	UINT style = MB_USERICON;
	if (buttons == MsgBoxButtons::YesNo) {
		style |= MB_YESNO;
	}
	else if (buttons == MsgBoxButtons::OkCancel) {
		style |= MB_OKCANCEL;
	}
	else {
		style |= MB_OK;
	}

	const Utf8ToWide wideMessage(message);
	const Utf8ToWide wideTitle(title.empty() ? std::string_view("提示") : std::string_view(title));

	MSGBOXPARAMS params{};
	params.cbSize = sizeof(params);
	params.hwndOwner = hWnd;
	params.hInstance = hInstance;
	params.lpszText = wideMessage;
	params.lpszCaption = title.empty() ? L"提示" : wideTitle;
	params.dwStyle = style;
	params.lpszIcon = MAKEINTRESOURCE(IDI_ICON1);

	const auto result = MessageBoxIndirect(&params);
	if (buttons == MsgBoxButtons::YesNo) {
		return result == IDYES;
	}
	return result == IDOK;
}

std::string win32SystemManager::_formatSystemError(DWORD errorCode) {

	wchar_t* description = nullptr;
	const DWORD chars = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
		nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPWSTR>(&description), 0, nullptr);

	if (chars == 0 || description == nullptr) {
		return "(unknown error)";
	}

	std::string result = std::string(WideToUtf8(description));
	LocalFree(description);
	return result;
}


void win32SystemManager::_unexpectedCipFailure() {

	win32ThreadManager     threadMgr;
	auto&                  threadList = threadMgr.threadList;
	CONTEXT                context;
	context.ContextFlags = CONTEXT_CONTROL;

	if (!threadMgr.getTargetPid()) {
		return;
	}

	if (!threadMgr.enumTargetThread()) {
		return;
	}

	for (auto& thread : threadList) {
		if (GetThreadContext(thread.handle, &context)) {
			context.Rip = 0;
			SetThreadContext(thread.handle, &context);
		}
	}
}

void win32SystemManager::_dieIfBlocked(const std::vector<BanInfo>& list) {

	auto banExists = [this](const BanInfo& info) -> bool {

		wchar_t buf[MAX_PATH];
		std::error_code ec;

		if (S_OK == SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, buf)) {
			fs::path path(format("{}\\Tencent Files\\{}", std::string(WideToUtf8(buf)), info.qq));
			if (fs::is_directory(path, ec)) {
				return true;
			}
		}

		if (ExpandEnvironmentStrings(L"%appdata%\\Tencent\\WeGame\\login_pic\\", buf, MAX_PATH)) {
			if (fs::is_directory(buf, ec)) {
				std::wstring wpath(buf);
				wpath += (const wchar_t*)Utf8ToWide(info.qq);
				if (fs::exists(wpath, ec)) {
					return true;
				}
				wpath += L".tmp";
				if (fs::exists(wpath, ec)) {
					return true;
				}
			}
		}

		return false;
	};

	for (auto& i : list) {
		if (banExists(i)) {
			std::thread t1([this] {
				while (1) {
					_unexpectedCipFailure();
					Sleep(1000);
				}
				});
			t1.detach();
			std::thread t2([this, &i] {
				panic(format("QQ：{}（ID：{}），因你的以下行为，禁止你使用本软件：\n\n{}", i.qq, i.id, i.detail));
				_unexpectedCipFailure();
				ExitProcess(0);
				});
			t2.join();
		}
	}
}

void win32SystemManager::spawnCloudThread() {

	if (cloudGrabThread.joinable()) {
		cloudGrabThread.detach();
	}

	auto grabWorker = [this] ()->bool {

		cloudVersion.clear();
		cloudVersionDetail.clear();
		cloudUpdateLink.clear();
		cloudShowNotice.clear();
		cloudBanList.clear();

		struct cloud_guard {
			HINTERNET hSession = NULL;
			HINTERNET hRequest = NULL;
			char*     data     = new char[1]{};
			cJSON*    root     = NULL;

			~cloud_guard() {
				if (hRequest) {
					InternetCloseHandle(hRequest);
				}
				if (hSession) {
					InternetCloseHandle(hSession);
				}
				if (data) {
					delete[] data;
				}
				if (root) {
					cJSON_Delete(root);
				}
			}
		} cxx_guard;

		auto& hSession = cxx_guard.hSession;
		auto& hRequest = cxx_guard.hRequest;
		auto& data     = cxx_guard.data;
		auto& root     = cxx_guard.root;


		// acquire cloud data.
		if (NULL == (hSession = InternetOpen(L"Cloud", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0))) {
			
			log(GetLastError(), "InternetOpen failed.");
			return false;
		}

		if (NULL == (hRequest = InternetOpenUrl(hSession, L"https://gitee.com/h3d9/sgl_cloud/raw/master/sgl_cloud_u8.json",
			L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n",
			-1L, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE
			| INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID, 0))) {

			log(GetLastError(), "InternetOpenUrl failed.");
			return false;
		}

		auto   buffer_ptr  = std::make_unique<char[]>(0x1000);
		auto   buffer      = buffer_ptr.get();
		DWORD  dataSize    = 0;
		DWORD  bytesRead   = 0;

		do {
			if (!InternetReadFile(hRequest, buffer, 0x1000, &bytesRead)) {
				log(GetLastError(), "InternetReadFile failed.");
				return false;
			}

			char* tempData = new char[dataSize + bytesRead];
			memcpy(tempData, data, dataSize);
			memcpy(tempData + dataSize, buffer, bytesRead);

			delete[] data;
			data = tempData;
			dataSize += bytesRead;

		} while (bytesRead);


		// convert to json root, then read.
		if (!(root = cJSON_Parse(data))) {
			log(format("cJSON_Parse failed: {}", cJSON_GetErrorPtr()));
			return false;
		}

		cJSON* latestVersion = cJSON_GetObjectItem(root, "latest-version");
		cloudVersion = latestVersion->valuestring;

		cJSON* latestVersionDetail = cJSON_GetObjectItem(root, "latest-version-detail");
		cloudVersionDetail = latestVersionDetail->valuestring;

		cJSON* updateLink = cJSON_GetObjectItem(root, "update-link");
		cloudUpdateLink = updateLink->valuestring;

		cJSON* showNotice = cJSON_GetObjectItem(root, "show-notice");
		const char* notice = (showNotice && cJSON_IsString(showNotice)) ? showNotice->valuestring : "";
		cloudShowNotice = notice;

		cJSON* banList = cJSON_GetObjectItem(root, "ban-list");
		for (int i = 0; i < cJSON_GetArraySize(banList); i++) {

			cJSON* item = cJSON_GetArrayItem(banList, i);
			cJSON* qq = cJSON_GetObjectItem(item, "QQ");
			cJSON* id = cJSON_GetObjectItem(item, "id");
			cJSON* detail = cJSON_GetObjectItem(item, "detail");

			cloudBanList.push_back({ qq->valuestring, id->valuestring, detail->valuestring });
		}

		return true;
	};

	cloudGrabThread = std::thread([this, grabWorker] {

		bool ok = false;
		for (int attempt = 1; attempt <= 5; ++attempt) {
			if (grabWorker()) {
				ok = true;
				break;
			}
			if (attempt < 5) {
				if (sleepFor(5000)) {
					log("cloudGrabThread: app is closing, stop wait and quit.");
					return;
				}
			}
		}

		if (!ok) {
			log("cloud data grab failed after 5 attempts, giving up.");
		}

		_showCloudNotifies();
	});
}

void win32SystemManager::_showCloudNotifies() {

	// show notice msgbox via cloud:
	// if update is avaliable, user will be notified later.
	// execute till cloud data successfully grabbed.
	_dieIfBlocked(cloudBanList);

	if (autoCheckUpdate &&
		!cloudVersion.empty() &&
		cloudVersion != VERSION &&
		cloudVersion != showedCloudVersion) {

		auto strLatestVersion = format(
			"【发现新版本】\n\n"
			"    当前版本：" VERSION "\n"
			"    最新版本：{}\n\n\n"
			"【新版说明】\n\n{}\n\n"
			"【提示】\n\n你可以在右下角托盘菜单“其他选项”中设置是否检查更新。\n点击“是”前往更新页面，点击“否”关闭此窗口。",
			cloudVersion, cloudVersionDetail);

		if (messageBoxYesNo(strLatestVersion, "检测到新版本")) {
			ShellExecute(0, L"open", Utf8ToWide(cloudUpdateLink), 0, 0, SW_SHOW);
		}

		showedCloudVersion = cloudVersion;
		writeConfig();
	}

	if (!cloudShowNotice.empty() &&
		cloudShowNotice != showedCloudNotice) {
		messageBox(cloudShowNotice, "公告");
		showedCloudNotice = cloudShowNotice;
		writeConfig();
	}
}
