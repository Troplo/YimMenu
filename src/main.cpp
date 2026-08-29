#include "backend/backend.hpp"
#include "byte_patch_manager.hpp"
#include "common.hpp"
#include "features.h"
#include "fiber_pool.hpp"
#include "gui.hpp"
#include "hooking/hooking.hpp"
#include "hooks/Anticheat/UnpackHandler.hpp"
#include "hooks/Anticheat/VehPackHandler.hpp"
#include "http_client/http_client.hpp"
#include "logger/exception_handler.hpp"
#include "lua/lua_manager.hpp"
#include "native_hooks/native_hooks.hpp"
#include "natives/native_registration.hpp"
#include "pointers.hpp"
#include "renderer/renderer.hpp"
#include "script_mgr.hpp"
#include "services/api/api_service.hpp"
#include "services/context_menu/context_menu_service.hpp"
#include "services/custom_text/custom_text_service.hpp"
#include "services/gta_data/gta_data_service.hpp"
#include "services/gui/gui_service.hpp"
#include "services/hotkey/hotkey_service.hpp"
#include "services/matchmaking/matchmaking_service.hpp"
#include "services/mobile/mobile_service.hpp"
#include "services/model_preview/model_preview_service.hpp"
#include "services/notifications/notification_service.hpp"
#include "services/paragon/rgsc/RgscRegistration.hpp"
#include "services/pickups/pickup_service.hpp"
#include "services/player_database/player_database_service.hpp"
#include "services/players/player_service.hpp"
#include "services/script_connection/script_connection_service.hpp"
#include "services/script_function_hook/script_function_hook_service.hpp"
#include "services/script_patcher/script_patcher_service.hpp"
#include "services/tunables/tunables_service.hpp"
#include "services/vehicle/handling_service.hpp"
#include "services/vehicle/xml_vehicles_service.hpp"
#include "services/xml_maps/xml_map_service.hpp"
#include "thread_pool.hpp"
#include "util/is_proton.hpp"
#include "version.hpp"

#include <Psapi.h>
#include <tlhelp32.h>

namespace big
{
	std::string ReadRegistryKeySZ(HKEY hKeyParent, std::string subkey, std::string valueName)
	{
		HKEY hKey;
		char value[1024];
		DWORD value_length = 1024;
		LONG ret           = RegOpenKeyEx(hKeyParent, subkey.c_str(), 0, KEY_READ, &hKey);
		if (ret != ERROR_SUCCESS)
		{
			LOG(INFO) << "Unable to read registry key " << subkey;
			return "";
		}
		ret = RegQueryValueEx(hKey, valueName.c_str(), NULL, NULL, (LPBYTE)&value, &value_length);
		RegCloseKey(hKey);
		if (ret != ERROR_SUCCESS)
		{
			LOG(INFO) << "Unable to read registry key " << valueName;
			return "";
		}
		return std::string(value);
	}

	DWORD ReadRegistryKeyDWORD(HKEY hKeyParent, std::string subkey, std::string valueName)
	{
		HKEY hKey;
		DWORD value;
		DWORD value_length = sizeof(DWORD);
		LONG ret           = RegOpenKeyEx(hKeyParent, subkey.c_str(), 0, KEY_READ, &hKey);
		if (ret != ERROR_SUCCESS)
		{
			LOG(INFO) << "Unable to read registry key " << subkey;
			return NULL;
		}
		ret = RegQueryValueEx(hKey, valueName.c_str(), NULL, NULL, (LPBYTE)&value, &value_length);
		RegCloseKey(hKey);
		if (ret != ERROR_SUCCESS)
		{
			LOG(INFO) << "Unable to read registry key " << valueName;
			return NULL;
		}
		return value;
	}

	std::unique_ptr<char[]> GetWindowsVersion()
	{
		typedef LPWSTR(WINAPI * BFS)(LPCWSTR);
		LPWSTR UTF16   = BFS(GetProcAddress(LoadLibrary("winbrand.dll"), "BrandingFormatString"))(L"%WINDOWS_LONG%");
		int BufferSize = WideCharToMultiByte(CP_UTF8, 0, UTF16, -1, NULL, 0, NULL, NULL);
		std::unique_ptr<char[]> UTF8(new char[BufferSize]);
		WideCharToMultiByte(CP_UTF8, 0, UTF16, -1, UTF8.get(), BufferSize, NULL, NULL);
		// BrandingFormatString requires a GlobalFree.
		GlobalFree(UTF16);
		return UTF8;
	}

	HMODULE CheckForFSL()
	{
		HMODULE modules[1024];
		DWORD needed;

		if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
		{
			return nullptr;
		}

		size_t count = needed / sizeof(HMODULE);

		for (size_t i = 0; i < count; ++i)
		{
			if (GetProcAddress(modules[i], "LawnchairGetVersion"))
			{
				return modules[i];
			}
		}

		return nullptr;
	}
}

namespace
{
    std::vector<uintptr_t> g_watchedAddresses = {
        0x00000141150540
 ,   	0x000001412D37D1
    	,
    	0x00000001412D37CC,
    	0x1411A1480,
    	0x1411A1488,
    	0x143B1661C,
    	0x142636B50
    };

    struct PageProtectionInfo
    {
        DWORD originalProtect;
        DWORD restrictedProtect;
    };

    std::unordered_map<uintptr_t, PageProtectionInfo> g_pageMap;
    PVOID g_vehHandle = nullptr;
    thread_local uintptr_t t_rearmPage = 0;


    void LogRegister(const char* name, DWORD64 value)
    {
        LOG(VERBOSE)
            << "    "
            << std::left
            << std::setw(8)
            << name
            << " = 0x"
            << std::right
            << std::hex
            << std::setw(16)
            << std::setfill('0')
            << value
            << std::dec
            << std::setfill(' ');
    }

    void LogMemoryBytes(
        uintptr_t address,
        size_t count)
    {
        LOG(VERBOSE)
            << "    0x"
            << std::hex
            << std::setw(16)
            << std::setfill('0')
            << address
            << ": ";

        for (size_t i = 0; i < count; ++i)
        {
            MEMORY_BASIC_INFORMATION mbi{};

            if (VirtualQuery(
                    reinterpret_cast<LPCVOID>(address + i),
                    &mbi,
                    sizeof(mbi)) == 0)
            {
                LOG(VERBOSE) << "?? ";
                continue;
            }

            const DWORD protect = mbi.Protect;

            const bool readable =
                protect == PAGE_READONLY ||
                protect == PAGE_READWRITE ||
                protect == PAGE_WRITECOPY ||
                protect == PAGE_EXECUTE_READ ||
                protect == PAGE_EXECUTE_READWRITE ||
                protect == PAGE_EXECUTE_WRITECOPY;

            if (!readable)
            {
                LOG(VERBOSE) << "?? ";
                continue;
            }

            const auto* ptr =
                reinterpret_cast<const uint8_t*>(address + i);

            LOG(VERBOSE)
                << std::setw(2)
                << static_cast<unsigned>(*ptr)
                << ' ';
        }

        LOG(VERBOSE) << std::dec << "\n";
    }

    bool IsReadable(uintptr_t address, size_t size)
    {
        MEMORY_BASIC_INFORMATION mbi{};

        if (VirtualQuery(
                reinterpret_cast<LPCVOID>(address),
                &mbi,
                sizeof(mbi)) == 0)
        {
            return false;
        }

        if (mbi.State != MEM_COMMIT)
            return false;

        if (mbi.Protect & PAGE_GUARD)
            return false;

        if (mbi.Protect & PAGE_NOACCESS)
            return false;

        const uintptr_t regionEnd =
            reinterpret_cast<uintptr_t>(mbi.BaseAddress) +
            mbi.RegionSize;

        if (address > regionEnd)
            return false;

        return size <= regionEnd - address;
    }

    void LogStackMemory(
        const CONTEXT& context,
        size_t entries)
    {
        const uintptr_t rsp =
            static_cast<uintptr_t>(context.Rsp);

        LOG(VERBOSE)
            << "  Stack memory:\n";

        for (size_t i = 0; i < entries; ++i)
        {
            const uintptr_t address =
                rsp + i * sizeof(uint64_t);

            if (!IsReadable(address, sizeof(uint64_t)))
            {
                LOG(VERBOSE)
                    << "    [RSP + 0x"
                    << std::hex
                    << i * sizeof(uint64_t)
                    << "] <unreadable>\n";

                break;
            }

            const auto value =
                *reinterpret_cast<const uint64_t*>(address);

            LOG(VERBOSE)
                << "    [RSP + 0x"
                << std::hex
                << std::setw(3)
                << std::setfill('0')
                << i * sizeof(uint64_t)
                << "] = 0x"
                << std::setw(16)
                << value
                << std::dec
                << std::setfill(' ')
                << "\n";
        }
    }

    void LogExceptionCode(DWORD code)
    {
        LOG(VERBOSE)
            << "  Exception code: 0x"
            << std::hex
            << std::setw(8)
            << std::setfill('0')
            << code
            << std::dec
            << std::setfill(' ')
            << "\n";
    }
}

void LogExceptionContext(
    PEXCEPTION_POINTERS exceptionInfo)
{
    if (exceptionInfo == nullptr ||
        exceptionInfo->ExceptionRecord == nullptr ||
        exceptionInfo->ContextRecord == nullptr)
    {
        return;
    }

    const auto* record =
        exceptionInfo->ExceptionRecord;

    const auto& ctx =
        *exceptionInfo->ContextRecord;

    LOG(VERBOSE) << "\n";
    LOG(VERBOSE) << "========== Exception Context ==========\n";

    LogExceptionCode(record->ExceptionCode);

    LOG(VERBOSE)
        << "  Exception flags: 0x"
        << std::hex
        << record->ExceptionFlags
        << std::dec
        << "\n";

    LOG(VERBOSE)
        << "  Exception address: 0x"
        << std::hex
        << std::setw(16)
        << std::setfill('0')
        << reinterpret_cast<uintptr_t>(
               record->ExceptionAddress)
        << std::dec
        << std::setfill(' ')
        << "\n";

    LOG(VERBOSE)
        << "  Parameters: "
        << std::dec
        << record->NumberParameters
        << "\n";

    for (DWORD i = 0;
         i < record->NumberParameters;
         ++i)
    {
        LOG(VERBOSE)
            << "    ExceptionInformation["
            << i
            << "] = 0x"
            << std::hex
            << std::setw(16)
            << std::setfill('0')
            << record->ExceptionInformation[i]
            << std::dec
            << std::setfill(' ')
            << "\n";
    }

    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        record->NumberParameters >= 2)
    {
        const auto accessType =
            record->ExceptionInformation[0];

        const auto faultAddress =
            record->ExceptionInformation[1];

        LOG(VERBOSE) << "  Access violation:\n";

        LOG(VERBOSE)
            << "    Operation : "
            << (accessType == 0 ? "READ" :
                accessType == 1 ? "WRITE" :
                accessType == 8 ? "EXECUTE" :
                "UNKNOWN");

        LOG(VERBOSE)
            << "    Address   : 0x"
            << std::hex
            << std::setw(16)
            << std::setfill('0')
            << faultAddress
            << std::dec
            << std::setfill(' ');
    }

    LOG(VERBOSE) << "  Registers: ";

    LogRegister("RAX", ctx.Rax);
    LogRegister("RBX", ctx.Rbx);
    LogRegister("RCX", ctx.Rcx);
    LogRegister("RDX", ctx.Rdx);
    LogRegister("RSI", ctx.Rsi);
    LogRegister("RDI", ctx.Rdi);
    LogRegister("RBP", ctx.Rbp);
    LogRegister("RSP", ctx.Rsp);
    LogRegister("R8",  ctx.R8);
    LogRegister("R9",  ctx.R9);
    LogRegister("R10", ctx.R10);
    LogRegister("R11", ctx.R11);
    LogRegister("R12", ctx.R12);
    LogRegister("R13", ctx.R13);
    LogRegister("R14", ctx.R14);
    LogRegister("R15", ctx.R15);

    LogRegister("RIP", ctx.Rip);
    LogRegister("EFLAGS", ctx.EFlags);

    // LOG(VERBOSE) << "\n";
    // LOG(VERBOSE) << "  Segments:\n";
    //
    // LOG(VERBOSE)
    //     << "    CS = 0x"
    //     << std::hex
    //     << ctx.SegCs
    //     << "\n";
    //
    // LOG(VERBOSE)
    //     << "    SS = 0x"
    //     << std::hex
    //     << ctx.SegSs
    //     << "\n";
    //
    // LOG(VERBOSE)
    //     << "    DS = 0x"
    //     << std::hex
    //     << ctx.SegDs
    //     << "\n";
    //
    // LOG(VERBOSE)
    //     << "    ES = 0x"
    //     << std::hex
    //     << ctx.SegEs
    //     << "\n";
    //
    // LOG(VERBOSE)
    //     << "    FS = 0x"
    //     << std::hex
    //     << ctx.SegFs
    //     << "\n";
    //
    // LOG(VERBOSE)
    //     << "    GS = 0x"
    //     << std::hex
    //     << ctx.SegGs
    //     << "\n";

    LOG(VERBOSE) << std::dec;
    //
    // LOG(VERBOSE) << "\n";
    // LOG(VERBOSE) << "  Instruction bytes around RIP:\n";
    //
    // const uintptr_t rip =
    //     static_cast<uintptr_t>(ctx.Rip);
    //
    // constexpr size_t before = 16;
    // constexpr size_t after = 32;
    //
    // if (rip >= before)
    // {
    //     const uintptr_t start = rip - before;
    //
    //     if (IsReadable(start, before + after))
    //     {
    //         for (size_t i = 0; i < before + after; ++i)
    //         {
    //             if (i == before)
    //                 LOG(VERBOSE) << " | ";
    //
    //             const auto* ptr =
    //                 reinterpret_cast<const uint8_t*>(
    //                     start + i);
    //
    //             LOG(VERBOSE)
    //                 << std::hex
    //                 << std::setw(2)
    //                 << std::setfill('0')
    //                 << static_cast<unsigned>(*ptr)
    //                 << ' ';
    //         }
    //
    //         LOG(VERBOSE) << std::dec << "\n";
    //     }
    //     else
    //     {
    //         LOG(VERBOSE)
    //             << "    <unable to read instruction memory>\n";
    //     }
    // }

    // LOG(VERBOSE) << "\n";

    // LogStackMemory(ctx, 32);

    // LOG(VERBOSE) << "\n";
    LOG(VERBOSE) << "========================================\n";
}

    DWORD GetRestrictedProtection(DWORD protect)
    {
        if (protect & PAGE_EXECUTE_READWRITE) return PAGE_EXECUTE_READ;
        if (protect & PAGE_EXECUTE_WRITECOPY) return PAGE_EXECUTE_READ;
        if (protect & PAGE_READWRITE)         return PAGE_READONLY;
        if (protect & PAGE_WRITECOPY)         return PAGE_READONLY;
        return protect;
    }

    uintptr_t AlignToPage(uintptr_t address, size_t pageSize)
    {
        return address & ~(pageSize - 1);
    }

    LONG NTAPI VectoredExceptionHandler(PEXCEPTION_POINTERS exceptionInfo)
    {
        const DWORD exceptionCode = exceptionInfo->ExceptionRecord->ExceptionCode;
    	// LOG(INFO) << "VectoredExceptionHandler code: " << std::hex << exceptionCode;
        if (exceptionCode == EXCEPTION_ACCESS_VIOLATION)
        {
            const auto accessType = exceptionInfo->ExceptionRecord->ExceptionInformation[0];
            const auto faultAddress = static_cast<uintptr_t>(exceptionInfo->ExceptionRecord->ExceptionInformation[1]);
            const uintptr_t hitIp = exceptionInfo->ContextRecord->Rip;

            SYSTEM_INFO si;
            GetSystemInfo(&si);
            const uintptr_t pageBase = AlignToPage(faultAddress, si.dwPageSize);

            auto it = g_pageMap.find(pageBase);
            if (it != g_pageMap.end())
            {
                if (accessType == 1)
                {
                    if (std::ranges::find(g_watchedAddresses, faultAddress) != g_watchedAddresses.end())
                    {
                    	const auto* record = exceptionInfo->ExceptionRecord;
                    	const auto* context = exceptionInfo->ContextRecord;

                    	const auto oldValue = *reinterpret_cast<const std::uint8_t*>(faultAddress);

                    	LOG(VERBOSE) << "Memory write detected at 0x"
									 << std::hex << faultAddress
									 << ", old byte: 0x"
									 << static_cast<unsigned>(oldValue);

                    	LogExceptionContext(exceptionInfo);
                    }
                }

                DWORD oldProtect = 0;
                VirtualProtect(reinterpret_cast<LPVOID>(pageBase), si.dwPageSize, it->second.originalProtect, &oldProtect);

                t_rearmPage = pageBase;
                exceptionInfo->ContextRecord->EFlags |= (1 << 8);

                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
        else if (exceptionCode == EXCEPTION_SINGLE_STEP)
        {
            if (t_rearmPage != 0)
            {
                auto it = g_pageMap.find(t_rearmPage);
                if (it != g_pageMap.end())
                {
                    SYSTEM_INFO si;
                    GetSystemInfo(&si);

                    DWORD oldProtect = 0;
                    VirtualProtect(reinterpret_cast<LPVOID>(t_rearmPage), si.dwPageSize, it->second.restrictedProtect, &oldProtect);
                }

                t_rearmPage = 0;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

void InitializeMemoryWatchpoints()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    for (uintptr_t address : g_watchedAddresses)
    {
        uintptr_t pageBase = AlignToPage(address, si.dwPageSize);
        if (g_pageMap.contains(pageBase))
            continue;

        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(pageBase), &mbi, sizeof(mbi)))
        {
            DWORD restricted = GetRestrictedProtection(mbi.Protect);
            if (restricted != mbi.Protect)
            {
                DWORD oldProtect = 0;
                if (VirtualProtect(reinterpret_cast<LPVOID>(pageBase), si.dwPageSize, restricted, &oldProtect))
                {
                    g_pageMap[pageBase] = PageProtectionInfo{
                        .originalProtect = oldProtect,
                        .restrictedProtect = restricted
                    };
                }
            }
        }
    }

    g_vehHandle = AddVectoredExceptionHandler(1, VectoredExceptionHandler);
}

void CleanupMemoryWatchpoints()
{
    if (g_vehHandle)
    {
        RemoveVectoredExceptionHandler(g_vehHandle);
        g_vehHandle = nullptr;
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);

    for (const auto& [pageBase, pageInfo] : g_pageMap)
    {
        DWORD oldProtect = 0;
        VirtualProtect(reinterpret_cast<LPVOID>(pageBase), si.dwPageSize, pageInfo.originalProtect, &oldProtect);
    }

    g_pageMap.clear();
}

BOOL APIENTRY DllMain(HMODULE hmod, DWORD reason, PVOID)
{
	using namespace big;
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hmod);
		g_hmodule     = hmod;
		g_main_thread = CreateThread(
		    nullptr,
		    0,
		    [](PVOID) -> DWORD {
				std::srand(std::chrono::system_clock::now().time_since_epoch().count());
		    	std::filesystem::path base_dir = std::getenv("appdata");
				std::string shim_dir = "YimShim";
				constexpr std::string_view tacid_arg = "-tacidUserId=";
				const std::string command_line = GetCommandLineA();
				if (const auto arg_start = command_line.find(tacid_arg); arg_start != std::string::npos)
				{
					const auto value_start = arg_start + tacid_arg.size();
					const auto value_end   = command_line.find_first_of(" \t", value_start);
					const auto user_id     = command_line.substr(value_start, value_end - value_start);
					if (!user_id.empty()
						&& std::all_of(user_id.begin(), user_id.end(), [](const char c) { return c >= '0' && c <= '9'; }))
						shim_dir += "-" + user_id;
				}


		  //   	constexpr std::string_view addrString = "-packerAddress=";
		  //   	uint64_t packerAddr = 0;
				// if (const auto arg_start = command_line.find(addrString); arg_start != std::string::npos)
				// {
				// 	const auto value_start = arg_start + addrString.size();
				// 	const auto value_end   = command_line.find_first_of(" \t", value_start);
				// 	const auto user_id_str  = command_line.substr(value_start, value_end - value_start);
			 //
				// 	uint64_t user_id{};
				// 	const auto [ptr, ec] = std::from_chars(
				// 		user_id_str.data(),
				// 		user_id_str.data() + user_id_str.size(),
				// 		user_id
				// 	);
			 //
				// 	if (!user_id_str.empty() && ec == std::errc{} && ptr == user_id_str.data() + user_id_str.size())
				// 	{
				// 		packerAddr = user_id;
				// 	}
				// }

		    	constexpr std::string_view exportPacker = "-packerExport";
		    	bool import = true;
				if (const auto arg_start = command_line.find(exportPacker); arg_start != std::string::npos)
				{
					import = false;
				}

				base_dir /= std::filesystem::path("Paragon") / shim_dir;
				g_file_manager.init(base_dir);

				g.init(g_file_manager.get_project_file("./settings.json"));
				g_log.initialize("YimMenu for Paragon Legacy", g_file_manager.get_project_file("./cout.log"), g.debug.external_console);
					// while (!GetModuleHandleA("bink2w64.dll"))
					{
						std::this_thread::sleep_for(300ms);
					}
		    	InitializeMemoryWatchpoints();

		    	while (!GetModuleHandleA("Paragon.Sdk.dll"))
		    	{
					std::this_thread::sleep_for(100ms);
				}
		    	UnpackHandler::TakeTextSnapshot();



		    	LOG(INFO) << "Settings Loaded and logger initialized.";

				LOG(INFO) << "Yim's Menu Initializing";
				LOGF(INFO, "Git Info\n\tBranch:\t{}\n\tHash:\t{}\n\tDate:\t{}", version::GIT_BRANCH, version::GIT_SHA1, version::GIT_DATE);

				// more tech debt, YAY!
				if (is_proton())
				{
					LOG(INFO) << "Running on proton!";
				}
				else
				{
					auto display_version = ReadRegistryKeySZ(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "DisplayVersion");
					auto current_build = ReadRegistryKeySZ(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "CurrentBuild");
					auto UBR = ReadRegistryKeyDWORD(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "UBR");
					LOG(INFO) << GetWindowsVersion() << " Version " << display_version << " (OS Build " << current_build << "." << UBR << ")";
				}

#ifndef NDEBUG
				LOG(WARNING) << "Debug Build. Switch to RelWithDebInfo or Release Build for a more stable experience";
#endif

				auto thread_pool_instance = std::make_unique<thread_pool>();
				LOG(INFO) << "Thread pool initialized.";
		    	while (!FindWindow("grcWindow", nullptr))
		    		std::this_thread::sleep_for(100ms);


		    	auto sc_module = memory::module("Paragon.Sdk.dll");
				sc_module.wait_for_module();
				auto handler = exception_handler();
		    	auto pointers_instance = std::make_unique<pointers>();
				LOG(INFO) << "Pointers initialized.";
		    	std::this_thread::sleep_for(import ? 10000ms : 30000ms);
		    	if (import) UnpackHandler::DoImport();
		    	else
		    	{
		    		UnpackHandler::CompareTextSnapshot();
		    		UnpackHandler::DoExport();
		    	}

						// VehPackHandler::InitializeVehHooks(
						  // reinterpret_cast<void*>(
							  // g_pointers->m_gta.PackerList1
							  // packerAddr
							  // ),
						  // nullptr);
		    	RgscRegistration();
				LOG(INFO) << "Rgsc registration complete";

		    	if (!*g_pointers->m_gta.m_anticheat_initialized_hash)
			    {
				    *g_pointers->m_gta.m_anticheat_initialized_hash = new rage::Obf32; // this doesn't get freed so we don't have to use the game allocator
			    }
				(*g_pointers->m_gta.m_anticheat_initialized_hash)->setData(0x124EA49D);

			    // while (!disable_anticheat_skeleton())
			    // {
				    // LOG(WARNING) << "Failed patching anticheat gameskeleton (injected too early?). Waiting 100ms and trying again";
				    // std::this_thread::sleep_for(100ms);
			    // }
			    LOG(INFO) << "Disabled anticheat gameskeleton.";
				// if (HMODULE FSL = CheckForFSL())
				// {
				//     LOGF(INFO, "FSL Version: {}", reinterpret_cast<int (*)()>(GetProcAddress(FSL, "LawnchairGetVersion"))());
				//     LOGF(INFO, "FSL Local Saves: {}", reinterpret_cast<bool (*)()>(GetProcAddress(FSL, "LawnchairIsProvidingLocalSaves"))() ? "Enabled" : "Disabled");
				//     LOGF(INFO, "FSL BE Bypass: {}", reinterpret_cast<bool (*)()>(GetProcAddress(FSL, "LawnchairIsProvidingBattlEyeBypass"))() ? "Enabled" : "Disabled");
				// }
				// else
				// {
				//     LOGF(FATAL, "YimMenu requires FSL to be loaded. Please get it from UnknownCheats.me");
				// }

		    	auto byte_patch_manager_instance = std::make_unique<byte_patch_manager>();
				LOG(INFO) << "Byte Patch Manager initialized.";

				g_renderer.init();
				LOG(INFO) << "Renderer initialized.";
				auto gui_instance = std::make_unique<gui>();

				auto fiber_pool_instance = std::make_unique<fiber_pool>(11);
				LOG(INFO) << "Fiber pool initialized.";

				g_http_client.init(g_file_manager.get_project_file("./proxy_settings.json"));
				LOG(INFO) << "HTTP Client initialized.";

				g_translation_service.init();
				LOG(INFO) << "Translation Service initialized.";

		    	std::unique_ptr<hooking> hooking_instance{};
		    	if (import)
		    	{
		    		hooking_instance = std::make_unique<hooking>();
					LOG(INFO) << "Hooking initialized.";
		    	}
				g_gta_data_service.init();

			    auto context_menu_service_instance      = std::make_unique<context_menu_service>();
			    auto custom_text_service_instance       = std::make_unique<custom_text_service>();
			    auto mobile_service_instance            = std::make_unique<mobile_service>();
			    auto pickup_service_instance            = std::make_unique<pickup_service>();
			    auto player_service_instance            = std::make_unique<player_service>();
			    auto model_preview_service_instance     = std::make_unique<model_preview_service>();
			    auto handling_service_instance          = std::make_unique<handling_service>();
			    auto gui_service_instance               = std::make_unique<gui_service>();
			    auto script_patcher_service_instance    = std::make_unique<script_patcher_service>();
			    auto player_database_service_instance   = std::make_unique<player_database_service>();
			    auto hotkey_service_instance            = std::make_unique<hotkey_service>();
			    auto matchmaking_service_instance       = std::make_unique<matchmaking_service>();
			    auto api_service_instance               = std::make_unique<api_service>();
			    auto tunables_service_instance          = std::make_unique<tunables_service>();
			    auto script_connection_service_instance = std::make_unique<script_connection_service>();
			    auto xml_vehicles_service_instance      = std::make_unique<xml_vehicles_service>();
			    auto xml_maps_service_instance          = std::make_unique<xml_map_service>();
			    auto script_function_hook_service_instance = std::make_unique<script_function_hook_service>();
			    LOG(INFO) << "Registered service instances...";

				g_notification_service.initialise();
				LOG(INFO) << "Finished initialising services.";

				g_script_mgr.add_script(std::make_unique<script>(&gui::script_func, "GUI", false));

				g_script_mgr.add_script(std::make_unique<script>(&backend::loop, "Backend Loop", false));
				g_script_mgr.add_script(std::make_unique<script>(&backend::self_loop, "Self"));
                #if ENABLE_TOXIC_CHEATS
				g_script_mgr.add_script(std::make_unique<script>(&backend::weapons_loop, "Weapon"));
				g_script_mgr.add_script(std::make_unique<script>(&backend::vehicles_loop, "Vehicle"));
				#endif
				g_script_mgr.add_script(std::make_unique<script>(&backend::misc_loop, "Miscellaneous"));
				g_script_mgr.add_script(std::make_unique<script>(&backend::remote_loop, "Remote"));
                #if ENABLE_TOXIC_CHEATS
				g_script_mgr.add_script(std::make_unique<script>(&backend::rainbowpaint_loop, "Rainbow Paint"));
				#endif
				g_script_mgr.add_script(std::make_unique<script>(&backend::disable_control_action_loop, "Disable Controls"));
                #if ENABLE_TOXIC_CHEATS
				g_script_mgr.add_script(std::make_unique<script>(&backend::world_loop, "World"));
				g_script_mgr.add_script(std::make_unique<script>(&backend::orbital_drone, "Orbital Drone"));
				g_script_mgr.add_script(std::make_unique<script>(&backend::vehicle_control, "Vehicle Control"));
				#endif
				g_script_mgr.add_script(std::make_unique<script>(&context_menu_service::context_menu, "Context Menu"));
				g_script_mgr.add_script(std::make_unique<script>(&backend::tunables_script, "Tunables"));
                #if ENABLE_TOXIC_CHEATS
				g_script_mgr.add_script(std::make_unique<script>(&backend::squad_spawner, "Squad Spawner"));
				#endif
				g_script_mgr.add_script(std::make_unique<script>(&backend::ambient_animations_loop, "Ambient Animations"));

				LOG(INFO) << "Scripts registered.";

				if (import) g_hooking->enable();
				LOG(INFO) << "Hooking enabled.";

		    	std::unique_ptr<native_hooks> native_hooks_instance;
		    	if (import)
		    	{
		    		native_hooks_instance = std::make_unique<native_hooks>();
					LOG(INFO) << "Dynamic native hooker initialized.";
		    	}
				auto lua_manager_instance =
					std::make_unique<lua_manager>(g_file_manager.get_project_folder("scripts"), g_file_manager.get_project_folder("scripts_config"));
				LOG(INFO) << "Lua manager initialized.";

				g_running = true;

				while (g_running)
				{
					g.attempt_save();

					std::this_thread::sleep_for(500ms);
				}

				g_script_mgr.remove_all_scripts();
				LOG(INFO) << "Scripts unregistered.";

				lua_manager_instance.reset();
				LOG(INFO) << "Lua manager uninitialized.";

				if (import) g_hooking->disable();
				LOG(INFO) << "Hooking disabled.";

				if (native_hooks_instance) native_hooks_instance.reset();
				LOG(INFO) << "Dynamic native hooker uninitialized.";

				// Make sure that all threads created don't have any blocking loops
				// otherwise make sure that they have stopped executing
				thread_pool_instance->destroy();
				LOG(INFO) << "Destroyed thread pool.";

			    script_connection_service_instance.reset();
			    LOG(INFO) << "Script Connection Service reset.";
			    tunables_service_instance.reset();
			    LOG(INFO) << "Tunables Service reset.";
			    hotkey_service_instance.reset();
			    LOG(INFO) << "Hotkey Service reset.";
			    matchmaking_service_instance.reset();
			    LOG(INFO) << "Matchmaking Service reset.";
			    player_database_service_instance.reset();
			    LOG(INFO) << "Player Database Service reset.";
			    api_service_instance.reset();
			    LOG(INFO) << "API Service reset.";
			    script_patcher_service_instance.reset();
			    LOG(INFO) << "Script Patcher Service reset.";
			    gui_service_instance.reset();
			    LOG(INFO) << "Gui Service reset.";
			    handling_service_instance.reset();
			    LOG(INFO) << "Vehicle Service reset.";
			    model_preview_service_instance.reset();
			    LOG(INFO) << "Model Preview Service reset.";
			    mobile_service_instance.reset();
			    LOG(INFO) << "Mobile Service reset.";
			    player_service_instance.reset();
			    LOG(INFO) << "Player Service reset.";
			    pickup_service_instance.reset();
			    LOG(INFO) << "Pickup Service reset.";
			    custom_text_service_instance.reset();
			    LOG(INFO) << "Custom Text Service reset.";
			    context_menu_service_instance.reset();
			    LOG(INFO) << "Context Service reset.";
			    xml_vehicles_service_instance.reset();
			    LOG(INFO) << "Xml Vehicles Service reset.";
			    script_function_hook_service_instance.reset();
			    LOG(INFO) << "Script Function Hook Service reset.";
			    LOG(INFO) << "Services uninitialized.";

		    	if (!import && hooking_instance) hooking_instance.reset();
				LOG(INFO) << "Hooking uninitialized.";

				fiber_pool_instance.reset();
				LOG(INFO) << "Fiber pool uninitialized.";

				g_renderer.destroy();
				LOG(INFO) << "Renderer uninitialized.";

				byte_patch_manager_instance.reset();
				LOG(INFO) << "Byte Patch Manager uninitialized.";

				pointers_instance.reset();
				LOG(INFO) << "Pointers uninitialized.";

				thread_pool_instance.reset();
				LOG(INFO) << "Thread pool uninitialized.";

				LOG(INFO) << "Farewell!";
				g_log.destroy();

				CloseHandle(g_main_thread);
				FreeLibraryAndExitThread(g_hmodule, 0);
		    },
		    nullptr,
		    0,
		    &g_main_thread_id);
	}

	return true;
}
