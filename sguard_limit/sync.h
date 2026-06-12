#pragma once
#include <Windows.h>
#include "string_conv.h"

class SimpleStateSync {
private:
    HANDLE hMapFile = nullptr;
    DWORD* pState = nullptr;

public:
    bool create(const char* name) {
        hMapFile = CreateFileMapping(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            sizeof(DWORD),
            Utf8ToWide(name)
        );
        if (!hMapFile) return false;

        pState = (DWORD*)MapViewOfFile(
            hMapFile,
            FILE_MAP_ALL_ACCESS,
            0, 0,
            sizeof(DWORD)
        );
        if (!pState) {
            CloseHandle(hMapFile);
            return false;
        }

        *pState = 0;
        return true;
    }

    bool open(const char* name) {
        hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, Utf8ToWide(name));
        if (!hMapFile) return false;

        pState = (DWORD*)MapViewOfFile(
            hMapFile,
            FILE_MAP_ALL_ACCESS,
            0, 0,
            sizeof(DWORD)
        );
        if (!pState) {
            CloseHandle(hMapFile);
            return false;
        }

        return true;
    }

    void set_state(DWORD state) {
        if (pState) {
            InterlockedExchange((LONG*)pState, state);
        }
    }

    DWORD get_state() {
        if (!pState) return 0;
        return InterlockedCompareExchange((LONG*)pState, 0, 0);
    }

    void send_ack() {
        if (pState) {
            InterlockedExchange((LONG*)pState, 100);
        }
    }

    bool wait_for_ack() {
        if (!pState) return false;

        for (auto i = 0; i < 60; i++) {
            DWORD value = InterlockedCompareExchange((LONG*)pState, 0, 0);
            if (value == 100) {
                return true;
            }
            Sleep(1000);
        }

        return false;
    }

    ~SimpleStateSync() {
        if (pState) UnmapViewOfFile(pState);
        if (hMapFile) CloseHandle(hMapFile);
    }
};