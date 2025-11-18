#include "views/view.hpp"
#include "lua/lua_manager.hpp"

namespace big
{
	void view::root()
	{
		view::heading();
		view::navigation();
        #if ENABLE_TOXIC_CHEATS
		view::players();
		#endif
		view::active_view();

		debug::main();

		if (g_lua_manager)
			g_lua_manager->draw_independent_gui();

		if (g.window.demo) // It is not the YimMenu way.
			ImGui::ShowDemoWindow(&g.window.demo);
	}
}
