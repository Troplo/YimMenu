#ifndef RLPC_H
#define RLPC_H
#include "./public_interface/rgsc_interface.h"
#include "./public_interface/achievements_interface.h"
#include "./public_interface/profiles_interface.h"
#include "./public_interface/gamerpics_interface.h"
#include "./public_interface/pad_interface.h"
#include "./public_interface/players_interface.h"
#include "./public_interface/activation_interface.h"
#include "./public_interface/patching_interface.h"
#include "./public_interface/file_system_interface.h"
#include "./public_interface/rgsc_ui_interface.h"
#include "./public_interface/presence_interface.h"
#include "./public_interface/telemetry_interface.h"
#include "./public_interface/network_interface.h"
#include "./public_interface/cloudsave_interface.h"
// fuck
#include "windows.h"


class rlPc
{
public:
	char m_Delegator[32];
	int m_SocialClubVersionNum[4];
	char m_SocialClubVersion[32];
	bool m_bHasScuiRequestedQuit;
	rgsc::IRgscV20* m_Rgsc;
	rgsc::IProfileManagerV2* m_ProfileManager;
	rgsc::IGamepadManagerV3* m_GamepadManager;
	rgsc::IAchievementManagerV3* m_AchievementManager;
	rgsc::IPlayerManagerV2* m_PlayerManager;
	rgsc::IPresenceManagerV4* m_PresenceManager;
	rgsc::ICommerceManagerV4* m_CommerceManager;
	rgsc::IActivationV2* m_ActivationSystem;
	rgsc::IPatchingV1* m_PatchingSystem;
	rgsc::IFileSystemV3* m_FileSystem;
	rgsc::IRgscUiV7* m_RgscUi;
	rgsc::ITaskManagerV1* m_TaskManager;
	rgsc::ITelemetryV3* m_Telemetry;
	rgsc::INetworkV1* m_NetworkInterface;
	rgsc::ICloudSaveManagerV3* m_CloudSaveManager;
	rgsc::IGamerPicManagerV1* m_GamerPicManager;
	HMODULE m_hRgscDll;
	// IPC communication with the game's launcher
	class rlPCPipe* m_PCPipe;
	// Virtual Gamepads
	rgsc::RgscGamepad* m_ScuiPads[rgsc::RGSC_MAX_PADS];
	rgsc::u32 m_NumScuiPads;
	char m_RelayDelegate[64];
	char m_EpicDelegate[64];
	int m_InitializationErrorCode;
	bool m_WaitingForKeyboardResult;
	rgsc::u32 m_KeyboardCompleteTime;
	char m_ScuiVirtualKeyboard[2056];
	char m_AdditionalSessionAttr[rgsc::RGSC_ADDITIONAL_SESSION_ATTR_BUF_SIZE];
	char m_AdditionalJoinAttr[rgsc::RGSC_ADDITIONAL_SESSION_ATTR_BUF_SIZE];
	char m_MetaDataPath[rgsc::RGSC_MAX_PATH];
};
#endif //RLPC_H
