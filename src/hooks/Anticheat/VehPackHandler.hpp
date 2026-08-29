//
// Created by Troplo on 16/07/2026.
//

#ifndef VEHPACKHANDLER_H
#define VEHPACKHANDLER_H
#include <complex.h>
#include <winnt.h>

namespace big
{
	class VehPackHandler
	{
	public:
		static void InitializeVehHooks(void* pointerLoadTarget, void* execTarget = nullptr);

	private:
		static LONG WINAPI VehHandler(EXCEPTION_POINTERS* info);

		static uintptr_t m_PointerLoadTarget;
		static uintptr_t m_ExecTarget;
		static void* m_VehHandle;
		static bool m_Complete;

		struct PageProtectionInfo
		{
			DWORD originalProtect;
			DWORD restrictedProtect;
		};

		static std::unordered_map<uintptr_t, PageProtectionInfo> m_pageMap;
		static thread_local uintptr_t t_rearmPage;
		static thread_local bool t_replacePointer; // Added this
	};
}

#endif //VEHPACKHANDLER_H