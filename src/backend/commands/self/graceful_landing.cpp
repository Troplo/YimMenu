#include "backend/bool_command.hpp"

namespace big
{
	#if ENABLE_TOXIC_CHEATS
	bool_command g_graceful_landing("gracefullanding", "GRACEFUL_LANDING", "GRACEFUL_LANDING_DESC", g.self.graceful_landing);
	#endif
}