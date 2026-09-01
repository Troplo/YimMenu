#include "hooking/hooking.hpp"

#include "memory/byte_patch.hpp"
#include "memory/module.hpp"
#include "memory/pattern.hpp"
#include "util/current_module.hpp"

#include <array>
#include <intrin.h>

class CNetShopTransaction;
class CNetShopTransactionBase;

// Schizo testing shit. It kinda works but not really xD
namespace big
{
	namespace
	{
		using ProcessingStartFn = bool (*)(CNetShopTransaction*);
		using UpdateFn          = void (*)(CNetShopTransactionBase*);

		constexpr std::uintptr_t kShouldDoNullStubRva            = 0x17354;
		constexpr std::uintptr_t kNetworkShoppingMgrSingletonRva = 0x1F69B40;
		constexpr auto kProcessingStartPattern                   = "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 48 8D 68 A1 48 81 EC ? ? ? ? 83 79 1C 01 48 8B D9";
		constexpr auto kUpdatePattern                            = "48 83 EC 28 8B 51 1C 83 FA 02 72 ? 80 79 2E 00 74 ? C6 41 2E 00 8B 05 ? ? ? ? 89 41 30 83 FA 03";
		constexpr auto kReturnTruePatch                          = std::array<std::uint8_t, 5>{0xB0, 0x01, 0x90, 0x90, 0x90};
		constexpr auto kApplyDataToStatsPatch                    = std::array<std::uint8_t, 2>{0x90, 0x90};

		struct caller_patch
		{
			const char* name;
			const char* signature;
			std::ptrdiff_t call_offset;
			memory::byte_patch* patch{};
		};

		std::array caller_patches{
		    caller_patch{"spendcash", "48 8b 0d ? ? ? ? 41 8a d9 41 8a f0 e8 ? ? ? ? 84 c0 0f 85 ? ? ? ?", 13},
		    caller_patch{"canspendgtadollars", "48 8b 0d ? ? ? ? 45 33 ff 33 db 45 33 f6 45 33 e4 e8 ? ? ? ? 84 c0 75 ?", 18},
		    caller_patch{"earncash", "48 8b 0d ? ? ? ? 49 8b e9 40 8a fa e8 ? ? ? ? 84 c0 0f 85 ? ? ? ?", 13},
		    caller_patch{"initplayercash a", "85 c9 7e ? 48 8b 0d ? ? ? ? e8 ? ? ? ? 84 c0 74 ? 83 c9 ff", 11},
		    caller_patch{"initplayercash b", "85 ff 7e ? 48 8b 0d ? ? ? ? e8 ? ? ? ? 84 c0 74 ? e8 ? ? ? ? 48 85 c0", 11},
		};

		std::uintptr_t g_target{};
		std::uintptr_t g_module_begin{};
		std::uintptr_t g_module_end{};
		memory::byte_patch* g_patch{};
		memory::byte_patch* g_apply_data_to_stats_patch{};
		void* g_processing_start_target{};
		void* g_original_processing_start{};
		void* g_update_target{};
		UpdateFn g_original_update{};
		PVOID g_veh_handle{};
		std::atomic_bool g_enabled{};

		bool validate_processing_start(const std::uint8_t* target)
		{
			constexpr std::array<std::uint8_t, 23> prologue{
			    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x55, 0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81, 0xEC};

			for (std::size_t i = 0; i < prologue.size(); ++i)
				if (target[i] != prologue[i])
					return false;

			return target[27] == 0x83 && target[28] == 0x79 && target[29] == 0x1C && target[30] == 0x01 && target[31] == 0x48
			    && target[32] == 0x8B && target[33] == 0xD9;
		}

		bool hk_processing_start(CNetShopTransaction* self)
		{
			const auto original = reinterpret_cast<ProcessingStartFn>(g_original_processing_start);
			if (!self)
				return original(self);

			auto* base       = reinterpret_cast<std::uint8_t*>(self);
			auto* status_ptr = reinterpret_cast<volatile LONG*>(base + 0x1C);
			const auto status = InterlockedCompareExchange(status_ptr, 0, 0);

		    // Just hardcode this to true for now. It'll be tunable based if I ever get this working properly.
			const auto force_null_transaction = true;
			if (!force_null_transaction)
				return original(self);

			LOG(INFO) << "ProcessingStart before NULL transaction: this=" << self << " status=" << status << " checkout=" << static_cast<int>(base[0x2C])
			           << " nullTransaction=" << static_cast<int>(base[0x2D]) << " isProcessing=" << static_cast<int>(base[0x2E]);

			base[0x2C] = 0;
			base[0x2D] = 1;
			base[0x2E] = 1;
			InterlockedExchange(status_ptr, 1);

			LOG(INFO) << "ProcessingStart after NULL transaction: this=" << self << " status=" << InterlockedCompareExchange(status_ptr, 0, 0)
			           << " checkout=" << static_cast<int>(base[0x2C]) << " nullTransaction=" << static_cast<int>(base[0x2D])
			           << " isProcessing=" << static_cast<int>(base[0x2E]);

			return true;
		}

		void install_processing_start_hook()
		{
			if (g_processing_start_target)
				return;

			const memory::module module(GetCurrentModule());
			if (!module.loaded())
			{
				LOG(WARNING) << "Cannot locate ProcessingStart: current module is not loaded";
				return;
			}

			const auto matches = module.scan_all(memory::pattern(kProcessingStartPattern));
			if (matches.size() != 1)
			{
				LOG(WARNING) << "Expected one ProcessingStart match, found " << matches.size();
				return;
			}

			auto* target = matches.front().as<std::uint8_t*>();
			if (!validate_processing_start(target))
			{
				LOG(WARNING) << "ProcessingStart structural validation failed";
				return;
			}

			if (const auto status = MH_CreateHook(target, reinterpret_cast<void*>(&hk_processing_start), &g_original_processing_start); status != MH_OK)
			{
				LOG(WARNING) << "Failed to create ProcessingStart hook (error: " << MH_StatusToString(status) << ")";
				g_original_processing_start = nullptr;
				return;
			}

			if (const auto status = MH_EnableHook(target); status != MH_OK)
			{
				LOG(WARNING) << "Failed to enable ProcessingStart hook (error: " << MH_StatusToString(status) << ")";
				MH_RemoveHook(target);
				g_original_processing_start = nullptr;
				return;
			}

			g_processing_start_target = target;
			LOG(INFO) << "Hooked ProcessingStart";
		}

		void uninstall_processing_start_hook()
		{
			if (!g_processing_start_target)
				return;

			if (const auto status = MH_DisableHook(g_processing_start_target); status != MH_OK && status != MH_ERROR_DISABLED)
				LOG(WARNING) << "Failed to disable ProcessingStart hook (error: " << MH_StatusToString(status) << ")";

			if (const auto status = MH_RemoveHook(g_processing_start_target); status != MH_OK)
				LOG(WARNING) << "Failed to remove ProcessingStart hook (error: " << MH_StatusToString(status) << ")";

			g_processing_start_target = nullptr;
			g_original_processing_start = nullptr;
		}

		bool validate_update(const std::uint8_t* target)
		{
			return target[4] == 0x8B && target[5] == 0x51 && target[6] == 0x1C && target[12] == 0x80 && target[13] == 0x79 && target[14] == 0x2E
			    && target[18] == 0xC6 && target[19] == 0x41 && target[20] == 0x2E && target[21] == 0x00 && target[28] == 0x89 && target[29] == 0x41
			    && target[30] == 0x30 && target[31] == 0x83 && target[32] == 0xFA && target[33] == 0x03;
		}

		void hk_update(CNetShopTransactionBase* self)
		{
			if (!self)
			{
				g_original_update(self);
				return;
			}

			auto* base       = reinterpret_cast<std::uint8_t*>(self);
			auto* status_ptr = reinterpret_cast<volatile LONG*>(base + 0x1C);

			if (base[0x2D] != 0 && InterlockedCompareExchange(status_ptr, 0, 0) == 1)
			{
				*reinterpret_cast<std::int32_t*>(base + 0x20) = 0;
				InterlockedExchange(status_ptr, 3);
			}

			g_original_update(self);
		}

		void install_update_hook()
		{
			if (g_update_target)
				return;

			const memory::module module(GetCurrentModule());
			if (!module.loaded())
			{
				LOG(WARNING) << "Cannot locate CNetShopTransactionBase::Update: current module is not loaded";
				return;
			}

			const auto matches = module.scan_all(memory::pattern(kUpdatePattern));
			if (matches.size() != 1)
			{
				LOG(WARNING) << "Expected one CNetShopTransactionBase::Update match, found " << matches.size();
				return;
			}

			auto* target = matches.front().as<std::uint8_t*>();
			if (!validate_update(target))
			{
				LOG(WARNING) << "CNetShopTransactionBase::Update structural validation failed";
				return;
			}

			if (const auto status = MH_CreateHook(target, reinterpret_cast<void*>(&hk_update), reinterpret_cast<void**>(&g_original_update)); status != MH_OK)
			{
				LOG(WARNING) << "Failed to create CNetShopTransactionBase::Update hook (error: " << MH_StatusToString(status) << ")";
				g_original_update = nullptr;
				return;
			}

			if (const auto status = MH_EnableHook(target); status != MH_OK)
			{
				LOG(WARNING) << "Failed to enable CNetShopTransactionBase::Update hook (error: " << MH_StatusToString(status) << ")";
				MH_RemoveHook(target);
				g_original_update = nullptr;
				return;
			}

			g_update_target = target;
			LOG(INFO) << "Hooked CNetShopTransactionBase::Update";
		}

		void uninstall_update_hook()
		{
			if (!g_update_target)
				return;

			if (const auto status = MH_DisableHook(g_update_target); status != MH_OK && status != MH_ERROR_DISABLED)
				LOG(WARNING) << "Failed to disable CNetShopTransactionBase::Update hook (error: " << MH_StatusToString(status) << ")";

			if (const auto status = MH_RemoveHook(g_update_target); status != MH_OK)
				LOG(WARNING) << "Failed to remove CNetShopTransactionBase::Update hook (error: " << MH_StatusToString(status) << ")";

			g_update_target = nullptr;
			g_original_update = nullptr;
		}

		LONG WINAPI should_do_null_transaction_veh(EXCEPTION_POINTERS* exception_info)
		{
			if (!g_enabled || !exception_info || !exception_info->ExceptionRecord || !exception_info->ContextRecord)
				return EXCEPTION_CONTINUE_SEARCH;

			if (exception_info->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT
			    || (reinterpret_cast<std::uintptr_t>(exception_info->ExceptionRecord->ExceptionAddress) != g_target
			        && exception_info->ContextRecord->Rip != g_target + 1))
				return EXCEPTION_CONTINUE_SEARCH;

			const auto return_address_ptr = reinterpret_cast<const std::uintptr_t*>(exception_info->ContextRecord->Rsp);
		    /*
			if (IsBadReadPtr(return_address_ptr, sizeof(*return_address_ptr)))
			{
				LOG(INFO) << "ShouldDoNullTransaction caller RVA: <unavailable>";
			}
			else
			{
				const auto return_address = *return_address_ptr;
				if (return_address >= g_module_begin && return_address < g_module_end)
					LOG(INFO) << "ShouldDoNullTransaction caller RVA: 0x" << std::hex << (return_address - g_module_begin);
				else
					LOG(INFO) << "ShouldDoNullTransaction caller: 0x" << std::hex << return_address << ", caller RVA: <outside module>";
			}
			*/

			exception_info->ContextRecord->Rip = reinterpret_cast<std::uintptr_t>(&hooks::should_do_null_transaction);
			return EXCEPTION_CONTINUE_EXECUTION;
		}

		void patch_caller(caller_patch& caller)
		{
			if (caller.patch)
				return;

			const auto match = memory::module(GetCurrentModule()).scan(memory::pattern(caller.signature));
			if (!match)
			{
				LOG(WARNING) << "Failed to find " << caller.name;
				return;
			}

			auto* call_address = match->add(caller.call_offset).as<std::uint8_t*>();
			if (*call_address != 0xE8)
			{
				LOG(WARNING) << "Unexpected " << caller.name << " call instruction";
				return;
			}

			caller.patch = memory::byte_patch::make(call_address, kReturnTruePatch).get();
			caller.patch->apply();
			LOG(INFO) << "Patched " << caller.name;
		}

		void unpatch_caller(caller_patch& caller)
		{
			if (!caller.patch)
				return;

			caller.patch->restore();
			caller.patch->remove();
			caller.patch = nullptr;
		}
	}

	__declspec(noinline) bool hooks::should_do_null_transaction(void* instance)
	{
		const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));

		const auto shopping_mgr = *reinterpret_cast<void**>(base + kNetworkShoppingMgrSingletonRva);

		return shopping_mgr != nullptr && instance == shopping_mgr;
	}

	void install_should_do_null_transaction_veh()
	{
		if (g_veh_handle)
			return;

		const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
		const memory::module current_module(GetCurrentModule());
		g_module_begin = current_module.begin().as<std::uintptr_t>();
		g_module_end = current_module.end().as<std::uintptr_t>();
		g_target = base + kShouldDoNullStubRva;

		auto* target = reinterpret_cast<std::uint8_t*>(g_target);
		if (target[0] != 0x32 || target[1] != 0xC0 || target[2] != 0xC3)
		{
			LOG(WARNING) << "Unexpected ShouldDoNullTransaction stub";
			g_target = 0;
			return;
		}

		g_veh_handle = AddVectoredExceptionHandler(1, should_do_null_transaction_veh);
		if (!g_veh_handle)
		{
			LOG(WARNING) << "Failed to install ShouldDoNullTransaction VEH";
			g_target = 0;
			return;
		}

		g_patch = memory::byte_patch::make(target, std::uint8_t{0xCC}).get();
		g_enabled = true;
		g_patch->apply();
		LOG(INFO) << "Hooked ShouldDoNullTransaction with VEH";
	}

	void uninstall_should_do_null_transaction_veh()
	{
		if (g_patch)
		{
			g_patch->restore();
			g_patch->remove();
			g_patch = nullptr;
		}

		g_enabled = false;

		if (g_veh_handle)
		{
			RemoveVectoredExceptionHandler(g_veh_handle);
			g_veh_handle = nullptr;
		}

		g_target = 0;
		g_module_begin = 0;
		g_module_end = 0;
	}

	void install_null_transaction_patches()
	{
		install_processing_start_hook();
		install_update_hook();

		for (auto& caller : caller_patches)
			patch_caller(caller);

		const auto match = memory::module(GetCurrentModule()).scan(memory::pattern("83 79 1C 03 75 ? 80 79 2D 00 74 ? B0 01 EB ?"));
		g_apply_data_to_stats_patch = memory::byte_patch::make(match->add(10).as<std::uint8_t*>(), kApplyDataToStatsPatch).get();
		g_apply_data_to_stats_patch->apply();
	}

	void uninstall_null_transaction_patches()
	{
		uninstall_update_hook();
		uninstall_processing_start_hook();

		if (g_apply_data_to_stats_patch)
		{
			g_apply_data_to_stats_patch->restore();
			g_apply_data_to_stats_patch->remove();
			g_apply_data_to_stats_patch = nullptr;
		}

		for (auto& caller : caller_patches)
			unpatch_caller(caller);
	}
}
