// x64 SGUARD限制器，适用于各种腾讯游戏
// H3d9, 写于2021.2.5晚。
#include <Windows.h>
#include <thread>
#include <atomic>
#include "resource.h"
#include "wndproc.h"
#include "win32utility.h"
#include "config.h"
#include "kdriver.h"
#include "limitcore.h"
#include "mempatch.h"
#include "transproxy.h"


KernelDriver&           driver                  = KernelDriver::getInstance();
win32SystemManager&     systemMgr               = win32SystemManager::getInstance();
ConfigManager&          configMgr               = ConfigManager::getInstance();
LimitManager&           limitMgr                = LimitManager::getInstance();
PatchManager&           patchMgr                = PatchManager::getInstance();
ProxyManager&           proxyMgr                = ProxyManager::getInstance();

std::atomic<bool>       g_HijackThreadWaiting   = true;


namespace {

void HijackThreadWorker() {
	
	systemMgr.log("hijack thread: created.");

	win32ThreadManager threadMgr;

	while (1) {

		// scan per 5 seconds when idle;
		// if process is found, trap into usr-selected mode.
		if (threadMgr.getTargetPid()) {

			systemMgr.log("hijack thread: pid found.");

			// launch clean thread to kill GameLoader at appropriate time.
			if (systemMgr.killAceLoader) {
				systemMgr.raiseCleanThread();
			}

			// select mode.
			if (systemMgr.mode == 0 && limitMgr.limitEnabled) {
				g_HijackThreadWaiting = false;
				limitMgr.hijack();
				g_HijackThreadWaiting = true;
			}
			if (systemMgr.mode == 2 && patchMgr.patchEnabled) {
				g_HijackThreadWaiting = false;
				patchMgr.patch();
				g_HijackThreadWaiting = true;
			}
			if (systemMgr.mode == 3 && proxyMgr.mountEnabled) {
				g_HijackThreadWaiting = false;
				proxyMgr.mount();
				g_HijackThreadWaiting = true;
			}
		}

		g_HijackThreadWaiting.notify_all();   // inform main thread for blocked actions.

		if (systemMgr.sleepFor(5000)) {       // wait 5s before next loop.
			break;                            // if stop signal triggers, exit imm.
		}
	}

	systemMgr.log("hijack thread: exit.");
}

bool AutoLoadDriverOrProxy() {

	auto setModeToProxy = [] {
		systemMgr.mode = 3;
		systemMgr.writeConfig();
	};
	
	auto setModeToPatch = [] {
		systemMgr.mode = 2;
		systemMgr.writeConfig();
	};

	auto onDriverLoadable = [&] {
		setModeToPatch();
		if (proxyMgr.checkProxyLoaded() && !proxyMgr.checkFileInstalled()) {
			proxyMgr.uninstallProxy();
			systemMgr.log("orphan proxy without dll, cleaned");

		}
		else if (proxyMgr.checkFileInstalled() && proxyMgr.checkProxyLoaded()) {
			// silent if only file exists.
			proxyMgr.uninstallProxy();
			systemMgr.log("user installed proxy, but not necessary, uninstalled.");
			systemMgr.messageBox("你安装了透明代理，但现在无需开启此功能，已经自动关闭。\n"
				"你可以随时在右下角托盘菜单中选择“配置透明代理”来卸载它。（需重启电脑以完全卸载）");
		}
	};

	auto promptEnableProxy = [&]() -> bool {
		if (proxyMgr.checkFileInstalled() && proxyMgr.checkProxyLoaded()) {
			systemMgr.log("user already installed proxy, continue");
			setModeToProxy();
			return true;
		}
		if (systemMgr.messageBoxYesNo("驱动加载失败，需要启用透明代理才可以正常使用。你要启用该功能吗？\n\n"
		                              "【注意】透明代理和一些外服反作弊冲突（如吃鸡、apex等），玩这些游戏的时候关掉限制器或者卸载透明代理即可！")) {
			systemMgr.log("user selected to install proxy");
			setModeToProxy();
			return true;

		} else {
			systemMgr.log("user selected not install proxy, quit.");
			return false;
		}
	};

	// if not first run: check mode==3 (proxy).
	// > if mode==3: do not try to load driver. load proxy only
	if (!systemMgr.isFirstRun && systemMgr.mode == 3) {
		return proxyMgr.load();
	}

	// if first run OR mode==2: try load driver.
	// > if is loadable: disable proxy, continue;
	// > if is not loadable: notice user to select if we can roll back to proxy.
	//   > if user select use proxy: record to config (mode=3), do not try to load driver anymore.
	//   > if user select not use: quit program.
	if (driver.checkLoadable()) {
		onDriverLoadable();
	} else {
		if (!promptEnableProxy()) {
			return false;
		}
		if (!proxyMgr.load()) {
			return false;
		}
	}

	return true;
}

}

INT WINAPI wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd) {


	// program initialize:
	// 1. acquire uac && raise privilege
	// 2. init config manager (used by system manager and others)
	// 3. init system manager (check sington, get path and os version, init log subsystem)
	// 4. init worker managers
	// 5. init win32 gui (create wndproc and tray) and launch worker thread

#ifndef _DEBUG

	// [prerequisite for raise privilege] acquire uac manually:
	// if not do this (but acquire in manifest), win10/11 with uac will refuse auto start-up.
	// return true if already in admin; false if not (will get admin with a restart).

	if (!systemMgr.runWithUac()) {
		return -1;
	}

#endif

	if (!systemMgr.enableDebugPrivilege()) {
		return -1;
	}

	// initialize config manager:
	// load/writeConfig wrapped in specific modules; fields r/w by module itself.

	configMgr.init();


	// initialize system manager:
	// module configs are loaded / inited by module initializer.
	// init system manager first (workers depends on it), and show update window if first run.

	if (!systemMgr.systemInit(hInstance)) {
		return -1;
	}

	if (systemMgr.isFirstRun) {
		systemMgr.messageBox(
			"【更新说明】\n\n"
			" 内存补丁 " MEMPATCH_VERSION "：\n\n"
			"1. 驱动已兼容至Win11 26H1的系统内核 (Build 28020.2207)。\n"
			"2. 修复不兼容中文系统用户名的问题。\n"
			"3. 自动适配安全启动问题和其他优化。\n\n\n"

			"【重要提示】\n\n"
			"1. 关注【B站】@H3d9 防迷路，有问题会在动态更新。\n\n"
			"2. 本工具是免费软件，任何出售本工具的人都是骗子哦！\n\n"
			"3. 使用遇到问题时，请先仔细阅读附带的“常见问题（必看）”，\n"
			"   如果看了仍未解决你的问题，可以加群反馈：851512842，或者B站私信@H3d9。\n",  // 775176979 :(
			VERSION "  by @H3d9");
	}


	// initialize all worker managers.
	// this will load or init module configs.

	driver.init();

	limitMgr.init();

	patchMgr.init();

	proxyMgr.init();


	// decide using driver or proxy. graph:
	// if first run: try load driver.
	// > if is loadable: disable proxy, continue;
	// > if is not loadable: notice user to select if we can roll back to proxy.
	//   > if user select use proxy: record to config (mode=3), do not try to load driver anymore.
	//   > if user select not use proxy: quit program.
	// if not first run: check mode.
	// > if mode==3: do not try to load driver. load proxy only
	// > else: same as not loadable, notice user to select.

	if (!AutoLoadDriverOrProxy()) {
		return -1;
	}


	// create wndproc and tray:
	// create after all module init complete (after state loaded).

	if (!systemMgr.createWindow(WndProc, IDI_ICON1)) {
		return -1;
	}

	systemMgr.createTray(WM_TRAYACTIVATE);


	// create working thread:
	// using std::thread (_beginthreadex) is more safe than winapi CreateThread;
	// because we use heap and crt functions in working thread.

	std::thread hijackThread(HijackThreadWorker);


	// create cloud thread:
	// acquire data from cloud, incluing updates etc.
	// network connection is async here; notify after fetch completes.

	systemMgr.spawnCloudThread();


	// enter primary msg loop:
	// main thread will wait for window msgs from user, while working thread do actual works.

	auto ret =
	systemMgr.messageLoop();


	// program exit:
	// stop hijack worker before static singletons are destructed.

	hijackThread.join();

	systemMgr.joinBackgroundThreads();

	if (proxyMgr.checkProxyLoaded()) {
		proxyMgr.uninstallProxy();
	}

	systemMgr.removeTray();

	return (INT) ret;
}