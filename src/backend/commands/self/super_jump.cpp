#include "backend/bool_command.hpp"

namespace big
{
	#if ENABLE_TOXIC_CHEATS
	bool_command g_super_jump("superjump", "SUPER_JUMP", "SUPER_JUMP_DESC", g.self.super_jump);
	#endif
}