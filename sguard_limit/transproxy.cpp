// NOTE: PART OF THIS SOURCE CODE IS NOT SAFE.
// FOR SECURITY REASONS, SOME PART OF THIS FILE IS REMOVED.

#include <Shlwapi.h>
#include <bcrypt.h>
#include <wil/resource.h>
#include <wil/registry.h>

#include "sync.h"
#include "win32utility.h"  // tiny::format
#include "transproxy.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "bcrypt.lib")

extern win32SystemManager& systemMgr;


namespace {

WCHAR sysDllPath[MAX_PATH] = L"C:\\Windows\\System32\\vsocks.dll";

constexpr UCHAR kVsocksDllSha256[32] = {
	0xbe, 0x36, 0xa7, 0x1a, 0xec, 0x51, 0x8b, 0x38,
	0xc1, 0xcf, 0xde, 0x22, 0x2a, 0x28, 0x2a, 0x21,
	0xc2, 0x27, 0xb0, 0xcf, 0x49, 0x34, 0xce, 0xa7,
	0x7e, 0x7e, 0x9f, 0xfe, 0x9f, 0x0e, 0xb2, 0xf2,
};

__declspec(noinline)
DWORD check()
{
	return 0;
}

__declspec(noinline)
DWORD sub_1()
{
	return 1;
}

__declspec(noinline)
DWORD Uninstall()
{
	return 1;
}

} // namespace


// ProxyManager impl

void ProxyManager::init() {
}

bool ProxyManager::load() {

	bool needReboot = false;

	if (checkProxyLoaded() && !checkFileInstalled()) {
		uninstallProxy();
		systemMgr.log("proxy(): orphan proxy without dll, cleaned.");
	}

	auto handleFailure = [&](error_t err) -> bool {
		const auto& [message, errorCode] = err;

		systemMgr.log("proxy(): load failed, uninstalling.");
		uninstallProxy();

		if (needReboot) {
			systemMgr.log(err);
			if (systemMgr.messageBoxYesNo(
				message + "\n\n【注意】建议重启电脑以清理残留文件。要现在重启吗？", "透明代理加载失败")) {
				removeFileOnReboot();
				systemMgr.rebootSystem();
			}
		} else {
			systemMgr.panic(err);
		}

		return false;
	};

	// 取消重启删除注册表；失败则 pending 删除项仍在，继续安装会在重启后丢 dll
	auto result = _cancelPendingDelete();
	if (!result) {
		needReboot = true;
		return handleFailure(result.error());
	}

	result = _installDllFile(needReboot);
	if (!result) {
		return handleFailure(result.error());
	}

	result = _hashDllFile(needReboot);
	if (!result) {
		return handleFailure(result.error());
	}

	// install
	auto ret = sub_1();
	if (ret == 1)
	{
		const char* msg = "proxy(): load success.";
		systemMgr.log(msg);
		return true;
	}
	else if (ret == 2)
	{
		const char* msg = "proxy(): already loaded.";
		systemMgr.log(msg);
		return true;
	}

	return handleFailure({ format(__FUNCTION__ "(): 加载失败：{}", ret), GetLastError() });
}


__declspec(noinline)
void ProxyManager::mount() {

	win32ThreadManager     threadMgr;
	SimpleStateSync        sync;

	systemMgr.log("mount(): entering.");

	bool loaded = false;
	for (auto i = 0; i < 10; i++) {
		if (sync.open("unknown object")) {
			systemMgr.log("mount(): sync load ok.");
			loaded = true;
			break;
		}
		if (systemMgr.sleepFor(1000)) {
			systemMgr.log("mount(): app is closing, stop wait and quit.");
			return;
		}
	}
	if (!loaded) {
		systemMgr.log("mount(): sync load fail, exit.");
		return;
	}

	bool success = false;
	for (auto i = 0; i < 60; i++) {
		DWORD state = sync.get_state();

		if (appliedCount.load() != state) {
			systemMgr.log(format("mount(): sync state to {}", state));

			appliedCount = state;

			if (state == 5) {
				sync.send_ack();
				systemMgr.log("mount(): sync success");
				success = true;
				break;
			}
		}

		if (systemMgr.sleepFor(1000)) {
			systemMgr.log("mount(): app is closing, stop wait and quit.");
			return;
		}
	}

	if (!success) {
		systemMgr.log("mount(): sync load state timeout...");
	}

	mountPid = threadMgr.getTargetPid();
	systemMgr.log("mount(): all operation complete.");

	systemMgr.log("mount(): fall in wait loop.");

	while (mountEnabled) {

		auto pid = threadMgr.getTargetPid();

		if (pid == 0 /* target no more exists */ || pid != mountPid /* target is not current */) {
			mountPid = 0;
			break;
		}

		if (systemMgr.sleepFor(5000)) {
			systemMgr.log("mount(): app is closing, exit and release lock.");
			break;
		}
	}

	systemMgr.log("mount(): leave.");
}


bool ProxyManager::checkFileInstalled() {
	return _fileExists(sysDllPath);
}

bool ProxyManager::checkFileHashMatch() {
	UCHAR hash[32]{};
	if (!_getDllFileHash(hash)) {
		return false;
	}
	return memcmp(hash, kVsocksDllSha256, 32) == 0;
}

bool ProxyManager::checkProxyLoaded() {
	return check() != 0;
}


bool ProxyManager::uninstallProxy() {

	if (Uninstall()) {
		const char* msg = "proxy(): unload success.";
		systemMgr.log(msg);
		return true;
	}
	else {
		const char* msg = "proxy(): unloaded already.";
		systemMgr.log(GetLastError(), msg);

		return false;
	}

}

bool ProxyManager::removeFileOnReboot() {

	// delete file while reboot.
	// //DANGER//: only delete when user try to un-select.
	MoveFileEx(sysDllPath, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
	return true;
}


void ProxyManager::enable() {
	mountEnabled = true;
}

void ProxyManager::disable() {
	mountEnabled = false;
}


result_t ProxyManager::_installDllFile(bool& needReboot) {

	// 获取当前目录和profile目录下的dll路径 curPath profilePath
	wchar_t curPath[MAX_PATH];
	GetModuleFileName(NULL, curPath, MAX_PATH);
	if (PathRemoveFileSpec(curPath)) {
		PathAppend(curPath, L"\\vsocks.dll");
	}
	else {
		return unexpected_error(__FUNCTION__ "(): 获取当前目录失败。", GetLastError());
	}

	wchar_t profilePath[MAX_PATH];
	ExpandEnvironmentStrings(L"%appdata%\\Hutao\\vsocks.dll", profilePath, MAX_PATH);

	// 如果当前目录和profile目录都没有dll，检查系统目录，如果有dll，直接复用，不再拷贝；没有则报错
	if (!_fileExists(curPath) && !_fileExists(profilePath)) {
		if (!_fileExists(sysDllPath)) {
			return unexpected_error(__FUNCTION__ "(): 没有找到vsocks.dll，建议重新解压再运行（不要在压缩包里点开）", ERROR_FILE_NOT_FOUND);
		}
		return true;
	}

	// 尝试拷贝当前目录dll到profile目录
	if (_fileExists(curPath)) {
		if (!MoveFileEx(curPath, profilePath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
			return unexpected_error(__FUNCTION__ "(): MoveFileW失败：无法拷贝vsocks.dll，请检查是否被杀毒拦截。", GetLastError());
		}
	}

	// 尝试拷贝profile目录dll到系统目录
	if (_fileExists(profilePath)) {

		// 如果系统目录没dll 或者系统目录的dll内容不一致 则尝试拷贝 拷贝成功就继续 失败则报错并提示用户重启
		if (!_fileExists(sysDllPath) || !_filesContentEqual(profilePath, sysDllPath)) {
			if (!CopyFile(profilePath, sysDllPath, FALSE)) {
				const DWORD err = GetLastError();
				if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED) {
					needReboot = true;
					return unexpected_error(__FUNCTION__ "(): 当前已安装了其他版本的透明代理，需要重启电脑来卸载。", err);
				}
				else {
					return unexpected_error(__FUNCTION__ "(): 无法将透明代理安装到系统目录。请检查磁盘空间、管理员权限或是否被杀毒拦截。", err);
				}
			}
		}
	}

	return true;
}

bool ProxyManager::_fileExists(const wchar_t* path) {
	return GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES;
}

bool ProxyManager::_filesContentEqual(const wchar_t* path1, const wchar_t* path2) {

	wil::unique_handle f1{ CreateFile(path1, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
	if (!f1) {
		return false;
	}

	wil::unique_handle f2{ CreateFile(path2, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
	if (!f2) {
		return false;
	}

	LARGE_INTEGER size1{};
	LARGE_INTEGER size2{};
	if (!GetFileSizeEx(f1.get(), &size1) || !GetFileSizeEx(f2.get(), &size2)) {
		return false;
	}

	if (size1.QuadPart != size2.QuadPart) {
		return false;
	}

	constexpr DWORD bufSize = 8192;
	BYTE            buf1[bufSize];
	BYTE            buf2[bufSize];
	auto            remaining = size1.QuadPart;

	while (remaining > 0) {
		const DWORD toRead = remaining > bufSize ? bufSize : static_cast<DWORD>(remaining);
		DWORD       read1  = 0;
		DWORD       read2  = 0;

		if (!ReadFile(f1.get(), buf1, toRead, &read1, nullptr) ||
			!ReadFile(f2.get(), buf2, toRead, &read2, nullptr) ||
			read1 != read2 ||
			memcmp(buf1, buf2, read1) != 0) {
			return false;
		}

		remaining -= read1;
	}

	return true;
}

result_t ProxyManager::_hashDllFile(bool& needReboot) {

	UCHAR hash[32]{};
	auto result = _getDllFileHash(hash);
	if (!result) {
		return result;
	}

	if (memcmp(hash, kVsocksDllSha256, 32) != 0) {
		if (checkProxyLoaded()) {
			needReboot = true;
		}
		return unexpected_error(__FUNCTION__ "(): 当前安装的透明代理版本与当前限制器版本不匹配，建议重新下载解压再运行（不要在压缩包里点开）。", 0);
	}

	return true;
}

result_t ProxyManager::_getDllFileHash(UCHAR hash[32]) {

	BCRYPT_ALG_HANDLE hAlg = nullptr;
	BCRYPT_HASH_HANDLE hHash = nullptr;

	auto cleanup = [&] {
		if (hHash) {
			BCryptDestroyHash(hHash);
		}
		if (hAlg) {
			BCryptCloseAlgorithmProvider(hAlg, 0);
		}
	};

	NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
	if (!BCRYPT_SUCCESS(status)) {
		return unexpected_error(__FUNCTION__ "(): BCryptOpenAlgorithmProvider 失败。", status);
	}

	status = BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0);
	if (!BCRYPT_SUCCESS(status)) {
		cleanup();
		return unexpected_error(__FUNCTION__ "(): BCryptCreateHash 失败。", status);
	}

	HANDLE hFileRaw = CreateFile(sysDllPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFileRaw == INVALID_HANDLE_VALUE) {
		cleanup();
		return unexpected_error(__FUNCTION__ "(): 打开文件失败。", GetLastError());
	}
	wil::unique_handle hFile{ hFileRaw };

	BYTE buffer[8192];
	DWORD bytesRead = 0;
	for (;;) {
		if (!ReadFile(hFile.get(), buffer, sizeof(buffer), &bytesRead, nullptr)) {
			cleanup();
			return unexpected_error(__FUNCTION__ "(): 读取文件失败。", GetLastError());
		}
		if (bytesRead == 0) {
			break;
		}

		status = BCryptHashData(hHash, buffer, bytesRead, 0);
		if (!BCRYPT_SUCCESS(status)) {
			cleanup();
			return unexpected_error(__FUNCTION__ "(): BCryptHashData 失败。", status);
		}
	}

	status = BCryptFinishHash(hHash, hash, 32, 0);
	cleanup();
	if (!BCRYPT_SUCCESS(status)) {
		return unexpected_error(__FUNCTION__ "(): BCryptFinishHash 失败。", status);
	}

	return true;
}

result_t ProxyManager::_cancelPendingDelete() {
	try {
		const HKEY hKeyRoot = HKEY_LOCAL_MACHINE;
		const wchar_t* subKey = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager";
		const wchar_t* valueName = L"PendingFileRenameOperations";

		// read registry
		auto readResult = wil::reg::try_get_value_multistring(hKeyRoot, subKey, valueName);

		// registry key not found / value is empty
		if (!readResult.has_value() || readResult.value().empty()) {
			return true;
		}

		const auto& regValues = readResult.value();

		// key check
		if (regValues.size() % 2 != 0) {
			return unexpected_error(format(__FUNCTION__ "(): 注册表项异常：PendingFileRenameOperations，长度是{}", regValues.size()), 0);
		}

		// filter targetFilePath
		std::vector<std::wstring> newValues;
		newValues.reserve(regValues.size());
		bool found = false;

		for (size_t i = 0; i < regValues.size(); i += 2) {
			std::wstring_view srcPath = regValues[i];
			std::wstring_view dstPath = regValues[i + 1]; // if delete, dstPath == L"\0"

			bool isTarget = false;
			auto prefixPos = srcPath.find(L"\\??\\");  // match before L"\??\"

			if (prefixPos != std::wstring_view::npos) {
				const wchar_t* pathStart = srcPath.data() + prefixPos + 4;
				if (_wcsicmp(pathStart, sysDllPath) == 0) {
					isTarget = true;
				}
			}

			if (isTarget) {
				found = true;  // drop
			}
			else {
				newValues.emplace_back(srcPath);
				newValues.emplace_back(dstPath);
			}
		}

		if (found) {
			if (newValues.empty()) {
				// empty, delete item
				LSTATUS status = RegDeleteKeyValue(hKeyRoot, subKey, valueName);
				if (status != ERROR_SUCCESS) {
					return unexpected_error("写入注册表项失败：PendingFileRenameOperations", status);
				}
			}
			else {
				// write back to registry
				wil::reg::set_value_multistring(hKeyRoot, subKey, valueName, newValues);
			}
		}

		return true;
	}
	catch (const wil::ResultException& e) {
		return unexpected_error(format(__FUNCTION__ "(): 注册表操作失败：{}", e.what()), e.GetErrorCode());
	}
}
