#include "custom_text_callbacks.hpp"

#include "rgsc/paragon/RgscRegistration.hpp"

namespace big
{
	const char* respawn_label_callback(const char* label)
	{
		if (g.self.god_mode)
			return "~r~Dying with god mode, how?";

		return nullptr;
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
#if ENABLE_PARAGON_SDK
		return std::format("Paragon SDK: {} (Legacy Edition) Online:",
					   RgscRegistration::GetSdkVersion()).c_str();
#else
		return std::format("Paragon SDK: {} (Legacy Edition) Online:", "Unknown").c_str();
#endif
	}

	const char* paragon_game_build()
	{
		return std::format("Build:").c_str();
	}
}
