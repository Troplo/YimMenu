//
// Created by Troplo on 20/11/2025.
//

#include "RgscRegistration.hpp"
#if RGSC_ENABLED
#include "gta_pointers.hpp"
#include "pointers.hpp"
#include "rgsc/rlpc.h"

namespace big {
	bool RgscRegistration::m_initialized = false;
	rgsc::IPresenceManagerV14* RgscRegistration::m_presence = nullptr;
	rgsc::IParagonV3* RgscRegistration::m_paragon = nullptr;
	const char* RgscRegistration::m_sdkVersion = "Unknown";

	RgscRegistration::RgscRegistration()
	{
		if (m_initialized)
		{
			return;
		}
		if (!g_rlPc)
		{
			LOG(WARNING) << "RLPC not initialized!";
			return;
		}
		rgsc::IParagonV3* paragon = NULL;
		rgsc::RGSC_HRESULT hr = g_rlPc->m_Rgsc->GetParagon()->QueryInterface(rgsc::IID_IParagonV3, (void**)&paragon);
		if (hr == rgsc::RGSC_OK)
		{
			m_paragon = paragon;
		}
		else
		{
			LOG(WARNING) << "Paragon SDK does not support IParagonV3 interface!";
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
		if (!paragon->SetEventDelegateV2(
			rgsc::IParagonV1::PARAGON_PRESENCE_EVENT_LAUNCHER_STOP_GAME,
			&RgscRegistration::OnParagonEvent,
			(void*)&CLOSE_EVT))
		{
			LOG(WARNING) << "Failed to register event delegate";
		}

		rgsc::IPresenceManagerV14* presence = NULL;
		rgsc::RGSC_HRESULT hrP = g_rlPc->m_Rgsc->GetPresenceManager()->QueryInterface(rgsc::IID_IPresenceManagerV14, (void**) &presence);
		if (hrP == rgsc::RGSC_OK)
		{
			m_presence = presence;
		} else
		{
			LOG(WARNING) << "Paragon SDK does not support IPresenceV14 interface!";
			return;
		}
		m_initialized = true;
	}

	const char* RgscRegistration::GetSdkVersion()
	{
		return m_sdkVersion;
	}
	void RgscRegistration::OnParagonEvent(void* userData, const char* payload)
	{
		if (userData)
		{
			int eventType = *(int*)userData;
			switch (eventType)
			{
				case CLOSE_EVT:
				{
					LOG(INFO) << "Received launcher stop game event!";
					std::exit(0);
					break;
				}
			default: {}
			}
		}
	}
} // big
#endif
