#include "byte_patch_manager.hpp"

#include "memory/byte_patch.hpp"
#include "memory/module.hpp"
#include "memory/pattern.hpp"
#include "util/command_line.hpp"
#include "util/current_module.hpp"

#include <tlhelp32.h>

namespace {
    // Shit to suspend every other thread in the process while it is pattern searching
    class scoped_thread_suspend {
    public:
        ~scoped_thread_suspend() {
            for (const auto thread: m_threads) {
                ResumeThread(thread);
                CloseHandle(thread);
            }
        }

        bool acquire() {
            const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE)
                return false;

            THREADENTRY32 entry{};
            entry.dwSize = sizeof(entry);
            if (!Thread32First(snapshot, &entry)) {
                CloseHandle(snapshot);
                return false;
            }

            do {
                if (entry.th32OwnerProcessID == GetCurrentProcessId() && entry.th32ThreadID != GetCurrentThreadId()) {
                    const auto thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
                    if (!thread || SuspendThread(thread) == static_cast<DWORD>(-1)) {
                        if (thread)
                            CloseHandle(thread);
                        CloseHandle(snapshot);
                        return false;
                    }

                    m_threads.push_back(thread);
                }
            } while (Thread32Next(snapshot, &entry));

            CloseHandle(snapshot);
            return true;
        }

    private:
        std::vector<HANDLE> m_threads;
    };
}

namespace big {
    void byte_patch_manager::patch_intro() {
        const auto module_name = GetCurrentModule();

        const memory::module module(module_name);

        const memory::pattern intro_signature("48 8B 0D ? ? ? ? 48 85 C9 0F 84 ? ? ? ? 48 8D 55 A7 E8 ? ? ? ? 84 C0 74 41 48 8B 05 ? ? ? ?");
        const memory::pattern legal_signature("48 83 EC 28 84 C9 74 66 83 25 ? ? ? ? 00 E8 ? ? ? ? 8B 0D ? ? ? ?");

        bool intro_patched = false;
        bool legal_patched = false;
        const auto deadline = std::chrono::steady_clock::now() + 10s;

        while ((!intro_patched || !legal_patched) && std::chrono::steady_clock::now() < deadline) {
            const auto intro_match = intro_patched ? std::optional<memory::handle>{} : module.scan(intro_signature);
            const auto legal_match = legal_patched ? std::optional<memory::handle>{} : module.scan(legal_signature);

            if (!intro_match && !legal_match) {
                std::this_thread::sleep_for(10ms);
                continue;
            }

            scoped_thread_suspend suspended_threads;
            if (!suspended_threads.acquire()) {
                continue;
            }

            if (intro_match) {
                const auto stable_match = module.scan(intro_signature);
                if (stable_match && stable_match.value() == intro_match.value()) {
                    auto *call = stable_match->as<std::uint8_t *>() + 0x14;
                    memory::byte_patch::make(call, std::array<uint8_t, 5>{0x33, 0xC0, 0x90, 0x90, 0x90})->apply();
                    intro_patched = true;
                }
            }

            if (legal_match) {
                const auto stable_match = module.scan(legal_signature);
                if (stable_match && stable_match.value() == legal_match.value()) {
                    auto *assignment = stable_match->as<std::uint8_t *>() + 0x08;
                    const auto patch = std::array<uint8_t, 7>{0xC6, 0x05, assignment[2], assignment[3], assignment[4], assignment[5], 0x01};
                    memory::byte_patch::make(assignment, patch)->apply();
                    legal_patched = true;
                }
            }
        }

        if (intro_patched) {
            LOG(INFO) << "Patched out intro video";
        } else {
            LOG(WARNING) << "Timed out waiting for intro video code";
        }

        if (legal_patched) {
            LOG(INFO) << "Patched out legal page";
        } else {
            LOG(WARNING) << "Timed out waiting for legal page code";
        }
    }
}
