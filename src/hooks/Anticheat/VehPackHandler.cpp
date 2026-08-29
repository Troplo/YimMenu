//
// Created by Troplo on 16/07/2026.
//

#include "VehPackHandler.hpp"

#include <tlhelp32.h>

namespace big
{
    uintptr_t VehPackHandler::m_PointerLoadTarget{};
    uintptr_t VehPackHandler::m_ExecTarget{};
    void* VehPackHandler::m_VehHandle{};
    bool VehPackHandler::m_Complete{};

    std::unordered_map<uintptr_t, VehPackHandler::PageProtectionInfo> VehPackHandler::m_pageMap;
    thread_local uintptr_t VehPackHandler::t_rearmPage = 0;

	static constexpr uint8_t TABLE[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x0F};

    static uintptr_t AlignToPage(uintptr_t address, size_t pageSize)
    {
        return address & ~(pageSize - 1);
    }

	thread_local bool VehPackHandler::t_replacePointer = false;

    LONG WINAPI VehPackHandler::VehHandler(EXCEPTION_POINTERS* info)
    {
        auto record = info->ExceptionRecord;
        auto ctx = info->ContextRecord;

        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        {
            const auto faultAddress = static_cast<uintptr_t>(record->ExceptionInformation[1]);
            const uintptr_t hitIp = ctx->Rip;

            SYSTEM_INFO si;
            GetSystemInfo(&si);
            const uintptr_t pageBase = AlignToPage(faultAddress, si.dwPageSize);

            auto it = m_pageMap.find(pageBase);
            if (it != m_pageMap.end())
            {
                if (hitIp == m_PointerLoadTarget)
                {
                    // Do NOT modify RAX here, the CPU will just overwrite it.
                    // Instead, flag it so the Single Step handler modifies it AFTER execution.
                    t_replacePointer = true;
                }

                DWORD oldProtect = 0;
                VirtualProtect(reinterpret_cast<LPVOID>(pageBase), si.dwPageSize, it->second.originalProtect, &oldProtect);

                // Always set Trap Flag when hooking to re-arm the page (and replace the pointer)
                t_rearmPage = pageBase;
                ctx->EFlags |= (1 << 8);

                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
        else if (record->ExceptionCode == EXCEPTION_SINGLE_STEP)
        {
            bool handled = false;

            if (t_replacePointer)
            {
                // The LEA instruction has now executed. Overwrite its result.
                ctx->Rax = reinterpret_cast<uintptr_t>(&TABLE);
                m_Complete = true;
                LOG(VERBOSE) << std::format("Replaced the table. RAX: {:X} RIP: {:X}", ctx->Rax, ctx->Rip);

                t_replacePointer = false;
                handled = true;
            }

            if (t_rearmPage != 0)
            {
                if (!m_Complete) // Only re-arm if we haven't successfully finished the hook
                {
                    auto it = m_pageMap.find(t_rearmPage);
                    if (it != m_pageMap.end())
                    {
                        SYSTEM_INFO si;
                        GetSystemInfo(&si);

                        DWORD oldProtect = 0;
                        VirtualProtect(reinterpret_cast<LPVOID>(t_rearmPage), si.dwPageSize, it->second.restrictedProtect, &oldProtect);
                    }
                }

                t_rearmPage = 0;
                handled = true;
            }

            if (handled)
            {
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    void VehPackHandler::InitializeVehHooks(void* pointerLoadTarget, void* execTarget)
    {
        if (m_VehHandle)
        {
            return;
        }

        m_PointerLoadTarget = reinterpret_cast<uintptr_t>(pointerLoadTarget);
        m_ExecTarget = reinterpret_cast<uintptr_t>(execTarget);

        SYSTEM_INFO si;
        GetSystemInfo(&si);

        // Protect the target instruction's page
        if (m_PointerLoadTarget)
        {
            uintptr_t pageBase = AlignToPage(m_PointerLoadTarget, si.dwPageSize);

            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(pageBase), &mbi, sizeof(mbi)))
            {
                // To intercept EXECUTION, we strip the PAGE_EXECUTE right.
                // Assuming .text is PAGE_EXECUTE_READ, we downgrade it to PAGE_READWRITE.
                DWORD restricted = PAGE_READWRITE;

                DWORD oldProtect = 0;
                if (VirtualProtect(reinterpret_cast<LPVOID>(pageBase), si.dwPageSize, restricted, &oldProtect))
                {
                    m_pageMap[pageBase] = PageProtectionInfo{
                        .originalProtect = oldProtect,
                        .restrictedProtect = restricted
                    };
                }
            }
        }

        m_VehHandle = AddVectoredExceptionHandler(2, VehHandler);

        // std::thread([]()
        // {
        //     std::this_thread::sleep_for(std::chrono::seconds(10));
        //
        //     if (!m_Complete)
        //     {
        //         MessageBoxA(
        //             nullptr,
        //             "Fatal timing error. Please restart the game.",
        //             "Fatal Error",
        //             MB_OK | MB_ICONERROR
        //         );
        //
        //         TerminateProcess(GetCurrentProcess(), 1);
        //     }
        // }).detach();

        LOG(VERBOSE) << "Inited veh pack handler (Page Exception Strategy)";
    }
}