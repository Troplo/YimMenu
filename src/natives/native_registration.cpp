#include "native_registration.hpp"

#include "pointers.hpp"

namespace big
{
	void native_registration::call_test(rage::scrNativeCallContext* ctx)
	{
		LOG(INFO) << "Test native";
	}

	void native_registration::init()
	{
		// LOG(INFO) << "Check for native rego (valid)" << g_pointers->m_gta.m_command_hash->Lookup(0x0D94071E55F4C9CE);
		// LOG(INFO) << "Check for native rego (invalid)" << g_pointers->m_gta.m_command_hash->Lookup(0x019FA71E55F4C9C6);
		// g_pointers->m_gta.m_command_hash->Insert(0x391c5280f4245775, call_test);
	}
}