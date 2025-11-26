#pragma once
#include "views/view.hpp"

namespace big
{
	enum class tabs
	{
		NONE,

		SELF,
#if ENABLE_TOXIC_CHEATS
		WEAPONS,
		TELEPORT,
		MOBILE,
#endif
		OUTFIT_EDITOR,
		OUTFIT_SLOTS,

#if ENABLE_TOXIC_CHEATS
		VEHICLE,
		HANDLING,
		HANDLING_SEARCH,
		HANDLING_SAVED_PROFILE,
		HANDLING_MY_PROFILES,
		HANDLING_CURRENT_PROFILE,
		LSC,
		SPAWN_VEHICLE,
		PV,
		PERSIST_CAR,
		FUN_VEHICLE,

		WORLD,
		SPAWN_PED,
		CREATOR,
		TRAIN,
		BLACKHOLE,
		MODEL_SWAPPER,
		NEARBY,
		ORBITAL_DRONE,
#endif
		NETWORK,
#if ENABLE_TOXIC_CHEATS
		SESSION,
		MISSIONS,
		SPOOFING,
		PLAYER_DATABASE,
		SESSION_BROWSER,
#endif
		STAT_EDITOR,

		SETTINGS,
		CONTEXT_MENU_SETTINGS,
#if ENABLE_TOXIC_CHEATS
		ESP_SETTINGS,
#endif
		GTA_CACHE_SETTINGS,
		GUI_SETTINGS,
		HOTKEY_SETTINGS,
		REACTION_SETTINGS,
		PROTECTION_SETTINGS,
		TRANSLATION_SETTINGS,
		DEBUG,

#if ENABLE_TOXIC_CHEATS
		PLAYER
#endif
	};

	struct navigation_struct
	{
		const char name[48]        = "";
		std::function<void()> func = nullptr;
		std::map<tabs, navigation_struct> sub_nav{};
	};

	class gui_service final
	{
		std::vector<tabs> current_tab{};
		bool switched_view = true;

		std::map<tabs, navigation_struct> nav = {
		    {
		        tabs::SELF,
		        {
		            "GUI_TAB_SELF",
		            view::self,
		            {
#if ENABLE_TOXIC_CHEATS
		                {tabs::WEAPONS, {"GUI_TAB_WEAPONS", view::weapons}},
		                {tabs::MOBILE, {"GUI_TAB_MOBILE", view::mobile}},
		                {tabs::TELEPORT, {"GUI_TAB_TELEPORT", view::teleport}},
#endif
		                {tabs::OUTFIT_EDITOR, {"GUI_TAB_OUTFIT_EDITOR", view::outfit_editor}},
		                {tabs::OUTFIT_SLOTS, {"GUI_TAB_OUTFIT_SLOTS", view::outfit_slots}},
		            },
		        },
		    },
#if ENABLE_TOXIC_CHEATS
		    {
		        tabs::VEHICLE,
		        {
		            "GUI_TAB_VEHICLE",
		            view::vehicle,
		            {
		                {
		                    tabs::HANDLING,
		                    {
		                        "GUI_TAB_HANDLING",
		                        view::handling_current_profile,
		                        {
		                            {tabs::HANDLING_CURRENT_PROFILE, {"GUI_TAB_HANDLING_CURRENT_PROFILE", view::handling_current_profile}},
		                            {tabs::HANDLING_SAVED_PROFILE, {"GUI_TAB_HANDLING_SAVED_PROFILES", view::handling_saved_profiles}},
		                        },
		                    },
		                },
		                {tabs::LSC, {"GUI_TAB_LSC", view::lsc}},
		                {tabs::SPAWN_VEHICLE, {"GUI_TAB_SPAWN_VEHICLE", view::spawn_vehicle}},
		                {tabs::PV, {"GUI_TAB_PERSONAL_VEHICLE", view::pv}},
		                {tabs::PERSIST_CAR, {"GUI_TAB_PERSIST_CAR", view::persist_car}},
		                {tabs::FUN_VEHICLE, {"GUI_TAB_VEHICLE_FUN_FEATURES", view::fun_vehicle}},
		            },
		        },
		    },
		    {
		        tabs::WORLD,
		        {
		            "GUI_TAB_WORLD",
		            view::world,
		            {
		                {tabs::SPAWN_PED, {"GUI_TAB_SPAWN_PED", view::spawn_ped}},
		                {tabs::CREATOR, {"GUI_TAB_CREATOR", view::creator}},
		                {tabs::TRAIN, {"GUI_TAB_TRAIN", view::train}},
		                {tabs::BLACKHOLE, {"GUI_TAB_BLACKHOLE", view::blackhole}},
		                {tabs::MODEL_SWAPPER, {"GUI_TAB_MODEL_SWAPPER", view::model_swapper}},
		                {tabs::NEARBY, {"GUI_TAB_NEARBY", view::nearby}},
		                {tabs::ORBITAL_DRONE, {"GUI_TAB_ORBITAL_DRONE", view::orbital_drone}},
		            },
		        },
		    },
#endif
		    {
		        tabs::NETWORK,
		        {
		            "GUI_TAB_NETWORK",
		            nullptr,
		            {
#if ENABLE_TOXIC_CHEATS
		                {tabs::SPOOFING, {"GUI_TAB_SPOOFING", view::spoofing}},
		                {tabs::SESSION, {"GUI_TAB_SESSION", view::session}},
		                {tabs::MISSIONS, {"GUI_TAB_MISSIONS", view::missions}},
		                {tabs::PLAYER_DATABASE, {"GUI_TAB_PLAYER_DB", view::player_database}},
		                {tabs::SESSION_BROWSER, {"GUI_TAB_SESSION_BROWSER", view::session_browser}},
#endif
		                {tabs::STAT_EDITOR, {"GUI_TAB_STAT_EDITOR", view::stat_editor}},
		            },
		        },
		    },
		    {
		        tabs::SETTINGS,
		        {
		            "GUI_TAB_SETTINGS",
		            view::settings,
		            {
		                {tabs::CONTEXT_MENU_SETTINGS, {"GUI_TAB_CONTEXT_MENU", view::context_menu_settings}},
#if ENABLE_TOXIC_CHEATS
		            	{tabs::ESP_SETTINGS, {"GUI_TAB_ESP", view::esp_settings}},
						{tabs::GTA_CACHE_SETTINGS, {"GTA Cache", view::gta_cache}},
#endif
		            	{tabs::GUI_SETTINGS, {"GUI_TAB_GUI", view::gui_settings}},
		                {tabs::HOTKEY_SETTINGS, {"GUI_TAB_HOTKEYS", view::hotkey_settings}},
		                {tabs::REACTION_SETTINGS, {"GUI_TAB_REACTIONS", view::reaction_settings}},
		                {tabs::PROTECTION_SETTINGS, {"GUI_TAB_PROTECTION", view::protection_settings}},
		                {tabs::TRANSLATION_SETTINGS, {"GUI_TAB_TRANSLATION", view::translation_settings}},
		                {tabs::DEBUG, {"GUI_TAB_DEBUG", nullptr}},
		            },
		        },
		    },
#if ENABLE_TOXIC_CHEATS
		    {
		        tabs::PLAYER,
		        {"", view::view_player},
		    },
#endif
		};

	public:
		gui_service();
		virtual ~gui_service();

		int nav_ctr = 0;

		navigation_struct* get_selected();
		std::vector<tabs>& get_selected_tab();
		bool has_switched_view();
		void set_selected(tabs);
		void set_nav_size(int);
		void increment_nav_size();
		void reset_nav_size();
		std::map<tabs, navigation_struct>& get_navigation();
	};

	inline gui_service* g_gui_service{};
}
