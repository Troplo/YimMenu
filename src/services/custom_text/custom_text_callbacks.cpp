#include "custom_text_service.hpp"
#include "hooking/hooking.hpp"
#include "pointers.hpp"
#include "services/paragon/rgsc/RgscRegistration.hpp"

namespace big
{
	const char* respawn_label_callback(const char* label)
	{
		if (g.self.god_mode)
			return "~r~Dying with god mode, how?";

		return nullptr;
	}

	const char* do_ceo_name_resize(const char* label)
	{
		auto original = g_hooking->get_original<hooks::get_label_text>()(g_pointers->m_gta.m_ctext_file_ptr, label);
		if (auto pos = strstr((char*)original, "15"))
		{
			pos[0] = '4';
			pos[1] = '1';
		}
		return original;
	}

	const char* paragon_banned_ros() {
	    return "We were unable to log you into Paragon, please make sure the Paragon Launcher is running, or have an up-to-date launch.bat file.  You may need to add Paragon to the Windows Defender exclusion list if a restart does not fix the issue. Contact support if the issue persists.";
	}

	const char* paragon_activation_description() {
		return "Please ensure that Paragon is able to write files to your system. You may need to add Paragon to the Windows Defender exclusion list if a restart does not fix the issue. Contact support if the issue persists.";
	}

	const char* paragon_activation_header()
	{
		return "Something went wrong!";
	}

	const char* paragon_online_build(const char* label)
	{
		return std::format("Paragon SDK: {} (Legacy Edition) Online:",
					   RgscRegistration::GetSdkVersion()).c_str();
	}

	const char* paragon_game_build()
	{
		return std::format("Build:").c_str();
	}
}
