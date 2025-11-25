#include "gta/fidevice.hpp"
#include "hooking/hooking.hpp"

namespace big
{
	bool hooks::fipackfile_open_archive(rage::fiPackfile* this_, const char* archive, bool b_true, int type, intptr_t very_false)
	{
		if (archive) LOG(VERBOSE) << archive;
		if (strcmp(archive, "update/update.rpf") == 0)
		{
			return g_hooking->get_original<fipackfile_open_archive>()(this_, "update/versions/2699/update.rpf", b_true, type, very_false);
		}
		if (strcmp(archive, "update/update2.rpf") == 0)
		{
			return g_hooking->get_original<fipackfile_open_archive>()(this_, "update/versions/2699/update2.rpf", b_true, type, very_false);
		}
		return g_hooking->get_original<fipackfile_open_archive>()(this_, archive, b_true, type, very_false);
	}
}
