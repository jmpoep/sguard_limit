#pragma once

#include <string>
#include <source_location>
#include <fmt/core.h> // msvc std::format generates large file size
#include <fmt/compile.h>


class util {

private:
	util();
	~util();
	util(const util&)              = delete;
	util(util&&)                   = delete;
	util& operator= (const util&)  = delete;
	util& operator= (util&&)       = delete;

public:
	static util& getInstance();

public:
	void init();

public:
	struct FormatWithLocation {
		const char* value;
		std::source_location loc;

		consteval FormatWithLocation(const char* s,
			const std::source_location& l = std::source_location::current())
			: value(s), loc(l) {
		}
	};

	template <typename... Args>
	__forceinline void log(FormatWithLocation format, Args&&... args) {
		consteval auto file_name = get_pure_file_name(format.loc.file_name());
		consteval auto func_name = get_pure_function_name(format.loc.function_name());  // function_name: utf8
		consteval auto fmt_str = fmt::format(FMT_COMPILE("[{}:{}] {}(): {}"), file_name, format.loc.line(), func_name, format.value);
		const auto str = fmt::format(fmt::runtime(fmt_str), std::forward<Args>(args)...);
		//std::cout << str << std::endl;
	}

private:
	std::string_view get_pure_file_name(std::string_view full_name);
	std::string_view get_pure_function_name(std::string_view full_name);

private:
	FILE* logfp;
};
