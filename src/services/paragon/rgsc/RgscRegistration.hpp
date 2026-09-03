//
// Created by Troplo on 20/11/2025.
//

#ifndef RGSCREGISTRATION_HPP
#define RGSCREGISTRATION_HPP
#define RGSC_ENABLED 1

#if RGSC_ENABLED
#include "rgsc/public_interface/paragon_interface.h"
#include "rgsc/public_interface/presence_interface.h"


namespace big {

class RgscRegistration {
private:
	constexpr static int CLOSE_EVT = 1;

	static bool m_initialized;
	static rgsc::IParagonV3* m_paragon;
	static rgsc::IPresenceManagerV14* m_presence;
	static const char* m_sdkVersion;
public:
	RgscRegistration();

	static const char* GetSdkVersion();
	static void RGSC_CALL OnParagonEvent(
		void* userData,
		const char* payload
	);
};

} // big
#endif

#endif //RGSCREGISTRATION_HPP
