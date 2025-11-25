//
// Created by Troplo on 20/11/2025.
//

#ifndef RGSCREGISTRATION_HPP
#define RGSCREGISTRATION_HPP
#include "rgsc/public_interface/paragon_interface.h"
#include "rgsc/public_interface/presence_interface.h"

namespace big {

class RgscRegistration {
private:
	static bool m_initialized;
	static rgsc::IParagonV2* m_paragon;
	static rgsc::IPresenceManagerV14* m_presence;
	static const char* m_sdkVersion;
public:
	RgscRegistration();

	static const char* GetSdkVersion();
};

} // big

#endif //RGSCREGISTRATION_HPP
