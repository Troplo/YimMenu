#include "hooking/hooking.hpp"

#include "memory/byte_patch.hpp"
#include "memory/module.hpp"
#include "memory/pattern.hpp"
#include "util/command_line.hpp"
#include "util/current_module.hpp"

#include <array>

// Recreates the sticky bomb fix done on Classic. Getting these offsets and value was fully cancer btw and everything about this gave me an aneurysm
namespace big {
    namespace {
        constexpr auto kProcessPostPhysicsPattern = "40 53 48 83 EC 20 48 8B D9 8B 89 10 06 00 00 8B C1 C1 E8 19 A8 01 74 10 " "C1 E9 12 F6 C1 01 75 08 48 8B CB E8 ? ? ? ? 0F 28 43 60";

        constexpr std::ptrdiff_t kInfoOffset = 0x4D8;
        constexpr std::ptrdiff_t kInfoFlagsOffset = 0x168;
        constexpr std::ptrdiff_t kAttachmentOwnerOffset = 0x50;
        constexpr std::ptrdiff_t kAttachmentExtOffset = 0x48;
        constexpr std::ptrdiff_t kAttachmentStateOffset = 0x5C;

        constexpr std::ptrdiff_t kActiveShiftPatchOffset = 0x11;
        constexpr std::ptrdiff_t kStickyCallOffset = 0x23;

        constexpr std::uint32_t kStickyFlag = 1u << 0;

        // m_pInfo->GetIsSticky()
        bool isSticky(void *projectile) {
            if (!projectile)
                return false;

            const auto self = reinterpret_cast<std::uintptr_t>(projectile);
            const auto info = *reinterpret_cast<const std::uintptr_t *>(self + kInfoOffset);
            if (!info)
                return false;

            const auto flags = *reinterpret_cast<const std::uint32_t *>(info + kInfoFlagsOffset);
            return (flags & kStickyFlag) != 0;
        }

        // this->GetIsAttached()
        bool isAttached(void *projectile) {
            if (!projectile)
                return false;

            const auto self = reinterpret_cast<std::uintptr_t>(projectile);
            const auto owner = *reinterpret_cast<const std::uintptr_t *>(self + kAttachmentOwnerOffset);
            const auto extension = owner ? *reinterpret_cast<const std::uintptr_t *>(owner + kAttachmentExtOffset) : 0;
            const auto state = extension ? *reinterpret_cast<const std::uint8_t *>(extension + kAttachmentStateOffset) : 0;

            return (state & 0x0F) >= 2;
        }
    }

    hooking::c4_collision_fix::~c4_collision_fix() {
        restore();
    }

    void hooking::c4_collision_fix::install() {
        if (!command_line::is_pvp_patch_enabled() || g_stickyShapeTestHook)
            return;

        auto *processPostPhysics = memory::module(GetCurrentModule())
                .scan(memory::pattern(kProcessPostPhysicsPattern))
                .value()
                .as<std::uint8_t *>();
        auto *sticky_call = processPostPhysics + kStickyCallOffset;

        g_stickyShapeTestHook = std::make_unique<call_hook>(sticky_call, reinterpret_cast<void *>(&hooks::processStickyShapeTest));
        g_originalProcessStickyShapeTest = g_stickyShapeTestHook->get_original<ProcessStickyShapeTestFn>();
        g_stickyShapeTestHook->enable();

        g_processPostPhysicsPatch = memory::byte_patch::make(
                    processPostPhysics + kActiveShiftPatchOffset,
                    std::array<std::uint8_t, 3>{0x90, 0x90, 0x90})
                .get();
        g_processPostPhysicsPatch->apply();
        LOG(INFO) << "Applied C4 collision fix";
    }

    void hooking::c4_collision_fix::restore() {
        if (g_processPostPhysicsPatch) {
            g_processPostPhysicsPatch->restore();
            g_processPostPhysicsPatch = nullptr;
        }

        if (g_stickyShapeTestHook) {
            g_stickyShapeTestHook->disable();
            g_stickyShapeTestHook.reset();
        }

        g_originalProcessStickyShapeTest = nullptr;
    }

    void hooking::c4_collision_fix::processStickyShapeTest(void *projectile) {
        if (!g_originalProcessStickyShapeTest || !isSticky(projectile) || isAttached(projectile))
            return;

        g_originalProcessStickyShapeTest(projectile);
    }

    void hooks::processStickyShapeTest(void *projectile) {
        if (g_hooking)
            g_hooking->m_c4_collision_fix.processStickyShapeTest(projectile);
    }
}
