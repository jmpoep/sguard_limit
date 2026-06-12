
#include "util.h"

// util
util::util()
	: logfp(NULL) {
}

util::~util() {
	if (logfp) {
		fclose(logfp);
	}
}

util& util::getInstance() {
	static util u;
	return u;
}

void util::init() {

}


std::string_view util::get_pure_file_name(std::string_view full_name) {  // todo consteval
	if (auto pos = full_name.rfind('\\'); pos != std::string_view::npos) {
		full_name = full_name.substr(pos + 1);
	}
	return full_name;
}

std::string_view util::get_pure_function_name(std::string_view full_name) {
	if (auto pos = full_name.find("::"); pos != std::string_view::npos) {
		full_name = full_name.substr(pos + 2);  // try remove class name
	}
	else if (auto pos = full_name.find("__cdecl"); pos != std::string_view::npos) {
		full_name = full_name.substr(pos + 8);  // if no class name, try from call convention
	}
	else if (auto pos = full_name.find("__stdcall"); pos != std::string_view::npos) {
		full_name = full_name.substr(pos + 10);
	}
	else if (auto pos = full_name.find("__fastcall"); pos != std::string_view::npos) {
		full_name = full_name.substr(pos + 11);
	}
	if (auto pos = full_name.find('('); pos != std::string_view::npos) {
		full_name = full_name.substr(0, pos);
	}
	return full_name;
}
