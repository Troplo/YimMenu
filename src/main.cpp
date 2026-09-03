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
#include <mutex>

namespace big
{
    std::mutex g_unpackLocationsMutex;
    std::vector<std::pair<uintptr_t, uint32_t>> g_unpackLocations;

    std::string ReadRegistryKeySZ(HKEY hKeyParent, std::string subkey, std::string valueName)
    {
        HKEY hKey;
        char value[1024];
        DWORD value_length = 1024;
        LONG ret           = RegOpenKeyEx(hKeyParent, subkey.c_str(), 0, KEY_READ, &hKey);
        if (ret != ERROR_SUCCESS) return "";
        ret = RegQueryValueEx(hKey, valueName.c_str(), NULL, NULL, (LPBYTE)&value, &value_length);
        RegCloseKey(hKey);
        if (ret != ERROR_SUCCESS) return "";
        return std::string(value);
    }

    DWORD ReadRegistryKeyDWORD(HKEY hKeyParent, std::string subkey, std::string valueName)
    {
        HKEY hKey;
        DWORD value;
        DWORD value_length = sizeof(DWORD);
        LONG ret           = RegOpenKeyEx(hKeyParent, subkey.c_str(), 0, KEY_READ, &hKey);
        if (ret != ERROR_SUCCESS) return NULL;
        ret = RegQueryValueEx(hKey, valueName.c_str(), NULL, NULL, (LPBYTE)&value, &value_length);
        RegCloseKey(hKey);
        if (ret != ERROR_SUCCESS) return NULL;
        return value;
    }

    std::unique_ptr<char[]> GetWindowsVersion()
    {
        typedef LPWSTR(WINAPI * BFS)(LPCWSTR);
        LPWSTR UTF16   = BFS(GetProcAddress(LoadLibrary("winbrand.dll"), "BrandingFormatString"))(L"%WINDOWS_LONG%");
        int BufferSize = WideCharToMultiByte(CP_UTF8, 0, UTF16, -1, NULL, 0, NULL, NULL);
        std::unique_ptr<char[]> UTF8(new char[BufferSize]);
        WideCharToMultiByte(CP_UTF8, 0, UTF16, -1, UTF8.get(), BufferSize, NULL, NULL);
        GlobalFree(UTF16);
        return UTF8;
    }
}

namespace
{
    std::vector<uintptr_t> g_watchedAddresses = {
        0x00000141150540,
        0x000001412D37D1,
        0x00000001412D37CC,
        0x1411A1480,
        0x1411A1488,
        0x143B1661C,
        0x142636B50,
    	0x142638D90,
    	0x142636AF2,
    	0x142636AF0
    };

    constexpr uintptr_t MEMCPY_ADDR = 0x1433084AF;

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
        LOG(VERBOSE) << "    " << std::left << std::setw(8) << name << " = 0x"
                     << std::right << std::hex << std::setw(16) << std::setfill('0')
                     << value << std::dec << std::setfill(' ');
    }

    void LogExceptionCode(DWORD code)
    {
        LOG(VERBOSE) << "  Exception code: 0x" << std::hex << std::setw(8)
                     << std::setfill('0') << code << std::dec << std::setfill(' ') << "\n";
    }
}

void LogExceptionContext(PEXCEPTION_POINTERS exceptionInfo)
{
    if (!exceptionInfo || !exceptionInfo->ExceptionRecord || !exceptionInfo->ContextRecord) return;

    const auto* record = exceptionInfo->ExceptionRecord;
    const auto& ctx = *exceptionInfo->ContextRecord;

    LOG(VERBOSE) << "\n========== Exception Context ==========\n";
    LogExceptionCode(record->ExceptionCode);
    LOG(VERBOSE) << "  Exception address: 0x" << std::hex << std::setw(16) << std::setfill('0')
                 << reinterpret_cast<uintptr_t>(record->ExceptionAddress) << std::dec << std::setfill(' ') << "\n";

    LOG(VERBOSE) << "  Registers: ";
    LogRegister("RAX", ctx.Rax); LogRegister("RBX", ctx.Rbx); LogRegister("RCX", ctx.Rcx);
    LogRegister("RDX", ctx.Rdx); LogRegister("RSI", ctx.Rsi); LogRegister("RDI", ctx.Rdi);
    LogRegister("RBP", ctx.Rbp); LogRegister("RSP", ctx.Rsp); LogRegister("RIP", ctx.Rip);

    LOG(VERBOSE) << std::dec << "========================================\n";
}

DWORD GetRestrictedProtectionWrite(DWORD protect)
{
    if (protect & PAGE_EXECUTE_READWRITE) return PAGE_EXECUTE_READ;
    if (protect & PAGE_EXECUTE_WRITECOPY) return PAGE_EXECUTE_READ;
    if (protect & PAGE_READWRITE)         return PAGE_READONLY;
    if (protect & PAGE_WRITECOPY)         return PAGE_READONLY;
    return protect;
}

DWORD GetRestrictedProtectionExecute(DWORD protect)
{
    if (protect & PAGE_EXECUTE_READ) return PAGE_READONLY;
    if (protect & PAGE_EXECUTE_READWRITE) return PAGE_READWRITE;
    if (protect & PAGE_EXECUTE_WRITECOPY) return PAGE_READWRITE;
    return protect;
}

uintptr_t AlignToPage(uintptr_t address, size_t pageSize)
{
    return address & ~(pageSize - 1);
}

LONG NTAPI VectoredExceptionHandler(PEXCEPTION_POINTERS exceptionInfo)
{
    const DWORD exceptionCode = exceptionInfo->ExceptionRecord->ExceptionCode;

    if (exceptionCode == EXCEPTION_ACCESS_VIOLATION)
    {
        const auto accessType = exceptionInfo->ExceptionRecord->ExceptionInformation[0];
        const auto faultAddress = static_cast<uintptr_t>(exceptionInfo->ExceptionRecord->ExceptionInformation[1]);
        const uintptr_t hitIp = exceptionInfo->ContextRecord->Rip;

        // Directly intercept the execution of memcpy, bypass stepping completely!
        if (hitIp == MEMCPY_ADDR)
        {
            uintptr_t dest = exceptionInfo->ContextRecord->Rcx;
            uintptr_t src = exceptionInfo->ContextRecord->Rdx;
            uint32_t size = static_cast<uint32_t>(exceptionInfo->ContextRecord->R8);

            LOG(VERBOSE) << "VEH Emulated MEMCPY! Dest: 0x" << std::hex << dest << " Size: 0x" << size;

            if (size > 0 && src != 0 && dest != 0)
            {
                // Exact emulation of their forward loop: *a1 = *a2; a1++; a2++;
                uint8_t* d = reinterpret_cast<uint8_t*>(dest);
                uint8_t* s = reinterpret_cast<uint8_t*>(src);
                for (uint32_t i = 0; i < size; ++i)
                {
                    d[i] = s[i];
                }

                // Log the location
                std::lock_guard<std::mutex> lock(big::g_unpackLocationsMutex);
                big::g_unpackLocations.emplace_back(dest, size);
            }

            // Simulate the RET instruction to return directly to caller
            uintptr_t rsp = exceptionInfo->ContextRecord->Rsp;
            uintptr_t returnAddress = *reinterpret_cast<uintptr_t*>(rsp);

            exceptionInfo->ContextRecord->Rip = returnAddress;
            exceptionInfo->ContextRecord->Rsp += 8;
            exceptionInfo->ContextRecord->Rax = size; // Original loop returns the size

            return EXCEPTION_CONTINUE_EXECUTION;
        }

        SYSTEM_INFO si;
        GetSystemInfo(&si);
        const uintptr_t pageBase = AlignToPage(faultAddress, si.dwPageSize);

        auto it = g_pageMap.find(pageBase);
        if (it != g_pageMap.end())
        {
            if (accessType == 1) // Memory Write Fault
            {
                if (std::ranges::find(g_watchedAddresses, faultAddress) != g_watchedAddresses.end())
                {
                    const auto oldValue = *reinterpret_cast<const std::uint8_t*>(faultAddress);
                    LOG(VERBOSE) << "Memory write detected at 0x" << std::hex << faultAddress << ", old byte: 0x" << static_cast<unsigned>(oldValue);
                    LogExceptionContext(exceptionInfo);
                }
            }

            // Temporarily restore original protection to execute the instruction for any OTHER function
            DWORD oldProtect = 0;
            VirtualProtect(reinterpret_cast<LPVOID>(pageBase), si.dwPageSize, it->second.originalProtect, &oldProtect);

            t_rearmPage = pageBase;
            exceptionInfo->ContextRecord->EFlags |= (1 << 8); // Enable TF (Trap Flag) for single-step

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
                // Re-apply restricted protections
                VirtualProtect(reinterpret_cast<LPVOID>(t_rearmPage), si.dwPageSize, it->second.restrictedProtect, &oldProtect);
            }

            t_rearmPage = 0;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

void InitializeMemoryWatchpoints(bool exportPacker)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    g_vehHandle = AddVectoredExceptionHandler(1, VectoredExceptionHandler);

    for (uintptr_t address : g_watchedAddresses)
    {
        uintptr_t pageBase = AlignToPage(address, si.dwPageSize);
        if (g_pageMap.contains(pageBase)) continue;

        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(pageBase), &mbi, sizeof(mbi)))
        {
            DWORD restricted = GetRestrictedProtectionWrite(mbi.Protect);
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

	if (exportPacker)
	{
		uintptr_t memcpyPageBase = AlignToPage(MEMCPY_ADDR, si.dwPageSize);
		if (!g_pageMap.contains(memcpyPageBase))
		{
			MEMORY_BASIC_INFORMATION mbi{};
			if (VirtualQuery(reinterpret_cast<LPCVOID>(memcpyPageBase), &mbi, sizeof(mbi)))
			{
				DWORD restricted = GetRestrictedProtectionExecute(mbi.Protect);
				if (restricted != mbi.Protect)
				{
					DWORD oldProtect = 0;
					if (VirtualProtect(reinterpret_cast<LPVOID>(memcpyPageBase), si.dwPageSize, restricted, &oldProtect))
					{
						LOG(VERBOSE) << "Set memcpy DEP execution page protection on 0x" << std::hex << memcpyPageBase;
						g_pageMap[memcpyPageBase] = PageProtectionInfo{
							.originalProtect = oldProtect,
							.restrictedProtect = restricted
						};
					}
				}
			}
		}
	}
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
            nullptr, 0,
            [](PVOID) -> DWORD {
                std::srand(std::chrono::system_clock::now().time_since_epoch().count());
                std::filesystem::path base_dir = std::getenv("appdata");
                std::string shim_dir = "YimShim-173";
                    constexpr std::string_view tacid_arg = "-tacidUserId=";
                    const std::string command_line = GetCommandLineA();

                if (command_line::get(L"-uniqueYimShimFolder", false))
                {
                    if (const auto arg_start = command_line.find(tacid_arg); arg_start != std::string::npos)
                    {
                        const auto value_start = arg_start + tacid_arg.size();
                        const auto value_end   = command_line.find_first_of(" \t", value_start);
                        const auto user_id     = command_line.substr(value_start, value_end - value_start);
                        if (!user_id.empty() && std::all_of(user_id.begin(), user_id.end(), [](const char c) { return c >= '0' && c <= '9'; }))
                            shim_dir += "-" + user_id;
                    }
                }

                constexpr std::string_view exportPacker = "-packerExport";
                bool import = true;
                if (const auto arg_start = command_line.find(exportPacker); arg_start != std::string::npos)
                    import = false;

                base_dir /= std::filesystem::path("Paragon") / shim_dir;
                g_file_manager.init(base_dir);
                g.init(g_file_manager.get_project_file("./settings.json"));
                g_log.initialize("YimMenu for Paragon Legacy", g_file_manager.get_project_file("./cout.log"), g.debug.external_console);

                if (command_line::get(L"-nobinkvideo", false)) {
                    byte_patch_manager::patch_intro();
                }

                std::this_thread::sleep_for(300ms);
#if __DEBUG
                InitializeMemoryWatchpoints(!import);
#endif

                auto sc_module = memory::module("Paragon.Sdk.dll");
                sc_module.wait_for_module();

#if !__DEBUG
            	if (!import)
#endif
            	{
            		UnpackHandler::TakeTextSnapshot();
            	}
                LOG(VERBOSE) << "Settings Loaded and logger initialized.";

                auto thread_pool_instance = std::make_unique<thread_pool>();
                while (!FindWindow("grcWindow", nullptr))
                    std::this_thread::sleep_for(100ms);

                auto handler = exception_handler();
                auto pointers_instance = std::make_unique<pointers>();
#if RGSC_ENABLED
				RgscRegistration();
#endif

                std::this_thread::sleep_for(10000ms);

                if (import) UnpackHandler::DoImport();
                else
                {
                    UnpackHandler::CompareTextSnapshot();
                    UnpackHandler::DoExport();
                }
            	LOG(INFO) << "Imported";

                if (!*g_pointers->m_gta.m_anticheat_initialized_hash)
                    *g_pointers->m_gta.m_anticheat_initialized_hash = new rage::Obf32;

                (*g_pointers->m_gta.m_anticheat_initialized_hash)->setData(0x124EA49D);

                auto byte_patch_manager_instance = std::make_unique<byte_patch_manager>();
                g_renderer.init();
                auto gui_instance = std::make_unique<gui>();
                auto fiber_pool_instance = std::make_unique<fiber_pool>(11);

                g_http_client.init(g_file_manager.get_project_file("./proxy_settings.json"));
                g_translation_service.init();

                std::unique_ptr<hooking> hooking_instance{};
                hooking_instance = std::make_unique<hooking>();
#if ENABLE_CACHE
                g_gta_data_service.init();
#endif
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

                g_notification_service.initialise();

                g_script_mgr.add_script(std::make_unique<script>(&gui::script_func, "GUI", false));
                g_script_mgr.add_script(std::make_unique<script>(&backend::loop, "Backend Loop", false));
                g_script_mgr.add_script(std::make_unique<script>(&backend::self_loop, "Self"));
                g_script_mgr.add_script(std::make_unique<script>(&backend::misc_loop, "Miscellaneous"));
                g_script_mgr.add_script(std::make_unique<script>(&backend::remote_loop, "Remote"));
                g_script_mgr.add_script(std::make_unique<script>(&backend::disable_control_action_loop, "Disable Controls"));
                g_script_mgr.add_script(std::make_unique<script>(&context_menu_service::context_menu, "Context Menu"));
                g_script_mgr.add_script(std::make_unique<script>(&backend::tunables_script, "Tunables"));
                g_script_mgr.add_script(std::make_unique<script>(&backend::ambient_animations_loop, "Ambient Animations"));

                if (hooking_instance) g_hooking->enable();

                std::unique_ptr<native_hooks> native_hooks_instance;
                native_hooks_instance = std::make_unique<native_hooks>();

                auto lua_manager_instance = std::make_unique<lua_manager>(g_file_manager.get_project_folder("scripts"), g_file_manager.get_project_folder("scripts_config"));

                g_running = true;
                while (g_running)
                {
                    g.attempt_save();
                    std::this_thread::sleep_for(500ms);
                }

                g_script_mgr.remove_all_scripts();
                lua_manager_instance.reset();
                if (import) g_hooking->disable();
                if (native_hooks_instance) native_hooks_instance.reset();

                thread_pool_instance->destroy();

                script_connection_service_instance.reset();
                tunables_service_instance.reset();
                hotkey_service_instance.reset();
                matchmaking_service_instance.reset();
                player_database_service_instance.reset();
                api_service_instance.reset();
                script_patcher_service_instance.reset();
                gui_service_instance.reset();
                handling_service_instance.reset();
                model_preview_service_instance.reset();
                mobile_service_instance.reset();
                player_service_instance.reset();
                pickup_service_instance.reset();
                custom_text_service_instance.reset();
                context_menu_service_instance.reset();
                xml_vehicles_service_instance.reset();
                script_function_hook_service_instance.reset();

                if (hooking_instance) hooking_instance.reset();

                fiber_pool_instance.reset();
                g_renderer.destroy();
                byte_patch_manager_instance.reset();
                pointers_instance.reset();
                thread_pool_instance.reset();

                g_log.destroy();
                CloseHandle(g_main_thread);
                FreeLibraryAndExitThread(g_hmodule, 0);
            }, nullptr, 0, &g_main_thread_id);
    }
    return true;
}
