#ifndef NATIVE_REGISTRATION_HPP
#define NATIVE_REGISTRATION_HPP
#include "script/scrNativeHandler.hpp"

namespace big
{
	class native_registration {
	public:
		static void call_test(rage::scrNativeCallContext* ctx);
		static void init();
	};
}


#endif //NATIVE_REGISTRATION_HPP
