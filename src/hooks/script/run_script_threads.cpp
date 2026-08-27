#include "hooking/hooking.hpp"
#include "native_hooks/native_hooks.hpp"
#include "script_mgr.hpp"

namespace big
{
	bool hooks::run_script_threads(uint32_t ops_to_execute)
	{
		if (g_running) [[likely]]
		{
			g_script_mgr.tick();
		}

		return g_hooking->get_original<run_script_threads>()(ops_to_execute);
	}

	rage::scrNativeHash get_community_hash_from_game_hash(rage::scrNativeHash hash)
	{
		// for (const rage::scrNativePair& mapping : g_crossmap)
		// {
		// 	if (mapping.second == hash)
		// 	{
		// 		return mapping.first;
		// 	}
		// }

		return hash;
	}

	void hooks::create_native(void* a1, rage::scrNativeHash native_hash, rage::scrNativeHandler native_handler)
	{
		LOG(INFO) << "Registering 0x" << std::hex << native_hash;
		rage::scrNativeHash community_hash = get_community_hash_from_game_hash(native_hash);

		g_native_invoker.add_native_handler(community_hash, native_handler);

		auto hooked_handler = g_native_hooks->get_hooked_handler(community_hash);
		if (hooked_handler.has_value())
		{
			LOG(INFO) << "Handler for above";
			native_handler = hooked_handler.value();
		}

		g_hooking->get_original<hooks::create_native>()(a1, native_hash, native_handler);
	}
}
