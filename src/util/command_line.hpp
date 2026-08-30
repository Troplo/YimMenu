#pragma once

#include <shellapi.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace big::command_line
{
	inline const std::vector<std::wstring>& arguments()
	{
		static const auto parsed_arguments = [] {
			std::vector<std::wstring> result;
			int argc = 0;

			if (const auto command_line = GetCommandLineW(); command_line)
			{
				if (auto argv = CommandLineToArgvW(command_line, &argc))
				{
					result.reserve(static_cast<std::size_t>(argc));
					for (int i = 0; i < argc; ++i)
						result.emplace_back(argv[i]);

					LocalFree(argv);
				}
			}

			return result;
		}();

		return parsed_arguments;
	}

	inline bool has_argument(const std::wstring_view argument)
	{
		const auto& args = arguments();
		return std::ranges::any_of(args, [argument](const std::wstring& current_argument) {
			return std::wstring_view(current_argument) == argument;
		});
	}

	inline bool is_pvp_patch_enabled()
	{
		static const bool enabled = has_argument(L"-pvpPatch");
		return enabled;
	}
}
