//
// Created by Troplo on 20/11/2025.
//

#include "RgscRegistration.hpp"

#include "gta_pointers.hpp"
#include "pointers.hpp"
#include "rgsc/rlpc.h"

namespace big {
	bool RgscRegistration::m_initialized = false;
	rgsc::IPresenceManagerV14* RgscRegistration::m_presence = nullptr;
	rgsc::IParagonV2* RgscRegistration::m_paragon = nullptr;
	const char* RgscRegistration::m_sdkVersion = "Unknown";

	RgscRegistration::RgscRegistration()
	{
		return;

		if (m_initialized)
		{
			return;
		}
		if (!g_rlPc)
		{
			LOG(WARNING) << "RLPC not initialized!";
			return;
		}
		rgsc::IParagonV2* paragon = NULL;
		rgsc::RGSC_HRESULT hr = g_rlPc->m_Rgsc->GetParagon()->QueryInterface(rgsc::IID_IParagonV2, (void**)&paragon);
		if (hr == rgsc::RGSC_OK)
		{
			m_paragon = paragon;
		}
		else
		{
			LOG(WARNING) << "Paragon SDK does not support IParagonV2 interface!";
			return;
		}


		const char* sdkVersion = paragon->GetParagonSdkVersion();
		LOG(INFO) << "Paragon SDK Version: " << sdkVersion;
		m_sdkVersion = sdkVersion;
		const bool launcherInUse = paragon->GetUsingLauncher();
		LOG(INFO) << "Paragon Launcher In Use: " << launcherInUse;
		if (launcherInUse)
		{
			const bool launcherConnected = paragon->GetLauncherConnected();
			LOG(INFO) << "Paragon Launcher Connected: " << launcherConnected;
		}

		paragon->SetGameVersion(g_pointers->m_gta.m_game_version);
		paragon->SetOnlineVersion(g_pointers->m_gta.m_online_version);
		paragon->SetParagonClientVersion("1.1.0");
		// paragon->SetEventDelegate(rgsc::IParagonV1::PARAGON_PRESENCE_EVENT_LAUNCHER_STOP_GAME, [this](const std::string& _) {
		// 	LOG(INFO) << "Received launcher stop game event!";
		// 	exit(0);
		// });
		// PRES
		// rgsc::IPresenceManagerV14* presence = NULL;
		// rgsc::RGSC_HRESULT hrP = g_rlPc->m_Rgsc->GetPresenceManager()->QueryInterface(rgsc::IID_IPresenceManagerV14, (void**) &presence);
		// if (hrP == rgsc::RGSC_OK)
		// {
		// 	m_presence = presence;
		// } else
		// {
		// 	LOG(WARNING) << "Paragon SDK does not support IPresenceV14 interface!";
		// 	return;
		// }
		m_initialized = true;
	}

	const char* RgscRegistration::GetSdkVersion()
	{
		return m_sdkVersion;
	}
} // big