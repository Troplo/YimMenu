#include "first_person_camera_patch.hpp"

#include "byte_patch.hpp"
#include "module.hpp"
#include "pattern.hpp"
#include "util/current_module.hpp"

#include <asmjit/asmjit.h>
#include <hde64.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

// An absolute monstrosity of code to recreate a Classic edition patch.
namespace big::first_person_camera_patch {
    namespace {
        class patch_instance {
        public:
            static constexpr std::size_t jump_size = 14;
            using emitter = std::function<void(asmjit::x86::Assembler &)>;

            static std::unique_ptr<patch_instance> make(void *address, const std::size_t patch_size, const emitter& emit) {
                auto patch = std::make_unique<patch_instance>();
                if (!patch->install(address, patch_size, emit))
                    return nullptr;

                return patch;
            }

            ~patch_instance() {
                if (m_patch) {
                    m_patch->restore();
                    m_patch->remove();
                }

                if (m_runtime && m_trampoline) {
                    m_runtime->_release(m_trampoline);
                }
            }

        private:
            bool install(void *address, const std::size_t patch_size, const emitter &emit) {
                if (patch_size < jump_size)
                    return false;

                m_runtime = std::make_unique<asmjit::JitRuntime>();

                asmjit::CodeHolder code;
                if (code.init(m_runtime->environment(), m_runtime->cpuFeatures()))
                    return false;

                asmjit::x86::Assembler assembler(&code);
                emit(assembler);
                if (assembler.finalize())
                    return false;

                if (m_runtime->add(&m_trampoline, &code))
                    return false;

                std::vector<std::uint8_t> jump(patch_size, 0x90);
                jump[0] = 0xFF;
                jump[1] = 0x25;
                jump[2] = jump[3] = jump[4] = jump[5] = 0;
                const auto trampoline_address = reinterpret_cast<std::uintptr_t>(m_trampoline);
                std::memcpy(jump.data() + 6, &trampoline_address, sizeof(trampoline_address));

                m_patch = memory::byte_patch::make(address, jump).get();
                m_patch->apply();
                return true;
            }

            std::unique_ptr<asmjit::JitRuntime> m_runtime;
            void *m_trampoline{};
            memory::byte_patch *m_patch{};
        };

        std::vector<std::unique_ptr<patch_instance> > g_patches;

        constexpr std::int32_t kPedConfigFlagsOffset = 0x146C;
        constexpr std::int32_t kWeaponSwapMask = 0x800;
        constexpr std::int32_t kWeaponSwapByteOffset = 0x146D;
        constexpr std::int32_t kWeaponSwapByteMask = 0x08;
        constexpr std::int32_t kMotionDataFlagsOffset = 0x5E0;
        constexpr std::int32_t kIndependentMoverTransition = 0x600;

        struct relative_branch {
            std::uint8_t *address;
            std::uint8_t *target;
            std::uint8_t *fallthrough;
            std::size_t length;
            std::uint8_t condition;
            bool conditional;
        };

        std::optional<hde64s> decode_instruction(const std::uint8_t *address) {
            hde64s instruction{};
            if (!hde64_disasm(address, &instruction) || instruction.flags & F_ERROR)
                return std::nullopt;

            return instruction;
        }

        std::optional<relative_branch> decode_relative_branch(std::uint8_t *address) {
            const auto instruction = decode_instruction(address);
            if (!instruction || !(instruction->flags & F_RELATIVE))
                return std::nullopt;

            const auto make_target = [&](const std::intptr_t displacement) {
                return reinterpret_cast<std::uint8_t *>(reinterpret_cast<std::uintptr_t>(address) + instruction->len + displacement);
            };

            if (instruction->opcode == 0xEB) {
                return relative_branch{
                    address,
                    make_target(static_cast<std::int8_t>(instruction->imm.imm8)),
                    address + instruction->len,
                    instruction->len,
                    0,
                    false
                };
            }

            if (instruction->opcode >= 0x70 && instruction->opcode <= 0x7F) {
                return relative_branch{
                    address,
                    make_target(static_cast<std::int8_t>(instruction->imm.imm8)),
                    address + instruction->len,
                    instruction->len,
                    static_cast<std::uint8_t>(instruction->opcode & 0x0F),
                    true
                };
            }

            if (instruction->opcode == 0x0F && instruction->opcode2 >= 0x80 && instruction->opcode2 <= 0x8F) {
                return relative_branch{
                    address,
                    make_target(static_cast<std::int32_t>(instruction->imm.imm32)),
                    address + instruction->len,
                    instruction->len,
                    static_cast<std::uint8_t>(instruction->opcode2 & 0x0F),
                    true
                };
            }

            return std::nullopt;
        }

        std::optional<std::size_t> instruction_span(const std::uint8_t *address, const std::size_t minimum_size) {
            std::size_t size{};
            while (size < minimum_size) {
                const auto instruction = decode_instruction(address + size);
                if (!instruction || !instruction->len)
                    return std::nullopt;

                size += instruction->len;
            }

            return size;
        }

        bool is_transition_test(const hde64s &instruction, const std::uint8_t ped_register) {
            if (instruction.opcode != 0xF7 || instruction.modrm_reg != 0 || !(instruction.flags & F_IMM32)
                || instruction.imm.imm32 != static_cast<std::uint32_t>(kIndependentMoverTransition))
                return false;

            const auto register_id = static_cast<std::uint8_t>((instruction.rex_b << 3) | instruction.modrm_rm);
            if (instruction.modrm_mod == 3)
                return register_id == 1;

            return instruction.modrm_mod == 2 && register_id == ped_register && (instruction.flags & F_DISP32)
                   && instruction.disp.disp32 == static_cast<std::uint32_t>(kMotionDataFlagsOffset);
        }

        std::vector<relative_branch> find_transition_branches(const memory::module &module, std::uint8_t *start, const std::uint8_t ped_register) {
            constexpr std::size_t search_size = 0x100;
            const auto search_end = std::min(module.end().as<std::uintptr_t>(), reinterpret_cast<std::uintptr_t>(start) + search_size);
            std::vector<relative_branch> result;

            for (auto address = reinterpret_cast<std::uintptr_t>(start); address < search_end;) {
                const auto instruction = decode_instruction(reinterpret_cast<const std::uint8_t *>(address));
                if (!instruction || !instruction->len || address + instruction->len > search_end)
                    break;

                auto *next_address = reinterpret_cast<std::uint8_t *>(address + instruction->len);
                if (is_transition_test(*instruction, ped_register)) {
                    if (const auto branch = decode_relative_branch(next_address); branch && branch->conditional && branch->condition == 5)
                        result.push_back(*branch);
                }

                address += instruction->len;
            }

            return result;
        }

        template<typename T>
        void keep_patch(std::unique_ptr<T> patch, const std::string_view name) {
            if (patch) {
                g_patches.push_back(std::move(patch));
                LOG(INFO) << "Patched " << name;
            }
        }

        void install_first_person_orientation_limits_patch(const memory::module &module) {
            constexpr auto pattern =
                    "F6 87 A0 07 00 00 08 41 BC 01 00 00 00 44 8A F0 0F 85 ? ? ? ? 8B 8E E0 05 00 00 C1 E9 06 41 84 CC 74 ? 80 BD 60 01 00 00 00 74 ? 80 7C 24 5A 00 0F 85 ? ? ? ?";
            constexpr std::ptrdiff_t patch_offset = 0x16;
            constexpr std::ptrdiff_t continue_offset = 0x24;
            constexpr std::ptrdiff_t fallback_offset = 0x2D;

            auto *block = module.scan(memory::pattern(pattern)).value().as<std::uint8_t *>();
            const auto patch_address = block + patch_offset;
            const auto continue_address = block + continue_offset;
            const auto fallback_address = block + fallback_offset;
            keep_patch(patch_instance::make(
                           patch_address,
                           patch_instance::jump_size,
                           [continue_address, fallback_address](asmjit::x86::Assembler &assembler) {
                               const auto original_sequence = assembler.newLabel();

                               assembler.test(asmjit::x86::dword_ptr(asmjit::x86::rsi, kPedConfigFlagsOffset), kWeaponSwapMask);
                               assembler.jz(original_sequence);
                               assembler.jmp(asmjit::imm(fallback_address));

                               assembler.bind(original_sequence);
                               assembler.mov(asmjit::x86::ecx, asmjit::x86::dword_ptr(asmjit::x86::rsi, kMotionDataFlagsOffset));
                               assembler.shr(asmjit::x86::ecx, 6);
                               assembler.test(asmjit::x86::r12b, asmjit::x86::cl);
                               const auto continue_sequence = assembler.newLabel();
                               assembler.jnz(continue_sequence);
                               assembler.jmp(asmjit::imm(fallback_address));
                               assembler.bind(continue_sequence);
                               assembler.jmp(asmjit::imm(continue_address));
                           }),
                       "first-person orientation limits");
        }

        void install_process_post_camera_weapon_swap_patch(const memory::module &module) {
            constexpr auto pattern = "8A 88 A9 07 00 00 41 BE 01 00 00 00 F6 C1 10 74 ? F6 C1 20 75 ? 41 8A FE EB ? 40 32 FF";
            constexpr std::ptrdiff_t patch_offset = 0x0F;
            constexpr std::ptrdiff_t condition_done_offset = 0x1E;
            constexpr std::int32_t sprint_breakout_block = 0x20;

            auto *block = module.scan(memory::pattern(pattern)).value().as<std::uint8_t *>();
            constexpr auto ped = asmjit::x86::rbx;
            const auto patch_address = block + patch_offset;
            const auto condition_done = block + condition_done_offset;
            keep_patch(patch_instance::make(
                           patch_address,
                           0x0F,
                           [condition_done, ped, sprint_breakout_block](asmjit::x86::Assembler &assembler) {
                               const auto camera_false = assembler.newLabel();
                               const auto weapon_swap_clear = assembler.newLabel();

                               assembler.jz(camera_false);
                               assembler.test(asmjit::x86::cl, sprint_breakout_block);
                               assembler.jnz(camera_false);
                               assembler.mov(asmjit::x86::dil, asmjit::x86::r14b);
                               assembler.jmp(asmjit::imm(condition_done));

                               assembler.bind(camera_false);
                               assembler.pushfq();
                               assembler.test(asmjit::x86::byte_ptr(ped, kWeaponSwapByteOffset), kWeaponSwapByteMask);
                               assembler.jz(weapon_swap_clear);
                               assembler.popfq();
                               assembler.mov(asmjit::x86::dil, asmjit::x86::r14b);
                               assembler.jmp(asmjit::imm(condition_done));

                               assembler.bind(weapon_swap_clear);
                               assembler.popfq();
                               assembler.xor_(asmjit::x86::dil, asmjit::x86::dil);
                               assembler.jmp(asmjit::imm(condition_done));
                           }),
                       "ProcessPostCamera weapon-swap condition");
        }

        void install_motion_aiming_weapon_swap_patch(const memory::module &module) {
            constexpr auto pattern =
                    "48 8B 51 10 F6 82 90 10 00 00 02 0F 84 ? ? ? ? 4C 8B 82 A8 10 00 00 4D 85 C0 74 ? 8B 8A E0 05 00 00 41 B1 01 8B C1 C1 E8 06 41 84 C1";

            const auto signature = memory::pattern(pattern);
            auto *block = module.scan(signature).value().as<std::uint8_t *>();
            const auto search_start = block + signature.m_bytes.size();
            const auto transition_branch = find_transition_branches(module, search_start, 2).front();
            const auto patch_size = instruction_span(transition_branch.address, patch_instance::jump_size).value();
            const auto patch_end = transition_branch.address + patch_size;
            const auto continuation_size = patch_size - transition_branch.length;
            constexpr auto ped = asmjit::x86::rdx;
            keep_patch(patch_instance::make(
                           transition_branch.address,
                           patch_size,
                           [transition_branch, patch_end, continuation_size, ped](asmjit::x86::Assembler &assembler) {
                               const auto transition_clear = assembler.newLabel();
                               const auto weapon_swap_set = assembler.newLabel();
                               const auto continue_original = assembler.newLabel();

                               assembler.jz(transition_clear);
                               assembler.pushfq();
                               assembler.test(asmjit::x86::dword_ptr(ped, kPedConfigFlagsOffset), kWeaponSwapMask);
                               assembler.jnz(weapon_swap_set);
                               assembler.popfq();
                               assembler.jmp(asmjit::imm(transition_branch.target));

                               assembler.bind(weapon_swap_set);
                               assembler.popfq();
                               assembler.jmp(continue_original);

                               assembler.bind(transition_clear);
                               assembler.jmp(continue_original);

                               assembler.bind(continue_original);
                               if (continuation_size) {
                                   assembler.embed(transition_branch.fallthrough, continuation_size);
                               }
                               assembler.jmp(asmjit::imm(patch_end));
                           }),
                       "DoPostCameraSetHeading weapon-swap condition");
        }
    }

    void apply() {
        const memory::module module(GetCurrentModule());
        install_first_person_orientation_limits_patch(module);
        install_process_post_camera_weapon_swap_patch(module);
        install_motion_aiming_weapon_swap_patch(module);
    }

    void restore() {
        g_patches.clear();
    }
}
