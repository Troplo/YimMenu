#pragma once
#include "common.hpp"

namespace big
{
	class backend
	{
	public:
		static void loop();
		static void self_loop();
		static void ambient_animations_loop();
		#if ENABLE_TOXIC_CHEATS
		static void weapons_loop();
		static void vehicles_loop();
		#endif
		static void misc_loop();
		static void remote_loop();
        #if ENABLE_TOXIC_CHEATS
		static void rainbowpaint_loop();
		#endif
		static void disable_control_action_loop();
        #if ENABLE_TOXIC_CHEATS
		static void world_loop();
		static void orbital_drone();
		#endif
		static void vehicle_control();
		static void tunables_script();
        #if ENABLE_TOXIC_CHEATS
		static void squad_spawner();
		#endif
	};
}
