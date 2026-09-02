#include "view_debug.hpp"

#include "hooks/Anticheat/UnpackHandler.hpp"
#include "services/gui/gui_service.hpp"

namespace big
{
	void parapak()
	{
		if (ImGui::BeginTabItem("ParaPak"_T.data()))
		{
			components::sub_title("ParaPak"_T);
			if (ImGui::Button("Generate ParaPak"))
			{
				UnpackHandler::CompareTextSnapshot();
				UnpackHandler::DoExport();
			}
			if (ImGui::Button("Reload ParaPak for game ver"))
			{
				UnpackHandler::DoImport();
			}
			ImGui::EndTabItem();
		}
	}
	void debug::main()
	{
		if (strcmp(g_gui_service->get_selected()->name, "GUI_TAB_DEBUG"))
			return;

		if (ImGui::Begin("DEBUG_WINDOW"_T.data()))
		{
			ImGui::BeginTabBar("debug_tabbar");
			misc();
#if ENABLE_TOXIC_CHEATS
			logs();
#endif
			tunables();
#if ENABLE_TOXIC_CHEATS
			globals();
			locals();
			script_events();
#endif
			scripts();
#if ENABLE_TOXIC_CHEATS
			threads();
#endif
			parapak();
		}
		ImGui::End();
	}
}
