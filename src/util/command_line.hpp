#pragma once

#include <shellapi.h>

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace big::command_line {
    namespace detail {
        using parsed_arguments = std::unordered_map<std::wstring, std::optional<std::wstring> >;

        inline std::wstring lowercase(std::wstring_view value) {
            std::wstring result(value);
            for (auto &character: result) {
                character = static_cast<wchar_t>(std::towlower(character));
            }
            return result;
        }

        inline bool is_value_token(std::wstring_view value) {
            return value.empty() || value.front() != L'-' || (value.size() > 1 && (std::iswdigit(value[1]) || value[1] == L'.'));
        }

        inline const parsed_arguments &arguments() {
            static const auto parsed = [] {
                parsed_arguments result;
                int argc = 0;

                if (const auto command_line = GetCommandLineW(); command_line) {
                    if (auto argv = CommandLineToArgvW(command_line, &argc)) {
                        for (int i = 1; i < argc; ++i) {
                            const std::wstring_view argument(argv[i]);
                            if (argument.size() < 2 || argument.front() != L'-')
                                continue;

                            const auto separator = argument.find(L'=');
                            const auto name = argument.substr(0, separator);
                            std::optional<std::wstring> value;

                            if (separator != std::wstring_view::npos) {
                                value = std::wstring(argument.substr(separator + 1));
                            } else if (i + 1 < argc && is_value_token(argv[i + 1])) {
                                value = std::wstring(argv[++i]);
                            }

                            result[lowercase(name)] = std::move(value);
                        }

                        LocalFree(argv);
                    }
                }

                return result;
            }();

            return parsed;
        }

        inline const std::optional<std::wstring> *find(std::wstring_view name) {
            const auto &parsed = arguments();
            const auto it = parsed.find(lowercase(name));
            return it == parsed.end() ? nullptr : &it->second;
        }
    }

    inline bool get(std::wstring_view name, bool default_value) {
        const auto *value = detail::find(name);
        if (!value || !value->has_value()) {
            return value ? true : default_value;
        }

        const auto normalized = detail::lowercase(value->value());
        if (normalized == L"1" || normalized == L"true") {
            return true;
        }
        if (normalized == L"0" || normalized == L"false") {
            return false;
        }

        return default_value;
    }

    inline int get(std::wstring_view name, int default_value) {
        const auto *value = detail::find(name);
        if (!value || !value->has_value()) {
            return default_value;
        }

        const auto *begin = value->value().c_str();
        wchar_t *end = nullptr;
        errno = 0;
        const auto parsed = std::wcstol(begin, &end, 10);
        if (errno == ERANGE || end == begin || *end != L'\0' || parsed < INT_MIN || parsed > INT_MAX) {
            return default_value;
        }
        return static_cast<int>(parsed);
    }

    inline float get(std::wstring_view name, float default_value) {
        const auto *value = detail::find(name);
        if (!value || !value->has_value()) {
            return default_value;
        }

        const auto *begin = value->value().c_str();
        wchar_t *end = nullptr;
        errno = 0;
        const auto parsed = std::wcstof(begin, &end);
        if (errno == ERANGE || end == begin || *end != L'\0' || !std::isfinite(parsed)) {
            return default_value;
        }

        return parsed;
    }
}
