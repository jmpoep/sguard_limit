#pragma once
#include <Windows.h>
#include <string>
#include <string_view>


// UTF-8 <-> UTF-16 conversion wrappers for Win32 Unicode APIs.
// Usage:
//   MessageBoxW(hwnd, Utf8ToWide(msg), Utf8ToWide(title), MB_OK);
//   std::wstring path = Utf8ToWide(utf8Path);
//   std::string  text = WideToUtf8(widePath);

class Utf8ToWide {

	std::wstring wide_;

public:
	explicit Utf8ToWide(std::string_view utf8) {

		if (utf8.empty()) {
			return;
		}

		const int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
		if (wideLen <= 0) {
			return;
		}

		wide_.assign(static_cast<size_t>(wideLen), L'\0');
		if (MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide_.data(), wideLen) <= 0) {
			wide_.clear();
		}
	}

	operator std::wstring() const {
		return wide_;
	}

	// no explicit: enable temporary usage for unicode winapi.
	operator const wchar_t*() const {
		return wide_.c_str();
	}
};


class WideToUtf8 {

	std::string utf8_;

public:
	explicit WideToUtf8(std::wstring_view wide) {

		if (wide.empty()) {
			return;
		}

		const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
		if (utf8Len <= 0) {
			return;
		}

		utf8_.assign(static_cast<size_t>(utf8Len), '\0');
		if (WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8_.data(), utf8Len, nullptr, nullptr) <= 0) {
			utf8_.clear();
		}
	}

	operator std::string() const {
		return utf8_;
	}

	explicit operator const char*() const {
		return utf8_.c_str();
	}
};
