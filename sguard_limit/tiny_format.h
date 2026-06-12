// tiny fmt::format (ansi & utf8 compatible only)
#pragma once
#include <string>
#include <cstdio>
#include <cstring>


namespace tiny {

    struct FmtSpec {
        char buf[64];

        FmtSpec(std::string_view spec, std::string_view def) {
            buf[0] = '%';
            std::string_view s = spec.empty() ? def : spec;
            size_t n = s.size() <= 30 ? s.size() : 30;
            std::memcpy(buf + 1, s.data(), n);
            buf[n + 1] = '\0';
        }

        // used in append_to
        operator const char* () const { return buf; }
    };

    // append_to: append directly, avoid temp string
    inline void append_to(std::string& out, int x, std::string_view spec = {}) {
        char buf[64];
        int len = std::snprintf(buf, sizeof(buf), FmtSpec(spec, "d"), x);
        out.append(buf, len);
    }
    inline void append_to(std::string& out, unsigned int x, std::string_view spec = {}) {
        char buf[64];
        int len = std::snprintf(buf, sizeof(buf), FmtSpec(spec, "u"), x);
        out.append(buf, len);
    }
    inline void append_to(std::string& out, long x, std::string_view spec = {}) {
        char buf[64];
        int len = std::snprintf(buf, sizeof(buf), FmtSpec(spec, "ld"), x);
        out.append(buf, len);
    }
    inline void append_to(std::string& out, unsigned long x, std::string_view spec = {}) {
        char buf[64];
        int len = std::snprintf(buf, sizeof(buf), FmtSpec(spec, "lu"), x);
        out.append(buf, len);
    }
    inline void append_to(std::string& out, long long x, std::string_view spec = {}) {
        char buf[64];
        int len = std::snprintf(buf, sizeof(buf), FmtSpec(spec, "lld"), x);
        out.append(buf, len);
    }
    inline void append_to(std::string& out, unsigned long long x, std::string_view spec = {}) {
        char buf[64];
        int len = std::snprintf(buf, sizeof(buf), FmtSpec(spec, "llu"), x);
        out.append(buf, len);
    }
    inline void append_to(std::string& out, float x, std::string_view spec = {}) {
        char buf[64];
        int len = std::snprintf(buf, sizeof(buf), FmtSpec(spec, ".2f"), x);
        out.append(buf, len);
    }
    inline void append_to(std::string& out, double x, std::string_view spec = {}) {
        char buf[64];
        int len = std::snprintf(buf, sizeof(buf), FmtSpec(spec, ".2f"), x);
        out.append(buf, len);
    }
    inline void append_to(std::string& out, bool x, std::string_view spec = {}) {
        out.append(x ? "true" : "false");
    }

    inline void append_to(std::string& out, const char* x, std::string_view spec = {}) {
        out.append(x ? x : "(null)");
    }
    inline void append_to(std::string& out, const std::string& x, std::string_view spec = {}) {
        out.append(x);
    }
    inline void append_to(std::string& out, std::string_view x, std::string_view spec = {}) {
        out.append(x);
    }


    // type erase
    using Appender = void(*)(std::string&, const void*, std::string_view);

    // type recover:
    // data pointer + converter pointer -> type
    template <typename T>
    void type_erased_append(std::string& out, const void* ptr, std::string_view spec) {
        append_to(out, *static_cast<const T*>(ptr), spec);
    }

    // core logic: only one copy in binary
    // inline: C++17 odr, auto optimize for one copy
    inline std::string format_impl(const char* fmt, const void* const* arg_ptrs, Appender const* funcs, size_t arg_count) {

        std::string result;
        result.reserve(128);

        size_t len = std::strlen(fmt);
        const char* start = fmt;
        const char* end = fmt + len;

        size_t arg_index = 0;

        while (start < end) {
            // find format
            const char* p = start;
            while (p < end) {
                if ((*p == '{' && p + 1 < end && p[1] == '{') ||
                    (*p == '}' && p + 1 < end && p[1] == '}')) {
                    result.append(start, p);
                    result += *p;
                    start = p + 2;
                    p = start;
                }
                if (*p == '{') {
                    break;
                }
                p++;
            }

            // copy string before format to result
            result.append(start, p);
            if (p >= end) break;

            // now: p -> '{'
            const char* brace = p;
            const char* close_brace = nullptr;
            std::string_view spec;

            if (brace + 1 < end && brace[1] == '}') { // {}
                close_brace = brace + 1;
            }
            else if (brace + 1 < end && brace[1] == ':') { // {:spec}
                const char* q = brace + 2;
                while (q < end && *q != '}') q++;  // try find '}'
                if (q < end) { // found '}', take spec
                    close_brace = q;
                    spec = std::string_view(brace + 2, q - (brace + 2));
                }
                else {  // fallback, treat '{' as normal char
                    result += '{';
                    start = brace + 1;
                    continue;
                }
            }
            else {  // fallback, treat '{' as normal char
                result += '{';
                start = brace + 1;
                continue;
            }

            // call formatter
            if (arg_index < arg_count) {
                funcs[arg_index](result, arg_ptrs[arg_index], spec);
            }
            else {
                result += "{?}";
            }
            arg_index++;

            start = close_brace + 1;
        }
        return result;
    }


    // tiny fmt::format frontend
    // note: std::string_view fmt may expand binary size ~2KB
    template<typename... Args>
    std::string format(const char* fmt, Args&&... args) {

        // compile-time zero arg opt
        if constexpr (sizeof...(Args) == 0) {
            return fmt;
        }

        // type erase: one function table for each set of args
        // tradeoff: size expansion of data section(.rdata) is less than code section(.text),
        //           and data section does not need to execute.
        static constexpr Appender funcs[] = { type_erased_append<std::remove_reference_t<Args>>... };

        const void* ptrs[] = { &args... };
        return format_impl(fmt, ptrs, funcs, sizeof...(Args));
    }
}
