#include "backend/command.hpp"
#include "natives.hpp"

#if ENABLE_TOXIC_CHEATS
namespace big
{
	class suicide : command
	{
		using command::command;

		virtual void execute(const command_arguments&, const std::shared_ptr<command_context> ctx) override
		{
			ENTITY::SET_ENTITY_HEALTH(self::ped, 0, 0, 0);
		}
	};

	suicide g_suicide("suicide", "SUICIDE", "SUICIDE_DESC", 0);
}
#endif
