#include "byte_patch_manager.hpp"

#include "backend/commands/weapons/no_sway.hpp"
#include "gta/net_array.hpp"
#include "memory/byte_patch.hpp"
#include "memory/module.hpp"
#include "memory/pattern.hpp"
#include "pointers.hpp"
#include "util/command_line.hpp"
#include "util/current_module.hpp"
#include "util/explosion_anti_cheat_bypass.hpp"
#include "util/vehicle.hpp"
#include "util/world_model.hpp"

extern "C" void sound_overload_detour();
uint64_t g_sound_overload_ret_addr;

namespace big
{
	static void init()
	{
		// Disable alt enter fullsreen
		if (!command_line::has_argument(L"-enableAltEnter"))
		{
			const memory::module module(GetCurrentModule());

			auto* match = module.scan(memory::pattern("48 83 FE 0D 75 22 E8 ? ? ? ? 3B 05 ? ? ? ? 76 2D E8 ? ? ? ? E8 ? ? ? ? 05 D0 07 00 00 89 05 ? ? ? ?"))
			                  .value()
			                  .as<std::uint8_t*>();

			memory::byte_patch::make(match + 0x04, std::uint8_t{0xEB})->apply();
		}

		// Disable BLIP_CHANGE_FLASH, makes it easier to see exactly when you respawn
	    if (command_line::is_pvp_patch_enabled()) {
	        auto* blip_change_flash = memory::module(GetCurrentModule())
                                     .scan(memory::pattern("8B 15 ? ? ? ? B9 11 00 00 00 E8 ? ? ? ? 8B 15 ? ? ? ? B9 16 00 00 00"))
                                     .value()
                                     .as<std::uint8_t*>();
	        memory::byte_patch::make(blip_change_flash + 0x0B, std::array<uint8_t, 5>{0x90, 0x90, 0x90, 0x90, 0x90})->apply();

	        // Patches to prevent sticky bombs from disappearing if two are thrown by two players at the exact same time. Changes projectile.clear(true) calls to projectile.clear(false).
	        // That makes the projectiles not get deleted from existence...
			const auto module_name = GetCurrentModule();
			const memory::module module(module_name);

			auto* move_net_sync_projectile = module.scan(memory::pattern("48 8B D3 E8 ? ? ? ? B2 01 48 8B CB E8 ? ? ? ? B0 01"))
			                                    .value()
			                                    .as<std::uint8_t*>();
			auto* second_clear = module.scan(memory::pattern("84 C0 75 ? 48 8D 8F 10 01 00 00 B2 01 E8 ? ? ? ? 48 8B 6C 24 58"))
			                       .value()
			                       .as<std::uint8_t*>();

		    memory::byte_patch::make(move_net_sync_projectile + 0x09, std::uint8_t{0})->apply();
			memory::byte_patch::make(second_clear + 0x0C, std::uint8_t{0})->apply();
		}

	    // patches out m_lodLightsEnabled in CLODLights::Init
		if (command_line::has_argument(L"-nolodlights"))
		{
			const memory::module module(GetCurrentModule());

			auto* match = module.scan(memory::pattern("33 D2 88 0D ? ? ? ? 24 FE 48 8D 0D ? ? ? ?"))
			                  .value()
			                  .as<std::uint8_t*>();
			auto* instruction = match + 2;

			memory::byte_patch::make(instruction, std::array<uint8_t, 6>{0x90, 0x90, 0x90, 0x90, 0x90, 0x90})->apply();
		}

		// Patch World Model Spawn Bypass
		std::array<uint8_t, 24> world_spawn_patch;
		std::fill(world_spawn_patch.begin(), world_spawn_patch.end(), 0x90);
		world_model_bypass::m_world_model_spawn_bypass =
		    memory::byte_patch::make(g_pointers->m_gta.m_world_model_spawn_bypass, world_spawn_patch).get();

		// Patch blocked explosions
		explosion_anti_cheat_bypass::m_can_blame_others =
		    memory::byte_patch::make(g_pointers->m_gta.m_blame_explode.as<uint16_t*>(), 0xE990).get();
		explosion_anti_cheat_bypass::m_can_use_blocked_explosions =
		    memory::byte_patch::make(g_pointers->m_gta.m_explosion_patch.sub(12).as<uint16_t*>(), 0x9090).get();

		// Skip matchmaking session validity checks
		memory::byte_patch::make(g_pointers->m_gta.m_is_matchmaking_session_valid.as<void*>(), std::to_array({0xB0, 0x01, 0xC3}))
		    ->apply(); // has no observable side effects

		// Bypass netarray buffer cache when enabled
		broadcast_net_array::m_patch =
		    memory::byte_patch::make(g_pointers->m_gta.m_broadcast_patch.as<uint8_t*>(), 0xEB).get();

		// Disable cheat activated netevent when creator warping
		memory::byte_patch::make(g_pointers->m_gta.m_creator_warp_cheat_triggered_patch.as<uint8_t*>(), 0xEB)->apply();

		// Disable collision when enabled
		vehicle::disable_collisions::m_patch =
		    memory::byte_patch::make(g_pointers->m_gta.m_disable_collision.sub(2).as<uint8_t*>(), 0xEB).get();

		// Crash Trigger
		memory::byte_patch::make(g_pointers->m_gta.m_crash_trigger.add(4).as<uint8_t*>(), 0x00)->apply();

		// Script VM patches

		memory::byte_patch::make(g_pointers->m_gta.m_script_vm_patch_1.add(2).as<uint32_t*>(), 0xc9310272)->apply();
		memory::byte_patch::make(g_pointers->m_gta.m_script_vm_patch_1.add(6).as<uint16_t*>(), 0x9090)->apply();

		memory::byte_patch::make(g_pointers->m_gta.m_script_vm_patch_2.add(2).as<uint32_t*>(), 0xc9310272)->apply();
		memory::byte_patch::make(g_pointers->m_gta.m_script_vm_patch_2.add(6).as<uint16_t*>(), 0x9090)->apply();

		memory::byte_patch::make(g_pointers->m_gta.m_script_vm_patch_3.add(2).as<uint32_t*>(), 0xd2310272)->apply();
		memory::byte_patch::make(g_pointers->m_gta.m_script_vm_patch_3.add(6).as<uint16_t*>(), 0x9090)->apply();

		memory::byte_patch::make(g_pointers->m_gta.m_script_vm_patch_4.add(2).as<uint32_t*>(), 0xd2310272)->apply();
		memory::byte_patch::make(g_pointers->m_gta.m_script_vm_patch_4.add(6).as<uint16_t*>(), 0x9090)->apply();

		memory::byte_patch::make(g_pointers->m_gta.m_script_vm_patch_5.add(2).as<uint32_t*>(), 0xd2310272)->apply();
		memory::byte_patch::make(g_pointers->m_gta.m_script_vm_patch_5.add(6).as<uint16_t*>(), 0x9090)->apply();

		memory::byte_patch::make(g_pointers->m_gta.m_script_vm_patch_6.add(2).as<uint32_t*>(), 0xd2310272)->apply();
		memory::byte_patch::make(g_pointers->m_gta.m_script_vm_patch_6.add(6).as<uint16_t*>(), 0x9090)->apply();

		// Patch script network check
		//memory::byte_patch::make(g_pointers->m_gta.m_model_spawn_bypass, std::vector{0x90, 0x90})->apply(); // this is no longer integrity checked

		// Increase Start Get Presence Attributes limit from 32 to 100
		// memory::byte_patch::make(g_pointers->m_sc.m_num_handles_patch, std::vector{0x64})->apply();

		// Prevent the attribute task from failing
		// memory::byte_patch::make(g_pointers->m_sc.m_read_attribute_patch, std::vector{0x90, 0x90})->apply();
		// memory::byte_patch::make(g_pointers->m_sc.m_read_attribute_patch_2, std::vector{0xB0, 0x01})->apply();

		// Prevent the game from crashing when flooded with outgoing events
		memory::byte_patch::make(g_pointers->m_gta.m_free_event_error, std::vector{0x90, 0x90, 0x90, 0x90, 0x90})->apply();

		// Always send the special ability event
		memory::byte_patch::make(g_pointers->m_gta.m_activate_special_ability_patch, std::to_array({0xB0, 0x01, 0xC3}))->apply();

		weapons::m_no_sway_patch = memory::byte_patch::make(g_pointers->m_gta.m_scope_sway_function, std::vector{0xEB}).get();

		memory::byte_patch::make(g_pointers->m_gta.m_report_myself_sender, std::vector{0xC3})->apply();

		// Patch BattlEye network bail
		memory::byte_patch::make(g_pointers->m_gta.m_be_network_bail_patch, std::to_array({0xC3}))->apply();
	}

	byte_patch_manager::byte_patch_manager()
	{
		init();

		g_byte_patch_manager = this;
	}

	byte_patch_manager::~byte_patch_manager()
	{
		memory::byte_patch::restore_all();

		g_byte_patch_manager = nullptr;
	}
}
