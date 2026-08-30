#include "native_registration.hpp"

#include "pointers.hpp"
#include "services/matchmaking_networking/matchmaking_networking.hpp"

namespace big
{
	void native_registration::init()
	{
		auto* command_hash = reinterpret_cast<scrCommandHash<scrCmd>*>(g_pointers->m_gta.m_native_registration_table);
		if (!command_hash)
		{
			LOG(FATAL) << "Native registration table is unavailable";
			return;
		}

		command_hash->Insert(0x7D2E9B14F0A6C385ULL, matchmaking_networking::report_matchmaking_kill);
		command_hash->Insert(0x2ED4B9D53994053EULL, matchmaking_networking::send_matchmaking_heartbeat);
	}
}
