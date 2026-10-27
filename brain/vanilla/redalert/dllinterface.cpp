// MODIFIED for OpenCNC3D. Assigns per-seat AI skill, exports diagnostics, and persists validated controller state.
//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed
// in the hope that it will be useful, but with permitted additional restrictions
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT
// distributed with this program. You should have received a copy of the
// GNU General Public License along with permitted additional restrictions
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection
//
// MODIFIED for OpenCNC3D. This is not EA's original file.
// Adds headless startup diagnostics, deterministic custom-scenario seeding, and
// suppresses presentation-only rendering when explicit headless mode is active.
// It changes initialization but not Red Alert gameplay rules.
// MODIFIED again in October 2026: the object dump also prints the shots fired since
// the last dump (EFX|FIRE lines, recorded by OpenCNC3D_Note_Fire from Fire_At) and
// the bullets in flight (EFX|BULLET lines), as the Tiberian Dawn brain's dump does.
// A copy for the renderer, read by nothing in the game.
// MODIFIED again in October 2026: the exports the Tiberian Dawn brain carries, with
// the same names and meanings. The lockstep order wire (OpenCNC3D_Event_ABI,
// _Drain_Events, _Post_Event, _Set/_Get_Lockstep): under lockstep a GlyphX match
// no longer moves orders straight from OutList to DoList, executes them in
// (frame, house) order, and repair, sale, wall sale, placement, production and
// the new building power switch become orders; a match is seeded from the agreed
// seed (OpenCNC3D_Set_Seed) and its random start shuffle draws from the
// synchronised stream, and a new match starts its frame counter and queues
// afresh. Rally points, the AI takeover and blow-up of a departed player, the
// cheat switches (invincibility, Build Anywhere, super weapons), the wall line
// reach, the proximity query, the potential build tree, trigger queries and a
// forced fire test hook. The match options gain the short game rule, kept in a
// save, and a staged disc map's .INI is read when no .mpr is beside it. The
// object dump carries the Tiberian Dawn brain's remaining fields (dimw, status,
// flash, pips, maxpips, cloak, cstage, repairing, wrench, poweroff, harv,
// rotating, navset, driving, primary, flashme, act) with the same meanings.
// A computer player in a multiplayer match takes the computer's handicap
// (Scen.CDifficulty, with Red Alert's easy bonus when more than one person
// plays), as the 1996 Assign_Houses gives the computers it creates, instead of
// the person's; OpenCNC3D_Set_Computer_Difficulty sets it for a network match.
// It DOES change the game simulation: in a lockstep match, in a second match
// played in one process, when a host turns one of the switches on, and for the
// computer players when a difficulty other than normal is chosen.
// The complete diff against upstream is brain/patches/vanilla-opencnc3d.patch,
// and NOTICE.md lists every modified file.

/*
** DLLInterfac.cpp
**
**	This is where we implement the API expected by the Instance Server.
**
** The Instance Server will pass in requests for loading and starting maps, control input from players,
** and requests for game simulation and rendering states.
**
**
*/

#include "../tiberiandawn/opencnc3d_compat.h"

#include <string>
#include <vector>
#include <set>

#include "function.h"
#include "fair_ai_ra.h"
#include "../../fair_ai.h"
#include "keyframe.h"
#include "utracker.h"
#include "carry.h"
#include "externs.h"
#include "dllinterface.h"
#include "gadget.h"
#include "defines.h" // VOC_COUNT, VOX_COUNT
#include "sidebarglyphx.h"
#include "common/irandom.h"
#include "../../../game/sidebar_build_potential.h"

#include <chrono>

/*
** Externs
*/
extern int DLL_Startup(const char* command_line);
extern bool OpenCNC3DHeadless;
extern void Reallocate_Big_Shape_Buffer(void);
extern bool ProgEndCalled;
extern int Write_PCX_File(char* name, GraphicViewPortClass& pic, unsigned char* palette);
extern void Color_Cycle(void);

bool Debug_Write_Shape_Type(const ObjectTypeClass* type, int shapenum);
bool Debug_Write_Shape(const char* file_name,
                       void const* shapefile,
                       int shapenum,
                       int flags = 0,
                       void const* ghostdata = NULL);

typedef void(__cdecl* CNC_Event_Callback_Type)(const EventCallbackStruct& event);
typedef unsigned __int64 uint64;
typedef __int64 int64;

/*
** Audio defines
**
**
**
**
**
*/
// From RedAlert\Audio.cpp
typedef enum
{
    IN_NOVAR, // No variation or alterations allowed.
    IN_VAR    // Infantry variance response modification.
} ContextType;

extern struct SoundEffectNameStruct
{
    char const* Name;  // Digitized voice file name.
    int Priority;      // Playback priority of this sample.
    ContextType Where; // In what game context does this sample exist.
} SoundEffectName[VOC_COUNT];

// From RedAlert\Audio.cpp
extern char const* Speech[VOX_COUNT];

/*
** Misc defines
**
**
**
**
**
*/
#define GAME_TO_PLAY      Session.Type
#define MULTIPLAYER_COUNT Session.Players.Count()
#define KEYBOARD          Keyboard

#define RANDOM_START_POSITION 0x7f

#define KILL_PLAYER_ON_DISCONNECT 1

/*
**  DLL Interface
**
**
**
**
**
*/
extern "C" __declspec(dllexport) unsigned int __cdecl CNC_Version(unsigned int version_in);
extern "C" __declspec(dllexport) void __cdecl CNC_Init(const char* command_line,
                                                       CNC_Event_Callback_Type event_callback);
extern "C" __declspec(dllexport) void __cdecl CNC_Config(const CNCRulesDataStruct& rules);
extern "C" __declspec(dllexport) void __cdecl CNC_Add_Mod_Path(const char* mod_path);
extern "C" __declspec(dllexport) bool __cdecl CNC_Get_Visible_Page(unsigned char* buffer_in,
                                                                   unsigned int& width,
                                                                   unsigned int& height);
extern "C" __declspec(dllexport) bool __cdecl CNC_Get_Palette(unsigned char (&palette_in)[256][3]);
extern "C" __declspec(dllexport) bool __cdecl CNC_Start_Instance(int scenario_index,
                                                                 int build_level,
                                                                 const char* faction,
                                                                 const char* game_type,
                                                                 const char* content_directory,
                                                                 int sabotaged_structure,
                                                                 const char* override_map_name);
extern "C" __declspec(dllexport) bool __cdecl CNC_Start_Instance_Variation(int scenario_index,
                                                                           int scenario_variation,
                                                                           int scenario_direction,
                                                                           int build_level,
                                                                           const char* faction,
                                                                           const char* game_type,
                                                                           const char* content_directory,
                                                                           int sabotaged_structure,
                                                                           const char* override_map_name);
extern "C" __declspec(dllexport) bool __cdecl CNC_Start_Custom_Instance(const char* content_directory,
                                                                        const char* directory_path,
                                                                        const char* scenario_name,
                                                                        int build_level,
                                                                        bool multiplayer);
extern "C" __declspec(dllexport) bool __cdecl CNC_Advance_Instance(uint64 player_id);
extern "C" __declspec(dllexport) bool __cdecl CNC_Get_Game_State(GameStateRequestEnum state_type,
                                                                 uint64 player_id,
                                                                 unsigned char* buffer_in,
                                                                 unsigned int buffer_size);
extern "C" __declspec(dllexport) bool __cdecl CNC_Read_INI(int scenario_index,
                                                           int scenario_variation,
                                                           int scenario_direction,
                                                           const char* content_directory,
                                                           const char* override_map_name,
                                                           char* ini_buffer,
                                                           int _ini_buffer_size);
extern "C" __declspec(dllexport) void __cdecl CNC_Set_Home_Cell(int x, int y, uint64 player_id);
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Game_Request(GameRequestEnum request_type);
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Game_Settings_Request(int health_bar_display_mode,
                                                                               int resource_bar_display_mode);
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Input(InputRequestEnum mouse_event,
                                                               unsigned char special_key_flags,
                                                               uint64 player_id,
                                                               int x1,
                                                               int y1,
                                                               int x2,
                                                               int y2);
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Structure_Request(StructureRequestEnum request_type,
                                                                           uint64 player_id,
                                                                           int object_id);
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Unit_Request(UnitRequestEnum request_type, uint64 player_id);
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Sidebar_Request(SidebarRequestEnum request_type,
                                                                         uint64 player_id,
                                                                         int buildable_type,
                                                                         int buildable_id,
                                                                         short cell_x,
                                                                         short cell_y);
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_SuperWeapon_Request(SuperWeaponRequestEnum request_type,
                                                                             uint64 player_id,
                                                                             int buildable_type,
                                                                             int buildable_id,
                                                                             int x1,
                                                                             int y1);
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_ControlGroup_Request(ControlGroupRequestEnum request_type,
                                                                              uint64 player_id,
                                                                              unsigned char control_group_index);
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Debug_Request(DebugRequestEnum debug_request_type,
                                                                       uint64 player_id,
                                                                       const char* object_name,
                                                                       int x,
                                                                       int y,
                                                                       bool unshroud,
                                                                       bool enemy);
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Beacon_Request(BeaconRequestEnum beacon_request_type,
                                                                        uint64 player_id,
                                                                        int pixel_x,
                                                                        int pixel_y);
extern "C" __declspec(dllexport) bool __cdecl CNC_Set_Multiplayer_Data(int scenario_index,
                                                                       CNCMultiplayerOptionsStruct& game_options,
                                                                       int num_players,
                                                                       CNCPlayerInfoStruct* player_list,
                                                                       int max_players);
extern "C" __declspec(dllexport) bool __cdecl CNC_Clear_Object_Selection(uint64 player_id);
extern "C" __declspec(dllexport) bool __cdecl CNC_Select_Object(uint64 player_id,
                                                                int object_type_id,
                                                                int object_to_select_id);
extern "C" __declspec(dllexport) bool __cdecl CNC_Save_Load(bool save,
                                                            const char* file_path_and_name,
                                                            const char* game_type);
extern "C" __declspec(dllexport) void __cdecl CNC_Set_Difficulty(int difficulty);
extern "C" __declspec(dllexport) void __cdecl CNC_Restore_Carryover_Objects(const CarryoverObjectStruct* objects);
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Player_Switch_To_AI(uint64 player_id);
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Human_Team_Wins(uint64 player_id);
extern "C" __declspec(dllexport) void __cdecl CNC_Start_Mission_Timer(int time);
extern "C" __declspec(dllexport) bool __cdecl CNC_Get_Start_Game_Info(uint64 player_id,
                                                                      int& start_location_waypoint_index);

/*
** Class to implement the interface, and contain additional game state required by the conversion from peer/peer to
*client/server
**
**
**
**
**
*/
class DLLExportClass
{
public:
    static void Init(void);
    static void Shutdown(void);
    static void Config(const CNCRulesDataStruct& rules);
    static void Add_Mod_Path(const char* mod_path);
    static void Set_Home_Cell(int x, int y, uint64 player_id);
    static void Set_Content_Directory(const char* dir);

    static bool Get_Layer_State(uint64 player_id, unsigned char* buffer_in, unsigned int buffer_size);
    static bool Get_Sidebar_State(uint64 player_id, unsigned char* buffer_in, unsigned int buffer_size);
    static bool Start_Construction(uint64 player_id, int buildable_type, int buildable_id);
    static bool Hold_Construction(uint64 player_id, int buildable_type, int buildable_id);
    static bool Cancel_Construction(uint64 player_id, int buildable_type, int buildable_id);
    static bool Start_Placement(uint64 player_id, int buildable_type, int buildable_id);
    static BuildingClass* Get_Pending_Placement_Object(uint64 player_id, int buildable_type, int buildable_id);
    static bool Get_Placement_State(uint64 player_id, unsigned char* buffer_in, unsigned int buffer_size);
    static void Convert_Type(const ObjectClass* object, CNCObjectStruct& object_out);
    static void DLL_Draw_Intercept(int shape_number,
                                   int x,
                                   int y,
                                   int width,
                                   int height,
                                   int flags,
                                   const ObjectClass* object,
                                   DirType rotation,
                                   long scale,
                                   const char* shape_file_name = NULL,
                                   char override_owner = HOUSE_NONE);
    static void DLL_Draw_Pip_Intercept(const ObjectClass* object, int pip);
    static bool Place(uint64 player_id, int buildable_type, int buildable_id, short cell_x, short cell_y);
    static bool Cancel_Placement(uint64 player_id, int buildable_type, int buildable_id);
    static bool Place_Super_Weapon(uint64 player_id, int buildable_type, int buildable_id, int x, int y);
    static bool Create_Control_Group(unsigned char control_group_index);
    static bool Add_To_Control_Group(unsigned char control_group_index);
    static bool Toggle_Control_Group_Selection(unsigned char control_group_index);
    static bool
    Construction_Action(SidebarRequestEnum construction_action, uint64 player_id, int buildable_type, int buildable_id);
    static bool MP_Construction_Action(SidebarRequestEnum construction_action,
                                       uint64 player_id,
                                       int buildable_type,
                                       int buildable_id);
    static bool
    Passes_Proximity_Check(CELL cell_in, BuildingTypeClass* placement_type, unsigned char* placement_distance);
    static void Calculate_Start_Positions(void);
    static void Computer_Message(bool last_player_taunt);

    static void Repair_Mode(uint64 player_id);
    static void Repair(uint64 player_id, int object_id);
    static void Sell_Mode(uint64 player_id);
    static void Sell(uint64 player_id, int object_id);
    static void Repair_Sell_Cancel(uint64 player_id);

    static void Scatter_Selected(uint64 player_id);
    static void Select_Next_Unit(uint64 player_id);
    static void Select_Previous_Unit(uint64 player_id);
    static void Selected_Guard_Mode(uint64 player_id);
    static void Selected_Stop(uint64 player_id);
    static void Team_Units_Formation_Toggle_On(uint64 player_id);
    static void Units_Queued_Movement_Toggle(uint64 player_id, bool toggle);

    static void Cell_Class_Draw_It(CNCDynamicMapStruct* dynamic_map,
                                   int& entry_index,
                                   CellClass* cell_ptr,
                                   int xpixel,
                                   int ypixel,
                                   bool debug_output);
    static bool Get_Dynamic_Map_State(uint64 player_id, unsigned char* buffer_in, unsigned int buffer_size);
    static bool Get_Shroud_State(uint64 player_id, unsigned char* buffer_in, unsigned int buffer_size);
    static bool Get_Occupier_State(uint64 player_id, unsigned char* buffer_in, unsigned int buffer_size);
    static bool Get_Player_Info_State(uint64 player_id, unsigned char* buffer_in, unsigned int buffer_size);

    static void Set_Event_Callback(CNC_Event_Callback_Type event_callback)
    {
        EventCallback = event_callback;
    }
    static void Debug_Spawn_Unit(const char* object_name, int x, int y, bool enemy = false);
    static void Debug_Spawn_All(int x, int y);
    static bool Try_Debug_Spawn_Unlimbo(TechnoClass* techno, int& cell_x, int& cell_y);
    static void Debug_Kill_Unit(int x, int y);
    static void Debug_Heal_Unit(int x, int y);

    static void On_Play_Movie(const char* movie_name, ThemeType theme, bool immediate);
    static void On_Display_Briefing_Text();

    static void On_Sound_Effect(const HouseClass* player_ptr,
                                int sound_effect_index,
                                const char* extension,
                                int variation,
                                COORDINATE coord);
    static void On_Speech(const HouseClass* player_ptr, int speech_index);
    static void On_Message(const HouseClass* player_ptr,
                           const char* message,
                           float timeout_seconds,
                           EventCallbackMessageEnum message_type,
                           int64 message_id);
    static void On_Update_Map_Cell(int cell_x, int cell_y, const char* template_type_name);
    static void On_Special_Weapon_Targetting(const HouseClass* player_ptr, SpecialWeaponType weapon_type);
    static void On_Ping(const HouseClass* player_ptr, COORDINATE coord);

    static void On_Game_Over(uint64 glyphx_player_id, bool player_wins);
    static void On_Multiplayer_Game_Over(void);

    static void On_Debug_Output(const char* debug_text);

    static void
    On_Achievement(const HouseClass* player_ptr, const char* achievement_type, const char* achievement_reason);

    static void On_Center_Camera(const HouseClass* player_ptr, int coord_x, int coord_y);

    static void Glyphx_Queue_AI();

    static void Store_Carryover_Objects();

    static void Force_Human_Team_Wins(uint64 quitting_player_id);

    /*
    ** Player context switching for input/output
    */
    static bool Set_Player_Context(uint64 glyphx_player, bool force = false);
    static void Reset_Player_Context(void);
    static void Adjust_Internal_View(bool force_ignore_view_constraints = false);
    static void Logic_Switch_Player_Context(ObjectClass* object);
    static void Logic_Switch_Player_Context(HouseClass* house);
    static void Refresh_Player_Control_Flags(void);
    static __int64 Get_GlyphX_Player_ID(const HouseClass* house);

    static void Recalculate_Placement_Distances();

    static void Reset_Sidebars(void);

    static SidebarGlyphxClass* Get_Current_Context_Sidebar(HouseClass* player_ptr = NULL);

    static uint64 GlyphxPlayerIDs[MAX_PLAYERS];

    static const void* Get_Shadow_Shapes(void)
    {
        return Map.ShadowShapes;
    }
    static const unsigned char* Get_Shadow_Trans(void)
    {
        return &Map.ShadowTrans[0];
    }

    static bool Legacy_Render_Enabled(void);

    static bool Get_Input_Key_State(KeyNumType key);

    static void Set_Special_Key_Flags(unsigned char special_key_flags);
    static void Clear_Special_Key_Flags();

    static bool Load(Straw& file);
    static bool Save(Pipe& file);
    static void Code_Pointers(void);
    static void Decode_Pointers(void);

    static bool Get_Game_Over()
    {
        return GameOver;
    }

private:
    static void Calculate_Single_Player_Score(EventCallbackStruct&);

    static int RA_Calculate_Leadership(HousesType player_house, int units_lost, int buildings_lost);
    static int RA_Calculate_Economy(long available_money,
                                    int stolen_buildings_credits,
                                    unsigned harvested_credits,
                                    long initial_credits);
    static int RA_Calculate_Score(int uspoints, int leadership, int economy);

    static void Convert_Action_Type(ActionType type, ObjectClass* object, TARGET target, DllActionTypeEnum& dll_type);
    static void Convert_Special_Weapon_Type(SpecialWeaponType weapon_type,
                                            DllSuperweaponTypeEnum& dll_weapon_type,
                                            char* weapon_name);
    static void Fill_Sidebar_Entry_From_Special_Weapon(CNCSidebarEntryStruct& sidebar_entry_out,
                                                       SuperClass*& super_weapon_out,
                                                       SpecialWeaponType weapon_type);

    static void Calculate_Placement_Distances(BuildingTypeClass* placement_type, unsigned char* placement_distance);

    static int CurrentDrawCount;
    static int TotalObjectCount;
    static int SortOrder;
    static int ExportLayer;

    static CNCObjectListStruct* ObjectList;

    static CNC_Event_Callback_Type EventCallback;

    static int CurrentLocalPlayerIndex;

    static bool GameOver;

    static std::set<int64> MessagesSent;

    /*
    ** Pseudo sidebars for players in multiplayer
    */
    static SidebarGlyphxClass MultiplayerSidebars[MAX_PLAYERS];

    static CELL MultiplayerStartPositions[MAX_PLAYERS];

    static BuildingTypeClass* PlacementType[MAX_PLAYERS];

    static unsigned char PlacementDistance[MAX_PLAYERS][MAP_CELL_TOTAL];

    static unsigned char SpecialKeyFlags[MAX_PLAYERS];

    /*
    ** Mod directories
    */
    static DynamicVectorClass<char*> ModSearchPaths;
};

/*
** DLLExportClass static data
**
**
**
**
**
*/
int DLLExportClass::CurrentDrawCount = 0;
int DLLExportClass::TotalObjectCount = 0;
int DLLExportClass::SortOrder = 0;
int DLLExportClass::ExportLayer = 0;
CNCObjectListStruct* DLLExportClass::ObjectList = NULL;
SidebarGlyphxClass DLLExportClass::MultiplayerSidebars[MAX_PLAYERS];
uint64 DLLExportClass::GlyphxPlayerIDs[MAX_PLAYERS] = {0xffffffffl};
int DLLExportClass::CurrentLocalPlayerIndex = -1;
CELL DLLExportClass::MultiplayerStartPositions[MAX_PLAYERS];
BuildingTypeClass* DLLExportClass::PlacementType[MAX_PLAYERS];
unsigned char DLLExportClass::PlacementDistance[MAX_PLAYERS][MAP_CELL_TOTAL];
unsigned char DLLExportClass::SpecialKeyFlags[MAX_PLAYERS] = {0U};
DynamicVectorClass<char*> DLLExportClass::ModSearchPaths;
std::set<int64> DLLExportClass::MessagesSent;
bool DLLExportClass::GameOver = false;

/*
** Global variables
**
**
**
**
**
*/
int DLLForceMouseX = 0;
int DLLForceMouseY = 0;

CNC_Event_Callback_Type DLLExportClass::EventCallback = NULL;

// Needed to accomodate Glyphx client sidebar. ST - 4/12/2019 5:29PM
int GlyphXClientSidebarWidthInLeptons = 0;

bool MPlayerIsHuman[MAX_PLAYERS];
int MPlayerTeamIDs[MAX_PLAYERS];
int MPlayerStartLocations[MAX_PLAYERS];

bool MPSuperWeaponDisable = false;
bool ShareAllyVisibility = true;
bool UseGlyphXStartLocations = true;

SpecialClass* SpecialBackup = NULL;

int GetRandSeed()
{
#if 0
    using namespace std::chrono;
    time_point<system_clock> time_since_epoch = system_clock::now();
    auto microseconds_since_epoch = floor<std::chrono::microseconds>(time_since_epoch);

    return abs(static_cast<int>(microseconds_since_epoch.time_since_epoch().count()));
#endif
    // for mingw compatility
    return GetTickCount();
}

void Play_Movie_GlyphX(const char* movie_name, ThemeType theme, bool immediate = false)
{
    if ((movie_name[0] == 'x' || movie_name[0] == 'X') && movie_name[1] == 0) {
        return;
    }

    DLLExportClass::On_Play_Movie(movie_name, theme, immediate);
}

void Display_Briefing_Text_GlyphX()
{
    DLLExportClass::On_Display_Briefing_Text();
}

void On_Sound_Effect(int sound_index, int variation, COORDINATE coord, int house)
{
    int voc = sound_index;
    if (voc == VOC_NONE) {
        return;
    }

// Borrowed from RedAlert\AUDIO.CPP Sound_Effect()
//
#if 1
    /*
    **	Fetch a pointer to the sound effect data. Modify the sound as appropriate and desired.
    */
    char const* ext = ""; // ".AUD";
    if (SoundEffectName[voc].Where == IN_VAR) {

        /*
        **	If there is no forced  house, then use the current player
        **	act like house.
        */
        if (house == HOUSE_NONE) {
            house = PlayerPtr->ActLike;
        }

        /*
        **	Change the extension based on the variation and house accent requested.
        */
        if (((1 << house) & HOUSEF_ALLIES) != 0) {

            /*
            **	For infantry, use a variation on the response. For vehicles, always
            **	use the vehicle response table.
            */
            if (variation < 0) {
                if (ABS(variation) % 2) {
                    ext = ".V00";
                } else {
                    ext = ".V02";
                }
            } else {
                if (variation % 2) {
                    ext = ".V01";
                } else {
                    ext = ".V03";
                }
            }
        } else {
            if (variation < 0) {
                if (ABS(variation) % 2) {
                    ext = ".R00";
                } else {
                    ext = ".R02";
                }
            } else {
                if (variation % 2) {
                    ext = ".R01";
                } else {
                    ext = ".R03";
                }
            }
        }
    }
#endif
    // END MBL

    DLLExportClass::On_Sound_Effect(PlayerPtr, sound_index, ext, variation, coord);

#if 0
	int voc = sound_index;
	if (voc == VOC_NONE)
	{
		return;
	}

	// Borrowed from AUDIO.CPP Sound_Effect()
	//
	char const * ext = ""; // ".AUD";
#ifdef TIBERIAN_DAWN
	if (Special.IsJuvenile && SoundEffectName[voc].Where == IN_JUV) {
		ext = ".JUV";
	} else {
#endif
		if (SoundEffectName[voc].Where == IN_VAR) {
			/*
			**	For infantry, use a variation on the response. For vehicles, always
			**	use the vehicle response table.
			*/
			if (variation < 0) {
				if (ABS(variation) % 2) {
					ext = ".V00";
				} else {
					ext = ".V02";
				}
			} else {
				if (variation % 2) {
					ext = ".V01";
				} else {
					ext = ".V03";
				}
			}
		}
#ifdef TIBERIAN_DAWN
	}
#endif
	// END MBL 

	DLLExportClass::On_Sound_Effect(PlayerPtr, sound_index, ext, variation, coord);
#endif
}

void On_Speech(int speech_index, HouseClass* house)
{
    if (house == NULL) {
        DLLExportClass::On_Speech(PlayerPtr, speech_index);
    } else {
        DLLExportClass::On_Speech(house, speech_index);
    }
}

void On_Message(const char* message, float timeout_seconds, int64 id)
{
    DLLExportClass::On_Message(PlayerPtr, message, timeout_seconds, MESSAGE_TYPE_DIRECT, id);
}

void On_Update_Map_Cell(int cell_x, int cell_y, const char* template_type_name)
{
    DLLExportClass::On_Update_Map_Cell(cell_x, cell_y, template_type_name);
}

void On_Special_Weapon_Targetting(const HouseClass* player_ptr, SpecialWeaponType weapon_type)
{
    DLLExportClass::On_Special_Weapon_Targetting(player_ptr, weapon_type);
}

void On_Ping(const HouseClass* player_ptr, COORDINATE coord)
{
    DLLExportClass::On_Ping(player_ptr, coord);
}

void On_Defeated_Message(const char* message, float timeout_seconds)
{
    DLLExportClass::On_Message(PlayerPtr, message, timeout_seconds, MESSAGE_TYPE_PLAYER_DEFEATED, -1);
}

void GlyphX_Debug_Print(const char* debug_text)
{
    DLLExportClass::On_Debug_Output(debug_text);
}

void On_Achievement_Event(const HouseClass* player_ptr, const char* achievement_type, const char* achievement_reason)
{
    DLLExportClass::On_Achievement(player_ptr, achievement_type, achievement_reason);
}

/**************************************************************************************************
 * CNC_Version -- Check DLL/Server version
 *
 * In:		Version expected
 *
 * Out:	Actual version
 *
 *
 *
 * History: 4/9/2020 2:12PM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) unsigned int __cdecl CNC_Version(unsigned int version_in)
{
    // Unreferenced, but potentially useful to know which version the server is expecting
    version_in;

    return CNC_DLL_API_VERSION;
}

/**************************************************************************************************
 * CNC_Init -- Initialize the .DLL
 *
 * In:   Command line
 *
 * Out:
 *
 *
 *
 * History: 1/3/2019 11:33AM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Init(const char* command_line, CNC_Event_Callback_Type event_callback)
{
    DLLExportClass::Set_Content_Directory(NULL);

    /* OpenCNC3D installs the callback first so a failed startup still reports its last stage. */
    DLLExportClass::Set_Event_Callback(event_callback);
    GlyphX_Debug_Print("RA3D|stage|startup.begin");
    DLL_Startup(command_line);
    GlyphX_Debug_Print("RA3D|stage|startup.engine-ready");
    DLLExportClass::Init();
    GlyphX_Debug_Print("RA3D|stage|startup.abi-ready");
}

/**************************************************************************************************
 * DLL_Shutdown -- Shutdown the .DLL
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 2/20/2020 1:58PM - ST
 **************************************************************************************************/
void DLL_Shutdown(void)
{
    DLLExportClass::Shutdown();
}

/**************************************************************************************************
 * CNC_Config -- Configure the plugin
 *
 * In:   Configuration data
 *
 * Out:
 *
 *
 *
 * History: 10/03/2019 - SKY
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Config(const CNCRulesDataStruct& rules)
{
    DLLExportClass::Config(rules);
}

/**************************************************************************************************
 * CNC_Add_Mod_Path -- Add a path to load mod files from
 *
 * In:   Path to load mods from
 *
 * Out:
 *
 *
 *
 * History: 2/20/2020 2:04PM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Add_Mod_Path(const char* mod_path)
{
    DLLExportClass::Add_Mod_Path(mod_path);
}

/**************************************************************************************************
 * CNC_Get_Visible_Page -- Get the screen buffer 'SeenBuff' from the game
 *
 * In:   If buffer_in is null, just return info about page
 *
 * Out:  false if not changed since last call
 *
 *
 *
 * History: 1/3/2019 11:33AM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl CNC_Get_Visible_Page(unsigned char* buffer_in,
                                                                   unsigned int& width,
                                                                   unsigned int& height)
{
    if (!DLLExportClass::Legacy_Render_Enabled() || (buffer_in == NULL)) {
        return false;
    }

    /*
    ** Assume the seen page viewport is the same size as the page
    */

    GraphicBufferClass* gbuffer = HidPage.Get_Graphic_Buffer();
    if (gbuffer == NULL) {
        return false;
    }

    int view_port_width = Map.MapCellWidth * CELL_PIXEL_W;
    int view_port_height = Map.MapCellHeight * CELL_PIXEL_H;

    if (view_port_width == 0 || view_port_height == 0) {
        return false;
    }

    unsigned char* raw_buffer = (unsigned char*)gbuffer->Get_Buffer();
    long raw_size = gbuffer->Get_Size();
    if (raw_buffer == NULL || gbuffer->Get_Width() < view_port_width || gbuffer->Get_Height() < view_port_height) {
        return false;
    }

    width = view_port_width;
    height = view_port_height;

    int pitch = gbuffer->Get_Width();
    for (int i = 0; i < view_port_height; ++i, buffer_in += view_port_width, raw_buffer += pitch) {
        memcpy(buffer_in, raw_buffer, view_port_width);
    }

    return true;
}

extern "C" __declspec(dllexport) bool __cdecl CNC_Get_Palette(unsigned char (&palette_in)[256][3])
{
    memcpy(palette_in, CurrentPalette, sizeof(palette_in));
    return true;
}

/**************************************************************************************************
 * CNC_Set_Multiplayer_Data -- Set up for a multiplayer match
 *
 * In:   Multiplayer data
 *
 * Out:  false if data is bad
 *
 *
 *
 * History: 1/7/2019 5:20PM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl CNC_Set_Multiplayer_Data(int scenario_index,
                                                                       CNCMultiplayerOptionsStruct& game_options,
                                                                       int num_players,
                                                                       CNCPlayerInfoStruct* player_list,
                                                                       int max_players)
{

    if (num_players <= 0) {
        return false;
    }

    if (num_players > min(MAX_PLAYERS, max_players)) {
        return false;
    }

    DLLExportClass::Init();

    Session.Options.Bases = game_options.MPlayerBases;       // 1 = bases are on for this scenario
    Session.Options.Credits = game_options.MPlayerCredits;   // # credits everyone gets
    Session.Options.Tiberium = game_options.MPlayerTiberium; // 1 = tiberium enabled for this scenario
    Session.Options.Goodies = game_options.MPlayerGoodies;   // 1 = goodies enabled for this scenario
    Session.Options.Ghosts = game_options.MPlayerGhosts;     // 1 = houses with no players will still play
    // MPlayerSolo			= game_options.MPlayerSolo;			// 1 = allows a single-player net game
    Session.Options.UnitCount = game_options.MPlayerUnitCount; // # units for non-base multiplayer scenarios

    Special.IsShadowGrow = game_options.MPlayerShadowRegrow;
    Special.IsCaptureTheFlag = game_options.CaptureTheFlag;

    if (Session.Options.Tiberium) {
        Special.IsTGrowth = 1;
        Special.IsTSpread = 1;
    } else {
        Special.IsTGrowth = 0;
        Special.IsTSpread = 0;
    }

    Session.Options.ScenarioIndex = scenario_index;

    Special.IsMCVDeploy = game_options.IsMCVDeploy;
    Special.UseMCVDeploy = true;

    MPSuperWeaponDisable = !game_options.EnableSuperweapons; // Are superweapons available

    // Session.Options.AIPlayers = WWGetPrivateProfileInt("Options", "AI", 0, buffer);		//Number of AI players

    Special.IsEarlyWin = game_options.DestroyStructures;
    /* OpenCNC3D SHORT GAME: see OpenCNC3D_ShortGame in externs.h. */
    OpenCNC3D_ShortGame = game_options.ShortGame;

    Special.ModernBalance = game_options.ModernBalance;

    /*
    ** Enable Counterstrike/Aftermath units
    */
    OverrideNewUnitsEnabled = game_options.MPlayerAftermathUnits;

    while (Session.Players.Count() > 0) {
        delete Session.Players[0];
        Session.Players.Delete(Session.Players[0]);
    }

    for (int i = 0; i < num_players; i++) {
        CNCPlayerInfoStruct& player_info = player_list[i];

        NodeNameType* who = new NodeNameType;
        strncpy(who->Name, player_info.Name, MPLAYER_NAME_MAX);
        who->Name[MPLAYER_NAME_MAX - 1] = 0; // Make sure it's terminated
        who->Player.House = (HousesType)player_info.House;
        who->Player.Color = (PlayerColorType)player_info.ColorIndex;
        Session.Players.Add(who);

        /*
        ** Player IDs are done differently in Red Alert vs. TD.
        ** In TD, the ID is created from the house/color combination
        ** In RA, the ID is HOUSE_MULTI1 + the index into the Session.Players vector
        */
        DLLExportClass::GlyphxPlayerIDs[i] = player_info.GlyphxPlayerID;

        MPlayerIsHuman[i] = !player_info.IsAI;
        MPlayerTeamIDs[i] = player_info.Team;
        MPlayerStartLocations[i] = player_info.StartLocationIndex;

        /*
        ** Temp fix for custom maps that don't have valid start positions set from matchmaking
        */
        if (i > 0 && MPlayerStartLocations[i] == 0 && MPlayerStartLocations[0] == 0) {
            MPlayerStartLocations[i] = i;
        }
    }

    /*
    ** Force smart defense always on for multiplayer/skirmish
    */
    Rule.IsSmartDefense = true;

    /*
    ** Backup special
    */
    if (SpecialBackup != NULL) {
        memcpy(SpecialBackup, &Special, sizeof(SpecialClass));
    }

    return true;
}

extern "C" __declspec(dllexport) bool __cdecl CNC_Clear_Object_Selection(uint64 player_id)
{
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return false;
    }

    Unselect_All();
    /* One selection response per gesture. CNC_Select_Object latches this false
       after the first object in a batch; the renderer clears once before the
       next click or band, so reset here rather than for every selected unit. */
    AllowVoice = true;

    return true;
}

extern "C" __declspec(dllexport) bool __cdecl CNC_Select_Object(uint64 player_id,
                                                                int object_type_id,
                                                                int object_to_select_id)
{
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return false;
    }

    switch (object_type_id) {
    case INFANTRY: {
        for (int index = 0; index < Infantry.Count(); index++) {
            InfantryClass* obj = Infantry.Ptr(index);

            if (obj && !obj->IsInLimbo && obj->House->IsPlayerControl
                && Infantry.ID((InfantryClass*)obj) == object_to_select_id) {
                if (!obj->Is_Selected_By_Player()) {
                    obj->Select();
                    AllowVoice = false;
                }
                return true;
            }
        }
    } break;
    case UNIT: {
        for (int index = 0; index < Units.Count(); index++) {
            UnitClass* obj = Units.Ptr(index);

            if (obj && !obj->IsInLimbo && obj->House->IsPlayerControl
                && Units.ID((UnitClass*)obj) == object_to_select_id) {
                if (!obj->Is_Selected_By_Player()) {
                    obj->Select();
                    AllowVoice = false;
                }
                return true;
            }
        }
    } break;
    case AIRCRAFT: {
        for (int index = 0; index < Aircraft.Count(); index++) {
            AircraftClass* obj = Aircraft.Ptr(index);

            if (obj && !obj->IsInLimbo && obj->House->IsPlayerControl
                && Aircraft.ID((AircraftClass*)obj) == object_to_select_id) {
                if (!obj->Is_Selected_By_Player()) {
                    obj->Select();
                    AllowVoice = false;
                }
                return true;
            }
        }
    } break;
    case BUILDING: {
        for (int index = 0; index < Buildings.Count(); index++) {
            BuildingClass* obj = Buildings.Ptr(index);
            if (obj && !obj->IsInLimbo && obj->House->IsPlayerControl
                && Buildings.ID((BuildingClass*)obj) == object_to_select_id) {
                if (!obj->Is_Selected_By_Player()) {
                    obj->Select();
                    AllowVoice = false;
                }
                return true;
            }
        }
    } break;
    case VESSEL: {
        for (int index = 0; index < Vessels.Count(); index++) {
            VesselClass* obj = Vessels.Ptr(index);

            if (obj && !obj->IsInLimbo && obj->House->IsPlayerControl
                && Vessels.ID((VesselClass*)obj) == object_to_select_id) {
                if (!obj->Is_Selected_By_Player()) {
                    obj->Select();
                    AllowVoice = false;
                }
                return true;
            }
        }
    } break;
    }

    return false;
}

/**************************************************************************************************
 * GlyphX_Assign_Houses -- Replacement for Assign_Houses in INI.CPP / SCNEARIO.CPP
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 8/8/2019 12:37PM - ST
 **************************************************************************************************/
void GlyphX_Assign_Houses(void)
{
    /*
    ** RA version
    */
    extern int _build_tech[11];

    int assigned[MAX_PLAYERS];
    bool preassigned;
    int i, j, random_start_location;
    HousesType house, house2;
    HouseClass *housep, *housep2;
    int lowest_color;
    int index;

    /*
    ** OpenCNC3D: when the scenario was seeded on purpose (a lockstep match, or a seed handed
    ** over through OpenCNC3D_Set_Seed or the command line), the start shuffle below draws from
    ** the synchronised stream, Scen.RandomNumber, instead of libc rand() seeded from the wall
    ** clock, so every peer, and every repeat of a seeded run, deals the same starts. Unseeded,
    ** this is exactly the code that was always here.
    */
    const bool opencnc3d_synced_shuffle = OpenCNC3D_Lockstep || CustomSeed != 0;
    FairAI_RA_Reset();
    if (!opencnc3d_synced_shuffle) {
        srand(timeGetTime());
    }

    /*
    ** Use pre-selected start locations as long as at least one has been defined, otherwise let the original code
    ** in SCENARIO.CPP figure it out
    */
    UseGlyphXStartLocations = false;

    /*
    ** Assign random start positions if needed.
    */
    int random_start_locations[26];
    int num_start_locations = 0;
    int num_random_start_locations = 0;
    for (i = 0; i < 26; i++) {
        if (Scen.Waypoint[i] != -1) {
            preassigned = false;
            for (j = 0; !preassigned && (j < Session.Players.Count()); j++) {
                if (MPlayerStartLocations[j] == num_start_locations) {
                    preassigned = true;
                    UseGlyphXStartLocations = true;
                }
            }
            if (!preassigned && i < MAX_PLAYERS) {
                random_start_locations[num_random_start_locations] = num_start_locations;
                num_random_start_locations++;
            }
            num_start_locations++;
        }
    }

    /*
    ** OpenCNC3D: in the synchronised case, shuffle only when some seat actually asked for a
    ** random start, so a match whose seats all chose theirs consumes no draw here.
    */
    bool opencnc3d_any_random = !opencnc3d_synced_shuffle;
    for (i = 0; !opencnc3d_any_random && i < Session.Players.Count(); i++) {
        if (MPlayerStartLocations[i] == RANDOM_START_POSITION) {
            opencnc3d_any_random = true;
        }
    }

    if (opencnc3d_any_random && num_random_start_locations > 1) {
        for (i = 0; i < num_random_start_locations - 1; i++) {
            j = opencnc3d_synced_shuffle ? Random_Pick(i, num_random_start_locations - 1)
                                         : i + rand() / (RAND_MAX / (num_random_start_locations - i) + 1);
            int t = random_start_locations[j];
            random_start_locations[j] = random_start_locations[i];
            random_start_locations[i] = t;
        }
    }

    //------------------------------------------------------------------------
    // Initialize
    //------------------------------------------------------------------------
    for (i = 0; i < MAX_PLAYERS; i++) {
        assigned[i] = 0;
    }

    random_start_location = 0;

    //	debugprint( "Assign_Houses()\n" );
    //------------------------------------------------------------------------
    // Assign each player in 'Players' to a multiplayer house.  Players will
    // be sorted by their chosen color value (this value must be unique among
    // all the players).
    //------------------------------------------------------------------------
    for (i = 0; i < Session.Players.Count(); i++) {

        //.....................................................................
        // Find the player with the lowest color index
        //.....................................................................
        index = 0;
        lowest_color = 255;
        for (j = 0; j < Session.Players.Count(); j++) {
            //..................................................................
            // If we've already assigned this house, skip it.
            //..................................................................
            if (assigned[j]) {
                continue;
            }
            if (Session.Players[j]->Player.Color < lowest_color) {
                lowest_color = Session.Players[j]->Player.Color;
                index = j;
            }
        }

        //.....................................................................
        // Mark this player as having been assigned.
        //.....................................................................
        assigned[index] = 1;

        //.....................................................................
        // Assign the lowest-color'd player to the next available slot in the
        // HouseClass array.
        //.....................................................................
        house = (HousesType)(i + HOUSE_MULTI1);
        housep = HouseClass::As_Pointer(house);
        memset((char*)housep->IniName, 0, MPLAYER_NAME_MAX);
        strncpy((char*)housep->IniName, Session.Players[index]->Name, MPLAYER_NAME_MAX - 1);
#ifdef WOLAPI_INTEGRATION
        //	Make another copy of name, permanent throughout entire game.
        strncpy((char*)housep->InitialName, Session.Players[index]->Name, MPLAYER_NAME_MAX - 1);
#endif
        housep->IsHuman = MPlayerIsHuman[index];
        housep->IsPlayerControl = housep->IsHuman;

        if (!housep->IsHuman) {
            housep->IsStarted = true;
            strncpy(housep->IniName, Text_String(TXT_COMPUTER), HOUSE_NAME_MAX);
            housep->IQ = Rule.MaxIQ;
            // housep->Control.TechLevel = _build_tech[BuildLevel];
        } else {
            housep->IQ = 0;
        }

        housep->Init_Data((PlayerColorType)(Session.Players[index]->Player.Color),
                          Session.Players[index]->Player.House,
                          Session.Options.Credits);

        /*
        **	Set the start location override
        */
        if (MPlayerStartLocations[index] != RANDOM_START_POSITION) {
            housep->StartLocationOverride = MPlayerStartLocations[index];
        } else {
            if (random_start_location < num_random_start_locations) {
                housep->StartLocationOverride = random_start_locations[random_start_location++];
            } else {
                housep->StartLocationOverride = -1;
            }
        }

        if (index == 0) {
            PlayerPtr = housep;
        }
        /*
        **	Convert the build level into an actual tech level to assign to the house.
        **	There isn't a one-to-one correspondence.
        */
        housep->Control.TechLevel = _build_tech[BuildLevel];

        /*
        ** OpenCNC3D: a computer player takes the COMPUTER's handicap, as Assign_Houses
        ** (scenario.cpp) gives the computers it creates, with the same easy bonus when more
        ** than one person is playing; a person takes the player's. Every house used to take
        ** the player's, so a skirmish's difficulty slider made the computers stronger when it
        ** was set to easy.
        */
        // Fair skirmish skill changes decisions only. Assign_Handicap(Normal) retains
        // the chosen country's normal bonuses and the shared game-speed setting.
        housep->Assign_Handicap(DIFF_NORMAL);
        FairAI_RA_Assign(housep, index);

        //.....................................................................
        // Record where we placed this player
        //.....................................................................
        Session.Players[index]->Player.ID = house;

        //		debugprint( "Assigned ID of %i to %s\n", house, Session.Players[index]->Name );
    }

    for (i = Session.Players.Count(); i < Rule.MaxPlayers; i++) {
        house = (HousesType)(i + HOUSE_MULTI1);
        housep = HouseClass::As_Pointer(house);
        if (housep != NULL) {
            housep->IsDefeated = true;
        }
    }

    for (i = 0; i < Session.Players.Count(); i++) {

        house = Session.Players[i]->Player.ID;
        housep = HouseClass::As_Pointer(house);

        if (housep) {

            int team = MPlayerTeamIDs[i];

            for (int j = 0; j < Session.Players.Count(); j++) {

                if (i != j) {

                    if (team == MPlayerTeamIDs[j]) {

                        house2 = Session.Players[j]->Player.ID;
                        housep2 = HouseClass::As_Pointer(house2);

                        if (housep2) {
                            housep->Make_Ally(house2);
                        }
                    }
                }
            }
        }
    }
}

/**************************************************************************************************
 * CNC_Set_Home_Cell -- Allows overriding the start position for the camera
 *
 *
 * History: 2/14/2020 - LLL
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Set_Home_Cell(int x, int y, uint64 player_id)
{
    DLLExportClass::Set_Home_Cell(x, y, player_id);
}

/**************************************************************************************************
 * CNC_Start_Instance -- Load and start a cnc map -> WITHOUT SPECIFYING A SCENARIO VARIATION (SCEN_VAR)
 *
 * In:   Map initialization parameters
 *
 * Out:  false if map load failed
 *
 *
 *
 * History: 7/10/2019 - LLL
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl CNC_Start_Instance(int scenario_index,
                                                                 int build_level,
                                                                 const char* faction,
                                                                 const char* game_type,
                                                                 const char* content_directory,
                                                                 int sabotaged_structure,
                                                                 const char* override_map_name)
{
    return CNC_Start_Instance_Variation(scenario_index,
                                        (int)SCEN_VAR_NONE,
                                        (int)SCEN_DIR_EAST,
                                        build_level,
                                        faction,
                                        game_type,
                                        content_directory,
                                        sabotaged_structure,
                                        override_map_name);
}

/**************************************************************************************************
 * CNC_Start_Instance -- Load and start a cnc map
 *
 * In:   Map initialization parameters
 *
 * Out:  false if map load failed
 *
 *
 * Renamed and modified to accept a scenario variation 7/10/2019 - LLL
 *
 * History: 1/7/2019 5:20PM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl CNC_Start_Instance_Variation(int scenario_index,
                                                                           int scenario_variation,
                                                                           int scenario_direction,
                                                                           int build_level,
                                                                           const char* faction,
                                                                           const char* game_type,
                                                                           const char* content_directory,
                                                                           int sabotaged_structure,
                                                                           const char* override_map_name)
{
    if (game_type == NULL) {
        return false;
    }

    if (faction == NULL) {
        return false;
    }

    if (content_directory == NULL) {
        return false;
    }

    ScenarioPlayerType scen_player = SCEN_PLAYER_NONE;

    if (stricmp(faction, "SPAIN") == 0) {
        scen_player = SCEN_PLAYER_SPAIN;
        Whom = HOUSE_GOOD;
    }

    if (stricmp(faction, "GREECE") == 0 || stricmp(faction, "ALLY") == 0) {
        scen_player = SCEN_PLAYER_GREECE;
        Whom = HOUSE_GOOD;
    }

    if (stricmp(faction, "USSR") == 0) {
        scen_player = SCEN_PLAYER_USSR;
        Whom = HOUSE_BAD;
    }

    DLLExportClass::Set_Content_Directory(content_directory);

    BuildLevel = build_level;
    Scen.Scenario = scenario_index;
    ScenarioDirType scen_dir = (ScenarioDirType)scenario_direction;
    if (stricmp(game_type, "GAME_NORMAL") == 0) {
        GAME_TO_PLAY = GAME_NORMAL;
        if (scen_player == SCEN_PLAYER_NONE) {
            return false;
        }
    } else {
        if (stricmp(game_type, "GAME_GLYPHX_MULTIPLAYER") == 0) {
            GAME_TO_PLAY = GAME_GLYPHX_MULTIPLAYER;
            scen_player = SCEN_PLAYER_MPLAYER;
        } else {
            return false;
        }
    }

    /*
    **	Load the scenario.  Specify variation 'A' for the editor; for the game,
    **	don't specify a variation, to make 'Set_Scenario_Name()' pick a random one.
    **	Skip this if we've already loaded a save-game.
    */
    Force_CD_Available(ALWAYS_RELOAD_CD);

    if (override_map_name && strlen(override_map_name)) {
        strcpy(Scen.ScenarioName, override_map_name);
    } else {
        Scen.Set_Scenario_Name(Scen.Scenario, scen_player, scen_dir, (ScenarioVarType)scenario_variation);
    }

    HiddenPage.Clear();
    VisiblePage.Clear();

    /*
    ** Set the mouse to some position where it's not going to scroll, or do something else wierd.
    */
    DLLForceMouseX = 100;
    DLLForceMouseY = 100;
    KEYBOARD->MouseQX = 100;
    KEYBOARD->MouseQY = 100;

    GlyphXClientSidebarWidthInLeptons = 0;

    Seed = CustomSeed != 0 ? CustomSeed : GetRandSeed();
    Scen.RandomNumber = Seed;

    if (!Start_Scenario(Scen.ScenarioName)) {
        return (false);
    }

    DLLExportClass::Reset_Sidebars();
    DLLExportClass::Reset_Player_Context();
    DLLExportClass::Calculate_Start_Positions();

    /*
    ** Make sure the scroll constraints are applied. This is important for GDI 1 where the map isn't wide enough for the
    *screen
    */
    COORDINATE origin_coord = Coord_Add(Map.TacticalCoord, XY_Coord(1, 0));
    Map.Set_Tactical_Position(origin_coord);
    origin_coord = Coord_Add(Map.TacticalCoord, XY_Coord(-1, 0));
    Map.Set_Tactical_Position(origin_coord);

    if (GAME_TO_PLAY == GAME_NORMAL) {
        MPSuperWeaponDisable = false;
    } else {
        if (MPSuperWeaponDisable) {
            /*
            ** Write over the tecb level settings we just loaded from the Rules ini
            */
            Rule.GPSTechLevel = 100;
            Rule.ParaInfantryTechLevel = 100;
            Rule.SpyPlaneTechLevel = 100;
            Rule.ParaBombTechLevel = 100;
            Rule.ChronoTechLevel = 100;
        }
    }

    /*
    **	Hide the SeenBuff; force the map to render one frame.  The caller can
    **	then fade the palette in.
    **	(If we loaded a game, this step will fade out the title screen.  If we
    **	started a scenario, Start_Scenario() will have played a couple of VQ
    **	movies, which will have cleared the screen to black already.)
    */

    // Fade_Palette_To(BlackPalette, FADE_PALETTE_MEDIUM, Call_Back);
    HiddenPage.Clear();
    VisiblePage.Clear();
    Set_Logic_Page(SeenBuff);

    /*
    ** Sidebar is always active in hi-res.
    */
    if (!Debug_Map) {
        Map.SidebarClass::Activate(1);
    }

    Map.Flag_To_Redraw(true);

    Set_Palette(GamePalette.Get_Data());
    Map.Render();

    Set_Palette(GamePalette.Get_Data());

    return true;
}

/**************************************************************************************************
 * CNC_Read_INI -- Load an ini file into the supplied buffer
 *
 * In:   Map initialization parameters
 *
 * Out:  false if ini load failed
 *
 *
 * History: 12/16/2019 11:44AM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl CNC_Read_INI(int scenario_index,
                                                           int scenario_variation,
                                                           int scenario_direction,
                                                           const char* content_directory,
                                                           const char* override_map_name,
                                                           char* ini_buffer,
                                                           int _ini_buffer_size)
{
    if (content_directory == NULL) {
        return false;
    }

    DLLExportClass::Set_Content_Directory(content_directory);

    Scen.Scenario = scenario_index;
    ScenarioDirType scen_dir = (ScenarioDirType)scenario_direction;

    GAME_TO_PLAY = GAME_GLYPHX_MULTIPLAYER;
    ScenarioPlayerType scen_player = SCEN_PLAYER_MPLAYER;

    Force_CD_Available(ALWAYS_RELOAD_CD);

    if (override_map_name && strlen(override_map_name)) {
        strcpy(Scen.ScenarioName, override_map_name);
    } else {
        Scen.Set_Scenario_Name(Scen.Scenario, scen_player, scen_dir, (ScenarioVarType)scenario_variation);
    }

    if (_ini_buffer_size < _ShapeBufferSize) {
        GlyphX_Debug_Print("INI file buffer may be too small");
        return false;
    }

    if (!ini_buffer) {
        GlyphX_Debug_Print("No INI file buffer");
        return false;
    }

    memset(ini_buffer, _ini_buffer_size, 0);

    CCFileClass file(Scen.ScenarioName);
    if (!file.Is_Available()) {
        GlyphX_Debug_Print("Failed to find scenario file");
        GlyphX_Debug_Print(Scen.ScenarioName);
        return (false);

    } else {

        GlyphX_Debug_Print("Opened scenario file");
        GlyphX_Debug_Print(Scen.ScenarioName);

        int bytes_read = file.Read(ini_buffer, _ini_buffer_size - 1);
        if (bytes_read == _ini_buffer_size - 1) {
            GlyphX_Debug_Print("INI file buffer is too small");
            return false;
        }
    }

    /*
    ** Ini buffer should be zero terminated
    */
    if ((int)strlen(ini_buffer) >= _ini_buffer_size) {
        GlyphX_Debug_Print("INI file buffer overrun");
        return false;
    }

    return true;
}

/**************************************************************************************************
 * CNC_Start_Custom_Instance -- Load and start a custom map
 *
 * In:   Map initialization parameters
 *
 * Out:  false if map load failed
 *
 *
 *
 * History: 2019/10/28 - JAS
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl CNC_Start_Custom_Instance(const char* content_directory,
                                                                        const char* directory_path,
                                                                        const char* scenario_name,
                                                                        int build_level,
                                                                        bool multiplayer)
{

    GlyphX_Debug_Print("RA3D|stage|scenario.begin");

    DLLExportClass::Set_Content_Directory(content_directory);

    char fullname[_MAX_FNAME + _MAX_EXT];

    snprintf(fullname, _MAX_FNAME + _MAX_EXT, "%s%s.mpr", directory_path, scenario_name);

    /*
    **	OpenCNC3D: the 1996 disc's multiplayer maps are SCM01EA.INI .. SCM24EA.INI inside
    **	GENERAL.MIX, and the remaster's .mpr is the same INI under another extension. When no
    **	.mpr is beside it, the map's own .INI is read, so a staged disc map plays as it is.
    */
    {
        CCFileClass mpr(fullname);
        if (!mpr.Is_Available()) {
            char ininame[_MAX_FNAME + _MAX_EXT];
            snprintf(ininame, sizeof(ininame), "%s%s.INI", directory_path, scenario_name);
            CCFileClass ini_file(ininame);
            if (ini_file.Is_Available()) {
                strncpy(fullname, ininame, sizeof(fullname) - 1);
                fullname[sizeof(fullname) - 1] = '\0';
            }
        }
    }

    char name_buffer[128];
    char digest_buffer[32];

    CCFileClass file(fullname);

    INIClass ini;
    ini.Load(file);

    ini.Get_String("Basic", "Name", "No Name", name_buffer, sizeof(name_buffer));
    ini.Get_String("Digest", "1", "No Digest", digest_buffer, sizeof(digest_buffer));
    Session.Scenarios.Add(
        new MultiMission(fullname, name_buffer, digest_buffer, ini.Get_Bool("Basic", "Official", false), false));

    BuildLevel = build_level;

    ScenarioPlayerType scen_player;
    strncpy(Scen.ScenarioName, fullname, _MAX_FNAME + _MAX_EXT);

    if (multiplayer) {
        GAME_TO_PLAY = GAME_GLYPHX_MULTIPLAYER;
    } else {
        GAME_TO_PLAY = GAME_NORMAL;
    }
    scen_player = SCEN_PLAYER_MPLAYER;

    // Scen.Scenario = scenario_index;
    // ScenarioDirType scen_dir = (ScenarioDirType)scenario_direction;

    // if (stricmp(game_type, "GAME_NORMAL") == 0) {
    //	GAME_TO_PLAY = GAME_NORMAL;
    //}
    // else {
    //	if (stricmp(game_type, "GAME_GLYPHX_MULTIPLAYER") == 0) {
    //		GAME_TO_PLAY = GAME_GLYPHX_MULTIPLAYER;
    //		scen_player = SCEN_PLAYER_MPLAYER;
    //	}
    //	else {
    //		return false;
    //	}
    //}

    ///*
    //**	Load the scenario.  Specify variation 'A' for the editor; for the game,
    //**	don't specify a variation, to make 'Set_Scenario_Name()' pick a random one.
    //**	Skip this if we've already loaded a save-game.
    //*/

    // Force_CD_Available(ALWAYS_RELOAD_CD);

    // if (override_map_name && strlen(override_map_name)) {
    //	strcpy(Scen.ScenarioName, override_map_name);
    //}
    // else {
    //	Scen.Set_Scenario_Name(Scen.Scenario, scen_player, scen_dir, (ScenarioVarType)scenario_variation);
    //}

    HiddenPage.Clear();
    VisiblePage.Clear();

    /*
    ** Set the mouse to some position where it's not going to scroll, or do something else wierd.
    */
    DLLForceMouseX = 100;
    DLLForceMouseY = 100;
    KEYBOARD->MouseQX = 100;
    KEYBOARD->MouseQY = 100;

    GlyphXClientSidebarWidthInLeptons = 0;

    /*
    **	OpenCNC3D lockstep: EVERY PEER MUST START FROM THE SAME STREAM. Red Alert seeds the
    **	synchronised simulation stream afresh at every scenario start, from the wall clock
    **	(GetRandSeed is GetTickCount), so two peers of one match would deal different starts
    **	and diverge on the first random draw. In a lockstep match the seed is the one the
    **	host agreed through OpenCNC3D_Set_Seed, and zero when none was agreed, which is the
    **	same on every peer by construction. Outside a match the 1996 behaviour stands: a
    **	seed given on the command line or through OpenCNC3D_Set_Seed, otherwise the clock.
    **	Because the stream is reseeded here, a process that has already played a scenario
    **	starts the next one exactly where a fresh process would.
    */
    if (OpenCNC3D_Lockstep) {
        Seed = CustomSeed;
    } else {
        Seed = CustomSeed != 0 ? CustomSeed : GetRandSeed();
    }
    Scen.RandomNumber = Seed;

    /*
    **	OpenCNC3D: A NEW MATCH STARTS ITS CLOCK AT ZERO AND WITH NO ORDERS PENDING. In 1996 the
    **	reset lives in Select_Game (init.cpp, "[Re]set any globals that need it, in preparation
    **	for a new scenario"), which is the DOS menu and is never reached here, so a second match
    **	in one process inherited the first one's frame counter and any orders still queued.
    **	Frame is the master clock (build timers, AI think intervals, trigger times, super weapon
    **	charge, and the frame every lockstep order is stamped for), so two peers of a match, one
    **	of which had played before, disagreed from the first tick. This restores what a freshly
    **	launched game does.
    */
    Frame = 0;
    DoList.Init();
    OutList.Init();

    if (!Start_Scenario(Scen.ScenarioName)) {
        GlyphX_Debug_Print("RA3D|stage|scenario.failed");
        return (false);
    }

    GlyphX_Debug_Print("RA3D|stage|scenario.loaded");

    DLLExportClass::Reset_Sidebars();
    DLLExportClass::Reset_Player_Context();
    DLLExportClass::Calculate_Start_Positions();

    /*
    ** Make sure the scroll constraints are applied. This is important for GDI 1 where the map isn't wide enough for the
    *screen
    */
    COORDINATE origin_coord = Coord_Add(Map.TacticalCoord, XY_Coord(1, 0));
    Map.Set_Tactical_Position(origin_coord);
    origin_coord = Coord_Add(Map.TacticalCoord, XY_Coord(-1, 0));
    Map.Set_Tactical_Position(origin_coord);

    if (GAME_TO_PLAY == GAME_NORMAL) {
        MPSuperWeaponDisable = false;
    } else {
        if (MPSuperWeaponDisable) {
            /*
            ** Write over the tecb level settings we just loaded from the Rules ini
            */
            Rule.GPSTechLevel = 100;
            Rule.ParaInfantryTechLevel = 100;
            Rule.SpyPlaneTechLevel = 100;
            Rule.ParaBombTechLevel = 100;
            Rule.ChronoTechLevel = 100;
        }
    }

    /*
    **	Hide the SeenBuff; force the map to render one frame.  The caller can
    **	then fade the palette in.
    **	(If we loaded a game, this step will fade out the title screen.  If we
    **	started a scenario, Start_Scenario() will have played a couple of VQ
    **	movies, which will have cleared the screen to black already.)
    */

    // Fade_Palette_To(BlackPalette, FADE_PALETTE_MEDIUM, Call_Back);
    HiddenPage.Clear();
    VisiblePage.Clear();
    Set_Logic_Page(SeenBuff);
    /*
    ** Sidebar is always active in hi-res.
    */
    if (!Debug_Map) {
        Map.SidebarClass::Activate(1);
    }
    if (!OpenCNC3DHeadless) {
        Map.Flag_To_Redraw(true);
        Set_Palette(GamePalette.Get_Data());
        Map.Render();
        Set_Palette(GamePalette.Get_Data());
    }

    GlyphX_Debug_Print("RA3D|stage|scenario.ready");

    return true;
}

bool Debug_Write_Shape_Type(const ObjectTypeClass* type, int shapenum)
{
    char fullname[_MAX_FNAME + _MAX_EXT];
    char buffer[_MAX_FNAME];
    CCFileClass file;

    if (type->ImageData != NULL) {

        sprintf(buffer, "%s_%d", type->IniName, shapenum);
        _makepath(fullname, NULL, NULL, buffer, ".PCX");

        return Debug_Write_Shape(fullname, type->ImageData, shapenum);
    }

    return false;
}

bool Debug_Write_Shape(const char* file_name, void const* shapefile, int shapenum, int flags, void const* ghostdata)
{
    /*
    ** Build frame returns a pointer now instead of the shapes length
    */
    char* shape_pointer = (char*)Build_Frame(shapefile, shapenum, _ShapeBuffer);
    if (shape_pointer == NULL) {
        return false;
        ;
    }
    if (Get_Last_Frame_Length() > _ShapeBufferSize) {
        return false;
        ;
    }

    int width = Get_Build_Frame_Width(shapefile);
    int height = Get_Build_Frame_Height(shapefile);

    GraphicBufferClass temp_gbuffer(width, height);
    GraphicViewPortClass temp_viewport(&temp_gbuffer, 0, 0, width, height);

    WindowList[WINDOW_CUSTOM][WINDOWX] = 0;
    WindowList[WINDOW_CUSTOM][WINDOWY] = 0;
    WindowList[WINDOW_CUSTOM][WINDOWWIDTH] = width;
    WindowList[WINDOW_CUSTOM][WINDOWHEIGHT] = height;

    static const char _shape_trans = 0x40;

    if (flags == 0) {
        Buffer_Frame_To_Page(0,
                             0,
                             width,
                             height,
                             shape_pointer,
                             temp_viewport,
                             SHAPE_NORMAL | SHAPE_WIN_REL | _shape_trans); //, ghostdata, predoffset);
    } else {
        Buffer_Frame_To_Page(0, 0, width, height, shape_pointer, temp_viewport, flags, ghostdata);
    }
    Write_PCX_File((char*)file_name, temp_viewport, (unsigned char*)GamePalette.Get_Data());
    return true;
}

/**************************************************************************************************
 * CNC_Advance_Instance -- Process one logic frame
 *
 * In:
 *
 * Out:  Is game still playing?
 *
 *
 *
 * History: 1/7/2019 5:20PM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl CNC_Advance_Instance(uint64 player_id)
{
    // DLLExportClass::Set_Event_Callback(event_callback);

    if (Frame <= 10) { // Don't spam forever, but useful to know that we actually started advancing
        GlyphX_Debug_Print("CNC_Advance_Instance - RA");
    }

    /*
    ** Shouldn't really need to do this, but I like the idea of always running the main loop in the context of the same
    *player.
    ** Might make tbe bugs more repeatable and consistent. ST - 3/15/2019 11:58AM
    */
    if (player_id != 0) {
        DLLExportClass::Set_Player_Context(player_id);
    } else {
        DLLExportClass::Set_Player_Context(DLLExportClass::GlyphxPlayerIDs[0]);
    }

    /*
    ** Restore special from backup
    */
    if (SpecialBackup != NULL) {
        memcpy(&Special, SpecialBackup, sizeof(SpecialClass));
    }

#ifdef FIXIT_CSII //	checked - ajw 9/28/98
    TimeQuake = PendingTimeQuake;
    PendingTimeQuake = false;
#else
    TimeQuake = false;
#endif

    /*
    ** Allocate extra memory for uncompressed shapes as needed
    */
    Reallocate_Big_Shape_Buffer();

    /*
    **	If there is no theme playing, but it looks like one is required, then start one
    **	playing. This is usually the symptom of there being no transition score.
    */
    // if (SampleType && Theme.What_Is_Playing() == THEME_NONE) {
    //	Theme.Queue_Song(THEME_PICK_ANOTHER);
    //}

    /*
    **	Update the display, unless we're inside a dialog.
    */
    // if (SpecialDialog == SDLG_NONE && GameInFocus) {

    // WWMouse->Erase_Mouse(&HidPage, TRUE);
    // Map.Input(input, x, y);
    // if (input) {
    //	Keyboard_Process(input);
    //}
    /*
    ** The main loop passes these in uninitialized. ST - 2/7/2019 4:36PM
    */
    KeyNumType input = KN_NONE; // Player input.
    int x = 0;
    int y = 0;
    Map.Input(input, x, y);
    // if (input) {
    //	Keyboard_Process(input);
    //}

    if (GAME_TO_PLAY == GAME_GLYPHX_MULTIPLAYER) {
        /*
        ** Process the sidebar. ST - 4/18/2019 11:59AM
        */
        HouseClass* old_player_ptr = PlayerPtr;
        for (int i = 0; i < MULTIPLAYER_COUNT; i++) {
            HouseClass* player_ptr = HouseClass::As_Pointer(Session.Players[i]->Player.ID);
            DLLExportClass::Logic_Switch_Player_Context(player_ptr);
            Sidebar_Glyphx_AI(player_ptr, input);
        }
        DLLExportClass::Logic_Switch_Player_Context(old_player_ptr);
    }

    //}

    /*
    ** Sort the map's ground layer by y-coordinate value.  This is done
    ** outside the IsToRedraw check, for the purposes of game sync'ing
    ** between machines; this way, all machines will sort the Map's
    ** layer in the same way, and any processing done that's based on
    ** the order of this layer will sync on different machines.
    */
    Map.Layer[LAYER_GROUND].Sort();

    /*
    **	AI logic operations are performed here.
    */
    // Skip this block of code on first update of single-player games. This helps prevents trigger generated messages on
    // the first update from being lost during loading screen or movie. - LLL
    static bool FirstUpdate = GAME_TO_PLAY != GAME_GLYPHX_MULTIPLAYER;
    ;
    if (!FirstUpdate) {
        HouseClass* old_player_ptr = PlayerPtr;
        Logic.Clear_Recently_Created_Bits();
        Logic.AI();
        DLLExportClass::Logic_Switch_Player_Context(old_player_ptr);
    }
    FirstUpdate = false;

    TimeQuake = false;
#ifdef FIXIT_CSII //	checked - ajw 9/28/98
    if (!PendingTimeQuake) {
        TimeQuakeCenter = 0;
    }
#endif

    /*
    **	Manage the inter-player message list.  If Manage() returns true, it means
    **	a message has expired & been removed, and the entire map must be updated.
    */
    if (Session.Messages.Manage()) {
        HiddenPage.Clear();
        Map.Flag_To_Redraw(true);
    }

    /*
    **	Process all commands that are ready to be processed.
    */
    if (GAME_TO_PLAY == GAME_NORMAL) {
        Queue_AI();
    } else {
        if (GAME_TO_PLAY == GAME_GLYPHX_MULTIPLAYER) {
            DLLExportClass::Glyphx_Queue_AI();

            /*
            ** Process the sidebar. ST - 3/22/2019 2:07PM
            */
            for (int i = 0; i < MULTIPLAYER_COUNT; i++) {
                HouseClass* player_ptr = HouseClass::As_Pointer(Session.Players[i]->Player.ID);
                Sidebar_Glyphx_Recalc(player_ptr);
            }
        }
    }

    /*
    **	Keep track of elapsed time in the game.
    */
    // Score.ElapsedTime += TIMER_SECOND / TICKS_PER_SECOND;

    /*
    **	Perform any win/lose code as indicated by the global control flags.
    */
    /*
    **	Check for player wins or loses according to global event flag.
    */
    if (PlayerWins) {

        // WWMouse->Erase_Mouse(&HidPage, TRUE);
        PlayerLoses = false;
        PlayerWins = false;
        PlayerRestarts = false;
        Map.Help_Text(TXT_NONE);
        GlyphX_Debug_Print("PlayerWins = true");

        if (GAME_TO_PLAY == GAME_GLYPHX_MULTIPLAYER) {
            DLLExportClass::On_Multiplayer_Game_Over();
        } else {
            DLLExportClass::On_Game_Over(player_id, true);
        }

        // DLLExportClass::Set_Event_Callback(NULL);
        return false;
    }
    if (PlayerLoses) {

        // WWMouse->Erase_Mouse(&HidPage, TRUE);
        PlayerWins = false;
        PlayerLoses = false;
        PlayerRestarts = false;
        Map.Help_Text(TXT_NONE);
        GlyphX_Debug_Print("PlayerLoses = true");

        if (GAME_TO_PLAY == GAME_GLYPHX_MULTIPLAYER) {
            DLLExportClass::On_Multiplayer_Game_Over();
        } else {
            DLLExportClass::On_Game_Over(player_id, false);
        }

        // DLLExportClass::Set_Event_Callback(NULL);
        return false;
    }

    /*
    **	The frame logic has been completed. Increment the frame
    **	counter.
    */
    Frame++;

    /*
    ** Very rarely, the human players will get a message from the computer.
    **
    ** This was disabled in the RA source code, so copying over the functionality from TD
    */
    if (GAME_TO_PLAY != GAME_NORMAL && Session.Options.Ghosts && IRandom(0, 10000) == 1) {
        DLLExportClass::Computer_Message(false);
    }

    if (ProgEndCalled) {
        GlyphX_Debug_Print("ProgEndCalled - GameActive = false");
        GameActive = false;
    }

    if (!OpenCNC3DHeadless && DLLExportClass::Legacy_Render_Enabled()) {
        Map.Render();
    }

    // Sync_Delay();
    // DLLExportClass::Set_Event_Callback(NULL);
    Color_Cycle();

    /*
    ** Don't respect GameActive. Game will end in multiplayer on win/loss
    */
    if (GAME_TO_PLAY == GAME_GLYPHX_MULTIPLAYER) {
        return true;
    }

    return (GameActive);
}

/**************************************************************************************************
 * CNC_Save_Load -- Process a save or load game action
 *
 * In:
 *
 * Out:  Success?
 *
 *
 *
 * History: 1/7/2019 5:20PM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl CNC_Save_Load(bool save,
                                                            const char* file_path_and_name,
                                                            const char* game_type)
{
    bool result = false;

    if (save) {
        result = Save_Game(file_path_and_name, "internal");
    } else {

        if (game_type == NULL) {
            return false;
        }

        if (stricmp(game_type, "GAME_NORMAL") == 0) {
            GAME_TO_PLAY = GAME_NORMAL;
        } else {
            if (stricmp(game_type, "GAME_GLYPHX_MULTIPLAYER") == 0) {
                GAME_TO_PLAY = GAME_GLYPHX_MULTIPLAYER;
                // ScenPlayer = SCEN_PLAYER_MPLAYER;
            } else {
                return false;
            }
        }

        while (Session.Players.Count() > 0) {
            delete Session.Players[0];
            Session.Players.Delete(Session.Players[0]);
        }

        result = Load_Game(file_path_and_name);

        if (result == false) {
            return false;
        }

        DLLExportClass::Set_Player_Context(DLLExportClass::GlyphxPlayerIDs[0], true);
        DLLExportClass::Cancel_Placement(DLLExportClass::GlyphxPlayerIDs[0], -1, -1);
        Set_Logic_Page(SeenBuff);
        VisiblePage.Clear();
        Map.Flag_To_Redraw(true);
        if (DLLExportClass::Legacy_Render_Enabled()) {
            Map.Render();
        }
        Set_Palette(GamePalette.Get_Data());
    }

    return result;
}

/**************************************************************************************************
 * CNC_Set_Difficulty -- Set game difficulty
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 10/02/2019 - SKY
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Set_Difficulty(int difficulty)
{
    if (GAME_TO_PLAY == GAME_NORMAL) {
        Set_Scenario_Difficulty(difficulty);
    }
}

/* Campaign difficulty is chosen before the mission loads, just as Red Alert's
 * Fetch_Difficulty does. CNC_Set_Difficulty changes only the player after boot;
 * this sets both campaign handicaps while Read_Scenario can still inherit them. */
extern "C" __declspec(dllexport) void __cdecl OpenCNC3D_Set_Campaign_Difficulty(int choice)
{
    switch (choice) {
    case 0:
        Scen.CDifficulty = DIFF_HARD;
        Scen.Difficulty = DIFF_EASY;
        break;
    case 2:
        Scen.CDifficulty = DIFF_EASY;
        Scen.Difficulty = DIFF_HARD;
        break;
    default:
        Scen.CDifficulty = DIFF_NORMAL;
        Scen.Difficulty = DIFF_NORMAL;
        break;
    }
}

/* THE COMPUTER PLAYERS' DIFFICULTY FOR A NETWORK MATCH, chosen before the match starts:
 * 0 easy, 1 normal, 2 hard, the positions of Red Alert's skirmish slider. Only the
 * computers' handicap moves; every person plays at normal, as in Red Alert's own network
 * games, so all the peers' people are alike whatever any of them played before. A
 * skirmish, with one person, takes OpenCNC3D_Set_Campaign_Difficulty, which is the 1996
 * skirmish slider's rule for both handicaps. */
extern "C" __declspec(dllexport) void __cdecl OpenCNC3D_Set_Computer_Difficulty(int choice)
{
    Scen.Difficulty = DIFF_NORMAL;
    Scen.CDifficulty = choice == 0 ? DIFF_HARD : choice == 2 ? DIFF_EASY : DIFF_NORMAL;
}

/**************************************************************************************************
 * CNC_Restore_Carryover_Objects
 *
 * History: 11/15/2019 - LLL
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Restore_Carryover_Objects(const CarryoverObjectStruct* objects)
{
    // Delete the list
    while (Carryover) {
        CarryoverClass* cptr = (CarryoverClass*)Carryover->Get_Next();
        delete Carryover;
        Carryover = cptr;
    }

    // Populate the list
    const CarryoverObjectStruct* next_object = objects;
    while (next_object) {
        CarryoverClass* cptr = new CarryoverClass();

        cptr->RTTI = (RTTIType)next_object->RTTI;
        cptr->Type.Building = (StructType)next_object->Type; // This works regardless of what the RTTI-type and the enum
                                                             // type, because they're all enums. - LLL
        cptr->Cell = (CELL)next_object->Cell;
        cptr->House = (HousesType)next_object->House;
        cptr->Strength = next_object->Strength;

        next_object = next_object->Next;

        if (Carryover == NULL) {
            Carryover = cptr;
        } else {
            cptr->Add_Tail(*Carryover);
        }
    }
}

/**************************************************************************************************
 * CNC_Handle_Player_Switch_To_AI -- Renamed 3/9/2020 - LLL
 * CNC_Handle_Player_Disconnect -- Handle player disconnected during multiplayer game
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 12/3/2019 1:46PM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Player_Switch_To_AI(uint64 player_id)
{
    if (PlayerWins || PlayerLoses || DLLExportClass::Get_Game_Over()) {
        return;
    }

    GlyphX_Debug_Print("CNC_Handle_Player_Switch_To_AI");

    if (GAME_TO_PLAY == GAME_NORMAL) {
        return;
    }

#ifdef KILL_PLAYER_ON_DISCONNECT

    /*
    ** Kill player's units on disconnect.
    */
    if (player_id != 0) {
        DLLExportClass::Set_Player_Context(player_id);

        if (PlayerPtr) {
            PlayerPtr->Flag_To_Die();
        }
    }

#else // KILL_PLAYER_ON_DISCONNECT

    if (player_id != 0) {

        HousesType house;
        HouseClass* ptr;

        DLLExportClass::Set_Player_Context(player_id);

        if (PlayerPtr) {
            PlayerPtr->WasHuman = true;
            PlayerPtr->IsHuman = false;
            PlayerPtr->IQ = Rule.MaxIQ;
            FairAI_RA_Assign(PlayerPtr, -1);
            strcpy(PlayerPtr->IniName, Text_String(TXT_COMPUTER));

            PlayerPtr->IsBaseBuilding = true;

            /*
            ** Start the unload mission for MCVs
            */
            for (int index = 0; index < Units.Count(); index++) {
                UnitClass* obj = Units.Ptr(index);

                if (obj && !obj->IsInLimbo && obj->House == PlayerPtr) {
                    if (*obj == UNIT_MCV) {
                        obj->Assign_Mission(MISSION_GUARD);
                        obj->Assign_Target(TARGET_NONE);
                        obj->Assign_Destination(TARGET_NONE);
                        obj->Assign_Mission(MISSION_UNLOAD);
                        obj->Commence();
                    }
                }
            }

            DLLExportClass::On_Message(PlayerPtr, "", 60.0f, MESSAGE_TYPE_PLAYER_DISCONNECTED, -1);

            /*
            ** Send the disconnect taunt message
            */
            int human_count = 0;
            for (house = HOUSE_MULTI1; house < (HOUSE_MULTI1 + MULTIPLAYER_COUNT); house++) {
                ptr = HouseClass::As_Pointer(house);

                if (ptr && ptr->IsHuman && !ptr->IsDefeated) {
                    human_count++;
                }
            }

            if (human_count == 1) {
                DLLExportClass::Computer_Message(true);
            }
        }
    }

#endif // KILL_PLAYER_ON_DISCONNECT
}

/**************************************************************************************************
 * CNC_Handle_Human_Team_Wins
 *
 * History: 3/10/2020 - LLL
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Human_Team_Wins(uint64 quitting_player_id)
{
    GlyphX_Debug_Print("CNC_Handle_Human_Team_Wins");
    DLLExportClass::Force_Human_Team_Wins(quitting_player_id);
}

/**************************************************************************************************
 * CNC_Start_Mission_Timer
 *
 * History: 11/25/2019 - LLL
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Start_Mission_Timer(int time)
{
    if (GameActive) {
        Scen.MissionTimer = time;

        if (!Scen.MissionTimer.Is_Active()) {
            Scen.MissionTimer.Start();
        }

        Map.Redraw_Tab();
    }
}

/**************************************************************************************************
 * CNC_Get_Start_Game_Info
 *
 * History: 8/31/2020 11:37AM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl CNC_Get_Start_Game_Info(uint64 player_id,
                                                                      int& start_location_waypoint_index)
{
    start_location_waypoint_index = 0;
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return false;
    }

    start_location_waypoint_index = PlayerPtr->StartLocationOverride;
    return true;
}

/**************************************************************************************************
 * Is_Legacy_Render_Enabled -- Is the legacy rendering enabled?
 *
 * In:
 *
 * Out: True if only one human player
 *
 *
 *
 * History: 8/25/2020 5:55PM - ST
 **************************************************************************************************/
bool Is_Legacy_Render_Enabled(void)
{
    return DLLExportClass::Legacy_Render_Enabled();
}

/**************************************************************************************************
 * DLLExportClass::Init -- Init the class
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 3/12/2019 10:52AM - ST
 **************************************************************************************************/
void DLLExportClass::Init(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        GlyphxPlayerIDs[i] = 0xffffffffull;
    }

    CurrentLocalPlayerIndex = 0;

    MessagesSent.clear();

    if (SpecialBackup == NULL) {
        SpecialBackup = new SpecialClass;
    }
    memcpy(SpecialBackup, &Special, sizeof(SpecialClass));
}

/**************************************************************************************************
 * DLLExportClass::Shutdown -- Shutdown
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 2/20/2020 1:59PM - ST
 **************************************************************************************************/
void DLLExportClass::Shutdown(void)
{
    delete SpecialBackup;
    SpecialBackup = NULL;

    for (int i = 0; i < ModSearchPaths.Count(); i++) {
        delete[] ModSearchPaths[i];
    }
    ModSearchPaths.Clear();
}

/**************************************************************************************************
 * DLLExportClass::Add_Mod_Path -- Add a path to load mod files from
 *
 * In: Mod path
 *
 * Out:
 *
 *
 *
 * History: 2/20/2020 2:03PM - ST
 **************************************************************************************************/
void DLLExportClass::Add_Mod_Path(const char* mod_path)
{
    char* copy_path = strdup(mod_path);
    ModSearchPaths.Add(copy_path);
}

/**************************************************************************************************
 * DLLExportClass::Set_Content_Directory -- Update the locations that the original code will load from
 *
 * In: Main (official) content directory
 *
 * Out:
 *
 *
 *
 * History: 2/20/2020 2:03PM - ST
 **************************************************************************************************/
void DLLExportClass::Set_Content_Directory(const char* content_directory)
{
    CCFileClass::Clear_Search_Drives();
    CCFileClass::Reset_Raw_Path();

    if ((content_directory == NULL || strlen(content_directory) == 0) && ModSearchPaths.Count() == 0) {
        return;
    }

    char* all_paths = new char[_MAX_PATH * 100];
    *all_paths = 0;

    for (int i = 0; i < ModSearchPaths.Count(); i++) {
        if (i != 0) {
            strcat(all_paths, ";");
        }
        strcat(all_paths, ModSearchPaths[i]);
    }

    if (ModSearchPaths.Count() && content_directory && strlen(content_directory)) {
        strcat(all_paths, ";");
    }
    if (content_directory) {
        strcat(all_paths, content_directory);
    }

    CCFileClass::Set_Search_Drives(all_paths);
    delete[] all_paths;
}

/**************************************************************************************************
 * DLLExportClass::Config
 *
 * History: 1/16/2020 - SKY
 **************************************************************************************************/
void DLLExportClass::Config(const CNCRulesDataStruct& rules)
{
    for (int i = 0; i < 3; ++i) {
        Rule.Diff[i].FirepowerBias = rules.Difficulties[i].FirepowerBias;
        Rule.Diff[i].GroundspeedBias = rules.Difficulties[i].GroundspeedBias;
        Rule.Diff[i].AirspeedBias = rules.Difficulties[i].AirspeedBias;
        Rule.Diff[i].ArmorBias = rules.Difficulties[i].ArmorBias;
        Rule.Diff[i].ROFBias = rules.Difficulties[i].ROFBias;
        Rule.Diff[i].CostBias = rules.Difficulties[i].CostBias;
        Rule.Diff[i].BuildSpeedBias = rules.Difficulties[i].BuildSpeedBias;
        Rule.Diff[i].RepairDelay = rules.Difficulties[i].RepairDelay;
        Rule.Diff[i].BuildDelay = rules.Difficulties[i].BuildDelay;
        Rule.Diff[i].IsBuildSlowdown = rules.Difficulties[i].IsBuildSlowdown ? 1 : 0;
        Rule.Diff[i].IsWallDestroyer = rules.Difficulties[i].IsWallDestroyer ? 1 : 0;
        Rule.Diff[i].IsContentScan = rules.Difficulties[i].IsContentScan ? 1 : 0;
    }
}

/**************************************************************************************************
 * DLLExportClass::Set_Home_Cell
 *
 * History: 2/14/2020 - LLL
 **************************************************************************************************/
void DLLExportClass::Set_Home_Cell(int x, int y, uint64 player_id)
{
    if (GAME_TO_PLAY == GAME_NORMAL) {
        MultiplayerStartPositions[0] = XY_Cell(x, y);
    } else {
        for (int i = 0; i < MAX_PLAYERS && i < 4; i++) {
            if (GlyphxPlayerIDs[i] == player_id) {
                MultiplayerStartPositions[i] = XY_Cell(x, y);
            }
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::On_Play_Movie
 *
 * History: 7/23/2019 - LLL
 **************************************************************************************************/
void DLLExportClass::On_Play_Movie(const char* movie_name, ThemeType theme, bool immediate)
{
    if (EventCallback == NULL) {
        return;
    }

    EventCallbackStruct new_event;
    new_event.EventType = CALLBACK_EVENT_MOVIE;
    new_event.Movie.MovieName = movie_name;
    new_event.Movie.Theme = (int)theme;
    new_event.Movie.Immediate = immediate;

    EventCallback(new_event);
}

/**************************************************************************************************
 * DLLExportClass::On_Display_Briefing_Text
 *
 * Called when Red Alert wants to display the mission breifing screen before a mission.
 *
 * History: 12/04/2019 - LLL
 **************************************************************************************************/
void DLLExportClass::On_Display_Briefing_Text()
{
    if (EventCallback == NULL) {
        return;
    }

    EventCallbackStruct new_event;
    new_event.EventType = CALLBACK_EVENT_BRIEFING_SCREEN;
    EventCallback(new_event);
}

/**************************************************************************************************
 * DLLExportClass::On_Sound_Effect -- Called when C&C wants to play a sound effect
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 2/20/2019 2:39PM - ST
 **************************************************************************************************/
void DLLExportClass::On_Sound_Effect(const HouseClass* player_ptr,
                                     int sound_effect_index,
                                     const char* extension,
                                     int variation,
                                     COORDINATE coord)
{
    // player_ptr could be NULL

    if (EventCallback == NULL) {
        return;
    }

    EventCallbackStruct new_event;
    new_event.EventType = CALLBACK_EVENT_SOUND_EFFECT;
    new_event.SoundEffect.SFXIndex = sound_effect_index;
    new_event.SoundEffect.Variation = variation;

    new_event.GlyphXPlayerID = 0;
    if (player_ptr != NULL) {
        new_event.GlyphXPlayerID = Get_GlyphX_Player_ID(player_ptr);
    }

    if (coord == 0) {
        new_event.SoundEffect.PixelX = -1;
        new_event.SoundEffect.PixelY = -1;
    } else {
        // Use world pixel coordinates
        new_event.SoundEffect.PixelX = Lepton_To_Pixel(Coord_X(coord));
        new_event.SoundEffect.PixelY = Lepton_To_Pixel(Coord_Y(coord));
    }

    if (sound_effect_index >= VOC_FIRST && sound_effect_index < VOC_COUNT) {
        strncpy(new_event.SoundEffect.SoundEffectName, SoundEffectName[sound_effect_index].Name, 15);
        new_event.SoundEffect.SoundEffectName[15] = 0;
        if (extension != NULL) {
            const size_t used = strlen(new_event.SoundEffect.SoundEffectName);
            strncat(new_event.SoundEffect.SoundEffectName, extension, 15 - used);
        }
        new_event.SoundEffect.SoundEffectPriority = SoundEffectName[sound_effect_index].Priority;
        new_event.SoundEffect.SoundEffectContext = SoundEffectName[sound_effect_index].Where;
    } else {
        strcpy(new_event.SoundEffect.SoundEffectName, "BADINDEX");
        new_event.SoundEffect.SoundEffectPriority = -1;
        new_event.SoundEffect.SoundEffectContext = -1;
    }

    EventCallback(new_event);
}

/**************************************************************************************************
 * DLLExportClass::On_Speech -- Called when C&C wants to play a speech line
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 2/20/2019 2:39PM - ST
 **************************************************************************************************/
void DLLExportClass::On_Speech(const HouseClass* player_ptr, int speech_index)
{
    // player_ptr could be NULL

    if (EventCallback == NULL) {
        return;
    }

    EventCallbackStruct new_event;
    new_event.EventType = CALLBACK_EVENT_SPEECH;
    new_event.Speech.SpeechIndex = speech_index;

    new_event.GlyphXPlayerID = 0;
    if (player_ptr != NULL) {
        new_event.GlyphXPlayerID = Get_GlyphX_Player_ID(player_ptr);
    }

    if (speech_index >= VOX_FIRST && speech_index < VOX_COUNT) {
        strncpy(new_event.Speech.SpeechName, Speech[speech_index], 15);
        new_event.Speech.SpeechName[15] = 0;
    } else {
        strncpy(new_event.Speech.SpeechName, "BAD_SPEECH_INDE", 16);
    }

    EventCallback(new_event);
}

/**************************************************************************************************
 * DLLExportClass::RA_Calculate_Leadership --
 *
 * History: 10.30.2019 MBL (Based on LLL's Calculate_Single_Player_Score())
 **************************************************************************************************/
int DLLExportClass::RA_Calculate_Leadership(HousesType player_house, int units_lost, int buildings_lost)
{
    int house = (player_house == HOUSE_USSR || player_house == HOUSE_UKRAINE); // 0 or 1

    int leadership = 0;
    for (int index = 0; index < Logic.Count(); index++) {
        ObjectClass* object = Logic[index];
        HousesType owner = object->Owner();
        if ((house) && (owner == HOUSE_USSR || owner == HOUSE_BAD || owner == HOUSE_UKRAINE)) {
            leadership++;
        } else {
            if ((!house) && (object->Owner() == HOUSE_GREECE)) {
                leadership++;
            }
        }
    }
    if (!leadership)
        leadership++;
    leadership = 100 * fixed(leadership, (units_lost + buildings_lost + leadership));
    leadership = min(150, leadership);

    return leadership;
}

/**************************************************************************************************
 * DLLExportClass::RA_Calculate_Economy --
 *
 * History: 10.30.2019 MBL (Based on LLL's Calculate_Single_Player_Score())
 **************************************************************************************************/
int DLLExportClass::RA_Calculate_Economy(long available_money,
                                         int stolen_buildings_credits,
                                         unsigned harvested_credits,
                                         long initial_credits)
{
    int economy = 100
                  * fixed((unsigned)available_money + 1 + stolen_buildings_credits,
                          harvested_credits + (unsigned)initial_credits + 1);
    economy = min(economy, 150);

    return economy;
}

/**************************************************************************************************
 * DLLExportClass::RA_Calculate_Score --
 *
 * History: 10.30.2019 MBL (Based on LLL's Calculate_Single_Player_Score())
 **************************************************************************************************/
int DLLExportClass::RA_Calculate_Score(int uspoints, int leadership, int economy)
{
    int score_total = ((uspoints * leadership) / 100) + ((uspoints * economy) / 100);
    if (score_total < -9999)
        score_total = -9999;
    score_total = min(score_total, 99999);

    return score_total;
}

/**************************************************************************************************
 * DLLExportClass::CalculateScore*
 *
 * History: 10/16/2019 - LLL
 **************************************************************************************************/
void DLLExportClass::Calculate_Single_Player_Score(EventCallbackStruct& event)
{
    // Adapted from Red Alert SCORE.CPP Presentation() - LLL
    int house = (PlayerPtr->Class->House == HOUSE_USSR || PlayerPtr->Class->House == HOUSE_UKRAINE); // 0 or 1

    int good_units_lost = (HouseClass::As_Pointer(HOUSE_GOOD))->UnitsLost;
    int bad_units_lost = (HouseClass::As_Pointer(HOUSE_BAD))->UnitsLost;
    int neutral_units_lost = (HouseClass::As_Pointer(HOUSE_NEUTRAL))->UnitsLost;
    int good_buildings_lost = (HouseClass::As_Pointer(HOUSE_GOOD))->BuildingsLost;
    int bad_buildings_lost = (HouseClass::As_Pointer(HOUSE_BAD))->BuildingsLost;
    int neutral_buildings_lost = (HouseClass::As_Pointer(HOUSE_NEUTRAL))->BuildingsLost;
    int good_credits_harvested = (HouseClass::As_Pointer(HOUSE_GOOD))->HarvestedCredits;
    int bad_credits_harvested = (HouseClass::As_Pointer(HOUSE_BAD))->HarvestedCredits;

    int uspoints = 0;

    for (HousesType hous = HOUSE_SPAIN; hous <= HOUSE_BAD; hous++) {
        HouseClass* hows = HouseClass::As_Pointer(hous);
        if (hous == HOUSE_USSR || hous == HOUSE_BAD || hous == HOUSE_UKRAINE) {
            bad_units_lost += hows->UnitsLost;
            bad_buildings_lost += hows->BuildingsLost;
        } else {
            good_units_lost += hows->UnitsLost;
            good_buildings_lost += hows->BuildingsLost;
        }
        if (PlayerPtr->Is_Ally(hous)) {
            uspoints += hows->PointTotal;
        }
    }
    //	if(uspoints < 0) uspoints = 0;
    //	uspoints += 1000; //BG 1000 bonus points for winning mission

    /*
    **	Bias the base score upward according to the difficulty level.
    */
    switch (PlayerPtr->Difficulty) {
    case DIFF_EASY:
        uspoints += 500;
        break;

    case DIFF_NORMAL:
        uspoints += 1500;
        break;

    case DIFF_HARD:
        uspoints += 3500;
        break;
    }

    /*
    **	Determine leadership rating.
    */
    int leadership = 0;
    for (int index = 0; index < Logic.Count(); index++) {
        ObjectClass* object = Logic[index];
        HousesType owner = object->Owner();
        if ((house) && (owner == HOUSE_USSR || owner == HOUSE_BAD || owner == HOUSE_UKRAINE)) {
            leadership++;
        } else {
            if ((!house) && (object->Owner() == HOUSE_GREECE)) {
                leadership++;
            }
        }
    }
    if (!leadership)
        leadership++;
    leadership = 100
                 * fixed(leadership,
                         (house ? bad_units_lost + bad_buildings_lost + leadership
                                : good_units_lost + good_buildings_lost + leadership));
    leadership = min(150, leadership);

    /*
    **	Determine economy rating.
    */
    // int init = PlayerPtr->Control.InitialCredits;
    // int cred = PlayerPtr->Available_Money();

    int economy = 100
                  * fixed((unsigned)PlayerPtr->Available_Money() + 1 + PlayerPtr->StolenBuildingsCredits,
                          PlayerPtr->HarvestedCredits + (unsigned)PlayerPtr->Control.InitialCredits + 1);
    economy = min(economy, 150);

    int score_total = ((uspoints * leadership) / 100) + ((uspoints * economy) / 100);
    if (score_total < -9999)
        score_total = -9999;
    score_total = min(score_total, 99999);

    // Score Stats
    event.GameOver.Leadership = leadership;
    event.GameOver.Efficiency = economy;
    event.GameOver.Score = score_total;
    event.GameOver.CategoryTotal = uspoints;
    event.GameOver.NODKilled = bad_units_lost;
    event.GameOver.GDIKilled = good_units_lost;
    event.GameOver.CiviliansKilled = neutral_units_lost;
    event.GameOver.NODBuildingsDestroyed = bad_buildings_lost;
    event.GameOver.GDIBuildingsDestroyed = good_buildings_lost;
    event.GameOver.CiviliansBuildingsDestroyed = neutral_buildings_lost;
    event.GameOver.RemainingCredits = PlayerPtr->Available_Money();

    if (Scen.IsSkipScore) {
        event.GameOver.Score = -1;
    }
}

/**************************************************************************************************
 * DLLExportClass::On_Message -- Called when the game wants to display a message (ex. tutorial text)
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 10/16/2019 - SKY
 **************************************************************************************************/
void DLLExportClass::On_Message(const HouseClass* player_ptr,
                                const char* message,
                                float timeout_seconds,
                                EventCallbackMessageEnum message_type,
                                int64 message_id)
{
    if (EventCallback == NULL) {
        return;
    }

    const char* p_msg = message;

    std::string localized_text_name;
    if (message_id != -1) {
        if (message_id == TXT_INSUFFICIENT_FUNDS) {
            localized_text_name = "TEXT_INSUFFICIENT_FUNDS_MESSAGE";
        } else if (message_id == TXT_LOW_POWER) {
            localized_text_name = "TEXT_LOW_POWER_MESSAGE_001";
        } else if (message_id == TXT_POWER_TESLA) {
            localized_text_name = "TEXT_LOW_POWER_MESSAGE_002";
        } else if (message_id == TXT_POWER_AAGUN) {
            localized_text_name = "TEXT_LOW_POWER_MESSAGE_003";
        } else {
            if (MessagesSent.find(message_id) != MessagesSent.end()) {
                return;
            }

            MessagesSent.insert(message_id);

            localized_text_name = "TEXT_RA_TUTORIAL_MESSAGE_";
            localized_text_name += std::to_string(message_id);
        }
        p_msg = localized_text_name.c_str();

        Sound_Effect(VOC_INCOMING_MESSAGE);
    }

    EventCallbackStruct new_event;
    new_event.EventType = CALLBACK_EVENT_MESSAGE;
    new_event.Message.Message = p_msg;
    new_event.Message.TimeoutSeconds = timeout_seconds;
    new_event.Message.MessageType = message_type;
    new_event.Message.MessageParam1 = message_id;

    new_event.GlyphXPlayerID = 0;
    if (player_ptr != NULL) {
        new_event.GlyphXPlayerID = Get_GlyphX_Player_ID(player_ptr);
    }

    EventCallback(new_event);
}

/**************************************************************************************************
 * DLLExportClass::On_Update_Map_Cell -- Called when an individual map cell template is updated
 *
 *
 * History: 11/7/2019 - SKY
 **************************************************************************************************/
void DLLExportClass::On_Update_Map_Cell(int cell_x, int cell_y, const char* template_type_name)
{
    if (EventCallback == NULL) {
        return;
    }

    EventCallbackStruct new_event;
    new_event.EventType = CALLBACK_EVENT_UPDATE_MAP_CELL;
    new_event.UpdateMapCell.CellX = cell_x;
    new_event.UpdateMapCell.CellY = cell_y;
    strncpy(new_event.UpdateMapCell.TemplateTypeName, template_type_name, 32);
    new_event.UpdateMapCell.TemplateTypeName[31] = '\0';

    EventCallback(new_event);
}

/**************************************************************************************************
 * DLLExportClass::On_Special_Weapon_Targetting -- Called when the server initiates targetting
 *
 *
 * History: 11/19/2019 - SKY
 **************************************************************************************************/
void DLLExportClass::On_Special_Weapon_Targetting(const HouseClass* player_ptr, SpecialWeaponType weapon_type)
{
    if (EventCallback == NULL) {
        return;
    }

    EventCallbackStruct new_event;
    new_event.EventType = CALLBACK_EVENT_SPECIAL_WEAPON_TARGETTING;
    new_event.SpecialWeaponTargetting.Type = RTTI_SPECIAL;
    new_event.SpecialWeaponTargetting.ID = weapon_type;
    Convert_Special_Weapon_Type(
        weapon_type, new_event.SpecialWeaponTargetting.WeaponType, new_event.SpecialWeaponTargetting.Name);

    new_event.GlyphXPlayerID = 0;
    if (player_ptr != NULL) {
        new_event.GlyphXPlayerID = Get_GlyphX_Player_ID(player_ptr);
    }

    EventCallback(new_event);
}

/**************************************************************************************************
 * DLLExportClass::On_Ping -- Called when a radar ping is needed
 *
 *
 * History: 05/15/2019 - SKY
 **************************************************************************************************/
void DLLExportClass::On_Ping(const HouseClass* player_ptr, COORDINATE coord)
{
    if (EventCallback == NULL) {
        return;
    }

    EventCallbackStruct new_event;
    new_event.EventType = CALLBACK_EVENT_PING;
    new_event.Ping.CoordX = Coord_X(coord);
    new_event.Ping.CoordY = Coord_Y(coord);

    new_event.GlyphXPlayerID = 0;
    if (player_ptr != NULL) {
        new_event.GlyphXPlayerID = Get_GlyphX_Player_ID(player_ptr);
    }

    EventCallback(new_event);
}

/**************************************************************************************************
 * DLLExportClass::On_Game_Over -- Called when the C&C campaign game finishes
 *
 *
 * History: 6/19/2019 - LLL
 **************************************************************************************************/
void DLLExportClass::On_Game_Over(uint64 glyphx_Player_id, bool player_wins)
{
    if (EventCallback == NULL) {
        return;
    }

    GameOver = true;

    SaveTanya = IsTanyaDead;
    Scen.CarryOverTimer = Scen.MissionTimer;
    //	int timer = Scen.MissionTimer;

    Scen.CarryOverMoney = PlayerPtr->Credits;

    DLLExportClass::Store_Carryover_Objects();

    EventCallbackStruct new_event;
    new_event.EventType = CALLBACK_EVENT_GAME_OVER;
    new_event.GlyphXPlayerID = glyphx_Player_id;
    new_event.GameOver.PlayerWins = player_wins;
    new_event.GameOver.AfterScoreMovieName = "";
    new_event.GameOver.Multiplayer = false;
    new_event.GameOver.MultiPlayerTotalPlayers = 0;

    Calculate_Single_Player_Score(new_event);

    VQType movie = player_wins ? Scen.WinMovie : Scen.LoseMovie;
    if (movie > VQ_NONE && movie < VQ_COUNT) {
        new_event.GameOver.MovieName = VQName[movie];
    } else {
        new_event.GameOver.MovieName = "";
    }

    movie = player_wins ? Scen.WinMovie2 : VQ_NONE;
    if (movie > VQ_NONE && movie < VQ_COUNT) {
        new_event.GameOver.MovieName2 = VQName[movie];
    } else {
        new_event.GameOver.MovieName2 = "";
    }

    movie = player_wins ? Scen.WinMovie3 : VQ_NONE;
    if (movie > VQ_NONE && movie < VQ_COUNT) {
        new_event.GameOver.MovieName3 = VQName[movie];
    } else {
        new_event.GameOver.MovieName3 = "";
    }

    movie = player_wins ? Scen.WinMovie4 : VQ_NONE;
    if (movie > VQ_NONE && movie < VQ_COUNT) {
        new_event.GameOver.MovieName4 = VQName[movie];
    } else {
        new_event.GameOver.MovieName4 = "";
    }

    // Campaign win & credits
    if (Scen.IsEndOfGame) {
        if (PlayerPtr->ActLike == HOUSE_USSR) {
            new_event.GameOver.AfterScoreMovieName = VQName[VQ_SOVFINAL];
        } else {
            new_event.GameOver.AfterScoreMovieName = VQName[VQ_ALLYEND];
        }
    }

    new_event.GameOver.SabotagedStructureType = -1;
    new_event.GameOver.TimerRemaining = Scen.MissionTimer.Was_Started() ? Scen.MissionTimer.Value() : -1;

    EventCallback(new_event);
}

/**************************************************************************************************
 * DLLExportClass::On_Multiplayer_Game_Over -- Called when the C&C multiplayer game finishes
 *
 *
 * History: 6/19/2019 - LLL
 * History: 10/31/2019 - MBL - Adding the multi-player score stats support for debrief
 **************************************************************************************************/
void DLLExportClass::On_Multiplayer_Game_Over(void)
{
    if (EventCallback == NULL) {
        return;
    }

    GameOver = true;

    EventCallbackStruct event;

    event.EventType = CALLBACK_EVENT_GAME_OVER;

    // Includes AI's for skirmish
    int player_count = Session.Players.Count();

    // Multiplayer players data for debrief stats

    event.GameOver.Multiplayer = true;
    event.GameOver.MultiPlayerTotalPlayers = player_count;

    for (int player_index = 0; player_index < player_count; player_index++) {
        HouseClass* player_ptr = HouseClass::As_Pointer(Session.Players[player_index]->Player.ID);
        if (player_ptr != NULL) {
            HousesType player_house = PlayerPtr->Class->House;

            int uspoints = 0;
            for (HousesType hous = HOUSE_SPAIN; hous <= HOUSE_BAD; hous++) {
                HouseClass* hows = HouseClass::As_Pointer(hous);
                if (player_ptr->Is_Ally(hous)) {
                    uspoints += hows->PointTotal;
                }
            }
//	if(uspoints < 0) uspoints = 0;
//	uspoints += 1000; //BG 1000 bonus points for winning mission

// N/A for multi-player
#if 0
				// Bias the base score upward according to the difficulty level.
				switch (PlayerPtr->Difficulty) {
					case DIFF_EASY:
					uspoints += 500;
					break;
			
					case DIFF_NORMAL:
					uspoints += 1500;
					break;
			
					case DIFF_HARD:
					uspoints += 3500;
					break;
				}
#endif

            int leadership =
                RA_Calculate_Leadership(player_ptr->Class->House, player_ptr->UnitsLost, player_ptr->BuildingsLost);
            int economy = RA_Calculate_Economy(player_ptr->Available_Money(),
                                               player_ptr->StolenBuildingsCredits,
                                               player_ptr->HarvestedCredits,
                                               player_ptr->Control.InitialCredits);
            int total_score = RA_Calculate_Score(uspoints, leadership, economy);

            int units_killed = 0;
            int structures_killed = 0;
            for (unsigned int house_index = 0; house_index < HOUSE_COUNT; house_index++) {
                units_killed += player_ptr->UnitsKilled[house_index];
                structures_killed += player_ptr->BuildingsKilled[house_index];
            }

            // Populate and copy the multiplayer player data structure

            GameOverMultiPlayerStatsStruct multi_player_data;

            multi_player_data.GlyphXPlayerID = Get_GlyphX_Player_ID(player_ptr);
            multi_player_data.IsHuman = (player_ptr->IsHuman || player_ptr->WasHuman);
            multi_player_data.WasHuman = player_ptr->WasHuman;
            multi_player_data.IsWinner = !player_ptr->IsDefeated;
            multi_player_data.Efficiency = economy;
            multi_player_data.Score = total_score;
            multi_player_data.ResourcesGathered = player_ptr->HarvestedCredits;
            multi_player_data.TotalUnitsKilled = units_killed;
            multi_player_data.TotalStructuresKilled = structures_killed;

            if (player_index < GAME_OVER_MULTIPLAYER_MAX_PLAYERS_TRACKED) {
                event.GameOver.MultiPlayerPlayersData[player_index] = multi_player_data;
            }
        }
    }
    for (int player_index = player_count; player_index < GAME_OVER_MULTIPLAYER_MAX_PLAYERS_TRACKED; player_index++) {
        memset(&event.GameOver.MultiPlayerPlayersData[player_index], 0, sizeof(GameOverMultiPlayerStatsStruct));
    }

    // Single-player N/A stuff

    event.GameOver.MovieName = "";
    event.GameOver.MovieName2 = "";
    event.GameOver.MovieName3 = "";
    event.GameOver.MovieName4 = "";
    event.GameOver.AfterScoreMovieName = "";
    event.GameOver.Leadership = 0;
    event.GameOver.Efficiency = 0;
    event.GameOver.Score = 0;
    event.GameOver.NODKilled = 0;
    event.GameOver.GDIKilled = 0;
    event.GameOver.CiviliansKilled = 0;
    event.GameOver.NODBuildingsDestroyed = 0;
    event.GameOver.GDIBuildingsDestroyed = 0;
    event.GameOver.CiviliansBuildingsDestroyed = 0;
    event.GameOver.RemainingCredits = 0;
    event.GameOver.SabotagedStructureType = 0;
    event.GameOver.TimerRemaining = -1;

    // Trigger an event for each human player, winner first (even if it's an AI)
    for (int i = 0; i < player_count; i++) {
        HouseClass* player_ptr = HouseClass::As_Pointer(Session.Players[i]->Player.ID);
        if (!player_ptr->IsDefeated) {
            event.GlyphXPlayerID = Get_GlyphX_Player_ID(player_ptr);
            event.GameOver.IsHuman = player_ptr->IsHuman;
            event.GameOver.PlayerWins = true;
            event.GameOver.RemainingCredits = player_ptr->Available_Money();
            EventCallback(event);
        }
    }

    for (int i = 0; i < player_count; i++) {
        HouseClass* player_ptr = HouseClass::As_Pointer(Session.Players[i]->Player.ID);
        if (player_ptr->IsHuman && player_ptr->IsDefeated) {
            event.GlyphXPlayerID = Get_GlyphX_Player_ID(player_ptr);
            event.GameOver.IsHuman = true;
            event.GameOver.PlayerWins = false;
            event.GameOver.RemainingCredits = player_ptr->Available_Money();
            EventCallback(event);
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::On_Achievement -- Called when something achievement-related happens
 *
 * In:   Type of achievement, reason this happened
 *
 * Out:
 *
 *
 *
 * History: 11/11/2019 11:37AM - ST
 **************************************************************************************************/
void DLLExportClass::On_Achievement(const HouseClass* player_ptr,
                                    const char* achievement_type,
                                    const char* achievement_reason)
{
    if (EventCallback == NULL) {
        return;
    }

    EventCallbackStruct new_event;
    new_event.EventType = CALLBACK_EVENT_ACHIEVEMENT;
    new_event.Achievement.AchievementType = achievement_type;
    new_event.Achievement.AchievementReason = achievement_reason;

    new_event.GlyphXPlayerID = 0;
    if (player_ptr != NULL) {
        new_event.GlyphXPlayerID = Get_GlyphX_Player_ID(player_ptr);
    }

    EventCallback(new_event);
}

void DLLExportClass::On_Center_Camera(const HouseClass* player_ptr, int coord_x, int coord_y)
{
    if (EventCallback == NULL) {
        return;
    }

    EventCallbackStruct new_event;
    new_event.EventType = CALLBACK_EVENT_CENTER_CAMERA;
    new_event.CenterCamera.CoordX = coord_x;
    new_event.CenterCamera.CoordY = coord_y;

    new_event.GlyphXPlayerID = 0;
    if (player_ptr != NULL) {
        new_event.GlyphXPlayerID = Get_GlyphX_Player_ID(player_ptr);
    }

    EventCallback(new_event);
}

/**************************************************************************************************
 * DLLExportClass::On_Debug_Output -- Called when C&C wants to print debug output
 *
 * In:   String to print to GlyphX log system
 *
 * Out:
 *
 *
 *
 * History: 2/20/2019 2:39PM - ST
 **************************************************************************************************/
void DLLExportClass::On_Debug_Output(const char* debug_text)
{
    if (EventCallback == NULL) {
        return;
    }

    EventCallbackStruct new_event;
    new_event.EventType = CALLBACK_EVENT_DEBUG_PRINT;
    new_event.DebugPrint.PrintString = debug_text;
    EventCallback(new_event);
}

/**************************************************************************************************
 * DLLExportClass::Force_Human_Team_Wins
 *
 * History: 3/10/2020 - LL
 **************************************************************************************************/
void DLLExportClass::Force_Human_Team_Wins(uint64 quitting_player_id)
{
    int winning_team = -1;

    // Find the first human's multiplayer team.
    for (int i = 0; i < Session.Players.Count(); i++) {
        if (GlyphxPlayerIDs[i] != quitting_player_id) {
            HousesType house_type = Session.Players[i]->Player.ID;
            HouseClass* house_class = HouseClass::As_Pointer(house_type);
            if (house_class && house_class->IsHuman && !house_class->IsDefeated) {
                winning_team = MPlayerTeamIDs[i];
                break;
            }
        }
    }

    // Mark all players not on that team as defeated.
    for (int i = 0; i < Session.Players.Count(); i++) {
        HousesType house_type = Session.Players[i]->Player.ID;
        HouseClass* house_class = HouseClass::As_Pointer(house_type);
        if (house_class) {
            house_class->IsDefeated = MPlayerTeamIDs[i] != winning_team;
        }
    }

    PlayerWins = true;
}

/**************************************************************************************************
 * CNC_Get_Game_State -- Get game state
 *
 * In:   Type of state requested
 *       Player perspective
 *       Buffer to contain game state
 *       Size of buffer
 *
 * Out:  Game state returned in buffer
 *
 *
 *
 * History: 1/7/2019 5:20PM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl CNC_Get_Game_State(GameStateRequestEnum state_type,
                                                                 uint64 player_id,
                                                                 unsigned char* buffer_in,
                                                                 unsigned int buffer_size)
{
    bool got_state = false;

    switch (state_type) {

    case GAME_STATE_LAYERS: {
        got_state = DLLExportClass::Get_Layer_State(player_id, buffer_in, buffer_size);
        break;
    }

    case GAME_STATE_SIDEBAR: {
        got_state = DLLExportClass::Get_Sidebar_State(player_id, buffer_in, buffer_size);
        break;
    }

    case GAME_STATE_PLACEMENT: {
        got_state = DLLExportClass::Get_Placement_State(player_id, buffer_in, buffer_size);
        break;
    }

    case GAME_STATE_DYNAMIC_MAP:
        got_state = DLLExportClass::Get_Dynamic_Map_State(player_id, buffer_in, buffer_size);
        break;

    case GAME_STATE_SHROUD:
        got_state = DLLExportClass::Get_Shroud_State(player_id, buffer_in, buffer_size);
        break;

    case GAME_STATE_OCCUPIER:
        got_state = DLLExportClass::Get_Occupier_State(player_id, buffer_in, buffer_size);
        break;

    case GAME_STATE_PLAYER_INFO:
        got_state = DLLExportClass::Get_Player_Info_State(player_id, buffer_in, buffer_size);
        break;

    case GAME_STATE_STATIC_MAP: {
        if (buffer_size < sizeof(CNCMapDataStruct)) {
            got_state = false;
            break;
        }

        int map_cell_x = Map.MapCellX;
        int map_cell_y = Map.MapCellY;
        int map_cell_width = Map.MapCellWidth;
        int map_cell_height = Map.MapCellHeight;

        CNCMapDataStruct* map_data = (CNCMapDataStruct*)buffer_in;

        map_data->OriginalMapCellX = map_cell_x;
        map_data->OriginalMapCellY = map_cell_y;
        map_data->OriginalMapCellWidth = map_cell_width;
        map_data->OriginalMapCellHeight = map_cell_height;

        if (map_cell_x > 0) {
            map_cell_x--;
            map_cell_width++;
        }

        if (map_cell_width < MAP_MAX_CELL_WIDTH) {
            map_cell_width++;
        }

        if (map_cell_y > 0) {
            map_cell_y--;
            map_cell_height++;
        }

        if (map_cell_height < MAP_MAX_CELL_HEIGHT) {
            map_cell_height++;
        }

        map_data->MapCellX = map_cell_x;
        map_data->MapCellY = map_cell_y;
        map_data->MapCellWidth = map_cell_width;
        map_data->MapCellHeight = map_cell_height;

        /*
        ** Red Alert's enum is Temperate, Snow, Interior. The shared remaster ABI is
        ** Desert, Jungle, Temperate, Winter, so a direct cast silently mislabels every
        ** RA map. Interior has no shared Phase 2 value and is reported as NONE until the
        ** unified Phase 3 ABI adds it explicitly.
        */
        switch (Scen.Theater) {
        case THEATER_TEMPERATE:
            map_data->Theater = CNC_THEATER_TEMPERATE;
            break;
        case THEATER_SNOW:
            map_data->Theater = CNC_THEATER_WINTER;
            break;
        default:
            map_data->Theater = CNC_THEATER_NONE;
            break;
        }

        char* dot_ptr = strchr(Scen.ScenarioName, '.');
        const int count = (dot_ptr != nullptr) ? (dot_ptr - Scen.ScenarioName) : sizeof(Scen.ScenarioName);
        memset(map_data->ScenarioName, 0, sizeof(map_data->ScenarioName));
        strncpy(map_data->ScenarioName, Scen.ScenarioName, min(sizeof(map_data->ScenarioName) - 1, count));

        int cell_index = 0;
        char cell_name[_MAX_PATH];
        char icon_number[32];

        for (int y = 0; y < map_cell_height; y++) {
            for (int x = 0; x < map_cell_width; x++) {
                CELL cell = XY_Cell(map_cell_x + x, map_cell_y + y);
                CellClass* cellptr = &Map[cell];

                cell_name[0] = 0;
                int icon = 0;
                void* image_data = 0;
                if (cellptr->Get_Template_Info(cell_name, icon, image_data)) {
                    itoa(icon, icon_number, 10);
                    strncat(cell_name, "_i", 32);
                    strncat(cell_name, icon_number, 32);
                    strncat(cell_name, ".tga", 32);

                    CNCStaticCellStruct& cell_info = map_data->StaticCells[cell_index++];
                    strncpy(cell_info.TemplateTypeName, cell_name, 32);
                    cell_info.IconNumber = icon;
                }
            }
        }

        got_state = true;
        break;
    }

    default: {
        got_state = false;
        break;
    }
    }

    return got_state;
}

/**************************************************************************************************
 * CNC_Handle_Game_Request
 *
 * Callback for when the requested movie is done playing.
 *
 * 7/23/2019 - LLL
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Game_Request(GameRequestEnum request_type)
{
    switch (request_type) {
    case INPUT_GAME_MOVIE_DONE:
        break;
    }
}

extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Game_Settings_Request(int health_bar_display_mode,
                                                                               int resource_bar_display_mode)
{
    if (!DLLExportClass::Legacy_Render_Enabled()) {
        return;
    }

    RulesClass::eHealthBarDisplayMode new_hb_mode = (RulesClass::eHealthBarDisplayMode)health_bar_display_mode;
    if (new_hb_mode != Rule.HealthBarDisplayMode) {
        Rule.HealthBarDisplayMode = new_hb_mode;
        Map.Flag_To_Redraw(true);
    }

    RulesClass::eResourceBarDisplayMode new_rb_mode = (RulesClass::eResourceBarDisplayMode)resource_bar_display_mode;
    if (new_rb_mode != Rule.ResourceBarDisplayMode) {
        Rule.ResourceBarDisplayMode = new_rb_mode;
        Map.Flag_To_Redraw(true);
    }
}

void DLL_Draw_Intercept(int shape_number,
                        int x,
                        int y,
                        int width,
                        int height,
                        int flags,
                        const ObjectClass* object,
                        DirType rotation,
                        long scale,
                        const char* shape_file_name,
                        char override_owner)
{
    DLLExportClass::DLL_Draw_Intercept(
        shape_number, x, y, width, height, flags, object, rotation, scale, shape_file_name, override_owner);
}

void DLL_Draw_Pip_Intercept(const ObjectClass* object, int pip)
{
    DLLExportClass::DLL_Draw_Pip_Intercept(object, pip);
}

void DLLExportClass::DLL_Draw_Intercept(int shape_number,
                                        int x,
                                        int y,
                                        int width,
                                        int height,
                                        int flags,
                                        const ObjectClass* object,
                                        DirType rotation,
                                        long scale,
                                        const char* shape_file_name,
                                        char override_owner)
{
    CNCObjectStruct& new_object = ObjectList->Objects[TotalObjectCount + CurrentDrawCount];
    memset(&new_object, 0, sizeof(new_object));
    Convert_Type(object, new_object);
    if (new_object.Type == UNKNOWN) {
        return;
    }

    CNCObjectStruct* base_object = NULL;
    char sub_object = 0;
    for (int i = 0; i < CurrentDrawCount; ++i) {
        CNCObjectStruct& draw_object = ObjectList->Objects[TotalObjectCount + i];
        if (draw_object.CNCInternalObjectPointer == object) {
            if (base_object == NULL) {
                base_object = &draw_object;
            }
            sub_object++;
        }
    }

    new_object.CNCInternalObjectPointer = (void*)object;
    new_object.OccupyListLength = 0;
    if (CurrentDrawCount == 0) {
        new_object.SortOrder = (ExportLayer << 29) + (object->Sort_Y() >> 3);
    } else {
        new_object.SortOrder = ObjectList->Objects[TotalObjectCount].SortOrder + CurrentDrawCount;
    }

    strncpy(new_object.TypeName, object->Class_Of().IniName, CNC_OBJECT_ASSET_NAME_LENGTH);

    if (shape_file_name != NULL) {
        strncpy(new_object.AssetName, shape_file_name, CNC_OBJECT_ASSET_NAME_LENGTH);
    } else {
        strncpy(new_object.AssetName, object->Class_Of().Graphic_Name(), CNC_OBJECT_ASSET_NAME_LENGTH);
    }

    new_object.Owner = (base_object != NULL) ? ((override_owner != HOUSE_NONE) ? override_owner : base_object->Owner)
                                             : (char)object->Owner();

    HouseClass* owner_house = nullptr;
    for (int i = 0; i < Houses.Count(); ++i) {
        HouseClass* hptr = Houses.Ptr(i);
        if ((hptr != nullptr) && (hptr->Class->House == new_object.Owner)) {
            owner_house = hptr;
            break;
        }
    }

    new_object.RemapColor = (owner_house != nullptr) ? owner_house->RemapColor : -1;

    if (base_object == NULL) {
        CNCObjectStruct& root_object = ObjectList->Objects[TotalObjectCount];

        if (new_object.Type == BUILDING) {
            BuildingClass* building = (BuildingClass*)object;
            if (building->BState == BSTATE_CONSTRUCTION) {
                strncat(new_object.AssetName, "MAKE", CNC_OBJECT_ASSET_NAME_LENGTH);
            }
            const BuildingTypeClass* building_type = building->Class;
            short const* occupy_list = building_type->Occupy_List();
            if (occupy_list) {
                while (*occupy_list != REFRESH_EOL && new_object.OccupyListLength < MAX_OCCUPY_CELLS) {
                    new_object.OccupyList[new_object.OccupyListLength] = *occupy_list;
                    new_object.OccupyListLength++;
                    occupy_list++;
                }
            }
        }

        COORDINATE coord = object->Render_Coord();
        CELL cell = Coord_Cell(coord);
        int dimx, dimy;
        object->Class_Of().Dimensions(dimx, dimy);

        new_object.PositionX = x;
        new_object.PositionY = y;
        new_object.Width = width;
        new_object.Height = height;
        new_object.Altitude = object->Height;
        new_object.DrawFlags = flags;
        new_object.SubObject = 0;
        /* The virtual draw uses DoControlsVirtual, whose infantry offsets
           deliberately point into the TD compatibility sheet (E1 deaths are
           94 frames later). Classic draws the original RA SHPs, so export the
           tactical frame for the body while retaining the virtual draw's
           coordinates and other callbacks. */
        new_object.ShapeIndex = (unsigned short)(new_object.Type == INFANTRY
            ? static_cast<const InfantryClass*>(object)->Shape_Number(WINDOW_TACTICAL)
            : shape_number);
        new_object.IsTheaterSpecific = IsTheaterShape;
        new_object.Rotation = (unsigned char)rotation;
        new_object.Scale = scale;
        new_object.FlashingFlags = 0;
        new_object.IsBlushing = false;
        new_object.Cloak = (CurrentDrawCount > 0) ? root_object.Cloak : UNCLOAKED;
        new_object.VisibleFlags = CNCObjectStruct::VISIBLE_FLAGS_ALL;
        new_object.SpiedByFlags = 0U;

        new_object.IsSelectable = object->Class_Of().IsSelectable;
        new_object.IsSelectedMask = object->IsSelectedMask;
        new_object.MaxStrength = object->Class_Of().MaxStrength;
        new_object.Strength = object->Strength;
        new_object.CellX = (CurrentDrawCount > 0) ? root_object.CellX : Cell_X(cell);
        new_object.CellY = (CurrentDrawCount > 0) ? root_object.CellY : Cell_Y(cell);
        new_object.CenterCoordX = Coord_X(object->Center_Coord());
        new_object.CenterCoordY = Coord_Y(object->Center_Coord());
        new_object.DimensionX = dimx;
        new_object.DimensionY = dimy;
        new_object.SimLeptonX = (CurrentDrawCount > 0) ? root_object.SimLeptonX : 0;
        new_object.SimLeptonY = (CurrentDrawCount > 0) ? root_object.SimLeptonY : 0;
        new_object.BaseObjectID =
            ((CurrentDrawCount > 0) && (root_object.Type != BUILDING) && (root_object.Type != VESSEL)) ? root_object.ID
                                                                                                       : 0;
        new_object.BaseObjectType =
            ((CurrentDrawCount > 0) && (root_object.Type != BUILDING) && (root_object.Type != VESSEL))
                ? root_object.Type
                : UNKNOWN;
        new_object.NumLines = 0;
        new_object.RecentlyCreated = object->IsRecentlyCreated;
        new_object.NumPips = 0;
        new_object.MaxPips = 0;
        new_object.CanDemolish = object->Can_Demolish();
        new_object.CanDemolishUnit = object->Can_Demolish_Unit();
        new_object.CanRepair = object->Can_Repair();
        memset(new_object.CanMove, false, sizeof(new_object.CanMove));
        memset(new_object.CanFire, false, sizeof(new_object.CanFire));
        memset(new_object.ActionWithSelected, DAT_NONE, sizeof(new_object.ActionWithSelected));

        HouseClass* old_player_ptr = PlayerPtr;
        for (int i = 0; i < Houses.Count(); ++i) {
            HouseClass* hptr = Houses.Ptr(i);
            if ((hptr != nullptr) && hptr->IsActive && hptr->IsHuman) {
                HousesType house = hptr->Class->House;
                DynamicVectorClass<ObjectClass*>& selected_objects = CurrentObject.Raw(house);
                if (selected_objects.Count() > 0) {
                    Logic_Switch_Player_Context(hptr);
                    Convert_Action_Type(Best_Object_Action(selected_objects, object),
                                        (selected_objects.Count() == 1) ? selected_objects[0] : NULL,
                                        object->As_Target(),
                                        new_object.ActionWithSelected[house]);
                }
            }
        }
        Logic_Switch_Player_Context(old_player_ptr);

        RTTIType what_is_object = object->What_Am_I();

        new_object.IsRepairing = false;
        new_object.IsDumping = false;
        new_object.IsALoaner = false;
        new_object.IsFactory = false;
        new_object.IsPrimaryFactory = false;
        new_object.IsNominal = false;
        new_object.IsDog = false;
        new_object.IsIronCurtain = false;
        new_object.IsAntiGround = false;
        new_object.IsAntiAircraft = false;
        new_object.IsSubSurface = false;
        new_object.IsFake = false;
        new_object.ProductionAssetName[0] = '\0';
        new_object.OverrideDisplayName = "\0";

        bool is_building = what_is_object == RTTI_BUILDING;
        if (is_building) {
            const BuildingClass* building = static_cast<const BuildingClass*>(object);
            new_object.IsRepairing = building->IsRepairing;
            new_object.IsFactory = building->Class->Is_Factory();
            new_object.IsPrimaryFactory = building->IsLeader;
            new_object.IsFake = building->Class->IsFake;
        }

        if (object->Is_Techno()) {
            const TechnoClass* techno_object = static_cast<const TechnoClass*>(object);
            const TechnoTypeClass* ttype = techno_object->Techno_Type_Class();

            new_object.MaxSpeed = (unsigned char)ttype->MaxSpeed;
            new_object.IsALoaner = techno_object->IsALoaner;
            new_object.IsNominal = ttype->IsNominal;
            new_object.MaxPips = ttype->Max_Pips();
            new_object.IsAntiGround = (ttype->PrimaryWeapon != NULL) && (ttype->PrimaryWeapon->Bullet != NULL)
                                      && ttype->PrimaryWeapon->Bullet->IsAntiGround;
            new_object.IsAntiAircraft = (ttype->PrimaryWeapon != NULL) && (ttype->PrimaryWeapon->Bullet != NULL)
                                        && ttype->PrimaryWeapon->Bullet->IsAntiAircraft;
            new_object.IsSubSurface = (ttype->PrimaryWeapon != NULL) && (ttype->PrimaryWeapon->Bullet != NULL)
                                      && ttype->PrimaryWeapon->Bullet->IsSubSurface;
            new_object.IsIronCurtain = techno_object->IronCurtainCountDown > 0;

            int full_name = techno_object->Full_Name();
            if (full_name < 0) {
                new_object.OverrideDisplayName = Text_String(full_name);
            }

            HouseClass* old_player_ptr = PlayerPtr;
            for (int i = 0; i < Houses.Count(); ++i) {
                HouseClass* hptr = Houses.Ptr(i);
                if ((hptr != nullptr) && hptr->IsActive && hptr->IsHuman) {
                    Logic_Switch_Player_Context(hptr);
                    HousesType house = hptr->Class->House;
                    new_object.CanMove[house] = techno_object->Can_Player_Move();
                    new_object.CanFire[house] = techno_object->Can_Player_Fire();
                }
            }
            Logic_Switch_Player_Context(old_player_ptr);
        }

        new_object.ControlGroup = (unsigned char)(-1);
        new_object.IsInFormation = false;
        new_object.CanPlaceBombs = false;

        if (object->Is_Foot()) {
            const FootClass* foot = static_cast<const FootClass*>(object);
            new_object.ControlGroup = foot->Group;
            new_object.IsInFormation = foot->XFormOffset != INVALID_FORMATION;
        }

        bool is_infantry = what_is_object == RTTI_INFANTRY;
        if (is_infantry) {
            const InfantryClass* infantry = static_cast<const InfantryClass*>(object);
            new_object.IsDog = infantry->Class->IsDog;
            new_object.CanPlaceBombs = infantry->Class->IsBomber;
        }

        new_object.CanHarvest = false;
        bool is_unit = what_is_object == RTTI_UNIT;
        if (is_unit) {
            const UnitClass* unit = static_cast<const UnitClass*>(object);
            if (unit->Class->Type == UNIT_HARVESTER) {
                new_object.CanHarvest = true;
            }

            new_object.IsDumping = unit->IsDumping;
        }

        new_object.IsFixedWingedAircraft = false;
        bool is_aircraft = what_is_object == RTTI_AIRCRAFT;
        if (is_aircraft) {
            const AircraftClass* aircraft = static_cast<const AircraftClass*>(object);
            ;
            new_object.IsFixedWingedAircraft = aircraft->Class->IsFixedWing;
        }

        switch (what_is_object) {
        case RTTI_INFANTRY:
        case RTTI_INFANTRYTYPE:
        case RTTI_UNIT:
        case RTTI_UNITTYPE:
        case RTTI_AIRCRAFT:
        case RTTI_AIRCRAFTTYPE:
        case RTTI_BUILDING:
        case RTTI_BUILDINGTYPE:
        case RTTI_VESSEL:
        case RTTI_VESSELTYPE: {
            const TechnoClass* techno_object = static_cast<const TechnoClass*>(object);
            new_object.FlashingFlags = techno_object->Get_Flashing_Flags();
            new_object.IsBlushing = techno_object->IsBlushing;
            new_object.Cloak = techno_object->Cloak;
            new_object.SpiedByFlags = techno_object->Spied_By();

            if (techno_object->Techno_Type_Class()->IsInvisible) {
                // Hide for enemy players
                HouseClass* owner = HouseClass::As_Pointer(object->Owner());
                if (owner != nullptr) {
                    for (int i = 0; i < Houses.Count(); ++i) {
                        HouseClass* hptr = Houses.Ptr(i);
                        if ((hptr != nullptr) && hptr->IsActive && !owner->Is_Ally(hptr)) {
                            new_object.VisibleFlags &= ~(1 << hptr->Class->House);
                        }
                    }
                }
            }
        } break;
        case RTTI_ANIM:
        case RTTI_ANIMTYPE: {
            const AnimClass* anim_object = static_cast<const AnimClass*>(object);
            new_object.Owner = anim_object->OwnerHouse;
            new_object.RemapColor = -1;
            new_object.VisibleFlags = anim_object->Get_Visible_Flags();

            const AnimTypeClass& anim_type = static_cast<const AnimTypeClass&>(anim_object->Class_Of());
            if (anim_type.VirtualName != NULL) {
                strncpy(new_object.AssetName, anim_type.VirtualName, CNC_OBJECT_ASSET_NAME_LENGTH);
            }
        } break;
        case RTTI_TERRAIN:
        case RTTI_TERRAINTYPE: {
            if (strncmp(new_object.AssetName, "MINE", CNC_OBJECT_ASSET_NAME_LENGTH) == 0) {
                strncpy(new_object.AssetName, "OREMINE", CNC_OBJECT_ASSET_NAME_LENGTH);
            }
        } break;
        }

    } else {
        new_object.MaxStrength = 0;
        new_object.MaxSpeed = 0;
        new_object.Strength = 0;
        new_object.CellX = base_object->CellX;
        new_object.CellY = base_object->CellY;
        new_object.CenterCoordX = base_object->CenterCoordX;
        new_object.CenterCoordY = base_object->CenterCoordY;
        new_object.DimensionX = base_object->DimensionX;
        new_object.DimensionY = base_object->DimensionY;
        new_object.IsSelectable = false;
        new_object.IsSelectedMask = 0U;
        new_object.SimLeptonX = base_object->SimLeptonX;
        new_object.SimLeptonY = base_object->SimLeptonY;

        new_object.PositionX = x;
        new_object.PositionY = y;
        new_object.Width = width;
        new_object.Height = height;
        new_object.Altitude = base_object->Altitude;
        new_object.DrawFlags = flags;
        new_object.ShapeIndex = (unsigned short)shape_number;
        new_object.IsTheaterSpecific = IsTheaterShape;
        new_object.Rotation = (unsigned char)rotation;
        new_object.Scale = scale;
        new_object.SubObject = sub_object;
        new_object.BaseObjectID = base_object->ID;
        new_object.BaseObjectType = base_object->Type;

        new_object.FlashingFlags = base_object->FlashingFlags;
        new_object.IsBlushing = base_object->IsBlushing;
        new_object.Cloak = base_object->Cloak;
        new_object.OccupyListLength = 0;
        new_object.NumPips = 0;
        new_object.MaxPips = 0;
        new_object.NumLines = 0;
        new_object.IsRepairing = false;
        new_object.IsDumping = false;
        new_object.IsALoaner = base_object->IsALoaner;
        new_object.CanDemolish = base_object->CanDemolish;
        new_object.CanDemolishUnit = base_object->CanDemolishUnit;
        new_object.CanRepair = base_object->CanRepair;
        new_object.RecentlyCreated = base_object->RecentlyCreated;
        new_object.IsFactory = base_object->IsFactory;
        new_object.IsPrimaryFactory = base_object->IsPrimaryFactory;
        new_object.IsAntiGround = base_object->IsAntiGround;
        new_object.IsAntiAircraft = base_object->IsAntiAircraft;
        new_object.IsSubSurface = base_object->IsSubSurface;
        new_object.IsNominal = base_object->IsNominal;
        new_object.IsDog = base_object->IsDog;
        new_object.IsIronCurtain = base_object->IsIronCurtain;
        new_object.IsInFormation = false;
        new_object.CanHarvest = base_object->CanHarvest;
        new_object.CanPlaceBombs = base_object->CanPlaceBombs;
        new_object.ControlGroup = base_object->ControlGroup;
        new_object.VisibleFlags = base_object->VisibleFlags;
        new_object.SpiedByFlags = base_object->SpiedByFlags;
        new_object.IsFixedWingedAircraft = base_object->IsFixedWingedAircraft;
        new_object.IsFake = base_object->IsFake;
        new_object.ProductionAssetName[0] = '\0';
        new_object.OverrideDisplayName = "\0";
        memset(new_object.CanMove, false, sizeof(new_object.CanMove));
        memset(new_object.CanFire, false, sizeof(new_object.CanFire));
        memset(new_object.ActionWithSelected, DAT_NONE, sizeof(new_object.ActionWithSelected));
    }

    CurrentDrawCount++;
}

void DLLExportClass::DLL_Draw_Pip_Intercept(const ObjectClass* object, int pip)
{
    CNCObjectStruct* base_object = NULL;
    for (int i = 0; i < CurrentDrawCount; ++i) {
        CNCObjectStruct& draw_object = ObjectList->Objects[TotalObjectCount + i];
        if (draw_object.CNCInternalObjectPointer == object) {
            base_object = &draw_object;
            break;
        }
    }

    if ((base_object != NULL) && (base_object->NumPips < MAX_OBJECT_PIPS)) {
        base_object->Pips[base_object->NumPips] = pip;
        base_object->NumPips++;
        base_object->MaxPips = max(base_object->MaxPips, base_object->NumPips);
    }
}

/**************************************************************************************************
 * DLLExportClass::Get_Layer_State -- Get game objects from the layers
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 1/29/2019 11:37AM - ST
 **************************************************************************************************/
bool DLLExportClass::Get_Layer_State(uint64 player_id, unsigned char* buffer_in, unsigned int buffer_size)
{
    player_id;

    static int _export_count = 0;

    bool got_state = false;

    ObjectList = (CNCObjectListStruct*)buffer_in;

    TotalObjectCount = 0;

    /*
    ** Get a reference draw coordinate for cells
    */
    int map_cell_x = Map.MapCellX;
    int map_cell_y = Map.MapCellY;
    if (map_cell_x > 0) {
        map_cell_x--;
    }
    if (map_cell_y > 0) {
        map_cell_y--;
    }

    SortOrder = 0;

    /*
    **	Get the ground layer first and then followed by all the layers in increasing altitude.
    */
    for (int layer = 0; layer < DLL_LAYER_COUNT; layer++) {

        ExportLayer = layer;

        for (int index = 0; index < Map.Layer[layer].Count(); index++) {

            ObjectClass* object = Map.Layer[layer][index];
            if (object->IsActive) {

                unsigned int memory_needed = sizeof(CNCObjectListStruct);
                memory_needed += (TotalObjectCount + 10) * sizeof(CNCObjectStruct);
                if (memory_needed >= buffer_size) {
                    return false;
                }

                if (object->Is_Techno()) {
                    /*
                    **  Skip units tethered to buildings, since the building will draw them itself
                    */
                    TechnoClass* techno_object = static_cast<TechnoClass*>(object);
                    TechnoClass* contact_object =
                        techno_object->In_Radio_Contact() ? techno_object->Contact_With_Whom() : nullptr;
                    if ((object->What_Am_I() != RTTI_BUILDING) && (contact_object != nullptr)
                        && (contact_object->What_Am_I() == RTTI_BUILDING) && contact_object->IsTethered
                        && *((BuildingClass*)contact_object) == STRUCT_WEAP) {
                        continue;
                    }

                    /*
                    **  Skip units tethered to vessels, since the vessel will draw them itself
                    */
                    if ((contact_object != nullptr) && (contact_object->What_Am_I() == RTTI_VESSEL)
                        && !contact_object->Is_Door_Closed() && contact_object->IsTethered
                        && !techno_object->IsInLimbo) {
                        continue;
                    }
                }

                if (Debug_Map || Debug_Unshroud || (object->IsDown && !object->IsInLimbo)) {
                    int x, y;
                    Map.Coord_To_Pixel(object->Render_Coord(), x, y);

                    /*
                    ** Call to Draw_It can result in multiple callbacks to the draw intercept
                    */
                    CurrentDrawCount = 0;
                    object->Draw_It(x, y, WINDOW_VIRTUAL);

                    /*
                    ** If the root object is a factory, then the last base object is the object in production (rendered
                    *after infiltrated buildings when selected).
                    ** The root object is updated with the production asset name, but otherwise a separate object isn't
                    *created.
                    ** This only occurs in skirmish and multiplayer.
                    */
                    if ((GAME_TO_PLAY != GAME_NORMAL) && (CurrentDrawCount > 0)) {
                        CNCObjectStruct& root_object = ObjectList->Objects[TotalObjectCount];
                        if (root_object.IsFactory) {
                            BuildingClass* building = (BuildingClass*)root_object.CNCInternalObjectPointer;
                            FactoryClass* factory = building->House->IsHuman
                                                        ? building->House->Fetch_Factory(building->Class->ToBuild)
                                                        : (FactoryClass*)building->Factory;
                            if (factory != nullptr) {
                                for (int i = CurrentDrawCount - 1; i > 0; --i) {
                                    CNCObjectStruct& base_object = ObjectList->Objects[TotalObjectCount + i];
                                    if (base_object.SubObject) {
                                        continue;
                                    }
                                    strncpy(root_object.ProductionAssetName,
                                            base_object.TypeName,
                                            CNC_OBJECT_ASSET_NAME_LENGTH);
                                    void* production_object = base_object.CNCInternalObjectPointer;
                                    int new_draw_count = i;
                                    for (int j = i + 1; j < CurrentDrawCount; ++j) {
                                        CNCObjectStruct& cnc_object = ObjectList->Objects[TotalObjectCount + j];
                                        if (cnc_object.CNCInternalObjectPointer != production_object) {
                                            memcpy(ObjectList->Objects + TotalObjectCount + new_draw_count,
                                                   &cnc_object,
                                                   sizeof(CNCObjectStruct));
                                            new_draw_count++;
                                        }
                                    }
                                    memset(ObjectList->Objects + TotalObjectCount + new_draw_count,
                                           0,
                                           (CurrentDrawCount - new_draw_count) * sizeof(CNCObjectStruct));
                                    CurrentDrawCount = new_draw_count;
                                    break;
                                }
                            }
                        }
                    }

                    /*
                    ** Shadows need to be rendered before the base object so they appear underneath,
                    ** even though they get drawn as sub-objects (after the base object)
                    */
                    for (int i = 1; i < CurrentDrawCount; ++i) {
                        CNCObjectStruct& sub_object = ObjectList->Objects[TotalObjectCount + i];
                        if (!sub_object.SubObject) {
                            continue;
                        }
                        static const int shadow_flags = SHAPE_PREDATOR | SHAPE_FADING;
                        if (((sub_object.DrawFlags & shadow_flags) == shadow_flags)
                            || (strncmp(sub_object.AssetName, "WAKE", CNC_OBJECT_ASSET_NAME_LENGTH) == 0)) {
                            if ((strncmp(sub_object.AssetName, "RROTOR", CNC_OBJECT_ASSET_NAME_LENGTH) != 0)
                                && (strncmp(sub_object.AssetName, "LROTOR", CNC_OBJECT_ASSET_NAME_LENGTH) != 0)) {
                                for (int j = i - 1; j >= 0; --j) {
                                    CNCObjectStruct& base_object = ObjectList->Objects[TotalObjectCount + j];
                                    if (!base_object.SubObject
                                        && (base_object.CNCInternalObjectPointer
                                            == sub_object.CNCInternalObjectPointer)) {
                                        int sort_order = base_object.SortOrder;
                                        base_object.SortOrder = sub_object.SortOrder;
                                        sub_object.SortOrder = sort_order;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    TotalObjectCount += CurrentDrawCount;
                }
            }
        }
    }

    ObjectList->Count = TotalObjectCount;

    if (ObjectList->Count) {
        _export_count++;
        return true;
    }

    return false;
}

void DLLExportClass::Convert_Type(const ObjectClass* object, CNCObjectStruct& object_out)
{
    object_out.Type = UNKNOWN;
    object_out.ID = -1;

    if (object == NULL) {
        return;
    }

    RTTIType type = object->What_Am_I();

    switch (type) {
    default:
        break;

    case RTTI_VESSEL:
        object_out.Type = VESSEL;
        object_out.ID = Vessels.ID((VesselClass*)object);
        break;

    case RTTI_INFANTRY:
        object_out.Type = INFANTRY;
        object_out.ID = Infantry.ID((InfantryClass*)object);
        break;

    case RTTI_UNIT:
        object_out.Type = UNIT;
        object_out.ID = Units.ID((UnitClass*)object);
        break;

    case RTTI_AIRCRAFT:
        object_out.Type = AIRCRAFT;
        object_out.ID = Aircraft.ID((AircraftClass*)object);
        break;

    case RTTI_BUILDING:
        object_out.Type = BUILDING;
        object_out.ID = Buildings.ID((BuildingClass*)object);
        break;

    case RTTI_BULLET:
        object_out.Type = BULLET;
        object_out.ID = Bullets.ID((BulletClass*)object);
        break;

    case RTTI_ANIM:
        object_out.Type = ANIM;
        object_out.ID = Anims.ID((AnimClass*)object);
        break;

    case RTTI_SMUDGE:
        object_out.Type = SMUDGE;
        object_out.ID = Smudges.ID((SmudgeClass*)object);
        break;

    case RTTI_TERRAIN:
        object_out.Type = TERRAIN;
        object_out.ID = Terrains.ID((TerrainClass*)object);
        break;
    }
}

/**************************************************************************************************
 * CNC_Handle_Input -- Process input to the game
 *
 * In:
 *
 *
 *
 *
 * Out:  Game state returned in buffer
 *
 *
 *
 * History: 1/7/2019 5:20PM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Input(InputRequestEnum input_event,
                                                               unsigned char special_key_flags,
                                                               uint64 player_id,
                                                               int x1,
                                                               int y1,
                                                               int x2,
                                                               int y2)
{

    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    switch (input_event) {

    /*
    ** Special keys have changed
    */
    case INPUT_REQUEST_SPECIAL_KEYS: {
        DLLExportClass::Set_Special_Key_Flags(special_key_flags);
        break;
    }

    /*
    ** The mouse is moving
    */
    case INPUT_REQUEST_MOUSE_MOVE: {
        if (!DLLExportClass::Legacy_Render_Enabled()) {
            break;
        }

        DLLForceMouseX = x1;
        DLLForceMouseY = y1;
        KEYBOARD->MouseQX = x1;
        KEYBOARD->MouseQY = y1;

        COORDINATE coord = Map.Pixel_To_Coord(x1, y1);
        CELL cell = Coord_Cell(coord);
        if (coord) {
            // x -= Map.TacPixelX;
            // y -= Map.TacPixelY;

            /*
            ** Cause any displayed cursor to move along with the mouse cursor.
            */
            if (cell != Map.ZoneCell) {
                Map.Set_Cursor_Pos(cell);
            }
        }
        break;
    }

    /*
    ** Player left-clicked
    */
    case INPUT_REQUEST_MOUSE_LEFT_CLICK: {
        DLLExportClass::Adjust_Internal_View();

        DLLForceMouseX = x1;
        DLLForceMouseY = y1;
        KEYBOARD->MouseQX = x1;
        KEYBOARD->MouseQY = y1;

        KeyNumType key = (KeyNumType)(KN_LMOUSE | KN_RLSE_BIT);

        if (Map.Pixel_To_Coord(x1, y1)) {
            // DisplayClass::TacButton.Clicked_On(key, GadgetClass::LEFTRELEASE, x1, y1);
            DisplayClass::TacButton.Clicked_On(key, GadgetClass::LEFTRELEASE, 100, 100);
        }
        break;
    }

    /*
    ** Player right-clicked (on up)
    */
    case INPUT_REQUEST_MOUSE_RIGHT_CLICK: {
        DLLExportClass::Adjust_Internal_View();

        DLLForceMouseX = x1;
        DLLForceMouseY = y1;
        KEYBOARD->MouseQX = x1;
        KEYBOARD->MouseQY = y1;

        KeyNumType key = (KeyNumType)(KN_RMOUSE | KN_RLSE_BIT);

        if (Map.Pixel_To_Coord(x1, y1)) {
            // DisplayClass::TacButton.Clicked_On(key, GadgetClass::RIGHTRELEASE, x1, y1);
            DisplayClass::TacButton.Clicked_On(key, GadgetClass::RIGHTRELEASE, 100, 100);
        }
        break;
    }

    /*
    ** Player right button down
    */
    case INPUT_REQUEST_MOUSE_RIGHT_DOWN: {
        DLLExportClass::Adjust_Internal_View();

        DLLForceMouseX = x1;
        DLLForceMouseY = y1;
        KEYBOARD->MouseQX = x1;
        KEYBOARD->MouseQY = y1;

        KeyNumType key = (KeyNumType)(KN_RMOUSE);

        if (Map.Pixel_To_Coord(x1, y1)) {
            // DisplayClass::TacButton.Clicked_On(key, GadgetClass::RIGHTPRESS, x1, y1);
            DisplayClass::TacButton.Clicked_On(key, GadgetClass::RIGHTPRESS, 100, 100);
        }
        break;
    }

    /*
    ** Player drag selected
    */
    case INPUT_REQUEST_MOUSE_AREA: {
        DLLExportClass::Adjust_Internal_View();
        Map.Select_These(XYP_Coord(x1, y1), XYP_Coord(x2, y2), false);
        break;
    }

    case INPUT_REQUEST_MOUSE_AREA_ADDITIVE: {
        DLLExportClass::Adjust_Internal_View();
        Map.Select_These(XYP_Coord(x1, y1), XYP_Coord(x2, y2), true);
        break;
    }

    case INPUT_REQUEST_SELL_AT_POSITION: {
        DLLExportClass::Adjust_Internal_View();
        DLLForceMouseX = x1;
        DLLForceMouseY = y1;
        KEYBOARD->MouseQX = x1;
        KEYBOARD->MouseQY = y1;

        COORDINATE coord = Map.Pixel_To_Coord(x1, y1);
        CELL cell = Coord_Cell(coord);

        /*
        **	OpenCNC3D lockstep: a wall sale is a SELLCELL order, the form the 1996 mouse handler
        **	queued. Direct only outside a match.
        */
        if (OpenCNC3D_Lockstep) {
            OutList.Add(EventClass(EventClass::SELLCELL, cell));
        } else {
            PlayerPtr->Sell_Wall(cell);
        }

        break;
    }

    case INPUT_REQUEST_SELECT_AT_POSITION: {
        DLLExportClass::Adjust_Internal_View();
        DLLForceMouseX = x1;
        DLLForceMouseY = y1;
        Keyboard->MouseQX = x1;
        Keyboard->MouseQY = y1;

        COORDINATE coord = Map.Pixel_To_Coord(x1, y1);
        CELL cell = Coord_Cell(coord);

        if (Map.Pixel_To_Coord(x1, y1)) {
            KeyNumType key = (KeyNumType)(KN_LMOUSE | KN_RLSE_BIT);

            DisplayClass::TacButton.Selection_At_Mouse(GadgetClass::LEFTRELEASE, key);
        }

        break;
    }

    case INPUT_REQUEST_COMMAND_AT_POSITION: {
        DLLExportClass::Adjust_Internal_View();
        DLLForceMouseX = x1;
        DLLForceMouseY = y1;
        Keyboard->MouseQX = x1;
        Keyboard->MouseQY = y1;

        COORDINATE coord = Map.Pixel_To_Coord(x1, y1);
        CELL cell = Coord_Cell(coord);

        if (Map.Pixel_To_Coord(x1, y1)) {
            KeyNumType key = (KeyNumType)(KN_LMOUSE | KN_RLSE_BIT);
            DisplayClass::TacButton.Command_Object(GadgetClass::LEFTRELEASE, key);
        }

        break;
    }

    // MBL 09.08.2020 - Mod Support
    case INPUT_REQUEST_MOD_GAME_COMMAND_1_AT_POSITION:
    case INPUT_REQUEST_MOD_GAME_COMMAND_2_AT_POSITION:
    case INPUT_REQUEST_MOD_GAME_COMMAND_3_AT_POSITION:
    case INPUT_REQUEST_MOD_GAME_COMMAND_4_AT_POSITION: {
        DLLExportClass::Adjust_Internal_View();
        DLLForceMouseX = x1;
        DLLForceMouseY = y1;
        Keyboard->MouseQX = x1;
        Keyboard->MouseQY = y1;

        COORDINATE coord = Map.Pixel_To_Coord(x1, y1);
        CELL cell = Coord_Cell(coord);

        if (Map.Pixel_To_Coord(x1, y1)) {
            // TBD: For our ever-awesome Community Modders!
            //
            // PlayerPtr->Handle_Mod_Game_Command(cell, input_event - INPUT_REQUEST_MOD_GAME_COMMAND_1_AT_POSITION);
        }

        break;
    }

    default:
        break;
    }
}

/**************************************************************************************************
 * CNC_Handle_Structure_Request -- Process requests to repair and sell structures.
 *
 * In:
 *
 *
 * Out:
 *
 *
 *
 * History: 4/29/2019 - LLL
 **************************************************************************************************/

extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Structure_Request(StructureRequestEnum request_type,
                                                                           uint64 player_id,
                                                                           int object_id)
{
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    switch (request_type) {
    case INPUT_STRUCTURE_REPAIR_START:
        DLLExportClass::Repair_Mode(player_id);
        break;
    case INPUT_STRUCTURE_REPAIR:
        DLLExportClass::Repair(player_id, object_id);
        break;
    case INPUT_STRUCTURE_SELL_START:
        DLLExportClass::Sell_Mode(player_id);
        break;
    case INPUT_STRUCTURE_SELL:
        DLLExportClass::Sell(player_id, object_id);
        break;
    case INPUT_STRUCTURE_CANCEL:
        DLLExportClass::Repair_Sell_Cancel(player_id);
        break;
    /*
    **	OpenCNC3D: the sidebar's power button, thrown on one of the player's own buildings. An
    **	order under lockstep, like every other gesture; a direct toggle otherwise.
    */
    case INPUT_STRUCTURE_POWER: {
        BuildingClass* building = As_Building(Build_Target(RTTI_BUILDING, object_id));
        if (building && building->IsActive && !building->IsInLimbo && building->House == PlayerPtr) {
            if (OpenCNC3D_Lockstep) {
                OutList.Add(EventClass(EventClass::POWER_SWITCH, TargetClass(building)));
            } else {
                building->Toggle_Power();
            }
        }
        break;
    }
    default:
        break;
    }
}

/**************************************************************************************************
 * CNC_Handle_Unit_Request -- Process requests on selected units.
 *
 * In:
 *
 *
 * Out:
 *
 *
 *
 * History: 10/15/2019 - SKY
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Unit_Request(UnitRequestEnum request_type, uint64 player_id)
{
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    switch (request_type) {
    case INPUT_UNIT_SCATTER:
        DLLExportClass::Scatter_Selected(player_id);
        break;
    case INPUT_UNIT_SELECT_NEXT:
        DLLExportClass::Select_Next_Unit(player_id);
        break;
    case INPUT_UNIT_SELECT_PREVIOUS:
        DLLExportClass::Select_Previous_Unit(player_id);
        break;
    case INPUT_UNIT_GUARD_MODE:
        DLLExportClass::Selected_Guard_Mode(player_id);
        break;
    case INPUT_UNIT_STOP:
        DLLExportClass::Selected_Stop(player_id);
        break;
    case INPUT_UNIT_FORMATION_TOGGLE:
        DLLExportClass::Team_Units_Formation_Toggle_On(player_id);
        break;
    case INPUT_UNIT_QUEUED_MOVEMENT_ON:
        // Red Alert Only
        DLLExportClass::Units_Queued_Movement_Toggle(player_id, true);
        break;
    case INPUT_UNIT_QUEUED_MOVEMENT_OFF:
        // Red Alert Only
        DLLExportClass::Units_Queued_Movement_Toggle(player_id, false);
        break;
    default:
        break;
    }
}

/**************************************************************************************************
 * CNC_Handle_Sidebar_Request -- Process an input request to the sidebar
 *
 * In:
 *
 *
 * Out:
 *
 *
 *
 * History: 1/7/2019 5:20PM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Sidebar_Request(SidebarRequestEnum request_type,
                                                                         uint64 player_id,
                                                                         int buildable_type,
                                                                         int buildable_id,
                                                                         short cell_x,
                                                                         short cell_y)
{
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    switch (request_type) {

    // Changing right-click support for first put building on hold, and then subsequenct right-clicks to decrement that
    // queue count for 1x or 5x; Then, 1x or 5x Left click will resume from hold Handle and fall through to start
    // construction (from hold state) below
    case SIDEBAR_REQUEST_START_CONSTRUCTION_MULTI:

    case SIDEBAR_REQUEST_START_CONSTRUCTION:
        DLLExportClass::Start_Construction(player_id, buildable_type, buildable_id);
        break;

    case SIDEBAR_REQUEST_HOLD_CONSTRUCTION:
        DLLExportClass::Hold_Construction(player_id, buildable_type, buildable_id);
        break;

    case SIDEBAR_REQUEST_CANCEL_CONSTRUCTION:
        DLLExportClass::Cancel_Construction(player_id, buildable_type, buildable_id);
        break;

    case SIDEBAR_REQUEST_START_PLACEMENT:
        DLLExportClass::Start_Placement(player_id, buildable_type, buildable_id);
        break;

    case SIDEBAR_REQUEST_PLACE:
        DLLExportClass::Place(player_id, buildable_type, buildable_id, cell_x, cell_y);
        break;

    case SIDEBAR_CANCEL_PLACE:
        DLLExportClass::Cancel_Placement(player_id, buildable_type, buildable_id);
        break;

    default:
        break;
    }
}

/**************************************************************************************************
 * CNC_Handle_SuperWeapon_Request
 *
 * In:
 *
 *
 * Out:
 *
 *
 *
 * History:
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_SuperWeapon_Request(SuperWeaponRequestEnum request_type,
                                                                             uint64 player_id,
                                                                             int buildable_type,
                                                                             int buildable_id,
                                                                             int x1,
                                                                             int y1)
{
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    switch (request_type) {
    case SUPERWEAPON_REQUEST_PLACE_SUPER_WEAPON:
        DLLExportClass::Place_Super_Weapon(player_id, buildable_type, buildable_id, x1, y1);
        break;
    }
}

/**************************************************************************************************
 * CNC_Handle_ControlGroup_Request
 *
 * In:
 *
 *
 * Out:
 *
 *
 *
 * History:
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_ControlGroup_Request(ControlGroupRequestEnum request_type,
                                                                              uint64 player_id,
                                                                              unsigned char control_group_index)
{
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    switch (request_type) {
    case CONTROL_GROUP_REQUEST_CREATE:
        DLLExportClass::Create_Control_Group(control_group_index);
        break;
    case CONTROL_GROUP_REQUEST_TOGGLE:
        DLLExportClass::Toggle_Control_Group_Selection(control_group_index);
        break;
    case CONTROL_GROUP_REQUEST_ADDITIVE_SELECTION:
        DLLExportClass::Add_To_Control_Group(control_group_index);
        break;
    }
}

/**************************************************************************************************
 * DLLExportClass::Get_Layer_State -- Get a snapshot of the sidebar state
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 1/29/2019 11:37AM - ST
 **************************************************************************************************/
bool DLLExportClass::Get_Sidebar_State(uint64 player_id, unsigned char* buffer_in, unsigned int buffer_size)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return false;
    }

    CNCSidebarStruct* sidebar = (CNCSidebarStruct*)buffer_in;

    unsigned int memory_needed =
        sizeof(*sidebar); // Base amount needed. Will need more depending on how many entries there are

    int entry_index = 0;

    sidebar->Credits = 0;
    sidebar->CreditsCounter = 0;
    sidebar->Tiberium = 0;
    sidebar->MaxTiberium = 0;
    sidebar->PowerProduced = 0;
    sidebar->PowerDrained = 0;
    sidebar->RepairBtnEnabled = false;
    sidebar->SellBtnEnabled = false;
    sidebar->RadarMapActive = false;
    sidebar->MissionTimer = Scen.MissionTimer.Is_Active() ? (Scen.MissionTimer / TICKS_PER_SECOND) : -1;

    sidebar->UnitsKilled = 0;
    sidebar->BuildingsKilled = 0;
    sidebar->UnitsLost = 0;
    sidebar->BuildingsLost = 0;
    sidebar->TotalHarvestedCredits = 0;

    if (PlayerPtr) {
        sidebar->Credits = PlayerPtr->Credits;
        sidebar->CreditsCounter = PlayerPtr->VisibleCredits.Current; // Timed display
        // sidebar->CreditsCounter = PlayerPtr->VisibleCredits.Credits;	// Actual
        sidebar->Tiberium = PlayerPtr->Tiberium;
        sidebar->MaxTiberium = PlayerPtr->Capacity;
        sidebar->PowerProduced = PlayerPtr->Power;
        sidebar->PowerDrained = PlayerPtr->Drain;

        sidebar->RepairBtnEnabled = PlayerPtr->BScan > 0;
        sidebar->SellBtnEnabled = PlayerPtr->BScan > 0;
        sidebar->RadarMapActive = PlayerPtr->Radar == RADAR_ON;

        // A. Get the DestroyedBuildings and DestroyedInfantry stats if they are available at this point
        for (int index = 0; index < PlayerPtr->DestroyedBuildings.Get_Unit_Count(); index++) {
            unsigned int count = (unsigned int)PlayerPtr->DestroyedBuildings.Get_Unit_Total(index);
            sidebar->BuildingsKilled += count;
        }
        for (int index = 0; index < PlayerPtr->DestroyedInfantry.Get_Unit_Count(); index++) {
            unsigned int count = (unsigned int)PlayerPtr->DestroyedInfantry.Get_Unit_Total(index);
            sidebar->UnitsKilled += count; // Includes Infantry, Vehicles, Aircraft
        }
        for (int index = 0; index < PlayerPtr->DestroyedUnits.Get_Unit_Count(); index++) {
            unsigned int count = (unsigned int)PlayerPtr->DestroyedUnits.Get_Unit_Total(index);
            sidebar->UnitsKilled += count; // Includes Infantry, Vehicles, Aircraft
        }
        for (int index = 0; index < PlayerPtr->DestroyedAircraft.Get_Unit_Count(); index++) {
            unsigned int count = (unsigned int)PlayerPtr->DestroyedAircraft.Get_Unit_Total(index);
            sidebar->UnitsKilled += count; // Includes Infantry, Vehicles, Aircraft
        }

        // B. If the DestroyedBuildings and DestroyedInfantry stats seemed to be unvailable, this is another way to do
        // it Note that we need to do both of these depending on which type of match we are running, as well as for
        // Replays/Observer and live stats reporting We can't just do it this way for everything, as it does not work
        // for all cases
        if (sidebar->BuildingsKilled == 0) {
            for (unsigned int house_index = 0; house_index < HOUSE_COUNT; house_index++) {
                sidebar->BuildingsKilled += PlayerPtr->BuildingsKilled[house_index];
            }
        }
        if (sidebar->UnitsKilled == 0) {
            for (unsigned int house_index = 0; house_index < HOUSE_COUNT; house_index++) {
                sidebar->UnitsKilled += PlayerPtr->UnitsKilled[house_index]; // Includes Infantry, Vehicles, Aircraft
            }
        }

        sidebar->UnitsLost = PlayerPtr->UnitsLost;
        sidebar->BuildingsLost = PlayerPtr->BuildingsLost;
        sidebar->TotalHarvestedCredits = PlayerPtr->HarvestedCredits;
    }

    if (GAME_TO_PLAY == GAME_NORMAL) {

        /*
        ** Get each sidebar column
        */
        for (int c = 0; c < 2; c++) {

            sidebar->EntryCount[c] = Map.Column[c].BuildableCount;

            /*
            ** Each production slot in the column
            */
            for (int b = 0; b < Map.Column[c].BuildableCount; b++) {

                CNCSidebarEntryStruct& sidebar_entry = sidebar->Entries[entry_index++];
                if ((entry_index + 1) * sizeof(CNCSidebarEntryStruct) + memory_needed > buffer_size) {
                    return false;
                }

                memset(&sidebar_entry, 0, sizeof(sidebar_entry));

                sidebar_entry.AssetName[0] = 0;
                sidebar_entry.Type = UNKNOWN;
                sidebar_entry.BuildableID = Map.Column[c].Buildables[b].BuildableID;
                sidebar_entry.BuildableType = Map.Column[c].Buildables[b].BuildableType;
                sidebar_entry.BuildableViaCapture = Map.Column[c].Buildables[b].BuildableViaCapture;
                sidebar_entry.Fake = false;

                TechnoTypeClass const* tech = Fetch_Techno_Type(Map.Column[c].Buildables[b].BuildableType,
                                                                Map.Column[c].Buildables[b].BuildableID);

                sidebar_entry.SuperWeaponType = SW_NONE;

                if (tech) {
                    sidebar_entry.Cost =
                        tech->Cost
                        * PlayerPtr->CostBias; // If this gets modified, also modify below for skirmish and multiplayer
                    sidebar_entry.PowerProvided = 0;
                    sidebar_entry.BuildTime = tech->Time_To_Build(
                        PlayerPtr->Class->House); // sidebar_entry.BuildTime = tech->Time_To_Build() / 60;
                    strncpy(sidebar_entry.AssetName, tech->IniName, CNC_OBJECT_ASSET_NAME_LENGTH);
                } else {
                    sidebar_entry.Cost = 0;
                    sidebar_entry.AssetName[0] = 0;
                }

                SuperClass* super_weapon = nullptr;

                bool isbusy = false;

                switch (Map.Column[c].Buildables[b].BuildableType) {
                case RTTI_INFANTRYTYPE:
                    sidebar_entry.Type = INFANTRY_TYPE;
                    isbusy = (PlayerPtr->InfantryFactory != -1);
                    isbusy |= Infantry.Avail() <= 0;
                    break;

                case RTTI_UNITTYPE:
                    isbusy = (PlayerPtr->UnitFactory != -1);
                    isbusy |= Units.Avail() <= 0;
                    sidebar_entry.Type = UNIT_TYPE;
                    break;

                case RTTI_AIRCRAFTTYPE:
                    isbusy = (PlayerPtr->AircraftFactory != -1);
                    isbusy |= Aircraft.Avail() <= 0;
                    sidebar_entry.Type = AIRCRAFT_TYPE;
                    break;

                case RTTI_BUILDINGTYPE: {
                    isbusy = (PlayerPtr->BuildingFactory != -1);
                    isbusy |= Buildings.Avail() <= 0;
                    sidebar_entry.Type = BUILDING_TYPE;

                    const BuildingTypeClass* build_type = static_cast<const BuildingTypeClass*>(tech);
                    sidebar_entry.PowerProvided = build_type->Power - build_type->Drain;
                    sidebar_entry.Fake = build_type->IsFake;
                } break;

                case RTTI_VESSELTYPE:
                    sidebar_entry.Type = VESSEL_TYPE;
                    isbusy = (PlayerPtr->VesselFactory != -1);
                    isbusy |= Vessels.Avail() <= 0;
                    break;

                default:
                    sidebar_entry.Type = UNKNOWN;
                    break;

                case RTTI_SPECIAL:
                    Fill_Sidebar_Entry_From_Special_Weapon(
                        sidebar_entry, super_weapon, (SpecialWeaponType)Map.Column[c].Buildables[b].BuildableID);
                    break;
                }

                if (super_weapon != nullptr) {
                    sidebar_entry.Progress = (float)super_weapon->Anim_Stage() / (float)SuperClass::ANIMATION_STAGES;
                    sidebar_entry.Completed = super_weapon->Is_Ready();
                    sidebar_entry.Constructing = super_weapon->Anim_Stage() != SuperClass::ANIMATION_STAGES;
                    sidebar_entry.ConstructionOnHold = false;
                    sidebar_entry.PlacementListLength = 0;
                    sidebar_entry.PowerProvided = 0;
                    sidebar_entry.BuildTime = super_weapon->Get_Recharge_Time();
                } else {

                    int fnumber = Map.Column[c].Buildables[b].Factory;
                    FactoryClass* factory = NULL;
                    if (tech && fnumber != -1) {
                        factory = Factories.Raw_Ptr(fnumber);
                    }

                    sidebar_entry.Completed = false;
                    sidebar_entry.Constructing = false;
                    sidebar_entry.ConstructionOnHold = false;
                    sidebar_entry.Progress = 0.0f;
                    sidebar_entry.Busy = isbusy;
                    sidebar_entry.PlacementListLength = 0;

                    if (factory) {
                        if (factory->Is_Building()) {
                            sidebar_entry.Constructing = true;
                            sidebar_entry.Progress = (float)factory->Completion() / (float)FactoryClass::STEP_COUNT;
                            sidebar_entry.Completed = factory->Has_Completed();
                        } else {
                            sidebar_entry.Completed = factory->Has_Completed();

                            if (!sidebar_entry.Completed) {
                                sidebar_entry.ConstructionOnHold = true;
                                sidebar_entry.Progress = (float)factory->Completion() / (float)FactoryClass::STEP_COUNT;
                            }

                            if (sidebar_entry.Completed && sidebar_entry.Type == BUILDING_TYPE) {
                                if (tech) {
                                    BuildingTypeClass* building_type = (BuildingTypeClass*)tech;
                                    short const* occupy_list = building_type->Occupy_List(true);
                                    if (occupy_list) {
                                        while (*occupy_list != REFRESH_EOL
                                               && sidebar_entry.PlacementListLength < MAX_OCCUPY_CELLS) {
                                            sidebar_entry.PlacementList[sidebar_entry.PlacementListLength] =
                                                *occupy_list;
                                            sidebar_entry.PlacementListLength++;
                                            occupy_list++;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

    } else {

        if (GAME_TO_PLAY == GAME_GLYPHX_MULTIPLAYER) {

            SidebarGlyphxClass* context_sidebar = DLLExportClass::Get_Current_Context_Sidebar();

            /*
            ** Get each sidebar column
            */
            for (int c = 0; c < 2; c++) {

                sidebar->EntryCount[c] = context_sidebar->Column[c].BuildableCount;

                /*
                ** Each production slot in the column
                */
                for (int b = 0; b < context_sidebar->Column[c].BuildableCount; b++) {

                    CNCSidebarEntryStruct& sidebar_entry = sidebar->Entries[entry_index++];
                    if ((entry_index + 1) * sizeof(CNCSidebarEntryStruct) + memory_needed > buffer_size) {
                        return false;
                    }

                    memset(&sidebar_entry, 0, sizeof(sidebar_entry));

                    sidebar_entry.AssetName[0] = 0;
                    sidebar_entry.Type = UNKNOWN;
                    sidebar_entry.BuildableID = context_sidebar->Column[c].Buildables[b].BuildableID;
                    sidebar_entry.BuildableType = context_sidebar->Column[c].Buildables[b].BuildableType;
                    sidebar_entry.BuildableViaCapture = context_sidebar->Column[c].Buildables[b].BuildableViaCapture;
                    sidebar_entry.Fake = false;

                    TechnoTypeClass const* tech =
                        Fetch_Techno_Type(context_sidebar->Column[c].Buildables[b].BuildableType,
                                          context_sidebar->Column[c].Buildables[b].BuildableID);

                    sidebar_entry.SuperWeaponType = SW_NONE;

                    if (tech) {

                        // Updated to apply and difficulty abd/or faction price modifier; See
                        // https://jaas.ea.com/browse/TDRA-6864 If this gets modified, also modify above for
                        // non-skirmish / non-multiplayer
                        //
                        // sidebar_entry.Cost = tech->Cost;
                        sidebar_entry.Cost = tech->Cost * PlayerPtr->CostBias;

                        sidebar_entry.PowerProvided = 0;
                        sidebar_entry.BuildTime = tech->Time_To_Build(
                            PlayerPtr->Class->House); // sidebar_entry.BuildTime = tech->Time_To_Build() / 60;
                        strncpy(sidebar_entry.AssetName, tech->IniName, CNC_OBJECT_ASSET_NAME_LENGTH);
                    } else {
                        sidebar_entry.Cost = 0;
                        sidebar_entry.AssetName[0] = 0;
                    }

                    SuperClass* super_weapon = nullptr;
                    bool isbusy = false;

                    switch (context_sidebar->Column[c].Buildables[b].BuildableType) {
                    case RTTI_INFANTRYTYPE:
                        sidebar_entry.Type = INFANTRY_TYPE;
                        isbusy = (PlayerPtr->InfantryFactory != -1);
                        isbusy |= Infantry.Avail() <= 0;
                        break;

                    case RTTI_UNITTYPE:
                        isbusy = (PlayerPtr->UnitFactory != -1);
                        isbusy |= Units.Avail() <= 0;
                        sidebar_entry.Type = UNIT_TYPE;
                        break;

                    case RTTI_AIRCRAFTTYPE:
                        isbusy = (PlayerPtr->AircraftFactory != -1);
                        isbusy |= Aircraft.Avail() <= 0;
                        sidebar_entry.Type = AIRCRAFT_TYPE;
                        break;

                    case RTTI_BUILDINGTYPE: {
                        isbusy = (PlayerPtr->BuildingFactory != -1);
                        isbusy |= Buildings.Avail() <= 0;
                        sidebar_entry.Type = BUILDING_TYPE;

                        const BuildingTypeClass* build_type = static_cast<const BuildingTypeClass*>(tech);
                        sidebar_entry.PowerProvided = build_type->Power - build_type->Drain;
                        sidebar_entry.Fake = build_type->IsFake;
                        break;
                    }

                    case RTTI_VESSELTYPE:
                        isbusy = (PlayerPtr->VesselFactory != -1);
                        isbusy |= Vessels.Avail() <= 0;
                        sidebar_entry.Type = VESSEL_TYPE;
                        break;

                    default:
                        sidebar_entry.Type = UNKNOWN;
                        break;

                    case RTTI_SPECIAL:
                        Fill_Sidebar_Entry_From_Special_Weapon(
                            sidebar_entry, super_weapon, (SpecialWeaponType)sidebar_entry.BuildableID);
                        break;
                    }

                    if (super_weapon != nullptr) {
                        sidebar_entry.Progress =
                            (float)super_weapon->Anim_Stage() / (float)SuperClass::ANIMATION_STAGES;
                        sidebar_entry.Completed = super_weapon->Is_Ready();
                        sidebar_entry.Constructing = super_weapon->Anim_Stage() != SuperClass::ANIMATION_STAGES;
                        sidebar_entry.ConstructionOnHold = false;
                        sidebar_entry.PlacementListLength = 0;
                        sidebar_entry.PowerProvided = 0;
                        sidebar_entry.BuildTime = super_weapon->Get_Recharge_Time();
                    } else {

                        int fnumber = context_sidebar->Column[c].Buildables[b].Factory;
                        FactoryClass* factory = NULL;
                        if (tech && fnumber != -1) {
                            factory = Factories.Raw_Ptr(fnumber);
                        }

                        sidebar_entry.Completed = false;
                        sidebar_entry.Constructing = false;
                        sidebar_entry.ConstructionOnHold = false;
                        sidebar_entry.Progress = 0.0f;
                        sidebar_entry.Busy = isbusy;
                        sidebar_entry.PlacementListLength = 0;

                        if (factory) {
                            if (factory->Is_Building()) {
                                sidebar_entry.Constructing = true;
                                sidebar_entry.Progress = (float)factory->Completion() / (float)FactoryClass::STEP_COUNT;
                                sidebar_entry.Completed = factory->Has_Completed();
                            } else {
                                sidebar_entry.Completed = factory->Has_Completed();

                                if (!sidebar_entry.Completed) {
                                    sidebar_entry.ConstructionOnHold = true;
                                    sidebar_entry.Progress =
                                        (float)factory->Completion() / (float)FactoryClass::STEP_COUNT;
                                }

                                if (sidebar_entry.Completed && sidebar_entry.Type == BUILDING_TYPE) {
                                    if (tech) {
                                        BuildingTypeClass* building_type = (BuildingTypeClass*)tech;
                                        short const* occupy_list = building_type->Occupy_List(true);
                                        if (occupy_list) {
                                            while (*occupy_list != REFRESH_EOL
                                                   && sidebar_entry.PlacementListLength < MAX_OCCUPY_CELLS) {
                                                sidebar_entry.PlacementList[sidebar_entry.PlacementListLength] =
                                                    *occupy_list;
                                                sidebar_entry.PlacementListLength++;
                                                occupy_list++;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return true;
}

void DLLExportClass::Convert_Action_Type(ActionType type,
                                         ObjectClass* object,
                                         TARGET target,
                                         DllActionTypeEnum& dll_type)
{
    switch (type) {
    case ACTION_NONE:
    default:
        dll_type = DAT_NONE;
        break;
    case ACTION_MOVE:
        dll_type = DAT_MOVE;
        break;
    case ACTION_NOMOVE:
        dll_type = DAT_NOMOVE;
        break;
    case ACTION_ENTER:
        dll_type = DAT_ENTER;
        break;
    case ACTION_SELF:
        dll_type = DAT_SELF;
        break;
    case ACTION_ATTACK:
        if (Target_Legal(target) && (object != NULL) && object->Is_Techno()
            && ((TechnoClass*)object)->In_Range(target, 0)) {
            dll_type = DAT_ATTACK;
        } else {
            dll_type = DAT_ATTACK_OUT_OF_RANGE;
        }
        break;
    case ACTION_GUARD_AREA:
        dll_type = DAT_GUARD;
        break;
    case ACTION_HARVEST:
        dll_type = DAT_ATTACK;
        break;
    case ACTION_SELECT:
    case ACTION_TOGGLE_SELECT:
        dll_type = DAT_SELECT;
        break;
    case ACTION_CAPTURE:
        dll_type = DAT_CAPTURE;
        break;
    case ACTION_DAMAGE:
        dll_type = DAT_DAMAGE;
        break;
    case ACTION_SABOTAGE:
        dll_type = DAT_SABOTAGE;
        break;
    case ACTION_HEAL:
        dll_type = DAT_HEAL;
        break;
    case ACTION_TOGGLE_PRIMARY:
        dll_type = DAT_TOGGLE_PRIMARY;
        break;
    case ACTION_NO_DEPLOY:
        dll_type = DAT_CANT_DEPLOY;
        break;
    case ACTION_GREPAIR:
        dll_type = DAT_REPAIR;
        break;
    case ACTION_NO_GREPAIR:
        dll_type = DAT_CANT_REPAIR;
        break;
    }
}

void DLLExportClass::Convert_Special_Weapon_Type(SpecialWeaponType weapon_type,
                                                 DllSuperweaponTypeEnum& dll_weapon_type,
                                                 char* weapon_name)
{
    switch (weapon_type) {
    case SPC_SONAR_PULSE:
        dll_weapon_type = SW_SONAR_PULSE;
        if (weapon_name != NULL) {
            strncpy(weapon_name, "SW_SonarPulse", 16);
        }
        break;
    case SPC_NUCLEAR_BOMB:
        dll_weapon_type = SW_NUKE;
        if (weapon_name != NULL) {
            strncpy(weapon_name, "SW_Nuke", 16);
        }
        break;
    case SPC_CHRONOSPHERE:
        dll_weapon_type = SW_CHRONOSPHERE;
        if (weapon_name != NULL) {
            strncpy(weapon_name, "SW_Chrono", 16);
        }
        break;
    case SPC_PARA_BOMB:
        dll_weapon_type = SW_PARA_BOMB;
        if (weapon_name != NULL) {
            strncpy(weapon_name, "SW_ParaBomb", 16);
        }
        break;
    case SPC_PARA_INFANTRY:
        dll_weapon_type = SW_PARA_INFANTRY;
        if (weapon_name != NULL) {
            strncpy(weapon_name, "SW_ParaInfantry", 16);
        }
        break;
    case SPC_SPY_MISSION:
        dll_weapon_type = SW_SPY_MISSION;
        if (weapon_name != NULL) {
            strncpy(weapon_name, "SW_SpyMission", 16);
        }
        break;
    case SPC_IRON_CURTAIN:
        dll_weapon_type = SW_IRON_CURTAIN;
        if (weapon_name != NULL) {
            strncpy(weapon_name, "SW_IronCurtain", 16);
        }
        break;
    case SPC_GPS:
        dll_weapon_type = SW_GPS;
        if (weapon_name != NULL) {
            strncpy(weapon_name, "SW_GPS", 16);
        }
        break;
    case SPC_CHRONO2:
        dll_weapon_type = SW_CHRONOSPHERE_DESTINATION;
        if (weapon_name != NULL) {
            strncpy(weapon_name, "SW_Chrono2", 16);
        }
        break;
    default:
        dll_weapon_type = SW_UNKNOWN;
        if (weapon_name != NULL) {
            weapon_name[0] = '\0';
        }
        break;
    }
}

void DLLExportClass::Fill_Sidebar_Entry_From_Special_Weapon(CNCSidebarEntryStruct& sidebar_entry_out,
                                                            SuperClass*& super_weapon_out,
                                                            SpecialWeaponType weapon_type)
{
    sidebar_entry_out.Type = SPECIAL;

    switch (weapon_type) {
    case SPC_SONAR_PULSE:
    case SPC_NUCLEAR_BOMB:
    case SPC_CHRONOSPHERE:
    case SPC_PARA_BOMB:
    case SPC_PARA_INFANTRY:
    case SPC_SPY_MISSION:
    case SPC_IRON_CURTAIN:
    case SPC_GPS:
    case SPC_CHRONO2:
        Convert_Special_Weapon_Type(weapon_type, sidebar_entry_out.SuperWeaponType, sidebar_entry_out.AssetName);
        break;
    default:
        sidebar_entry_out.SuperWeaponType = SW_UNKNOWN;
        sidebar_entry_out.Type = UNKNOWN;
        super_weapon_out = nullptr;
        return;
    }

    super_weapon_out = &(PlayerPtr->SuperWeapon[weapon_type]);
}

static const int _map_width_shift_bits = 7;

void DLLExportClass::Calculate_Placement_Distances(BuildingTypeClass* placement_type, unsigned char* placement_distance)
{
    int map_cell_x = Map.MapCellX;
    int map_cell_y = Map.MapCellY;
    int map_cell_width = Map.MapCellWidth;
    int map_cell_height = Map.MapCellHeight;

    if (map_cell_x > 0) {
        map_cell_x--;
        map_cell_width++;
    }

    if (map_cell_width < MAP_MAX_CELL_WIDTH) {
        map_cell_width++;
    }

    if (map_cell_y > 0) {
        map_cell_y--;
        map_cell_height++;
    }

    if (map_cell_height < MAP_MAX_CELL_WIDTH) {
        map_cell_height++;
    }

    static const FacingType _scan_facings[] = {FACING_E, FACING_S, FACING_W, FACING_N};

    memset(placement_distance, 255U, MAP_CELL_TOTAL);
    for (int y = 0; y < map_cell_height; y++) {
        for (int x = 0; x < map_cell_width; x++) {
            CELL cell = (CELL)map_cell_x + x + ((map_cell_y + y) << _map_width_shift_bits);
            BuildingClass* base = (BuildingClass*)Map[cell].Cell_Find_Object(RTTI_BUILDING);
            if ((base && base->House->Class->House == PlayerPtr->Class->House && base->Class->IsBase)
                || ((placement_type->IsWall
                     || ((Map[cell].Smudge != SMUDGE_NONE) && SmudgeTypeClass::As_Reference(Map[cell].Smudge).IsBib))
                    && Map[cell].Owner == PlayerPtr->Class->House)) {
                placement_distance[cell] = 0U;
                CELL startcell = cell;
                for (unsigned char distance = 1U; distance <= (placement_type->Adjacent + 1U); distance++) {
                    startcell = Adjacent_Cell(startcell, FACING_NW);
                    CELL scancell = startcell;
                    for (int i = 0; i < ARRAY_SIZE(_scan_facings); i++) {
                        CELL nextcell = scancell;
                        for (unsigned char scan = 0U; scan <= (distance * 2U); scan++) {
                            scancell = nextcell;
                            if (Map.In_Radar(scancell)) {
                                placement_distance[scancell] = min(placement_distance[scancell], distance);
                            }
                            nextcell = Adjacent_Cell(scancell, _scan_facings[i]);
                        }
                    }
                }
            }
        }
    }
}

void Recalculate_Placement_Distances()
{
    DLLExportClass::Recalculate_Placement_Distances();
}

void DLLExportClass::Recalculate_Placement_Distances()
{
    if (PlacementType[CurrentLocalPlayerIndex] != NULL) {
        Calculate_Placement_Distances(PlacementType[CurrentLocalPlayerIndex],
                                      PlacementDistance[CurrentLocalPlayerIndex]);
    }
}

/**************************************************************************************************
 * DLLExportClass::Get_Placement_State -- Get a snapshot of legal validity of placing a structure on all map cells
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 2/4/2019 3:11PM - ST
 **************************************************************************************************/
bool DLLExportClass::Get_Placement_State(uint64 player_id, unsigned char* buffer_in, unsigned int buffer_size)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return false;
    }

    if (PlacementType[CurrentLocalPlayerIndex] == NULL) {
        return false;
    }

    CNCPlacementInfoStruct* placement_info = (CNCPlacementInfoStruct*)buffer_in;

    unsigned int memory_needed =
        sizeof(*placement_info); // Base amount needed. Will need more depending on how many entries there are

    int map_cell_x = Map.MapCellX;
    int map_cell_y = Map.MapCellY;
    int map_cell_width = Map.MapCellWidth;
    int map_cell_height = Map.MapCellHeight;

    if (map_cell_x > 0) {
        map_cell_x--;
        map_cell_width++;
    }

    if (map_cell_width < MAP_MAX_CELL_WIDTH) {
        map_cell_width++;
    }

    if (map_cell_y > 0) {
        map_cell_y--;
        map_cell_height++;
    }

    if (map_cell_height < MAP_MAX_CELL_WIDTH) {
        map_cell_height++;
    }

    memory_needed += map_cell_width * map_cell_height * sizeof(CNCPlacementCellInfoStruct);

    if (memory_needed + 128 >= buffer_size) {
        return false;
    }

    placement_info->Count = map_cell_width * map_cell_height;

    int index = 0;
    for (int y = 0; y < map_cell_height; y++) {
        for (int x = 0; x < map_cell_width; x++) {

            CELL cell = (CELL)map_cell_x + x + ((map_cell_y + y) << _map_width_shift_bits);

            bool pass = Passes_Proximity_Check(
                cell, PlacementType[CurrentLocalPlayerIndex], PlacementDistance[CurrentLocalPlayerIndex]);

            CellClass* cellptr = &Map[cell];
            bool clear = cellptr->Is_Clear_To_Build(PlacementType[CurrentLocalPlayerIndex]->Speed);

            CNCPlacementCellInfoStruct& placement_cell_info = placement_info->CellInfo[index++];
            placement_cell_info.PassesProximityCheck = pass;
            placement_cell_info.GenerallyClear = clear;
        }
    }

    Map.ZoneOffset = 0;

    return true;
}

bool DLLExportClass::Passes_Proximity_Check(CELL cell_in,
                                            BuildingTypeClass* placement_type,
                                            unsigned char* placement_distance)
{

    /*
    **	Scan through all cells that the building foundation would cover. If any adjacent
    **	cells to these are of friendly persuasion, then consider the proximity check to
    **	have been a success.
    */
    short const* occupy_list = placement_type->Occupy_List(true);

    /*
    ** OpenCNC3D: the cheat menu's Build Anywhere. This routine fills in the per-cell flag the
    ** placement overlay draws, so it must agree with DisplayClass::Passes_Proximity_Check. It
    ** lifts the adjacency rule only; In_Radar is still demanded of every cell the foundation
    ** covers, so "anywhere" means anywhere ON THE MAP.
    */
    const bool anywhere =
        DisplayClass::OpenCNC3D_Build_Anywhere((PlayerPtr != NULL) ? PlayerPtr->Class->House : HOUSE_NONE);

    while (*occupy_list != REFRESH_EOL) {

        CELL center_cell = cell_in + *occupy_list++;

        if (!Map.In_Radar(center_cell)) {
            return false;
        }

        if (!anywhere && placement_distance[center_cell] <= (placement_type->Adjacent + 1)) {
            return true;
        }
    }

    return anywhere;
}

/**************************************************************************************************
 * DLLExportClass::Start_Construction -- Start sidebar construction
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 1/29/2019 11:37AM - ST
 **************************************************************************************************/
bool DLLExportClass::Start_Construction(uint64 player_id, int buildable_type, int buildable_id)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return false;
    }

    if (GAME_TO_PLAY == GAME_NORMAL) {
        return Construction_Action(SIDEBAR_REQUEST_START_CONSTRUCTION, player_id, buildable_type, buildable_id);
    }
    return MP_Construction_Action(SIDEBAR_REQUEST_START_CONSTRUCTION, player_id, buildable_type, buildable_id);
}

/**************************************************************************************************
 * DLLExportClass::Hold_Construction -- Pause sidebar construction
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 6/12/2019 JAS
 **************************************************************************************************/
bool DLLExportClass::Hold_Construction(uint64 player_id, int buildable_type, int buildable_id)
{
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return false;
    }

    if (GAME_TO_PLAY == GAME_NORMAL) {
        return Construction_Action(SIDEBAR_REQUEST_HOLD_CONSTRUCTION, player_id, buildable_type, buildable_id);
    }
    return MP_Construction_Action(SIDEBAR_REQUEST_HOLD_CONSTRUCTION, player_id, buildable_type, buildable_id);
}

/**************************************************************************************************
 * DLLExportClass::Cancel_Construction -- Stop sidebar construction
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 6/12/2019 JAS
 **************************************************************************************************/
bool DLLExportClass::Cancel_Construction(uint64 player_id, int buildable_type, int buildable_id)
{
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return false;
    }

    return Cancel_Placement(player_id, buildable_type, buildable_id)
           && ((GAME_TO_PLAY == GAME_NORMAL)
                   ? Construction_Action(SIDEBAR_REQUEST_CANCEL_CONSTRUCTION, player_id, buildable_type, buildable_id)
                   : MP_Construction_Action(
                       SIDEBAR_REQUEST_CANCEL_CONSTRUCTION, player_id, buildable_type, buildable_id));
}

/**************************************************************************************************
 * DLLExportClass::Construction_Action -- Reproduce actions on the sidebar
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 1/29/2019 11:37AM - ST
 **************************************************************************************************/
bool DLLExportClass::Construction_Action(SidebarRequestEnum construction_action,
                                         uint64 player_id,
                                         int buildable_type,
                                         int buildable_id)
{

    /*
    **
    ** Based on SidebarClass::StripClass::SelectClass::Action
    **
    ** Most of this code is validating that the game is in the correct state to be able to act on a sidebar icon
    **
    */

    for (int c = 0; c < 2; c++) {

        /*
        ** Each production slot in the column
        */
        for (int b = 0; b < Map.Column[c].BuildableCount; b++) {
            if (Map.Column[c].Buildables[b].BuildableID == buildable_id) {
                if (Map.Column[c].Buildables[b].BuildableType == buildable_type) {

                    int fnumber = Map.Column[c].Buildables[b].Factory;
                    int spc = 0;
                    ObjectTypeClass const* choice = NULL;

                    if (buildable_type != RTTI_SPECIAL) {
                        choice = Fetch_Techno_Type((RTTIType)buildable_type, buildable_id);
                    } else {
                        spc = buildable_id;
                    }

                    FactoryClass* factory = PlayerPtr->Fetch_Factory((RTTIType)buildable_type);
                    if (fnumber != -1) {
                        factory = Factories.Raw_Ptr(fnumber);
                    }

                    if (spc == 0 && choice) {
                        if (fnumber == -1 && factory != NULL) {
                            return (false);
                        }

                        if (factory) {

                            switch (construction_action) {
                            case SIDEBAR_REQUEST_CANCEL_CONSTRUCTION:
                                On_Speech(PlayerPtr, VOX_CANCELED); // Speak(VOX_CANCELED);
                                OutList.Add(EventClass(EventClass::ABANDON, (RTTIType)buildable_type, buildable_id));
                                break;
                            case SIDEBAR_REQUEST_HOLD_CONSTRUCTION:
                                if (factory->Is_Building()) {
                                    On_Speech(PlayerPtr, VOX_SUSPENDED); // Speak(VOX_SUSPENDED);
                                    OutList.Add(
                                        EventClass(EventClass::SUSPEND, (RTTIType)buildable_type, buildable_id));
                                }
                                break;

                            default:
                                /*
                                **	If this object is currently being built, then give a scold sound and text and then
                                **	bail.
                                */
                                if (factory->Is_Building()) {
                                    On_Speech(PlayerPtr, VOX_NO_FACTORY); // Speak(VOX_NO_FACTORY); // "Cannot Comply"
                                    return false;
                                } else {

                                    /*
                                    **	If production has completed, then attempt to have the object exit
                                    **	the factory or go into placement mode.
                                    */
                                    if (factory->Has_Completed()) {

                                        TechnoClass* pending = factory->Get_Object();
                                        if (!pending && factory->Get_Special_Item()) {
                                            // TO_DO
                                            // Map.IsTargettingMode = true;
                                        } else {

                                            BuildingClass* builder = pending->Who_Can_Build_Me(false, false);
                                            if (!builder) {
                                                OutList.Add(EventClass(
                                                    EventClass::ABANDON, (RTTIType)buildable_type, buildable_id));
                                                On_Speech(PlayerPtr,
                                                          VOX_NO_FACTORY); // Speak(VOX_NO_FACTORY); // "Cannot Comply"
                                            } else {

                                                /*
                                                **	If the completed object is a building, then change the
                                                **	game state into building placement mode. This fact is
                                                **	not transmitted to any linked computers until the moment
                                                **	the building is actually placed down.
                                                */
                                                if (pending->What_Am_I() == RTTI_BUILDING) {
                                                    if (construction_action == SIDEBAR_REQUEST_START_PLACEMENT) {
                                                        PlayerPtr->Manual_Place(builder, (BuildingClass*)pending);
                                                    }
                                                } else {

                                                    /*
                                                    **	For objects that can leave the factory under their own
                                                    **	power, queue this event and process through normal house
                                                    **	production channels.
                                                    */
                                                    // OutList.Add(EventClass(EventClass::PLACE, otype, -1));
                                                }
                                            }
                                        }

                                    } else {

                                        /*
                                        **	The factory must have been in a suspended state. Resume construction
                                        **	normally.
                                        */
                                        if (construction_action == SIDEBAR_REQUEST_START_CONSTRUCTION) {
                                            if ((RTTIType)buildable_type == RTTI_INFANTRYTYPE) {
                                                On_Speech(PlayerPtr, VOX_TRAINING); // Speak(VOX_TRAINING);
                                            } else {
                                                On_Speech(PlayerPtr, VOX_BUILDING); // Speak(VOX_BUILDING);
                                            }
                                            OutList.Add(EventClass(
                                                EventClass::PRODUCE, (RTTIType)buildable_type, buildable_id));
                                            return true;
                                        }
                                    }
                                }
                            }
                        } else {

                            switch (construction_action) {
                            case SIDEBAR_REQUEST_CANCEL_CONSTRUCTION:
                            case SIDEBAR_REQUEST_HOLD_CONSTRUCTION:
                                break;

                            default:
                                /*
                                **	If this side strip is already busy with production, then ignore the
                                **	input and announce this fact.
                                */
                                if ((RTTIType)buildable_type == RTTI_INFANTRYTYPE) {
                                    On_Speech(PlayerPtr, VOX_TRAINING); // Speak(VOX_TRAINING);
                                } else {
                                    On_Speech(PlayerPtr, VOX_BUILDING); // Speak(VOX_BUILDING);
                                }
                                OutList.Add(EventClass(EventClass::PRODUCE, (RTTIType)buildable_type, buildable_id));

                                /*
                                ** Execute immediately so we get the sidebar feedback
                                */
                                Queue_AI();

                                return true;
                            }
                        }
                    }
                }
            }
        }
    }
    return false;
}

/**************************************************************************************************
 * DLLExportClass::MP_Construction_Action -- Reproduce actions on the sidebar
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 3/26/2019 1:02PM - ST
 **************************************************************************************************/
bool DLLExportClass::MP_Construction_Action(SidebarRequestEnum construction_action,
                                            uint64 player_id,
                                            int buildable_type,
                                            int buildable_id)
{

    /*
    **
    ** Based on SidebarClass::StripClass::SelectClass::Action
    **
    ** Most of this code is validating that the game is in the correct state to be able to act on a sidebar icon
    **
    */

    SidebarGlyphxClass* context_sidebar = DLLExportClass::Get_Current_Context_Sidebar();

    for (int c = 0; c < 2; c++) {

        /*
        ** Each production slot in the column
        */
        for (int b = 0; b < context_sidebar->Column[c].BuildableCount; b++) {
            if (context_sidebar->Column[c].Buildables[b].BuildableID == buildable_id) {
                if (context_sidebar->Column[c].Buildables[b].BuildableType == buildable_type) {

                    int genfactory = -1;
                    switch (buildable_type) {
                    case RTTI_INFANTRYTYPE:
                        genfactory = PlayerPtr->InfantryFactory;
                        break;

                    case RTTI_UNITTYPE:
                        genfactory = PlayerPtr->UnitFactory;
                        break;

                    case RTTI_AIRCRAFTTYPE:
                        genfactory = PlayerPtr->AircraftFactory;
                        break;

                    case RTTI_BUILDINGTYPE:
                        genfactory = PlayerPtr->BuildingFactory;
                        break;

                    default:
                        genfactory = -1;
                        break;
                    }

                    int fnumber = context_sidebar->Column[c].Buildables[b].Factory;
                    int spc = 0;
                    ObjectTypeClass const* choice = NULL;

                    if (buildable_type != RTTI_SPECIAL) {
                        choice = Fetch_Techno_Type((RTTIType)buildable_type, buildable_id);
                    } else {
                        spc = buildable_id;
                    }

                    FactoryClass* factory = NULL;
                    if (fnumber != -1) {
                        factory = Factories.Raw_Ptr(fnumber);
                    }

                    if (spc == 0 && choice) {
                        /*
                        **	If there is already a factory attached to this strip but the player didn't click
                        **	on the icon that has the attached factory, then say that the factory is busy and
                        **	ignore the click.
                        */
                        if (fnumber == -1 && genfactory != -1) {
                            On_Speech(PlayerPtr, VOX_NO_FACTORY); // Speak(VOX_NO_FACTORY); // "Cannot Comply"
                            return (false);
                        }

                        if (factory) {

                            switch (construction_action) {
                            case SIDEBAR_REQUEST_CANCEL_CONSTRUCTION:
                                On_Speech(PlayerPtr, VOX_CANCELED); // Speak(VOX_CANCELED);
                                OutList.Add(EventClass(EventClass::ABANDON, (RTTIType)buildable_type, buildable_id));
                                break;
                            case SIDEBAR_REQUEST_HOLD_CONSTRUCTION:
                                if (factory->Is_Building()) {
                                    On_Speech(PlayerPtr, VOX_SUSPENDED); // Speak(VOX_SUSPENDED);
                                    OutList.Add(
                                        EventClass(EventClass::SUSPEND, (RTTIType)buildable_type, buildable_id));
                                }
                                break;

                            default:
                                /*
                                **	If this object is currently being built, then give a scold sound and text and then
                                **	bail.
                                */
                                if (factory->Is_Building()) {
                                    On_Speech(PlayerPtr, VOX_NO_FACTORY); // Speak(VOX_NO_FACTORY); // "Cannot Comply"
                                    return false;
                                } else {

                                    /*
                                    **	If production has completed, then attempt to have the object exit
                                    **	the factory or go into placement mode.
                                    */
                                    if (factory->Has_Completed()) {

                                        TechnoClass* pending = factory->Get_Object();
                                        if (!pending && factory->Get_Special_Item()) {
                                            // TO_DO
                                            // Map.IsTargettingMode = true;
                                        } else {

                                            BuildingClass* builder = pending->Who_Can_Build_Me(false, false);
                                            if (!builder) {
                                                On_Speech(PlayerPtr,
                                                          VOX_NO_FACTORY); // Speak(VOX_NO_FACTORY); // "Cannot Comply"
                                                OutList.Add(EventClass(
                                                    EventClass::ABANDON, (RTTIType)buildable_type, buildable_id));
                                                // Speak(VOX_NO_FACTORY);
                                            } else {

                                                /*
                                                **	If the completed object is a building, then change the
                                                **	game state into building placement mode. This fact is
                                                **	not transmitted to any linked computers until the moment
                                                **	the building is actually placed down.
                                                */
                                                if (pending->What_Am_I() == RTTI_BUILDING) {
                                                    if (construction_action == SIDEBAR_REQUEST_START_PLACEMENT) {
                                                        if (DLLExportClass::Legacy_Render_Enabled()) {
                                                            PlayerPtr->Manual_Place(builder, (BuildingClass*)pending);
                                                        } else {
                                                            Unselect_All();
                                                        }
                                                    }
                                                } else {

                                                    /*
                                                    **	For objects that can leave the factory under their own
                                                    **	power, queue this event and process through normal house
                                                    **	production channels.
                                                    */
                                                    // OutList.Add(EventClass(EventClass::PLACE, otype, -1));
                                                }
                                            }
                                        }

                                    } else {

                                        /*
                                        **	The factory must have been in a suspended state. Resume construction
                                        **	normally.
                                        */
                                        if (construction_action == SIDEBAR_REQUEST_START_CONSTRUCTION) {
                                            if ((RTTIType)buildable_type == RTTI_INFANTRYTYPE) {
                                                On_Speech(PlayerPtr, VOX_TRAINING); // Speak(VOX_TRAINING);
                                            } else {
                                                On_Speech(PlayerPtr, VOX_BUILDING); // Speak(VOX_BUILDING);
                                            }
                                            OutList.Add(EventClass(
                                                EventClass::PRODUCE, (RTTIType)buildable_type, buildable_id));
                                            return true;
                                        }
                                    }
                                }
                                break;
                            }

                        } else {

                            switch (construction_action) {
                            case SIDEBAR_REQUEST_CANCEL_CONSTRUCTION:
                            case SIDEBAR_REQUEST_HOLD_CONSTRUCTION:
                                break;

                            default:
                                /*
                                **
                                */
                                if ((RTTIType)buildable_type == RTTI_INFANTRYTYPE) {
                                    On_Speech(PlayerPtr, VOX_TRAINING); // Speak(VOX_TRAINING);
                                } else {
                                    On_Speech(PlayerPtr, VOX_BUILDING); // Speak(VOX_BUILDING);
                                }
                                OutList.Add(EventClass(EventClass::PRODUCE, (RTTIType)buildable_type, buildable_id));

                                /*
                                ** Execute immediately so we get the sidebar feedback
                                **
                                **	OpenCNC3D lockstep: NOT in a match. This pumps the queue from inside an
                                **	input handler, so the PRODUCE above would execute now and on this
                                **	machine only. Under lockstep the order goes out through the drain and
                                **	the sidebar entry appears when it executes everywhere at once.
                                */
                                if (!OpenCNC3D_Lockstep) {
                                    DLLExportClass::Glyphx_Queue_AI();
                                }

                                return true;
                            }
                        }
                    }
                }
            }
        }
    }
    return false;
}

/**************************************************************************************************
 * DLLExportClass::Start_Placement -- Start placing a completed structure
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 1/29/2019 11:37AM - ST
 **************************************************************************************************/
bool DLLExportClass::Start_Placement(uint64 player_id, int buildable_type, int buildable_id)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return false;
    }

    BuildingClass* building = Get_Pending_Placement_Object(player_id, buildable_type, buildable_id);

    if (building) {

        TechnoTypeClass const* tech = Fetch_Techno_Type((RTTIType)buildable_type, buildable_id);

        if (tech) {
            BuildingTypeClass* building_type = (BuildingTypeClass*)tech;
            // short const *occupy_list = building_type->Get_Occupy_List(true);

            PlacementType[CurrentLocalPlayerIndex] = building_type;
            Recalculate_Placement_Distances();

            if (GAME_TO_PLAY == GAME_NORMAL) {
                return Construction_Action(SIDEBAR_REQUEST_START_PLACEMENT, player_id, buildable_type, buildable_id);
            }
            return MP_Construction_Action(SIDEBAR_REQUEST_START_PLACEMENT, player_id, buildable_type, buildable_id);
        }
    }
    return true;
}

/**************************************************************************************************
 * DLLExportClass::Cancel_Placement -- Cancel placing a completed structure
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 2/7/2019 10:52AM - ST
 **************************************************************************************************/
bool DLLExportClass::Cancel_Placement(uint64 player_id, int buildable_type, int buildable_id)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return false;
    }

    PlacementType[CurrentLocalPlayerIndex] = NULL;

    Map.PendingObjectPtr = 0;
    Map.PendingObject = 0;
    Map.PendingHouse = HOUSE_NONE;
    Map.IsTargettingMode = SPC_NONE;
    Map.Set_Cursor_Shape(0);

    return true;
}

/**************************************************************************************************
 * DLLExportClass::Place -- Place a completed structure down
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 2/6/2019 11:51AM - ST
 **************************************************************************************************/
bool DLLExportClass::Place(uint64 player_id, int buildable_type, int buildable_id, short cell_x, short cell_y)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return false;
    }

    static const int _map_width_shift_bits = 7;

    BuildingClass* building = Get_Pending_Placement_Object(player_id, buildable_type, buildable_id);

    if (building) {

        TechnoTypeClass const* tech = Fetch_Techno_Type((RTTIType)buildable_type, buildable_id);

        if (tech) {
            BuildingTypeClass* building_type = (BuildingTypeClass*)tech;
            // short const *occupy_list = building_type->Get_Occupy_List(true);

            PlacementType[CurrentLocalPlayerIndex] = building_type;

            /*
            ** The cell coordinates passed in will be relative to the playable area that the client knows about
            */

            int map_cell_x = Map.MapCellX;
            int map_cell_y = Map.MapCellY;
            int map_cell_width = Map.MapCellWidth;
            int map_cell_height = Map.MapCellHeight;

            if (map_cell_x > 0) {
                map_cell_x--;
                map_cell_width++;
            }

            if (map_cell_y > 0) {
                map_cell_y--;
                map_cell_height++;
            }

            CELL cell = (CELL)(map_cell_x + cell_x) + ((map_cell_y + cell_y) << _map_width_shift_bits);

            /*
            ** Call the place directly instead of queueing it, so we can evaluate the return code.
            **
            **	OpenCNC3D lockstep: queue the PLACE event instead, the way the 1996 mouse handler
            **	always did, and clear the placement cursor at once. The return code was only ever
            **	used to clear PlacementType, which is presentation state. If the placement turns
            **	out illegal when the order executes, the building stays completed in its factory
            **	and the player places it again, which is the 1996 behaviour.
            */
            if (OpenCNC3D_Lockstep) {
                OutList.Add(EventClass(EventClass::PLACE, building->What_Am_I(), (CELL)(cell + Map.ZoneOffset)));
                PlacementType[CurrentLocalPlayerIndex] = NULL;
            } else if (PlayerPtr->Place_Object(building->What_Am_I(), cell + Map.ZoneOffset)) {
                PlacementType[CurrentLocalPlayerIndex] = NULL;
            }
        }
    }
    return true;
}

BuildingClass* DLLExportClass::Get_Pending_Placement_Object(uint64 player_id, int buildable_type, int buildable_id)
{
    /*
    **
    ** Based on SidebarClass::StripClass::SelectClass::Action
    **
    **
    */
    if (GAME_TO_PLAY == GAME_NORMAL) {

        for (int c = 0; c < 2; c++) {

            /*
            ** Each production slot in the column
            */
            for (int b = 0; b < Map.Column[c].BuildableCount; b++) {
                if (Map.Column[c].Buildables[b].BuildableID == buildable_id) {
                    if (Map.Column[c].Buildables[b].BuildableType == buildable_type) {

                        int genfactory = -1;
                        switch (buildable_type) {
                        case RTTI_INFANTRYTYPE:
                            genfactory = PlayerPtr->InfantryFactory;
                            break;

                        case RTTI_UNITTYPE:
                            genfactory = PlayerPtr->UnitFactory;
                            break;

                        case RTTI_AIRCRAFTTYPE:
                            genfactory = PlayerPtr->AircraftFactory;
                            break;

                        case RTTI_BUILDINGTYPE:
                            genfactory = PlayerPtr->BuildingFactory;
                            break;

                        default:
                            genfactory = -1;
                            break;
                        }

                        int fnumber = Map.Column[c].Buildables[b].Factory;
                        int spc = 0;
                        ObjectTypeClass const* choice = NULL;

                        if (buildable_type != RTTI_SPECIAL) {
                            choice = Fetch_Techno_Type((RTTIType)buildable_type, buildable_id);
                        } else {
                            spc = buildable_id;
                        }

                        FactoryClass* factory = NULL;
                        if (fnumber != -1) {
                            factory = Factories.Raw_Ptr(fnumber);
                        }

                        if (spc == 0 && choice) {
                            if (fnumber == -1 && genfactory != -1) {
                                return (NULL);
                            }

                            if (factory) {

                                /*
                                **	If production has completed, then attempt to have the object exit
                                **	the factory or go into placement mode.
                                */
                                if (factory->Has_Completed()) {

                                    TechnoClass* pending = factory->Get_Object();
                                    if (!pending && factory->Get_Special_Item()) {
                                        // Map.IsTargettingMode = true;
                                    } else {
                                        BuildingClass* builder = pending->Who_Can_Build_Me(false, false);
                                        if (!builder) {
                                            OutList.Add(EventClass(EventClass::ABANDON, buildable_type, buildable_id));
                                            On_Speech(PlayerPtr, VOX_NO_FACTORY); // Speak(VOX_NO_FACTORY);
                                        } else {

                                            /*
                                            **	If the completed object is a building, then change the
                                            **	game state into building placement mode. This fact is
                                            **	not transmitted to any linked computers until the moment
                                            **	the building is actually placed down.
                                            */
                                            if (pending->What_Am_I() == RTTI_BUILDING) {
                                                return (BuildingClass*)pending;
                                                // PlayerPtr->Manual_Place(builder, (BuildingClass *)pending);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

    } else {

        if (GAME_TO_PLAY == GAME_GLYPHX_MULTIPLAYER) {

            SidebarGlyphxClass* context_sidebar = DLLExportClass::Get_Current_Context_Sidebar();

            for (int c = 0; c < 2; c++) {

                /*
                ** Each production slot in the column
                */
                for (int b = 0; b < context_sidebar->Column[c].BuildableCount; b++) {
                    if (context_sidebar->Column[c].Buildables[b].BuildableID == buildable_id) {
                        if (context_sidebar->Column[c].Buildables[b].BuildableType == buildable_type) {

                            int genfactory = -1;
                            switch (buildable_type) {
                            case RTTI_INFANTRYTYPE:
                                genfactory = PlayerPtr->InfantryFactory;
                                break;

                            case RTTI_UNITTYPE:
                                genfactory = PlayerPtr->UnitFactory;
                                break;

                            case RTTI_AIRCRAFTTYPE:
                                genfactory = PlayerPtr->AircraftFactory;
                                break;

                            case RTTI_BUILDINGTYPE:
                                genfactory = PlayerPtr->BuildingFactory;
                                break;

                            default:
                                genfactory = -1;
                                break;
                            }

                            int fnumber = context_sidebar->Column[c].Buildables[b].Factory;
                            int spc = 0;
                            ObjectTypeClass const* choice = NULL;

                            if (buildable_type != RTTI_SPECIAL) {
                                choice = Fetch_Techno_Type((RTTIType)buildable_type, buildable_id);
                            } else {
                                spc = buildable_id;
                            }

                            FactoryClass* factory = NULL;
                            if (fnumber != -1) {
                                factory = Factories.Raw_Ptr(fnumber);
                            }

                            if (spc == 0 && choice) {
                                if (fnumber == -1 && genfactory != -1) {
                                    return (NULL);
                                }

                                if (factory) {

                                    /*
                                    **	If production has completed, then attempt to have the object exit
                                    **	the factory or go into placement mode.
                                    */
                                    if (factory->Has_Completed()) {

                                        TechnoClass* pending = factory->Get_Object();
                                        if (!pending && factory->Get_Special_Item()) {
                                            // Map.IsTargettingMode = true;
                                        } else {
                                            BuildingClass* builder = pending->Who_Can_Build_Me(false, false);
                                            if (!builder) {
                                                OutList.Add(
                                                    EventClass(EventClass::ABANDON, buildable_type, buildable_id));
                                                On_Speech(PlayerPtr, VOX_NO_FACTORY); // Speak(VOX_NO_FACTORY);
                                            } else {

                                                /*
                                                **	If the completed object is a building, then change the
                                                **	game state into building placement mode. This fact is
                                                **	not transmitted to any linked computers until the moment
                                                **	the building is actually placed down.
                                                */
                                                if (pending->What_Am_I() == RTTI_BUILDING) {
                                                    return (BuildingClass*)pending;
                                                    // PlayerPtr->Manual_Place(builder, (BuildingClass *)pending);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return NULL;
}

/**************************************************************************************************
 * DLLExportClass::Place_Super_Weapon
 *
 * History:
 **************************************************************************************************/
bool DLLExportClass::Place_Super_Weapon(uint64 player_id, int buildable_type, int buildable_id, int x, int y)
{
    if (buildable_type != RTTI_SPECIAL) {
        return false;
    }

    COORDINATE coord = Map.Pixel_To_Coord(x, y);
    CELL cell = Coord_Cell(coord);

    SpecialWeaponType weapon_type = (SpecialWeaponType)buildable_id;

    OutList.Add(EventClass(EventClass::SPECIAL_PLACE, weapon_type, cell));

    return true;
}

/**************************************************************************************************
 * DLLExportClass::Create_Control_Group
 *
 * History:
 **************************************************************************************************/
bool DLLExportClass::Create_Control_Group(unsigned char control_group_index)
{
    Handle_Team(control_group_index, 2);

    return true;
}

/**************************************************************************************************
 * DLLExportClass::Add_To_Control_Group
 *
 * History:
 **************************************************************************************************/
bool DLLExportClass::Add_To_Control_Group(unsigned char control_group_index)
{
    Handle_Team(control_group_index, 1);

    return true;
}

/**************************************************************************************************
 * DLLExportClass::Toggle_Control_Group_Selection
 *
 * History:
 **************************************************************************************************/
bool DLLExportClass::Toggle_Control_Group_Selection(unsigned char control_group_index)
{
    Handle_Team(control_group_index, 0);

    return true;
}

/**************************************************************************************************
 * DLLExportClass::Get_Shroud_State -- Get a snapshot of the shroud for the given player
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 4/12/2019 3:44PM - ST
 **************************************************************************************************/
bool DLLExportClass::Get_Shroud_State(uint64 player_id, unsigned char* buffer_in, unsigned int buffer_size)
{
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return false;
    }

    /*
    ** Apply mobile gap generators
    */
    static unsigned int _shroud_bits[UNIT_MAX];

    if (GAME_TO_PLAY == GAME_GLYPHX_MULTIPLAYER) {
        for (int index = 0; index < Units.Count(); index++) {
            UnitClass* obj = Units.Ptr(index);
            if (obj->Class->IsGapper && obj->IsActive && obj->Strength) {
                if (!obj->House->Is_Ally(PlayerPtr)) {
                    _shroud_bits[index] = obj->Apply_Temporary_Jamming_Shroud(PlayerPtr);
                }
            }
        }
    }

    CNCShroudStruct* shroud = (CNCShroudStruct*)buffer_in;

    unsigned int memory_needed =
        sizeof(*shroud) + 256; // Base amount needed. Will need more depending on how many entries there are

    int entry_index = 0;

    /*
    **
    **  Based loosely on DisplayClass::Redraw_Icons
    **
    **
    */
    int map_cell_x = Map.MapCellX;
    int map_cell_y = Map.MapCellY;
    int map_cell_width = Map.MapCellWidth;
    int map_cell_height = Map.MapCellHeight;

    if (map_cell_x > 0) {
        map_cell_x--;
        map_cell_width++;
    }

    if (map_cell_width < MAP_MAX_CELL_WIDTH) {
        map_cell_width++;
    }

    if (map_cell_y > 0) {
        map_cell_y--;
        map_cell_height++;
    }

    if (map_cell_height < MAP_MAX_CELL_HEIGHT) {
        map_cell_height++;
    }

    for (int y = 0; y < map_cell_height; y++) {
        for (int x = 0; x < map_cell_width; x++) {
            CELL cell = XY_Cell(map_cell_x + x, map_cell_y + y);
            COORDINATE coord = Cell_Coord(cell) & 0xFF00FF00;

            memory_needed += sizeof(CNCShroudEntryStruct);
            if (memory_needed >= buffer_size) {
                return false;
            }

            int xpixel;
            int ypixel;

            Map.Coord_To_Pixel(coord, xpixel, ypixel);

            CellClass* cellptr = &Map[Coord_Cell(coord)];

            CNCShroudEntryStruct& shroud_entry = shroud->Entries[entry_index];

            shroud_entry.IsVisible = cellptr->Is_Visible(PlayerPtr);
            shroud_entry.IsMapped = cellptr->Is_Mapped(PlayerPtr);
            shroud_entry.IsJamming = cellptr->Is_Jamming(PlayerPtr);
            // shroud_entry.IsVisible = cellptr->IsVisible;
            // shroud_entry.IsMapped = cellptr->IsMapped;
            shroud_entry.ShadowIndex = -1;

            if (shroud_entry.IsMapped) {
                if (!shroud_entry.IsVisible) {
                    shroud_entry.ShadowIndex = (char)Map.Cell_Shadow(cell, PlayerPtr);
                }
            }

            entry_index++;
        }
    }

    shroud->Count = entry_index;

    if (GAME_TO_PLAY == GAME_GLYPHX_MULTIPLAYER) {
        for (int index = 0; index < Units.Count(); index++) {
            UnitClass* obj = Units.Ptr(index);
            if (obj->Class->IsGapper && obj->IsActive && obj->Strength) {
                if (!obj->House->Is_Ally(PlayerPtr)) {
                    obj->Unapply_Temporary_Jamming_Shroud(PlayerPtr, _shroud_bits[index]);
                }
            }
        }
    }

    return true;
}

/**************************************************************************************************
 * DLLExportClass::Get_Occupier_State -- Get the occupier state for this player
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 10/25/2019 - SKY
 **************************************************************************************************/
bool DLLExportClass::Get_Occupier_State(uint64 player_id, unsigned char* buffer_in, unsigned int buffer_size)
{
    UNREFERENCED_PARAMETER(player_id);

    CNCOccupierHeaderStruct* occupiers = (CNCOccupierHeaderStruct*)buffer_in;
    CNCOccupierEntryHeaderStruct* entry = reinterpret_cast<CNCOccupierEntryHeaderStruct*>(occupiers + 1U);
    occupiers->Count = 0;

    unsigned int memory_needed = sizeof(CNCOccupierHeaderStruct);

    int map_cell_x = Map.MapCellX;
    int map_cell_y = Map.MapCellY;
    int map_cell_width = Map.MapCellWidth;
    int map_cell_height = Map.MapCellHeight;

    if (map_cell_x > 0) {
        map_cell_x--;
        map_cell_width++;
    }

    if (map_cell_width < MAP_MAX_CELL_WIDTH) {
        map_cell_width++;
    }

    if (map_cell_y > 0) {
        map_cell_y--;
        map_cell_height++;
    }

    if (map_cell_height < MAP_MAX_CELL_HEIGHT) {
        map_cell_height++;
    }

    for (int y = 0; y < map_cell_height; y++) {
        for (int x = 0; x < map_cell_width; x++, occupiers->Count++) {
            CELL cell = XY_Cell(map_cell_x + x, map_cell_y + y);
            CellClass* cellptr = &Map[cell];

            int occupier_count = 0;
            ObjectClass* optr = cellptr->Cell_Occupier();
            while (optr != NULL) {
                occupier_count++;
                optr = optr->Next;
            }

            memory_needed += sizeof(CNCOccupierEntryHeaderStruct) + (sizeof(CNCOccupierObjectStruct) * occupier_count);
            if (memory_needed >= buffer_size) {
                return false;
            }

            CNCOccupierObjectStruct* occupier = reinterpret_cast<CNCOccupierObjectStruct*>(entry + 1U);
            entry->Count = 0;

            optr = cellptr->Cell_Occupier();
            for (int i = 0; i < occupier_count; i++, occupier++, entry->Count++) {
                CNCObjectStruct object;
                Convert_Type(optr, object);
                occupier->Type = object.Type;
                occupier->ID = object.ID;
                optr = optr->Next;
            }

            entry = reinterpret_cast<CNCOccupierEntryHeaderStruct*>(occupier + 1U);
        }
    }

    return true;
}

/**************************************************************************************************
 * DLLExportClass::Get_Player_Info_State -- Get the multiplayer info for this player
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 4/22/2019 10:33AM - ST
 **************************************************************************************************/
bool DLLExportClass::Get_Player_Info_State(uint64 player_id, unsigned char* buffer_in, unsigned int buffer_size)
{
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return false;
    }

    CNCPlayerInfoStruct* player_info = (CNCPlayerInfoStruct*)buffer_in;

    unsigned int memory_needed = sizeof(*player_info) + 32; // A little extra for no reason

    if (memory_needed >= buffer_size) {
        return false;
    }

    player_info->GlyphxPlayerID = 0;

    if (PlayerPtr == NULL) {
        return false;
        ;
    }

    if (Session.Players.Count() > CurrentLocalPlayerIndex) {
        strncpy(&player_info->Name[0], Session.Players[CurrentLocalPlayerIndex]->Name, MPLAYER_NAME_MAX);
    }

    player_info->Name[MPLAYER_NAME_MAX - 1] = 0; // Make sure it's terminated
    player_info->House = PlayerPtr->Class->House;
    player_info->AllyFlags = PlayerPtr->Get_Ally_Flags();

    if (Session.Players.Count() > CurrentLocalPlayerIndex) {
        player_info->ColorIndex = Session.Players[CurrentLocalPlayerIndex]->Player.Color;
    } else {
        player_info->ColorIndex =
            (PlayerPtr->Class->House == HOUSE_USSR) ? 2 : 1; // Fudge to a sensible color in campaign; 2 = red, 1 = blue
    }

    player_info->GlyphxPlayerID = player_id;
    player_info->HomeCellX = Cell_X(MultiplayerStartPositions[CurrentLocalPlayerIndex]);
    player_info->HomeCellY = Cell_Y(MultiplayerStartPositions[CurrentLocalPlayerIndex]);
    player_info->IsDefeated = PlayerPtr->IsDefeated;

    // Can see other players' power if ally (except for the player themself) or spying on a power plant
    // Can see other players' money if spying on a resource building
    player_info->SpiedPowerFlags = 0U;
    player_info->SpiedMoneyFlags = 0U;
    for (int i = 0; i < Houses.Count(); ++i) {
        HouseClass* house = Houses.Ptr(i);
        if ((house != nullptr) && house->IsActive && (house != PlayerPtr) && house->Is_Ally(PlayerPtr)) {
            player_info->SpiedPowerFlags |= 1U << house->Class->House;
        }
    }
    for (int i = 0; i < Buildings.Count(); ++i) {
        BuildingClass* building = Buildings.Ptr(i);
        if ((building != nullptr) && building->IsActive && (building->Spied_By() & (1U << PlayerPtr->Class->House))) {
            if ((*building == STRUCT_POWER) || (*building == STRUCT_ADVANCED_POWER)) {
                player_info->SpiedPowerFlags |= 1U << building->House->Class->House;
            } else if ((*building == STRUCT_REFINERY) || (*building == STRUCT_STORAGE)) {
                player_info->SpiedMoneyFlags |= 1U << building->House->Class->House;
            }
        }
    }

    // Populate spied data
    for (char house = 0; house < MAX_HOUSES; ++house) {
        HouseClass* hptr = HouseClass::As_Pointer((HousesType)house);
        if ((hptr != nullptr) && hptr->IsActive) {
            if (player_info->SpiedPowerFlags & (1U << house)) {
                player_info->SpiedInfo[house].Power = hptr->Power;
                player_info->SpiedInfo[house].Drain = hptr->Drain;
            }
            if (player_info->SpiedMoneyFlags & (1U << house)) {
                player_info->SpiedInfo[house].Money = hptr->Available_Money();
            }
        }
    }

    // Populate selection info
    if (CurrentObject.Count() > 0) {
        CNCObjectStruct object;
        Convert_Type(CurrentObject[0], object);
        player_info->SelectedID = object.ID;
        player_info->SelectedType = object.Type;

        const int left = Map.MapCellX;
        const int right = Map.MapCellX + Map.MapCellWidth - 1;
        const int top = Map.MapCellY;
        const int bottom = Map.MapCellY + Map.MapCellHeight - 1;

        // Use first object with a weapon, or first object if none
        ObjectClass* action_object = nullptr;
        for (int i = 0; i < CurrentObject.Count(); ++i) {
            ObjectClass* object = CurrentObject[i];
            if (object->Is_Techno()) {
                TechnoClass* techno = (TechnoClass*)object;
                if (techno->Techno_Type_Class()->PrimaryWeapon != NULL
                    || techno->Techno_Type_Class()->SecondaryWeapon != NULL) {
                    action_object = object;
                    break;
                }
            }
        }
        if (action_object == nullptr) {
            action_object = CurrentObject[0];
        }

        int index = 0;
        for (int y = top; y <= bottom; ++y) {
            for (int x = left; x <= right; ++x, ++index) {
                Convert_Action_Type(action_object->What_Action(XY_Cell(x, y)),
                                    (CurrentObject.Count() == 1) ? action_object : NULL,
                                    As_Target(XY_Cell(x, y)),
                                    player_info->ActionWithSelected[index]);
            }
        }

        player_info->ActionWithSelectedCount = Map.MapCellWidth * Map.MapCellHeight;
    } else {
        player_info->SelectedID = -1;
        player_info->SelectedType = UNKNOWN;
        player_info->ActionWithSelectedCount = 0U;
    }

    // Screen shake
    player_info->ScreenShake = PlayerPtr->ScreenShakeTime;

    // Radar jammed
    player_info->IsRadarJammed = Map.Get_Jammed(PlayerPtr);

    return true;
};

/**************************************************************************************************
 * DLLExportClass::Get_Dynamic_Map_State -- Get a snapshot of the smudges and overlays on the terrain
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 2/8/2019 10:45AM - ST
 **************************************************************************************************/
bool DLLExportClass::Get_Dynamic_Map_State(uint64 player_id, unsigned char* buffer_in, unsigned int buffer_size)
{
    /*
    ** Get the player for this...
    */
    player_id;

    static int _call_count = 0;

    CNCDynamicMapStruct* dynamic_map = (CNCDynamicMapStruct*)buffer_in;

    int entry_index = 0;

    /*
    **
    **  Based loosely on DisplayClass::Redraw_Icons
    **
    **
    */
    int map_cell_x = Map.MapCellX;
    int map_cell_y = Map.MapCellY;
    int map_cell_width = Map.MapCellWidth;
    int map_cell_height = Map.MapCellHeight;

    if (map_cell_x > 0) {
        map_cell_x--;
        map_cell_width++;
    }

    if (map_cell_width < MAP_MAX_CELL_WIDTH) {
        map_cell_width++;
    }

    if (map_cell_y > 0) {
        map_cell_y--;
        map_cell_height++;
    }

    if (map_cell_height < MAP_MAX_CELL_WIDTH) {
        map_cell_height++;
    }

    int cell_index = 0;

    bool debug_output = false;
    // if (_call_count == 20) {
    // debug_output = true;
    //}

    // Need to ignore view constraints for dynamic map updates, so the radar map
    // has the latest tiberium state for cells outside the tactical view
    DLLExportClass::Adjust_Internal_View(true);

    for (int y = 0; y < map_cell_height; y++) {
        for (int x = 0; x < map_cell_width; x++) {
            CELL cell = XY_Cell(map_cell_x + x, map_cell_y + y);
            COORDINATE coord = Cell_Coord(cell) & 0xFF00FF00;

            /* Reserve room for the most this cell can emit: smudge, overlay and
               flag. Charging two entries for every terrain cell made a large
               mission's entire overlay export fail even when it held little ore. */
            if (sizeof(*dynamic_map) +
                (size_t)(entry_index + 3) * sizeof(CNCDynamicMapEntryStruct) > buffer_size) {
                return false;
            }

            /*
            **	Only cells flagged to be redraw are examined.
            */
            // if (In_View(cell) && Is_Cell_Flagged(cell)) {
            int xpixel;
            int ypixel;

            if (Map.Coord_To_Pixel(coord, xpixel, ypixel)) {
                CellClass* cellptr = &Map[Coord_Cell(coord)];

                /*
                **	If there is a portion of the underlying icon that could be visible,
                **	then draw it.  Also draw the cell if the shroud is off.
                */
                /* Export the world state independent of exploration. The caller
                   clips each cell with the player's current shroud; this also
                   makes the presentation-only reveal show ore and gems. */
                Cell_Class_Draw_It(dynamic_map, entry_index, cellptr, xpixel, ypixel, debug_output);

                /*
                **	If any cell is not fully mapped, then flag it so that the shadow drawing
                **	process will occur.  Only draw the shadow if Debug_Unshroud is false.
                */
                // if (!cellptr->IsMapped && !Debug_Unshroud) {
                //	IsShadowPresent = true;
                //}
            }
            //}
        }
    }

    if (entry_index) {
        _call_count++;
    }

    dynamic_map->Count = entry_index;

    dynamic_map->VortexActive = ChronalVortex.Is_Active();
    dynamic_map->VortexX = Coord_X(ChronalVortex.Get_Position());
    dynamic_map->VortexY = Coord_Y(ChronalVortex.Get_Position());
    dynamic_map->VortexWidth = Pixel_To_Lepton(64);
    dynamic_map->VortexHeight = Pixel_To_Lepton(64);

    return true;
}

/**************************************************************************************************
 * DLLExportClass::Cell_Class_Draw_It -- Go through the motions of drawing a cell to get the smudge and overlay info
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 2/8/2019 11:09AM - ST
 **************************************************************************************************/
void DLLExportClass::Cell_Class_Draw_It(CNCDynamicMapStruct* dynamic_map,
                                        int& entry_index,
                                        CellClass* cell_ptr,
                                        int xpixel,
                                        int ypixel,
                                        bool debug_output)
{
    /*
    **
    **  Based on CellClass::Draw_It and SmudgeTypeClass::Draw_It
    **
    **
    */

    CELL cell = cell_ptr->Cell_Number();

    /*
    **	Redraw any smudge.
    */
    if (cell_ptr->Smudge != SMUDGE_NONE) {
        // SmudgeTypeClass::As_Reference(Smudge).Draw_It(x, y, SmudgeData);

        const SmudgeTypeClass& smudge_type = SmudgeTypeClass::As_Reference(cell_ptr->Smudge);

        if (smudge_type.Get_Image_Data() != NULL) {

            if (debug_output) {
                IsTheaterShape = true;
                Debug_Write_Shape_Type(&smudge_type, 0);
                IsTheaterShape = false;
            }

            CNCDynamicMapEntryStruct& smudge_entry = dynamic_map->Entries[entry_index++];

            strncpy(smudge_entry.AssetName, smudge_type.IniName, CNC_OBJECT_ASSET_NAME_LENGTH);
            smudge_entry.Type = (short)cell_ptr->Smudge;
            smudge_entry.Owner = (char)cell_ptr->Owner;
            smudge_entry.DrawFlags = SHAPE_WIN_REL; // Looks like smudges are drawn top left
            smudge_entry.PositionX = xpixel;
            smudge_entry.PositionY = ypixel;
            smudge_entry.Width = Get_Build_Frame_Width(smudge_type.Get_Image_Data());
            smudge_entry.Height = Get_Build_Frame_Height(smudge_type.Get_Image_Data());
            smudge_entry.CellX = Cell_X(cell);
            smudge_entry.CellY = Cell_Y(cell);
            smudge_entry.ShapeIndex = cell_ptr->SmudgeData;
            smudge_entry.IsSmudge = true;
            smudge_entry.IsOverlay = false;
            smudge_entry.IsResource = false;
            smudge_entry.IsSellable = false;
            smudge_entry.IsTheaterShape = true; // Smudges are always theater-specific
            smudge_entry.IsFlag = false;
        }
    }

    /*
    **	Draw the overlay object.
    */
    if (cell_ptr->Overlay != OVERLAY_NONE) {
        // OverlayTypeClass const & otype = OverlayTypeClass::As_Reference(Overlay);
        // IsTheaterShape = (bool)otype.IsTheater;
        // CC_Draw_Shape(otype.Get_Image_Data(), OverlayData, (x+(CELL_PIXEL_W>>1)), (y+(CELL_PIXEL_H>>1)),
        // WINDOW_TACTICAL, SHAPE_CENTER|SHAPE_WIN_REL|SHAPE_GHOST, NULL, Map.UnitShadow); IsTheaterShape = false;

        const OverlayTypeClass& overlay_type = OverlayTypeClass::As_Reference(cell_ptr->Overlay);

        if (overlay_type.Get_Image_Data() != NULL) {

            CNCDynamicMapEntryStruct& overlay_entry = dynamic_map->Entries[entry_index++];

            if (debug_output) {
                IsTheaterShape = (bool)overlay_type.IsTheater;
                Debug_Write_Shape_Type(&overlay_type, 0);
                IsTheaterShape = false;
            }

            strncpy(overlay_entry.AssetName, overlay_type.IniName, CNC_OBJECT_ASSET_NAME_LENGTH);
            overlay_entry.Type = (short)cell_ptr->Overlay;
            overlay_entry.Owner = (char)cell_ptr->Owner;
            overlay_entry.DrawFlags =
                SHAPE_CENTER | SHAPE_WIN_REL | SHAPE_GHOST; // Looks like overlays are drawn centered and translucent
            overlay_entry.PositionX = xpixel + (CELL_PIXEL_W >> 1);
            overlay_entry.PositionY = ypixel + (CELL_PIXEL_H >> 1);
            overlay_entry.Width = Get_Build_Frame_Width(overlay_type.Get_Image_Data());
            overlay_entry.Height = Get_Build_Frame_Height(overlay_type.Get_Image_Data());
            overlay_entry.CellX = Cell_X(cell);
            overlay_entry.CellY = Cell_Y(cell);
            overlay_entry.ShapeIndex = cell_ptr->OverlayData;
            overlay_entry.IsSmudge = false;
            overlay_entry.IsOverlay = true;
            overlay_entry.IsResource = overlay_entry.Type >= OVERLAY_GOLD1 && overlay_entry.Type <= OVERLAY_GEMS4;
            overlay_entry.IsSellable =
                (overlay_entry.Type >= OVERLAY_SANDBAG_WALL && overlay_entry.Type <= OVERLAY_WOOD_WALL)
                || overlay_entry.Type == OVERLAY_FENCE;
            overlay_entry.IsTheaterShape = (bool)overlay_type.IsTheater;
            overlay_entry.IsFlag = false;
        }
    }

    if (cell_ptr->IsFlagged) {

        const void* image_data = MFCD::Retrieve("FLAGFLY.SHP");
        if (image_data != NULL) {

            CNCDynamicMapEntryStruct& flag_entry = dynamic_map->Entries[entry_index++];

            strncpy(flag_entry.AssetName, "FLAGFLY", CNC_OBJECT_ASSET_NAME_LENGTH);
            flag_entry.AssetName[CNC_OBJECT_ASSET_NAME_LENGTH - 1] = 0;
            flag_entry.Type = -1;
            flag_entry.Owner = cell_ptr->Owner;
            flag_entry.DrawFlags = SHAPE_CENTER | SHAPE_GHOST | SHAPE_FADING;
            flag_entry.PositionX = xpixel + (ICON_PIXEL_W / 2);
            flag_entry.PositionY = ypixel + (ICON_PIXEL_H / 2);
            flag_entry.Width = Get_Build_Frame_Width(image_data);
            flag_entry.Height = Get_Build_Frame_Height(image_data);
            flag_entry.CellX = Cell_X(cell);
            flag_entry.CellY = Cell_Y(cell);
            flag_entry.ShapeIndex = Frame % 14;
            flag_entry.IsSmudge = false;
            flag_entry.IsOverlay = false;
            flag_entry.IsResource = false;
            flag_entry.IsSellable = false;
            flag_entry.IsTheaterShape = false;
            flag_entry.IsFlag = true;
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::Glyphx_Queue_AI -- Special queue processing for Glyphx multiplayer mode
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 3/12/2019 10:52AM - ST
 **************************************************************************************************/
void DLLExportClass::Glyphx_Queue_AI(void)
{

    //------------------------------------------------------------------------
    //	Move events from the OutList (events generated by this player) into the
    //	DoList (the list of events to execute).
    //------------------------------------------------------------------------
    /*
    **	OpenCNC3D lockstep: in a match this loop MUST NOT RUN. An order that goes straight from
    **	OutList to DoList here executes on this machine and nowhere else, which is the whole
    **	desync in one statement. Under lockstep the host takes the orders out with
    **	OpenCNC3D_Drain_Events, sends them to every peer INCLUDING ITSELF, and puts them back
    **	with OpenCNC3D_Post_Event stamped for an agreed future frame. Outside a match this is
    **	exactly the code that was always here.
    */
    if (!OpenCNC3D_Lockstep) {
        while (OutList.Count) {
            OutList.First().IsExecuted = false;
            if (!DoList.Add(OutList.First())) {
                ;
            }
            OutList.Next();
        }
    }

    /*
    **	OpenCNC3D lockstep: execute in a CANONICAL order rather than in arrival order. Peers
    **	receive the same orders over the network in whatever sequence it hands them over, and
    **	two orders that touch the same object are order dependent. The key is (Frame, ID): the
    **	frame the order was stamped for, then the originating house index, which every peer
    **	computes from the same bytes. O(n squared) in the ready set, which is a handful.
    */
    if (OpenCNC3D_Lockstep) {
        for (;;) {
            int best = -1;
            for (int j = 0; j < DoList.Count; j++) {
                if (DoList[j].IsExecuted || (unsigned)Frame < DoList[j].Frame) {
                    continue;
                }
                if (best < 0 || DoList[j].Frame < DoList[best].Frame
                    || (DoList[j].Frame == DoList[best].Frame && DoList[j].ID < DoList[best].ID)) {
                    best = j;
                }
            }
            if (best < 0) {
                break;
            }
            DoList[best].Execute();
            DoList[best].IsExecuted = 1;
        }
    }

    /*
    ** Based on Execute_DoList in queue.cpp
    **
    ** The events have the ID of the player encoded in them, so no special per-player processing should be needed.
    ** When the event is created, the 'local player' is assumed to be the originator of the event, so PlayerPtr will
    *need
    ** to be swapped out to represent the real originating player prior to any events being created as a result of
    *GlyphX input
    **
    ** ST - 3/12/2019 10:51AM
    */

    for (int i = 0; OpenCNC3D_Lockstep == false && i < MULTIPLAYER_COUNT; i++) {

        HousesType house;
        HouseClass* housep;

        house = Session.Players[i]->Player.ID;

        housep = HouseClass::As_Pointer(house);

        //.....................................................................
        // If for some reason this house doesn't exist, skip it.
        // Also, if this house has exited the game, skip it.  (The user can
        // generate events after he exits, because the exit event is scheduled
        // at least FrameSendRate*3 frames ahead.  If one system gets these
        // packets & another system doesn't, they'll go out of sync because
        // they aren't checking the CommandCount for that house, since that
        // house isn't connected any more.)
        //.....................................................................
        if (!housep) {
            continue;
        }

        if (!housep->IsHuman) {
            continue;
        }

        //.....................................................................
        //	Loop through all events
        //.....................................................................
        for (int j = 0; j < DoList.Count; j++) {

            if (!DoList[j].IsExecuted && (unsigned)Frame >= DoList[j].Frame) {
                DoList[j].Execute();

                //...............................................................
                //	Mark this event as executed.
                //...............................................................
                DoList[j].IsExecuted = 1;
            }
        }
    }

    //------------------------------------------------------------------------
    //	Clean out the DoList
    //------------------------------------------------------------------------
    while (DoList.Count) {

        //.....................................................................
        //	Discard events that have been executed, OR it's too late to execute.
        //	(This happens if another player exits the game; he'll leave FRAMEINFO
        //	events lying around in my queue.  They won't have been "executed",
        //	because his IPX connection was destroyed.)
        //.....................................................................
        if ((DoList.First().IsExecuted) || ((unsigned)Frame > DoList.First().Frame)) {
            DoList.Next();
        } else {
            break;
        }
    }
}

CarryoverClass Test_CC(CarryoverObjectStruct* cptr)
{
    CarryoverClass cc;

    cc.RTTI = (RTTIType)cptr->RTTI;
    cc.Type.Building =
        (StructType)cptr
            ->Type; // This works regardless of what the RTTI-type and the enum type, because they're all enums. - LLL
    cc.Cell = (CELL)cptr->Cell;
    cc.House = (HousesType)cptr->House;
    cc.Strength = cptr->Strength;

    return cc;
}

/**************************************************************************************************
 * DLLExportClass::Reset_Sidebars -- Init the multiplayer sidebars
 *
 *
 *
 * History: 11/8/2019 - LLL
 **************************************************************************************************/
void DLLExportClass::Store_Carryover_Objects()
{
    if (EventCallback == NULL || Scen.IsToCarryOver == false) {
        return;
    }

    CarryoverObjectStruct* carryover_list = 0;
    CarryoverObjectStruct* carryover_list_tail = 0;

    /*
    **	Record all objects, that are to be part of the carry over set, into the carry over list.
    */
    for (int building_index = 0; building_index < Buildings.Count(); building_index++) {
        BuildingClass* building = Buildings.Ptr(building_index);

        if (building && !building->IsInLimbo && building->Strength > 0) {
            CarryoverClass carryover = CarryoverClass(building);
            CarryoverObjectStruct* cptr = new CarryoverObjectStruct();
            if (cptr) {
                cptr->RTTI = carryover.RTTI;
                cptr->Type = (int)carryover.Type.Building;
                cptr->House = carryover.House;
                cptr->Cell = carryover.Cell;
                cptr->Strength = carryover.Strength;

                CarryoverClass cc = Test_CC(cptr);
                cc;

                if (!carryover_list_tail) {
                    carryover_list = cptr;
                    carryover_list_tail = cptr;
                } else {
                    carryover_list_tail->Next = cptr;
                    carryover_list_tail = cptr;
                }
            }
        }
    }
    for (int unit_index = 0; unit_index < Units.Count(); unit_index++) {
        UnitClass* unit = Units.Ptr(unit_index);

        if (unit && !unit->IsInLimbo && unit->Strength > 0) {
            CarryoverClass carryover = CarryoverClass(unit);
            CarryoverObjectStruct* cptr = new CarryoverObjectStruct();
            if (cptr) {
                cptr->RTTI = carryover.RTTI;
                cptr->Type = (int)carryover.Type.Unit;
                cptr->House = carryover.House;
                cptr->Cell = carryover.Cell;
                cptr->Strength = carryover.Strength;

                CarryoverClass cc = Test_CC(cptr);
                cc;

                if (!carryover_list_tail) {
                    carryover_list = cptr;
                    carryover_list_tail = cptr;
                } else {
                    carryover_list_tail->Next = cptr;
                    carryover_list_tail = cptr;
                }
            }
        }
    }
    for (int infantry_index = 0; infantry_index < Infantry.Count(); infantry_index++) {
        InfantryClass* infantry = Infantry.Ptr(infantry_index);

        if (infantry && !infantry->IsInLimbo && infantry->Strength > 0) {
            CarryoverClass carryover = CarryoverClass(infantry);
            CarryoverObjectStruct* cptr = new CarryoverObjectStruct();
            if (cptr) {
                cptr->RTTI = carryover.RTTI;
                cptr->Type = (int)carryover.Type.Building;
                cptr->House = carryover.House;
                cptr->Cell = carryover.Cell;
                cptr->Strength = carryover.Strength;

                CarryoverClass cc = Test_CC(cptr);
                cc;

                if (!carryover_list_tail) {
                    carryover_list = cptr;
                    carryover_list_tail = cptr;
                } else {
                    carryover_list_tail->Next = cptr;
                    carryover_list_tail = cptr;
                }
            }
        }
    }
    for (int vessel_index = 0; vessel_index < Vessels.Count(); vessel_index++) {
        VesselClass* vessel = Vessels.Ptr(vessel_index);

        if (vessel && !vessel->IsInLimbo && vessel->Strength > 0) {
            CarryoverClass carryover = CarryoverClass(vessel);
            CarryoverObjectStruct* cptr = new CarryoverObjectStruct();
            if (cptr) {
                cptr->RTTI = carryover.RTTI;
                cptr->Type = (int)carryover.Type.Building;
                cptr->House = carryover.House;
                cptr->Cell = carryover.Cell;
                cptr->Strength = carryover.Strength;

                CarryoverClass cc = Test_CC(cptr);
                cc;

                if (!carryover_list_tail) {
                    carryover_list = cptr;
                    carryover_list_tail = cptr;
                } else {
                    carryover_list_tail->Next = cptr;
                    carryover_list_tail = cptr;
                }
            }
        }
    }

    // Make & Send Event
    EventCallbackStruct event;
    event.EventType = CALLBACK_EVENT_STORE_CARRYOVER_OBJECTS;
    event.CarryoverObjects.CarryoverList = carryover_list;
    EventCallback(event);

    // Delete the list
    while (carryover_list) {
        CarryoverObjectStruct* cptr = (CarryoverObjectStruct*)carryover_list->Next;
        delete carryover_list;
        carryover_list = cptr;
    }
}

/**************************************************************************************************
 * DLLExportClass::Reset_Sidebars -- Init the multiplayer sidebars
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 3/14/2019 3:10PM - ST
 **************************************************************************************************/
void DLLExportClass::Reset_Sidebars(void)
{
    for (int i = 0; i < MULTIPLAYER_COUNT; i++) {
        if (i >= Session.Players.Count()) {
            continue;
        }
        if (Session.Players[i] == NULL) {
            continue;
        }
        HouseClass* player_ptr = HouseClass::As_Pointer(Session.Players[i]->Player.ID);
        MultiplayerSidebars[i].Init_Clear(player_ptr);
    }
}

/**************************************************************************************************
 * DLLExportClass::Set_Player_Context -- Switch the C&C local player context
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 3/14/2019 3:20PM - ST
 **************************************************************************************************/
bool DLLExportClass::Set_Player_Context(uint64 glyphx_player_id, bool force)
{
    /*
    ** Context never needs to change in single player
    */
    if (GAME_TO_PLAY == GAME_NORMAL) {
        if (PlayerPtr) {
            CurrentObject.Set_Active_Context(PlayerPtr->Class->House);
        }
        return true;
    }

    /*
    ** C&C relies a lot on PlayerPtr, which is a pointer to the 'local' player's house. Historically, in a peer-to-peer
    ** multiplayer game, each player's PlayerPtr pointed to their own local player.
    **
    ** Since much of the IO logic depends on PlayerPtr being the player performing the action, we need to set PlayerPtr
    ** correctly depending on which player generated input or needs output
    */

    for (int i = 0; i < MULTIPLAYER_COUNT; i++) {
        if (GlyphxPlayerIDs[i] == glyphx_player_id) {

            if (!force && i == CurrentLocalPlayerIndex) {
                return true;
            }

            PlayerPtr = HouseClass::As_Pointer(Session.Players[i]->Player.ID);
            CurrentObject.Set_Active_Context(PlayerPtr->Class->House);
            CurrentLocalPlayerIndex = i;

            Refresh_Player_Control_Flags();

            return true;
        }
    }

    return false;
}

/**************************************************************************************************
 * DLLExportClass::Reset_Player_Context -- Clear out old player context data
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 4/16/2019 10:36AM - ST
 **************************************************************************************************/
void DLLExportClass::Reset_Player_Context(void)
{
    CurrentLocalPlayerIndex = 0;
    CurrentObject.Clear_All();
}

/**************************************************************************************************
 * DLLExportClass::Refresh_Player_Control_Flags -- Set the IsPlayerControl flags so that the player
 *																  in context has IsPlayerControl
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 4/16/2019 10:36AM - ST
 **************************************************************************************************/
void DLLExportClass::Refresh_Player_Control_Flags(void)
{
    for (int i = 0; i < MULTIPLAYER_COUNT; i++) {

        HouseClass* player_ptr = HouseClass::As_Pointer(Session.Players[i]->Player.ID);

        if (player_ptr) {

            if (i == CurrentLocalPlayerIndex && player_ptr->IsHuman) {
                player_ptr->IsPlayerControl = true;
            } else {
                player_ptr->IsPlayerControl = false;
            }
        }
    }
}

/**************************************************************************************************
 * Logic_Switch_Player_Context -- Called when the internal game locic needs to switch player context
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 4/17/2019 9:45AM - ST
 **************************************************************************************************/
void Logic_Switch_Player_Context(ObjectClass* object)
{
    DLLExportClass::Logic_Switch_Player_Context(object);
}

/**************************************************************************************************
 * DLLExportClass::Logic_Switch_Player_Context -- Called when the internal game locic needs to switch player context
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 4/17/2019 9:45AM - ST
 **************************************************************************************************/
void DLLExportClass::Logic_Switch_Player_Context(ObjectClass* object)
{
    if (object == NULL) {
        return;
    }

    /*
    ** If it's not a techno, it can't be owned.
    */
    if (!object->Is_Techno()) {
        return;
    }

    TechnoClass* tech = static_cast<TechnoClass*>(object);

    // HousesType house = tech->House->Class->House;
    DLLExportClass::Logic_Switch_Player_Context(tech->House);
}

/**************************************************************************************************
 * Logic_Switch_Player_Context -- Called when the internal game locic needs to switch player context
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 4/17/2019 9:45AM - ST
 **************************************************************************************************/
void Logic_Switch_Player_Context(HouseClass* object)
{
    DLLExportClass::Logic_Switch_Player_Context(object);
}

/**************************************************************************************************
 * DLLExportClass::Logic_Switch_Player_Context -- Called when the internal game locic needs to switch player context
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 4/17/2019 9:45AM - ST
 **************************************************************************************************/
void DLLExportClass::Logic_Switch_Player_Context(HouseClass* house)
{
    if (GAME_TO_PLAY == GAME_NORMAL) {
        CurrentObject.Set_Active_Context(PlayerPtr->Class->House);
        return;
    }

    if (house == NULL) {
        return;
    }

    /*
    ** C&C relies a lot on PlayerPtr, which is a pointer to the 'local' player's house. Historically, in a peer-to-peer
    ** multiplayer game, each player's PlayerPtr pointed to their own local player.
    **
    ** Since much of the IO logic depends on PlayerPtr being the player performing the action, we need to set PlayerPtr
    ** correctly depending on which player generated input or needs output
    */

    HousesType house_type = house->Class->House;

    for (int i = 0; i < MULTIPLAYER_COUNT; i++) {

        if (house_type == Session.Players[i]->Player.ID) {

            if (i == CurrentLocalPlayerIndex) {
                return;
            }

            PlayerPtr = HouseClass::As_Pointer(Session.Players[i]->Player.ID);
            CurrentObject.Set_Active_Context(PlayerPtr->Class->House);
            CurrentLocalPlayerIndex = i;

            Refresh_Player_Control_Flags();
            return;
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::Calculate_Start_Positions -- Calculate the initial view positions for the players
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 4/16/2019 6/12/2019 3:00PM - ST
 **************************************************************************************************/
void DLLExportClass::Calculate_Start_Positions(void)
{
    if (GAME_TO_PLAY == GAME_NORMAL) {
        MultiplayerStartPositions[0] = Scen.Views[0];
        return;
    }

    HouseClass* player_ptr = PlayerPtr;

    ScenarioInit++;
    COORDINATE old_tac = Map.TacticalCoord;
    for (int i = 0; i < MULTIPLAYER_COUNT; i++) {
        PlayerPtr = HouseClass::As_Pointer(Session.Players[i]->Player.ID);
        if (PlayerPtr) {
            int x, y;
            Map.Compute_Start_Pos(x, y);
            MultiplayerStartPositions[i] = XY_Cell(x, y);
        }
    }
    Map.TacticalCoord = old_tac;
    ScenarioInit--;

    PlayerPtr = player_ptr;
}

/**************************************************************************************************
 * DLLExportClass::Get_GlyphX_Player_ID -- Get the external GlyphX player ID from the C&C house/player pointer
 *                                         Returns 0 in single player or if player ID isn't found
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 4/22/2019 6:23PM - ST
 **************************************************************************************************/
__int64 DLLExportClass::Get_GlyphX_Player_ID(const HouseClass* house)
{
    /*
    ** C&C relies a lot on PlayerPtr, which is a pointer to the 'local' player's house. Historically, in a peer-to-peer
    ** multiplayer game, each player's PlayerPtr pointed to their own local player.
    **
    ** Since much of the IO logic depends on PlayerPtr being the player performing the action, we need to set PlayerPtr
    ** correctly depending on which player generated input or needs output
    */

    if (GAME_TO_PLAY == GAME_NORMAL) {
        return 0;
    }

    if (house == NULL) {
        return 0;
    }

    HousesType house_type = house->Class->House;

    for (int i = 0; i < MULTIPLAYER_COUNT; i++) {
        if (house_type == Session.Players[i]->Player.ID) {
            return GlyphxPlayerIDs[i];
        }
    }

    /*
    ** Failure case.
    */
    return 0;
}

/**************************************************************************************************
 * DLLExportClass::Adjust_Internal_View -- Set the internal tactical view to encompass the input coordinates
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 4/16/2019 3:00PM - ST
 **************************************************************************************************/
void DLLExportClass::Adjust_Internal_View(bool force_ignore_view_constraints)
{
    /*
    ** When legacy rendering is disabled (especially in multiplayer) we can get input coordinates that
    ** fall outside the engine's tactical view. In this case, we need to adjust the tactical view before the
    ** input will behave as expected.
    */

    if (!force_ignore_view_constraints && Legacy_Render_Enabled()) {
        /*
        ** Render view should already be tracking the player's local view
        */
        DisplayClass::IgnoreViewConstraints = false;
        return;
    }

    DisplayClass::IgnoreViewConstraints = true;
}

/**************************************************************************************************
 * DLLExportClass::Get_Current_Context_Sidebar -- Get the sidebar data for the current player context
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 3/14/2019 3:20PM - ST
 **************************************************************************************************/
SidebarGlyphxClass* DLLExportClass::Get_Current_Context_Sidebar(HouseClass* player_ptr)
{
    if (player_ptr) {

        for (int i = 0; i < MULTIPLAYER_COUNT; i++) {
            if (player_ptr == HouseClass::As_Pointer(Session.Players[i]->Player.ID)) {
                return &MultiplayerSidebars[i];
            }
        }
    }
    return &MultiplayerSidebars[CurrentLocalPlayerIndex];
}

SidebarGlyphxClass* Get_Current_Context_Sidebar(HouseClass* player_ptr)
{
    return DLLExportClass::Get_Current_Context_Sidebar(player_ptr);
}

/**************************************************************************************************
 * DLLExportClass::Repair_Mode -- Starts the player's repair mode. All it does here is unselect all units.
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 5/1/2019 - LLL
 **************************************************************************************************/
void DLLExportClass::Repair_Mode(uint64 player_id)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    Unselect_All();
}

/**************************************************************************************************
 * DLLExportClass::Repair -- Repairs a specific building
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 5/1/2019 - LLL
 **************************************************************************************************/
void DLLExportClass::Repair(uint64 player_id, int object_id)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    TARGET target = Build_Target(RTTI_BUILDING, object_id);
    if (target != TARGET_NONE) {
        BuildingClass* building = As_Building(target);
        if (building) {
            if (!building->IsActive) {
                GlyphX_Debug_Print("DLLExportClass::Repair -- trying to repair a non-active building");
            } else {

                if (building->Can_Repair() && building->House.Is_Valid()
                    && building->House->Class->House == PlayerPtr->Class->House) {
                    /*
                    **	OpenCNC3D lockstep: this click becomes an ORDER rather than an act. The
                    **	direct call changes this machine's world and no other. Under lockstep
                    **	the REPAIR event is queued instead, drained by the host, sent to every
                    **	peer and executed on all of them at the same frame. The checks above
                    **	still run here, at click time; the event arm checks ownership too.
                    */
                    if (OpenCNC3D_Lockstep) {
                        OutList.Add(EventClass(EventClass::REPAIR, TargetClass(building)));
                    } else {
                        building->Repair(-1);
                    }
                }
            }
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::Sell_Mode -- Starts the player's sell mode. All it does here is unselect all units.
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 5/1/2019 - LLL
 **************************************************************************************************/
void DLLExportClass::Sell_Mode(uint64 player_id)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    Unselect_All();
}

/**************************************************************************************************
 * DLLExportClass::Sell -- Sell's a player's speceific building.
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 5/1/2019 - LLL
 **************************************************************************************************/
void DLLExportClass::Sell(uint64 player_id, int object_id)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    TARGET target = Build_Target(RTTI_BUILDING, object_id);
    if (target != TARGET_NONE) {
        BuildingClass* building = As_Building(target);
        if (building) {
            if (!building->IsActive) {
                GlyphX_Debug_Print("DLLExportClass::Sell -- trying to sell a non-active building");
            } else {
                if (building->Can_Demolish() && building->House.Is_Valid()
                    && building->House->Class->House == PlayerPtr->Class->House) {
                    /*
                    **	OpenCNC3D lockstep: queue the SELL event instead of selling here, for the
                    **	reason given at Repair. One difference, confined to a match: the event arm
                    **	calls Sell_Back(-1), which TOGGLES, where this direct call passes 1, which
                    **	forces. So in a match a second click on a building being sold cancels the
                    **	sale, which is the 1996 gesture.
                    */
                    if (OpenCNC3D_Lockstep) {
                        OutList.Add(EventClass(EventClass::SELL, TargetClass(building)));
                    } else {
                        building->Sell_Back(1);
                    }
                }
            }
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::Repair_Sell_Cancel -- Ends the player's repair or sell mode. Doesn't do anything right now.
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 5/1/2019 - LLL
 **************************************************************************************************/
void DLLExportClass::Repair_Sell_Cancel(uint64 player_id)
{
    // OutputDebugString("Repair_Sell_Cancel\n");
}

/**************************************************************************************************
 * DLLExportClass::Scatter_Selected -- Scatter the selected units
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 10/15/2019 - SKY
 **************************************************************************************************/
void DLLExportClass::Scatter_Selected(uint64 player_id)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    if (CurrentObject.Count()) {
        for (int index = 0; index < CurrentObject.Count(); index++) {
            ObjectClass const* tech = CurrentObject[index];

            if (tech != NULL && tech->Can_Player_Move()) {
                OutList.Add(EventClass(EventClass::SCATTER, TargetClass(tech)));
            }
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::Select_Next_Unit
 *
 * History: 03.02.2020 MBL
 **************************************************************************************************/
void DLLExportClass::Select_Next_Unit(uint64 player_id)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    ObjectClass* obj = Map.Next_Object(CurrentObject.Count() ? CurrentObject[0] : NULL);
    if (obj) {
        Unselect_All();
        obj->Select();

        COORDINATE center = Map.Center_Map();
        Map.Flag_To_Redraw(true);
        if (center) {
            On_Center_Camera(PlayerPtr, Coord_X(center), Coord_Y(center));
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::Select_Previous_Unit
 *
 * History: 03.02.2020 MBL
 **************************************************************************************************/
void DLLExportClass::Select_Previous_Unit(uint64 player_id)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    ObjectClass* obj = Map.Prev_Object(CurrentObject.Count() ? CurrentObject[0] : NULL);
    if (obj) {
        Unselect_All();
        obj->Select();

        COORDINATE center = Map.Center_Map();
        Map.Flag_To_Redraw(true);
        if (center) {
            On_Center_Camera(PlayerPtr, Coord_X(center), Coord_Y(center));
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::Selected_Guard_Mode
 *
 * History: 03.03.2020 MBL
 **************************************************************************************************/
void DLLExportClass::Selected_Guard_Mode(uint64 player_id)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    if (CurrentObject.Count()) {
        for (int index = 0; index < CurrentObject.Count(); index++) {
            ObjectClass const* tech = CurrentObject[index];

            if (tech != NULL && tech->Can_Player_Fire()) {
                if (tech->Can_Player_Move()) {
                    OutList.Add(EventClass(TargetClass(tech), MISSION_GUARD_AREA));
                } else {
                    OutList.Add(EventClass(TargetClass(tech), MISSION_GUARD));
                }
            }
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::Selected_Stop
 *
 * History: 03.03.2020 MBL
 **************************************************************************************************/
void DLLExportClass::Selected_Stop(uint64 player_id)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    // Copied from RedAlert/Conquer.cpp - Keyboard_Process() with Options.KeyStop (VK_S)
    if (CurrentObject.Count()) {
        for (int index = 0; index < CurrentObject.Count(); index++) {
            ObjectClass const* tech = CurrentObject[index];

            if (tech != NULL
                && (tech->Can_Player_Move() || (tech->Can_Player_Fire() && tech->What_Am_I() != RTTI_BUILDING))) {
                OutList.Add(EventClass(EventClass::IDLE, TargetClass(tech)));
            }
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::Units_Queued_Movement_Toggle
 *
 * History: 03.03.2020 MBL
 **************************************************************************************************/
void DLLExportClass::Units_Queued_Movement_Toggle(uint64 player_id, bool toggle)
{
    // Currently Red Alert only

    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    if (PlayerPtr != NULL) {
        PlayerPtr->IsQueuedMovementToggle = toggle;
    }
}

/**************************************************************************************************
 * DLLExportClass::Team_Units_Formation_Toggle_On
 *
 * History: 03.03.2020 MBL
 **************************************************************************************************/
// extern void Toggle_Formation(void); // Code\RedAlert\Conquer.cpp
extern char FormationEvent; // Code\RedAlert\Conquer.cpp
void DLLExportClass::Team_Units_Formation_Toggle_On(uint64 player_id)
{
    /*
    ** Get the player for this...
    */
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    //
    // Code here copied and modified from Toggle_Formation(), since obj->IsSelected is not supported
    // Replacing with ObjectClass::Is_Selected_By_Player(HouseClass *player);
    //

    int team = MAX_TEAMS;
    int minx = 0x7FFFFFFFL, miny = 0x7FFFFFFF;
    int maxx = 0, maxy = 0;
    int index;
    bool setform = 0;

    TeamFormDataStruct& team_form_data = TeamFormData[PlayerPtr->Class->House];

    //
    // Recording support
    //
    if (Session.Record) {
        FormationEvent = 1;
    }

    /*
    ** Find the first selected object that is a member of a team, and
    ** register his group as the team we're using.  Once we find the team
    ** number, update the 'setform' flag to know whether we should be setting
    ** the formation's offsets, or clearing them.  If they currently have
    ** illegal offsets (as in 0x80000000), then we're setting.
    */
    for (index = 0; index < Units.Count(); index++) {
        UnitClass* obj = Units.Ptr(index);
        if (obj) {
            if (obj->House == PlayerPtr) {
                if (!obj->IsInLimbo) {
                    if (obj->Is_Selected_By_Player(PlayerPtr)) {
                        team = obj->Group;
                        if (team < MAX_TEAMS) {
                            setform = obj->XFormOffset == INVALID_FORMATION;
                            team_form_data.TeamSpeed[team] = SPEED_WHEEL;
                            team_form_data.TeamMaxSpeed[team] = MPH_LIGHT_SPEED;
                            break;
                        }
                    }
                }
            }
        }
    }
    if (team >= MAX_TEAMS) {
        for (index = 0; index < Infantry.Count(); index++) {
            InfantryClass* obj = Infantry.Ptr(index);
            if (obj) {
                if (obj->House == PlayerPtr) {
                    if (!obj->IsInLimbo) {
                        if (obj->Is_Selected_By_Player(PlayerPtr)) {
                            team = obj->Group;
                            if (team < MAX_TEAMS) {
                                setform = obj->XFormOffset == INVALID_FORMATION;
                                team_form_data.TeamSpeed[team] = SPEED_WHEEL;
                                team_form_data.TeamMaxSpeed[team] = MPH_LIGHT_SPEED;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    if (team >= MAX_TEAMS) {
        for (index = 0; index < Vessels.Count(); index++) {
            VesselClass* obj = Vessels.Ptr(index);
            if (obj) {
                if (obj->House == PlayerPtr) {
                    if (!obj->IsInLimbo) {
                        if (obj->Is_Selected_By_Player(PlayerPtr)) {
                            team = obj->Group;
                            if (team < MAX_TEAMS) {
                                setform = obj->XFormOffset == INVALID_FORMATION;
                                team_form_data.TeamSpeed[team] = SPEED_WHEEL;
                                team_form_data.TeamMaxSpeed[team] = MPH_LIGHT_SPEED;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    if (team >= MAX_TEAMS)
        return;
    /*
    ** Now that we have a team, let's go set (or clear) the formation offsets.
    */
    for (index = 0; index < Units.Count(); index++) {
        UnitClass* obj = Units.Ptr(index);
        if (obj && !obj->IsInLimbo && obj->House == PlayerPtr && obj->Group == team) {
            obj->Mark(MARK_CHANGE);
            if (setform) {
                int xc = Cell_X(Coord_Cell(obj->Center_Coord()));
                int yc = Cell_Y(Coord_Cell(obj->Center_Coord()));
                if (xc < minx)
                    minx = xc;
                if (xc > maxx)
                    maxx = xc;
                if (yc < miny)
                    miny = yc;
                if (yc > maxy)
                    maxy = yc;
                if (obj->Class->MaxSpeed < team_form_data.TeamMaxSpeed[team]) {
                    team_form_data.TeamMaxSpeed[team] = obj->Class->MaxSpeed;
                    team_form_data.TeamSpeed[team] = obj->Class->Speed;
                }
            } else {
                obj->XFormOffset = obj->YFormOffset = INVALID_FORMATION;
            }
        }
    }

    for (index = 0; index < Infantry.Count(); index++) {
        InfantryClass* obj = Infantry.Ptr(index);
        if (obj && !obj->IsInLimbo && obj->House == PlayerPtr && obj->Group == team) {
            obj->Mark(MARK_CHANGE);
            if (setform) {
                int xc = Cell_X(Coord_Cell(obj->Center_Coord()));
                int yc = Cell_Y(Coord_Cell(obj->Center_Coord()));
                if (xc < minx)
                    minx = xc;
                if (xc > maxx)
                    maxx = xc;
                if (yc < miny)
                    miny = yc;
                if (yc > maxy)
                    maxy = yc;
                if (obj->Class->MaxSpeed < team_form_data.TeamMaxSpeed[team]) {
                    team_form_data.TeamMaxSpeed[team] = obj->Class->MaxSpeed;
                }
            } else {
                obj->XFormOffset = obj->YFormOffset = INVALID_FORMATION;
            }
        }
    }

    for (index = 0; index < Vessels.Count(); index++) {
        VesselClass* obj = Vessels.Ptr(index);
        if (obj && !obj->IsInLimbo && obj->House == PlayerPtr && obj->Group == team) {
            obj->Mark(MARK_CHANGE);
            if (setform) {
                int xc = Cell_X(Coord_Cell(obj->Center_Coord()));
                int yc = Cell_Y(Coord_Cell(obj->Center_Coord()));
                if (xc < minx)
                    minx = xc;
                if (xc > maxx)
                    maxx = xc;
                if (yc < miny)
                    miny = yc;
                if (yc > maxy)
                    maxy = yc;
                if (obj->Class->MaxSpeed < team_form_data.TeamMaxSpeed[team]) {
                    team_form_data.TeamMaxSpeed[team] = obj->Class->MaxSpeed;
                }
            } else {
                obj->XFormOffset = obj->YFormOffset = INVALID_FORMATION;
            }
        }
    }

    /*
    ** All the units have been counted to find the bounding rectangle and
    ** center of the formation, or to clear their offsets.  Now, if we're to
    ** set them into formation, proceed to do so.  Otherwise, bail.
    */
    if (setform) {
        int centerx = (int)((maxx - minx) / 2) + minx;
        int centery = (int)((maxy - miny) / 2) + miny;

        for (index = 0; index < Units.Count(); index++) {
            UnitClass* obj = Units.Ptr(index);
            if (obj && !obj->IsInLimbo && obj->House == PlayerPtr && obj->Group == team) {
                int xc = Cell_X(Coord_Cell(obj->Center_Coord()));
                int yc = Cell_Y(Coord_Cell(obj->Center_Coord()));

                obj->XFormOffset = xc - centerx;
                obj->YFormOffset = yc - centery;
            }
        }

        for (index = 0; index < Infantry.Count(); index++) {
            InfantryClass* obj = Infantry.Ptr(index);
            if (obj && !obj->IsInLimbo && obj->House == PlayerPtr && obj->Group == team) {
                int xc = Cell_X(Coord_Cell(obj->Center_Coord()));
                int yc = Cell_Y(Coord_Cell(obj->Center_Coord()));

                obj->XFormOffset = xc - centerx;
                obj->YFormOffset = yc - centery;
            }
        }

        for (index = 0; index < Vessels.Count(); index++) {
            VesselClass* obj = Vessels.Ptr(index);
            if (obj && !obj->IsInLimbo && obj->House == PlayerPtr && obj->Group == team) {
                int xc = Cell_X(Coord_Cell(obj->Center_Coord()));
                int yc = Cell_Y(Coord_Cell(obj->Center_Coord()));

                obj->XFormOffset = xc - centerx;
                obj->YFormOffset = yc - centery;
            }
        }
    }
}

/**************************************************************************************************
 * CNC_Handle_Debug_Request -- Process a debug input request
 *
 * In:
 *
 *
 * Out:
 *
 *
 * History: 1/7/2019 5:20PM - ST
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Debug_Request(DebugRequestEnum debug_request_type,
                                                                       uint64 player_id,
                                                                       const char* object_name,
                                                                       int x,
                                                                       int y,
                                                                       bool unshroud,
                                                                       bool enemy)
{
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    switch (debug_request_type) {

    case DEBUG_REQUEST_SPAWN_OBJECT:
        DLLExportClass::Debug_Spawn_Unit(object_name, x, y, enemy);
        break;

    case DEBUG_REQUEST_FORCE_CRASH:
        Debug_Force_Crash = true;
        break;

    case DEBUG_REQUEST_KILL_OBJECT:
        if (strcmp(object_name, "HEAL") == 0) {
            DLLExportClass::Debug_Heal_Unit(x, y);
        } else {
            DLLExportClass::Debug_Kill_Unit(x, y);
        }
        break;

    case DEBUG_REQUEST_END_GAME: {
        bool win = true;

        const char lose[] = "LOSE";
        if (strcmp(lose, object_name) == 0) {
            win = false;
        }

        PlayerWins = win;
        PlayerLoses = !win;
    } break;

    case DEBUG_REQUEST_UNSHROUD:
        Debug_Unshroud = unshroud;
        Map.Flag_To_Redraw(true);
        break;

    case DEBUG_REQUEST_SUPERWEAPON_RECHARGE:
        for (int i = 0; i < SPC_COUNT; ++i) {
            PlayerPtr->SuperWeapon[i].Forced_Charge(true);
        }
        break;

    case DEBUG_REQUEST_END_PRODUCTION: {
        for (int index = 0; index < Factories.Count(); index++) {
            FactoryClass* factory = Factories.Ptr(index);
            if (factory->Get_House()->IsHuman) {
                Factories.Ptr(index)->Force_Complete();
            }
        }
    } break;

    case DEBUG_REQUEST_ADD_RESOURCES: {
        if (object_name) {
            int amount = atoi(object_name);
            PlayerPtr->Credits += amount;
            if (PlayerPtr->Credits < 0) {
                PlayerPtr->Credits = 0;
            }
        }
    } break;

    case DEBUG_REQUEST_UNLOCK_BUILDABLES:
        PlayerPtr->DebugUnlockBuildables = !PlayerPtr->DebugUnlockBuildables;
        PlayerPtr->IsRecalcNeeded = true;
        break;

    case DEBUG_REQUEST_SET_GLOBAL_FLAG:
        Scen.Set_Global_To(x, true);
        break;

    default:
        break;
    }
}

extern "C" __declspec(dllexport) void __cdecl CNC_Handle_Beacon_Request(BeaconRequestEnum beacon_request_type,
                                                                        uint64 player_id,
                                                                        int pixel_x,
                                                                        int pixel_y)
{
    if (!DLLExportClass::Set_Player_Context(player_id)) {
        return;
    }

    // Beacons are only available if legacy rendering is disabled
    if (DLLExportClass::Legacy_Render_Enabled()) {
        return;
    }

    // Only allow one beacon per player
    for (int index = 0; index < Anims.Count(); ++index) {
        AnimClass* anim = Anims.Ptr(index);
        if (anim != NULL && (*anim == ANIM_BEACON || *anim == ANIM_BEACON_VIRTUAL)
            && anim->OwnerHouse == PlayerPtr->Class->House) {
            delete anim;
        }
    }

    OutList.Add(EventClass(
        ANIM_BEACON, PlayerPtr->Class->House, Map.Pixel_To_Coord(pixel_x, pixel_y), PlayerPtr->Get_Allies()));

    // Send sound effect to allies
    for (int index = 0; index < Houses.Count(); ++index) {
        HouseClass* hptr = Houses.Ptr(index);
        if (hptr != NULL && hptr->IsActive && hptr->IsHuman && PlayerPtr->Is_Ally(hptr)) {
            DLLExportClass::On_Sound_Effect(hptr, VOC_BEACON, "", 0, 0);
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::Debug_Spawn_All -- Debug spawn all buildable units and structures
 *
 * In:   Object to unlimbo , x & y cell positions
 *
 *
 * Out:  True if unlimbo succeeded
 *
 *
 *
 * History: 1/22/2020 2:57PM - ST
 **************************************************************************************************/
bool DLLExportClass::Try_Debug_Spawn_Unlimbo(TechnoClass* techno, int& cell_x, int& cell_y)
{
    if (techno) {

        int map_cell_x = Map.MapCellX;
        int map_cell_y = Map.MapCellY;
        int map_cell_right = map_cell_x + Map.MapCellWidth;
        int map_cell_bottom = map_cell_y + Map.MapCellHeight;

        map_cell_right =
            min(map_cell_right,
                cell_x + 26); // Generally try to prevent the objects from spawing off the right of the screen

        int try_x = cell_x;
        int try_y = cell_y;

        while (try_y < map_cell_bottom) {

            CELL cell = XY_Cell(try_x, try_y);

            if (techno->Unlimbo(Cell_Coord(cell))) {

                try_x++;
                if (try_x > map_cell_right - 2) {
                    try_x = cell_x; // map_cell_x + 2;
                    try_y++;
                }

                cell_x = try_x;
                cell_y = try_y;
                return true;
            }

            try_x++;
            if (try_x > map_cell_right - 2) {
                try_x = cell_x; // map_cell_x + 2;
                try_y++;
            }
        }

        cell_x = try_x;
        cell_y = try_y;
    }
    return false;
}

/**************************************************************************************************
 * DLLExportClass::Debug_Spawn_All -- Debug spawn all buildable units and structures
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 1/22/2020 2:57PM - ST
 **************************************************************************************************/
void DLLExportClass::Debug_Spawn_All(int x, int y)
{
    int map_cell_x = Map.MapCellX;
    int map_cell_y = Map.MapCellY;

    int map_cell_bottom = map_cell_y + Map.MapCellHeight;

    int origin_x = map_cell_x + 2;
    int origin_y = map_cell_y + 2;

    if (x != 0 || y != 0) {
        CELL screen_cell = Coord_Cell(Map.Pixel_To_Coord(x, y));
        origin_x = Cell_X(screen_cell);
        origin_y = Cell_Y(screen_cell);
    }

    int try_x = origin_x;
    int try_y = origin_y;

    HousesType house = PlayerPtr->Class->House;

    for (StructType sindex = STRUCT_FIRST; sindex < STRUCT_COUNT; sindex++) {
        BuildingTypeClass const& building_type = BuildingTypeClass::As_Reference(sindex);

        if (building_type.Get_Ownable() && building_type.Level != -1) {

            BuildingClass* building = new BuildingClass(building_type, house);
            if (building) {

                try_x = origin_x;
                try_y = origin_y;

                while (try_y < map_cell_bottom) {
                    if (Try_Debug_Spawn_Unlimbo(building, try_x, try_y)) {
                        break;
                    }
                }
            }
        }
    }

    for (UnitType index = UNIT_FIRST; index < UNIT_COUNT; index++) {
        UnitTypeClass const& unit_type = UnitTypeClass::As_Reference(index);

        /*
        **	Fetch the sidebar cameo image for this building.
        */
        if (unit_type.Get_Ownable() && unit_type.Level != -1) {

            UnitClass* unit = (UnitClass*)unit_type.Create_One_Of(PlayerPtr);
            if (unit) {

                try_x = origin_x;
                try_y = origin_y;

                while (try_y < map_cell_bottom) {
                    if (Try_Debug_Spawn_Unlimbo(unit, try_x, try_y)) {
                        break;
                    }
                }
            }
        }
    }

    for (InfantryType index = INFANTRY_FIRST; index < INFANTRY_COUNT; index++) {
        InfantryTypeClass const& infantry_type = InfantryTypeClass::As_Reference(index);

        /*
        **	Fetch the sidebar cameo image for this building.
        */
        if (infantry_type.Get_Ownable() && infantry_type.Level != -1) {

            InfantryClass* inf = (InfantryClass*)infantry_type.Create_One_Of(PlayerPtr);
            if (inf) {

                try_x = origin_x;
                try_y = origin_y;

                while (try_y < map_cell_bottom) {
                    if (Try_Debug_Spawn_Unlimbo(inf, try_x, try_y)) {
                        break;
                    }
                }
            }
        }
    }

    for (AircraftType index = AIRCRAFT_FIRST; index < AIRCRAFT_COUNT; index++) {
        AircraftTypeClass const& aircraft_type = AircraftTypeClass::As_Reference(index);

        /*
        **	Fetch the sidebar cameo image for this building.
        */
        if (aircraft_type.Get_Ownable() && aircraft_type.Level != -1) {

            AircraftClass* air = (AircraftClass*)aircraft_type.Create_One_Of(PlayerPtr);
            if (air) {

                try_x = origin_x;
                try_y = origin_y;

                while (try_y < map_cell_bottom) {
                    if (Try_Debug_Spawn_Unlimbo(air, try_x, try_y)) {
                        break;
                    }
                }
            }
        }
    }

    for (VesselType index = VESSEL_FIRST; index < VESSEL_COUNT; index++) {
        VesselTypeClass const& vessel_type = VesselTypeClass::As_Reference(index);

        /*
        **	Fetch the sidebar cameo image for this building.
        */
        if (vessel_type.Get_Ownable() && vessel_type.Level != -1) {

            VesselClass* boat = (VesselClass*)vessel_type.Create_One_Of(PlayerPtr);
            if (boat) {

                try_x = origin_x;
                try_y = origin_y;

                while (try_y < map_cell_bottom) {
                    if (Try_Debug_Spawn_Unlimbo(boat, try_x, try_y)) {
                        break;
                    }
                }
            }
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::Debug_Spawn_Unit -- Debug spawn a unit at the specified location
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 3/14/2019 3:20PM - ST
 **************************************************************************************************/
void DLLExportClass::Debug_Spawn_Unit(const char* object_name, int x, int y, bool enemy)
{
    if (object_name == NULL) {
        return;
    }

    if (strlen(object_name) == 0) {
        return;
    }

    COORDINATE coord = Map.Pixel_To_Coord(x, y);
    CELL cell = Coord_Cell(coord);

    HousesType house = PlayerPtr->Class->House;

    /*
    ** Place all?
    */
    if (stricmp(object_name, "ALLOBJECTS") == 0) {
        Debug_Spawn_All(x, y);
        return;
    }

    /*
    ** If this is for the enemy, find the enemy with the most stuff
    */

    if (enemy) {
        unsigned max_count = 0;
        for (int i = 0; i < Houses.Count(); ++i) {
            const HouseClass* player = Houses.Ptr(i);
            const unsigned count = player->CurUnits + player->CurBuildings + player->CurInfantry + player->CurVessels
                                   + player->CurAircraft;
            if (!PlayerPtr->Is_Ally(player) && (count >= max_count)) {
                house = player->Class->House;
                max_count = count;
            }
        }
    }

    /*
    ** What is this thing?
    */

    StructType structure_type = BuildingTypeClass::From_Name(object_name);
    if (structure_type != STRUCT_NONE) {

        BuildingClass* building = new BuildingClass(structure_type, house);
        if (building) {
            if (!building->Unlimbo(Cell_Coord(cell))) {
                delete building;
            }
        }

#if (0)
        Map.PendingObject = &BuildingTypeClass::As_Reference(structure_type);
        Map.PendingHouse = PlayerPtr->ActLike;
        Map.PendingObjectPtr = Map.PendingObject->Create_One_Of(PlayerPtr);
        if (Map.PendingObjectPtr) {
            Map.Set_Cursor_Pos();
            Map.Set_Cursor_Shape(Map.PendingObject->Occupy_List());

            // OutList.Add(EventClass(EventClass::PLACE, RTTI_BUILDING, (CELL)(cell + Map.ZoneOffset)));
        }
#endif
        return;
    }

    UnitType unit_type = UnitTypeClass::From_Name(object_name);
    if (unit_type != UNIT_NONE) {

        UnitClass* unit = new UnitClass(unit_type, house);
        if (unit) {
            unit->Unlimbo(Map.Pixel_To_Coord(x, y), DIR_N);
        }

        return;
    }

    InfantryType infantry_type = InfantryTypeClass::From_Name(object_name);
    if (infantry_type != INFANTRY_NONE) {

        InfantryClass* inf = new InfantryClass(infantry_type, house);
        if (inf) {
            inf->Unlimbo(Map.Pixel_To_Coord(x, y), DIR_N);
        }
        return;
    }

    AircraftType aircraft_type = AircraftTypeClass::From_Name(object_name);
    if (aircraft_type != AIRCRAFT_NONE) {

        AircraftClass* air = new AircraftClass(aircraft_type, house);
        if (air) {
            air->Unlimbo(Map.Pixel_To_Coord(x, y), DIR_N);
        }
        return;
    }

    VesselType vessel_type = VesselTypeClass::From_Name(object_name);
    if (vessel_type != VESSEL_NONE) {

        VesselClass* boat = new VesselClass(vessel_type, house);
        if (boat != NULL) {

            if (boat->Unlimbo(Map.Pixel_To_Coord(x, y), DIR_N)) {
                boat->Enter_Idle_Mode();
            } else {
                delete boat;
            }
        }
    }

    OverlayType overlay_type = OverlayTypeClass::From_Name(object_name);
    if (overlay_type != OVERLAY_NONE) {
        new OverlayClass(overlay_type, cell);

        return;
    }
}

/**************************************************************************************************
 * DLLExportClass::Debug_Kill_Unit -- Kill a unit at the specified location
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 8/19/2019 3:09PM - ST
 **************************************************************************************************/
void DLLExportClass::Debug_Kill_Unit(int x, int y)
{
    COORDINATE coord = Map.Pixel_To_Coord(x, y);
    CELL cell = Coord_Cell(coord);

    CellClass* cellptr = &Map[cell];

    if (cellptr) {
        ObjectClass* obj = cellptr->Cell_Object();
        static const int debug_damage = 100; // 100 = Incremental damage
        if (obj) {
            int damage = debug_damage;
            obj->Take_Damage(damage, 0, WARHEAD_HE, 0, true);
        } else {
            if (cellptr->Overlay != OVERLAY_NONE) {
                OverlayTypeClass const* optr = &OverlayTypeClass::As_Reference(cellptr->Overlay);
                if (optr->IsTiberium) {
                    cellptr->Reduce_Tiberium(1);
                }
                if (optr->IsWall) {
                    Map[cell].Reduce_Wall(debug_damage);
                }
            }
            if (cellptr->TType == TEMPLATE_BRIDGE1 || cellptr->TType == TEMPLATE_BRIDGE2
                || cellptr->TType == TEMPLATE_BRIDGE1H || cellptr->TType == TEMPLATE_BRIDGE2H
                || cellptr->TType == TEMPLATE_BRIDGE_1A || cellptr->TType == TEMPLATE_BRIDGE_1B
                || cellptr->TType == TEMPLATE_BRIDGE_2A || cellptr->TType == TEMPLATE_BRIDGE_2B
                || cellptr->TType == TEMPLATE_BRIDGE_3A || cellptr->TType == TEMPLATE_BRIDGE_3B) {
                Map.Destroy_Bridge_At(cell);
            }
        }
    }
}

void DLLExportClass::Debug_Heal_Unit(int x, int y)
{
    COORDINATE coord = Map.Pixel_To_Coord(x, y);
    CELL cell = Coord_Cell(coord);

    CellClass* cellptr = &Map[cell];

    if (cellptr) {
        ObjectClass* obj = cellptr->Cell_Object();
        if (obj) {
            obj->Strength = obj->Class_Of().MaxStrength;
        } else {
            if (cellptr->Overlay != OVERLAY_NONE) {
                OverlayTypeClass const* optr = &OverlayTypeClass::As_Reference(cellptr->Overlay);
                if (optr->IsTiberium) {
                    const int cellcount = (int)FACING_COUNT + 1;
                    CellClass* cells[cellcount];
                    cells[0] = cellptr;
                    for (FacingType index = FACING_N; index < FACING_COUNT; index++) {
                        cells[(int)index + 1] = cellptr->Adjacent_Cell(index);
                    }

                    for (int index = 0; index < cellcount; index++) {
                        CellClass* newcell = cells[index];

                        if (newcell != NULL) {
                            if (newcell->Can_Tiberium_Germinate()) {
                                switch (cellptr->Overlay) {
                                case OVERLAY_GOLD1:
                                case OVERLAY_GOLD2:
                                case OVERLAY_GOLD3:
                                case OVERLAY_GOLD4:
                                    new OverlayClass(Random_Pick(OVERLAY_GOLD1, OVERLAY_GOLD4), newcell->Cell_Number());
                                    newcell->OverlayData = 0;
                                    break;

                                case OVERLAY_GEMS1:
                                case OVERLAY_GEMS2:
                                case OVERLAY_GEMS3:
                                case OVERLAY_GEMS4:
                                    new OverlayClass(Random_Pick(OVERLAY_GEMS1, OVERLAY_GEMS4), newcell->Cell_Number());
                                    newcell->OverlayData = 0;
                                    break;

                                default:
                                    break;
                                }
                            } else if (newcell->Land_Type() == LAND_TIBERIUM) {
                                switch (newcell->Overlay) {
                                case OVERLAY_GOLD1:
                                case OVERLAY_GOLD2:
                                case OVERLAY_GOLD3:
                                case OVERLAY_GOLD4:
                                    newcell->OverlayData = MIN(newcell->OverlayData + 1, 11);
                                    newcell->Recalc_Attributes();
                                    newcell->Redraw_Objects();
                                    break;

                                case OVERLAY_GEMS1:
                                case OVERLAY_GEMS2:
                                case OVERLAY_GEMS3:
                                case OVERLAY_GEMS4:
                                    newcell->OverlayData = MIN(newcell->OverlayData + 1, 2);
                                    newcell->Recalc_Attributes();
                                    newcell->Redraw_Objects();
                                    break;

                                default:
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::Legacy_Render_Enabled -- Is the legacy rendering enabled?
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 4/15/2019 5:46PM - ST
 **************************************************************************************************/
bool DLLExportClass::Legacy_Render_Enabled(void)
{
    if (GAME_TO_PLAY == GAME_GLYPHX_MULTIPLAYER) {
        unsigned int num_humans = 0U;
        for (int i = 0; i < MULTIPLAYER_COUNT; ++i) {
            HouseClass* player_ptr = HouseClass::As_Pointer(Session.Players[i]->Player.ID);
            if (player_ptr && player_ptr->IsHuman) {
                if (++num_humans > 1)
                    break;
            }
        }
        return num_humans < 2;
    }

    // return false;
    return true;
}

/**************************************************************************************************
 * DLLExportClass::Computer_Message -- Replacement for original Computer_Message function
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 1/27/2020 1:42PM - ST
 **************************************************************************************************/
void DLLExportClass::Computer_Message(bool last_player_taunt)
{
    HousesType house;
    HouseClass* ptr;

    HouseClass* ai_players[MAX_PLAYERS];
    int ai_player_count = 0;

    /*------------------------------------------------------------------------
    Find the computer house that the message will be from
    ------------------------------------------------------------------------*/
    for (house = HOUSE_MULTI1; house < (HOUSE_MULTI1 + MULTIPLAYER_COUNT); house++) {
        ptr = HouseClass::As_Pointer(house);

        if (!ptr || ptr->IsHuman || ptr->IsDefeated) {
            continue;
        }

        ai_players[ai_player_count++] = ptr;
    }

    if (ai_player_count) {
        int ai_player_index = 0;
        if (ai_player_count > 1) {
            ai_player_index = IRandom(0, ai_player_count - 1);
        }

        int taunt_index;
        if (last_player_taunt) {
            taunt_index = 13;
        } else {
            taunt_index = IRandom(0, 12);
        }

        On_Message(ai_players[ai_player_index], "", 15.0f, MESSAGE_TYPE_COMPUTER_TAUNT, taunt_index);
    }
}

/**************************************************************************************************
 * DLLExportClass::Set_Special_Key_Flags --
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 6/27/2019 - JAS
 **************************************************************************************************/
void DLLExportClass::Set_Special_Key_Flags(unsigned char special_key_flags)
{
    SpecialKeyFlags[CurrentLocalPlayerIndex] = special_key_flags;
}

/**************************************************************************************************
 * DLLExportClass::Clear_Special_Key_Flags --
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 6/27/2019 - JAS
 **************************************************************************************************/
void DLLExportClass::Clear_Special_Key_Flags()
{
    SpecialKeyFlags[CurrentLocalPlayerIndex] = 0;
}

/**************************************************************************************************
 * DLLExportClass::Get_Input_Key_State --
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 6/27/2019 - JAS
 **************************************************************************************************/
bool DLLExportClass::Get_Input_Key_State(KeyNumType key)
{
    switch (key) {
    case KN_LCTRL:
        return (SpecialKeyFlags[CurrentLocalPlayerIndex] & INPUT_SPECIAL_KEY_CTRL) != 0;
        break;
    case KN_LSHIFT:
        return (SpecialKeyFlags[CurrentLocalPlayerIndex] & INPUT_SPECIAL_KEY_SHIFT) != 0;
        break;
    case KN_LALT:
        return (SpecialKeyFlags[CurrentLocalPlayerIndex] & INPUT_SPECIAL_KEY_ALT) != 0;
        break;
    default:
        break;
    };

    return false;
}

/**************************************************************************************************
 * Get_Input_Key_State
 *
 * History: 6/27/2019 - JAS
 **************************************************************************************************/
bool DLL_Export_Get_Input_Key_State(KeyNumType key)
{
    return DLLExportClass::Get_Input_Key_State(key);
}

bool DLLSave(Pipe& file)
{
    return DLLExportClass::Save(file);
}

bool DLLLoad(Straw& file)
{
    return DLLExportClass::Load(file);
}

/**************************************************************************************************
 * DLLExportClass::Save --
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 9/10/2019 10:24AM - ST
 **************************************************************************************************/
bool DLLExportClass::Save(Pipe& pipe)
{
    /*
    ** Version first
    */
    unsigned int version = CNC_DLL_API_VERSION;
    pipe.Put(&version, sizeof(version));

    pipe.Put(MultiplayerStartPositions, sizeof(MultiplayerStartPositions));

    pipe.Put(GlyphxPlayerIDs, sizeof(GlyphxPlayerIDs));

    pipe.Put(&GlyphXClientSidebarWidthInLeptons, sizeof(GlyphXClientSidebarWidthInLeptons));

    pipe.Put(MPlayerIsHuman, sizeof(MPlayerIsHuman));

    pipe.Put(MultiplayerStartPositions, sizeof(MultiplayerStartPositions));

    pipe.Put(PlacementType, sizeof(PlacementType));

    pipe.Put(&OverrideNewUnitsEnabled, sizeof(OverrideNewUnitsEnabled));

    for (int i = 0; i < MAX_PLAYERS; i++) {
        Sidebar_Glyphx_Save(pipe, &MultiplayerSidebars[i]);

        int has_player = false;
        if (i < Session.Players.Count() && Session.Players[i]) {
            has_player = true;
            pipe.Put(&has_player, sizeof(has_player));
            pipe.Put(Session.Players[i], sizeof(NodeNameType));
        } else {
            pipe.Put(&has_player, sizeof(has_player));
        }
    }

    pipe.Put(&Special, sizeof(Special));

    /*
    ** Special case for MPSuperWeaponDisable - store negated value so it defaults to enabled
    */
    bool not_allow_super_weapons = !MPSuperWeaponDisable;
    pipe.Put(&not_allow_super_weapons, sizeof(not_allow_super_weapons));

    /*
    ** Room for save game expansion
    */
    unsigned char padding[4095];
    memset(padding, 0, sizeof(padding));

    /* OpenCNC3D: the match's short game rule rides in the first byte of the expansion room,
       which every older save holds as zero, so an older save loads with the rule off. */
    padding[0] = OpenCNC3D_ShortGame ? 1 : 0;
    if (FairAI_RA_Save(padding + 1, sizeof(padding) - 1) < 0) return false;

    pipe.Put(padding, sizeof(padding));

    return true;
}

/**************************************************************************************************
 * DLLExportClass::Load --
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 9/10/2019 10:24AM - ST
 **************************************************************************************************/
bool DLLExportClass::Load(Straw& file)
{
    unsigned int version = 0;

    if (file.Get(&version, sizeof(version)) != sizeof(version)) {
        return false;
    }

    if (file.Get(MultiplayerStartPositions, sizeof(MultiplayerStartPositions)) != sizeof(MultiplayerStartPositions)) {
        return false;
    }

    if (file.Get(GlyphxPlayerIDs, sizeof(GlyphxPlayerIDs)) != sizeof(GlyphxPlayerIDs)) {
        return false;
    }

    if (file.Get(&GlyphXClientSidebarWidthInLeptons, sizeof(GlyphXClientSidebarWidthInLeptons))
        != sizeof(GlyphXClientSidebarWidthInLeptons)) {
        return false;
    }

    if (file.Get(MPlayerIsHuman, sizeof(MPlayerIsHuman)) != sizeof(MPlayerIsHuman)) {
        return false;
    }

    if (file.Get(MultiplayerStartPositions, sizeof(MultiplayerStartPositions)) != sizeof(MultiplayerStartPositions)) {
        return false;
    }

    if (file.Get(PlacementType, sizeof(PlacementType)) != sizeof(PlacementType)) {
        return false;
    }

    if (file.Get(&OverrideNewUnitsEnabled, sizeof(OverrideNewUnitsEnabled)) != sizeof(OverrideNewUnitsEnabled)) {
        return false;
    }

    if (Is_Aftermath_Installed()) {
        if (Session.Type == GAME_SKIRMISH || Session.Type == GAME_GLYPHX_MULTIPLAYER) {
            bAftermathMultiplayer = NewUnitsEnabled = OverrideNewUnitsEnabled;
        }
    }

    Session.NumPlayers = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        Sidebar_Glyphx_Load(file, &MultiplayerSidebars[i]);

        int has_player = false;
        file.Get(&has_player, sizeof(has_player));

        if (has_player) {
            NodeNameType* who = new NodeNameType;
            file.Get(who, sizeof(NodeNameType));
            Session.Players.Add(who);
            Session.NumPlayers++;
        }
    }

    if (file.Get(&Special, sizeof(Special)) != sizeof(Special)) {
        return false;
    }

    /*
    ** Restore backup
    */
    if (SpecialBackup != NULL) {
        memcpy(SpecialBackup, &Special, sizeof(SpecialClass));
    }

    /*
    ** Special case for MPSuperWeaponDisable - store negated value so it defaults to enabled
    */
    bool not_allow_super_weapons = false;
    if (file.Get(&not_allow_super_weapons, sizeof(not_allow_super_weapons)) != sizeof(not_allow_super_weapons)) {
        return false;
    }
    MPSuperWeaponDisable = !not_allow_super_weapons;

    unsigned char padding[4095];

    if (file.Get(padding, sizeof(padding)) != sizeof(padding)) {
        return false;
    }
    OpenCNC3D_ShortGame = (padding[0] == 1);
    if (!FairAI_RA_Load(padding + 1, sizeof(padding) - 1)) return false;

    return true;
}

/**************************************************************************************************
 * DLLExportClass::Code_Pointers --
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 9/10/2019 10:24AM - ST
 **************************************************************************************************/
void DLLExportClass::Code_Pointers(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Sidebar_Glyphx_Code_Pointers(&MultiplayerSidebars[i]);

        if (PlacementType[i]) {
            PlacementType[i] = (BuildingTypeClass*)PlacementType[i]->Type;
        }
    }
}

/**************************************************************************************************
 * DLLExportClass::Decode_Pointers --
 *
 * In:
 *
 * Out:
 *
 *
 *
 * History: 9/10/2019 10:24AM - ST
 **************************************************************************************************/
void DLLExportClass::Decode_Pointers(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Sidebar_Glyphx_Decode_Pointers(&MultiplayerSidebars[i]);

        if (PlacementType[i]) {
            StructType type = (StructType) reinterpret_cast<unsigned int>(PlacementType[i]);
            PlacementType[i] = NULL;
            if (type >= STRUCT_FIRST && type < STRUCT_COUNT) {

                TechnoTypeClass const* tech = Fetch_Techno_Type(RTTI_BUILDINGTYPE, type);
                if (tech) {
                    BuildingTypeClass* build_type = (BuildingTypeClass*)(tech);
                    if (build_type) {
                        PlacementType[i] = build_type;
                    }
                }
            }
        }
    }
}

void DLL_Code_Pointers(void)
{
    DLLExportClass::Code_Pointers();
}

void DLL_Decode_Pointers(void)
{
    DLLExportClass::Decode_Pointers();
}

/*
**	OpenCNC3D: SHOTS FIRED SINCE THE LAST DUMP, as the Tiberian Dawn brain records them.
**
**	TechnoClass::Fire_At calls OpenCNC3D_Note_Fire once for every projectile it launches,
**	visible or not, which is the one place a shot is certain: an anim is spawned only for
**	the weapons that have one, and an invisible round can be launched and spent inside a
**	single tick. Each record is the firer's heap id and RTTI kind (the pair an EFX|ANIM
**	line calls attref/attkind), the weapon (0 primary, 1 secondary), the barrel of a
**	two-shooter (the IsSecondShot the shot left with), the projectile's heap id, its
**	BulletType and name, and the coordinate the engine launched it from.
**
**	The dump prints them as EFX|FIRE lines and empties the list. It holds OPENCNC3D_FIRE_MAX;
**	past that the oldest are dropped, which only a host that stops dumping would ever see.
**	Nothing in the game reads this list, so a peer that never dumps is unaffected.
*/
#define OPENCNC3D_FIRE_MAX 256
struct OpenCNC3DFireRecord
{
    int kind, id, which, second, bullet, btype, lx, ly;
    char type[12], bname[12];
};
static OpenCNC3DFireRecord OpenCNC3D_Fires[OPENCNC3D_FIRE_MAX];
static int OpenCNC3D_FireCount = 0;

void OpenCNC3D_Note_Fire(TechnoClass* firer, int which, BulletClass* bullet, int second, COORDINATE fire)
{
    if (firer == NULL) {
        return;
    }
    if (OpenCNC3D_FireCount == OPENCNC3D_FIRE_MAX) {
        memmove(&OpenCNC3D_Fires[0], &OpenCNC3D_Fires[1], sizeof(OpenCNC3D_Fires[0]) * (OPENCNC3D_FIRE_MAX - 1));
        OpenCNC3D_FireCount--;
    }
    OpenCNC3DFireRecord& r = OpenCNC3D_Fires[OpenCNC3D_FireCount++];
    r.kind = (int)firer->What_Am_I();
    switch (firer->What_Am_I()) {
    case RTTI_INFANTRY:
        r.id = Infantry.ID((InfantryClass*)firer);
        break;
    case RTTI_UNIT:
        r.id = Units.ID((UnitClass*)firer);
        break;
    case RTTI_VESSEL:
        r.id = Vessels.ID((VesselClass*)firer);
        break;
    case RTTI_AIRCRAFT:
        r.id = Aircraft.ID((AircraftClass*)firer);
        break;
    case RTTI_BUILDING:
        r.id = Buildings.ID((BuildingClass*)firer);
        break;
    default:
        r.id = -1;
        break;
    }
    r.which = which;
    r.second = second;
    r.bullet = bullet ? Bullets.ID(bullet) : -1;
    r.btype = bullet ? (int)bullet->Class->Type : -1;
    r.lx = (int)Coord_X(fire);
    r.ly = (int)Coord_Y(fire);
    strncpy(r.type, firer->Techno_Type_Class()->IniName, sizeof(r.type) - 1);
    r.type[sizeof(r.type) - 1] = 0;
    strncpy(r.bname, bullet ? bullet->Class->IniName : "-", sizeof(r.bname) - 1);
    r.bname[sizeof(r.bname) - 1] = 0;
}

/* Read-only object state for headless RA3D probes. The first slice uses the
   renderer's text protocol for common identity and placement fields; RA-specific
   presentation states and controls need separate validation. */
static int OpenCNC3D_RA_Print_Object(const char* kind, ObjectClass* object, int id)
{
    if (object == NULL || !object->IsActive) {
        return 0;
    }
    HousesType house = object->Owner();
    const char* owner = house == HOUSE_NONE ? "None" : HouseTypeClass::As_Reference(house).IniName;
    CELL cell = Coord_Cell(object->Coord);
    CELL center = Coord_Cell(object->Center_Coord());
    int face = object->Is_Techno() ? (int)(unsigned char)((TechnoClass*)object)->PrimaryFacing.Current() : -1;
    const char* mission = object->Is_Techno()
        ? MissionClass::Mission_Name(((TechnoClass*)object)->Get_Mission()) : "-";
    /* These values come from the RA object and type, not the TD type tables.
       Vessel is a FootClass and has its own secondary facing. */
    const RTTIType rtti = object->What_Am_I();
    int fw = 1, fh = 1, tcx = Cell_X(cell), tcy = Cell_Y(cell);
    int tface = -1, nav = -1, tar = -1, group = -1, alt = 0;
    int selected = 0;
    if (rtti == RTTI_BUILDING) {
        BuildingClass* building = (BuildingClass*)object;
        fw = building->Class->Width();
        fh = building->Class->Height();
        CELL target_cell = Coord_Cell(building->Target_Coord());
        tcx = Cell_X(target_cell);
        tcy = Cell_Y(target_cell);
    }
    if (rtti == RTTI_UNIT)
        tface = (int)(unsigned char)((UnitClass*)object)->SecondaryFacing.Current();
    else if (rtti == RTTI_VESSEL) {
        /*
        **	OpenCNC3D: a vessel without a turret reports, in place of the turret facing it does
        **	not have, the facing its hull is turning to. The engine opens a Transport's door
        **	while its hull is still turning to the shore (Mission_Unload waits on IsRotating,
        **	which for a vessel is the turret's), and a renderer that opens the ramps in order
        **	needs to know where the hull will lie.
        */
        VesselClass* vessel = (VesselClass*)object;
        tface = vessel->Class->IsTurretEquipped ? (int)(unsigned char)vessel->Turret_Facing()
                                                : (int)(unsigned char)vessel->PrimaryFacing.Desired();
    }
    else if (rtti == RTTI_AIRCRAFT) {
        tface = (int)(unsigned char)((AircraftClass*)object)->SecondaryFacing.Current();
        alt = ((AircraftClass*)object)->Height;
    }
    if (rtti == RTTI_INFANTRY || rtti == RTTI_UNIT || rtti == RTTI_VESSEL || rtti == RTTI_AIRCRAFT) {
        FootClass* foot = (FootClass*)object;
        if (Target_Legal(foot->NavCom))
            nav = (int)::As_Cell(foot->NavCom);
        group = foot->Group <= 9 ? (int)foot->Group : -1;
    }
    if (object->Is_Techno()) {
        TechnoClass* techno = (TechnoClass*)object;
        if (Target_Legal(techno->TarCom))
            tar = (int)::As_Cell(techno->TarCom);
    }
    if (PlayerPtr != NULL && PlayerPtr->Class.Is_Valid())
        selected = (object->IsSelectedMask & (1U << (int)PlayerPtr->Class->House)) ? 1 : 0;
    /*
    **	OpenCNC3D infantry presentation. The renderer's 3-D soldiers play a clip for what the
    **	engine has the man doing (InfantryClass::Doing) and time a shot or a death by its
    **	stage (Fetch_Stage), as the Tiberian Dawn brain exports them. They are reported in
    **	TIBERIAN DAWN'S DoType numbering, which the renderer reads: Red Alert dropped Tiberian
    **	Dawn's fist fight (DO_ON_GUARD .. DO_READY_WEAPON, 11 values), so its deaths and
    **	gestures sit 11 lower, and it added DO_DOG_MAUL, reported as 40, past any Tiberian
    **	Dawn value. dying is 1 through a death's whole sequence. -1 for anything else.
    */
    int doing = -1, dostage = -1, dying = 0;
    if (rtti == RTTI_INFANTRY) {
        InfantryClass* inf = (InfantryClass*)object;
        int d = (int)inf->Doing;
        if (d >= DO_GUN_DEATH && d <= DO_SALUTE2)
            doing = d + 11;
        else if (d == DO_DOG_MAUL)
            doing = 40;
        else
            doing = d;
        dostage = inf->Fetch_Stage();
        dying = (inf->Doing >= DO_GUN_DEATH && inf->Doing <= DO_FIRE_DEATH) ? 1 : 0;
    }
    /* OpenCNC3D building presentation, read from the simulation. An intact mesh may
       replace the idle art only; construction and an open factory door retain the
       engine's original shape. These fields do not alter the simulation. */
    int makecnt = -1, doorstage = -1;
    if (rtti == RTTI_BUILDING) {
        BuildingClass* building = (BuildingClass*)object;
        doing = (int)building->BState;
        dostage = building->Fetch_Stage();
        makecnt = building->Class->Anims[BSTATE_CONSTRUCTION].Count;
        doorstage = building->Door_Stage();
    }
    /*
    **	OpenCNC3D: A TRANSPORT'S DOOR, as the Tiberian Dawn brain exports it. TechnoClass derives
    **	from DoorClass, and the APC and the Chinook open theirs to load and unload. For a unit
    **	or an aircraft door= is the door's state rather than a building's stage: -1 shut, 0
    **	closing, 1 + Door_Stage() opening or open (Door_Stage alone reads 0 both shut and wide
    **	open on a two-stage door). The renderer swings a remastered door or ramp from it.
    **
    **	A UNIT'S OWN ANIMATION, for the Ore Truck's scoop and dump: doing= is 1 while the
    **	harvester lifts ore under the four conditions UnitClass::Draw_It draws the lifting
    **	frames on (IsHarvesting, PrimaryFacing not rotating, no NavCom, not driving) and 2
    **	while it dumps a load (IsDumping), and then dostage= is the frame of the 1996
    **	animation Draw_It shows: Harvester_Load_List[stage] (0 to 7) or Harvester_Dump_List
    **	[stage] (0 to 14 and back), the stage clamped as Draw_It clamps it. Otherwise doing=
    **	is -1 and dostage= the unit's stage counter. Read only.
    */
    /*
    **	A ship's too (OpenCNC3D): the Transport opens its door, the ramps of its 1996 frames 1 to
    **	5, to take passengers aboard and to put them ashore (VesselClass::LST_Open_Door).
    */
    if (rtti == RTTI_UNIT || rtti == RTTI_AIRCRAFT || rtti == RTTI_VESSEL) {
        TechnoClass* door_owner = (TechnoClass*)object;
        if (door_owner->Is_Door_Closed()) {
            doorstage = -1;
        } else if (door_owner->Is_Door_Closing()) {
            doorstage = 0;
        } else {
            doorstage = 1 + door_owner->Door_Stage();
        }
    }
    if (rtti == RTTI_UNIT) {
        UnitClass* unit = (UnitClass*)object;
        unsigned stage = unit->Fetch_Stage();
        dostage = (int)stage;
        if (unit->IsHarvesting && !unit->PrimaryFacing.Is_Rotating() && !unit->NavCom && !unit->IsDriving) {
            if (stage >= ARRAY_SIZE(unit->Class->Harvester_Load_List))
                stage = ARRAY_SIZE(unit->Class->Harvester_Load_List) - 1;
            doing = 1;
            dostage = unit->Class->Harvester_Load_List[stage];
        } else if (unit->IsDumping) {
            if (stage >= ARRAY_SIZE(unit->Class->Harvester_Dump_List))
                stage = ARRAY_SIZE(unit->Class->Harvester_Dump_List) - 1;
            doing = 2;
            dostage = unit->Class->Harvester_Dump_List[stage];
        }
        /*
        **	OpenCNC3D: THE V2 ROCKET'S LAUNCHER, for the remastered model's erector. doing= is 3
        **	while the launcher stands to fire: the unit has a target (TarCom) inside its
        **	weapon's range and is not driving (RULES.INI gives the V2 NoMovingFire, so it
        **	fires only standing). That covers the turn onto the target, the launch and the
        **	time it stays on the target afterwards. dostage= is then the ticks before the
        **	engine will fire, so a renderer can have the launcher up by the shot: with a
        **	rocket on the rail 0 when Can_Fire passes, else the turn still to make onto the
        **	target past Can_Fire's own tolerance (8, or 32 for a homing rocket's, which the
        **	V2's is) at its rate of turn (ROT by the house's ground speed bias, as
        **	DriveClass::AI turns it); with the rail empty, the reload still to run and the
        **	tick after it (Reload_AI puts the rocket back only standing still, and the engine
        **	fires on the next tick it may). Whether a rocket is on the rail is ammo= below.
        **	Read only.
        */
        if (unit->Class->Type == UNIT_V2_LAUNCHER && doing < 0 && Target_Legal(unit->TarCom)
            && !unit->IsDriving && unit->In_Range(unit->TarCom, 0)) {
            doing = 3;
            dostage = 0;
            if (unit->Ammo == 0) {
                dostage = (int)unit->Reload + 1;
            } else if (unit->Can_Fire(unit->TarCom, 0) == FIRE_FACING) {
                WeaponTypeClass const* weapon = unit->Class->PrimaryWeapon;
                const int tolerance = (weapon != NULL && weapon->Bullet != NULL && weapon->Bullet->ROT != 0) ? 32 : 8;
                const int diff = ABS((int)unit->PrimaryFacing.Difference(unit->Direction(unit->TarCom)));
                int rate = (int)(unit->Class->ROT * unit->House->GroundspeedBias);
                if (rate < 1) rate = 1;
                dostage = diff > tolerance ? (diff - tolerance + rate - 1) / rate : 1;
            }
        }
    }
    /*
    **	OpenCNC3D: A TECHNO'S AMMUNITION, TechnoClass::Ammo: the rounds it carries, -1 for a
    **	type with no limit. The V2 Rocket carries one rocket, so it reads 1 while the rocket
    **	is on the rail and 0 from the launch until it has reloaded; -1 for anything that is
    **	not a techno. Read only.
    */
    const int ammo = object->Is_Techno() ? (int)((TechnoClass*)object)->Ammo : -1;
    int blush = object->Is_Techno() ? (((TechnoClass*)object)->IsBlushing ? 1 : 0) : 0;
    /*
    **	OpenCNC3D side colour. The owner's remap colour as the row of the disc's PALETTE.CPS it
    **	draws from (gold, light blue, red, green, orange, grey, blue, brown), which is how
    **	a 3-D soldier's uniform takes the side's colour the 1996 sprite would. A house
    **	name cannot say it: a multiplayer house's colour is the player's choice. The rows
    **	are named rather than counted because the remaster build swaps PCOLOR_BLUE and
    **	PCOLOR_GREY in the enum. -1 for anything that is not a techno.
    */
    int remap = -1;
    if (object->Is_Techno() && ((TechnoClass*)object)->House.Is_Valid()) {
        const PlayerColorType c = ((TechnoClass*)object)->House->RemapColor;
        remap = c == PCOLOR_GREY ? 5 : c == PCOLOR_BLUE ? 6 : (int)c;
        if (remap < 0 || remap > 7) remap = -1;
    }
    /*
    **	OpenCNC3D: THE REST OF THE TIBERIAN DAWN BRAIN'S FIELDS, with the same names and the same
    **	meaning, so the renderer's readers of them work for Red Alert unchanged. All read only.
    **
    **	  dimw      ObjectTypeClass::Dimensions' width in pixels (the health bar's length).
    **	  status    a building's MissionClass::Status, the mission state machine's own step;
    **	            -1 for anything else.
    **	  flash     TechnoClass::FlashCount, the counter behind blush=.
    **	  pips      Pip_Count, and maxpips the type's Max_Pips: the ore load, the ammunition, a
    **	            silo's store, a transport's passengers. A reader skips the row on maxpips 0.
    **	  cloak     TechnoClass::Cloak (0 uncloaked, 1 cloaking, 2 cloaked, 3 uncloaking) and
    **	  cstage    the cloaking device's stage, for the submarines.
    **	  repairing a building's IsRepairing, and wrench the blink of its repair wrench; both are
    **	            needed, because the blink bit is left wherever the last flip put it.
    **	  poweroff  a building switched off by its owner (BuildingClass::IsPowerOff).
    **	  harv, rotating, navset, driving: the four conditions a harvester's own animation is
    **	            gated on (IsHarvesting, PrimaryFacing.Is_Rotating, NavCom set, IsDriving).
    **	  primary   a factory's IsLeader: the one the next product leaves from.
    **	  flashme   whether the target blush is the LOCAL player's to see (Get_Flashing_Flags
    **	            for PlayerPtr's house). Player-relative by design, so a lockstep world hash
    **	            leaves it out.
    **	  act       WHICH SIDE THIS OBJECT FIGHTS FOR, in the Tiberian Dawn brain's numbering:
    **	            0 the Allies' side (the Allied countries, GoodGuy, and a multiplayer house that
    **	            plays an Allied country), 1 the Soviet side (USSR, Ukraine, BadGuy), 2 neutral,
    **	            3 special (HOUSE_JP), -1 for anything that is not a techno. Red Alert's ActLike is a
    **	            country; the renderer asks a side.
    **	act= STAYS LAST: readers match the line anchored to its end on act=.
    */
    int dimw = 0, dimh = 0;
    object->Class_Of().Dimensions(dimw, dimh);
    int status = -1, repairing = 0, wrench = 0, poweroff = 0, primary = 0;
    if (rtti == RTTI_BUILDING) {
        BuildingClass* building = (BuildingClass*)object;
        status = (int)building->Status;
        repairing = building->IsRepairing ? 1 : 0;
        wrench = building->IsWrenchVisible ? 1 : 0;
        poweroff = building->IsPowerOff ? 1 : 0;
        primary = building->IsLeader ? 1 : 0;
    }
    int flash = 0, flashme = 0, cloak = 0, cstage = 0, pips = 0, rotating = 0, act = -1;
    const int maxpips = object->Class_Of().Max_Pips();
    if (object->Is_Techno()) {
        TechnoClass* techno = (TechnoClass*)object;
        flash = (int)techno->FlashCount;
        if (PlayerPtr != NULL && PlayerPtr->Class.Is_Valid()) {
            const int ph = (int)PlayerPtr->Class->House;
            if (ph >= 0 && ph < HOUSE_COUNT) {
                flashme = (techno->Get_Flashing_Flags() & (1u << ph)) ? 1 : 0;
            }
        }
        cloak = (int)techno->Cloak;
        cstage = (int)techno->CloakingDevice.Fetch_Stage();
        pips = techno->Pip_Count();
        rotating = techno->PrimaryFacing.Is_Rotating() ? 1 : 0;
        if (techno->House.Is_Valid()) {
            switch (techno->House->ActLike) {
            case HOUSE_USSR:
            case HOUSE_UKRAINE:
            case HOUSE_BAD:
                act = 1;
                break;
            case HOUSE_NEUTRAL:
                act = 2;
                break;
            case HOUSE_JP:
                act = 3;
                break;
            default:
                act = 0;
                break;
            }
        }
    }
    int harv = 0, navset = 0, driving = 0;
    if (rtti == RTTI_INFANTRY || rtti == RTTI_UNIT || rtti == RTTI_VESSEL || rtti == RTTI_AIRCRAFT) {
        FootClass* mover = (FootClass*)object;
        navset = (mover->NavCom != TARGET_NONE) ? 1 : 0;
        driving = mover->IsDriving ? 1 : 0;
    }
    if (rtti == RTTI_UNIT) {
        harv = ((UnitClass*)object)->IsHarvesting ? 1 : 0;
    }
    printf("OBJ|%s|%s|%s|newcell=%d|x=%d|y=%d|inicell=%d|centercell=%d|str=%d|max=%d"
           "|limbo=%d|down=%d|mission=%s|lx=%d|ly=%d|clx=%d|cly=%d|face=%d|id=%d"
           "|fw=%d|fh=%d|tcx=%d|tcy=%d|tface=%d|sel=%d|nav=%d|tar=%d|group=%d|alt=%d"
           "|doing=%d|dostage=%d|dying=%d|remap=%d|makecnt=%d|door=%d|blush=%d"
           "|dimw=%d|status=%d|flash=%d|pips=%d|maxpips=%d|cloak=%d|cstage=%d|repairing=%d|wrench=%d"
           "|poweroff=%d|harv=%d|rotating=%d|navset=%d|driving=%d|primary=%d|flashme=%d|ammo=%d|act=%d\n",
           kind, object->Class_Of().IniName, owner, (int)cell, Cell_X(cell), Cell_Y(cell),
           (int)cell, (int)center, (int)object->Strength, (int)object->Class_Of().MaxStrength,
           (int)object->IsInLimbo, (int)object->IsDown, mission ? mission : "-",
           (int)Coord_X(object->Coord), (int)Coord_Y(object->Coord),
           (int)Coord_X(object->Center_Coord()), (int)Coord_Y(object->Center_Coord()), face, id,
           fw, fh, tcx, tcy, tface, selected, nav, tar, group, alt, doing, dostage, dying, remap, makecnt, doorstage, blush,
           dimw, status, flash, pips, maxpips, cloak, cstage, repairing, wrench,
           poweroff, harv, rotating, navset, driving, primary, flashme, ammo, act);
    return 1;
}

extern "C" __declspec(dllexport) int __cdecl OpenCNC3D_Dump_Objects(void)
{
    int total = 0;
    printf("OBJDUMP-BEGIN heaps: Infantry=%d Units=%d Vessels=%d Buildings=%d Terrains=%d Aircraft=%d Houses=%d\n",
           Infantry.Count(), Units.Count(), Vessels.Count(), Buildings.Count(), Terrains.Count(),
           Aircraft.Count(), Houses.Count());
    for (int i = 0; i < Infantry.Count(); ++i) {
        InfantryClass* object = Infantry.Ptr(i);
        total += OpenCNC3D_RA_Print_Object("INFANTRY", object, Infantry.ID(object));
    }
    for (int i = 0; i < Units.Count(); ++i) {
        UnitClass* object = Units.Ptr(i);
        total += OpenCNC3D_RA_Print_Object("UNIT", object, Units.ID(object));
    }
    for (int i = 0; i < Vessels.Count(); ++i) {
        VesselClass* object = Vessels.Ptr(i);
        total += OpenCNC3D_RA_Print_Object("VESSEL", object, Vessels.ID(object));
    }
    for (int i = 0; i < Buildings.Count(); ++i) {
        BuildingClass* object = Buildings.Ptr(i);
        total += OpenCNC3D_RA_Print_Object("BUILDING", object, Buildings.ID(object));
    }
    for (int i = 0; i < Terrains.Count(); ++i) {
        TerrainClass* object = Terrains.Ptr(i);
        total += OpenCNC3D_RA_Print_Object("TERRAIN", object, Terrains.ID(object));
    }
    for (int i = 0; i < Aircraft.Count(); ++i) {
        AircraftClass* object = Aircraft.Ptr(i);
        total += OpenCNC3D_RA_Print_Object("AIRCRAFT", object, Aircraft.ID(object));
    }
    /* The Classic renderer draws the engine's own explosion, impact and order
       acknowledgement SHPs from this live animation stream. Match the TD dump
       contract so both brains feed the same effect parser. */
    for (int i = 0; i < Anims.Count(); ++i) {
        AnimClass* anim = Anims.Ptr(i);
        if (anim == NULL || !anim->IsActive || anim->IsInLimbo)
            continue;
        const AnimTypeClass& type = (const AnimTypeClass&)anim->Class_Of();
        COORDINATE center = anim->Center_Coord();
        int attref = -1;
        int attkind = -1;
        if (Target_Legal(anim->xObject)) {
            ObjectClass* attached = As_Object(anim->xObject);
            if (attached != NULL && attached->IsActive) {
                attkind = (int)attached->What_Am_I();
                switch (attached->What_Am_I()) {
                case RTTI_INFANTRY: attref = Infantry.ID((InfantryClass*)attached); break;
                case RTTI_UNIT: attref = Units.ID((UnitClass*)attached); break;
                case RTTI_VESSEL: attref = Vessels.ID((VesselClass*)attached); break;
                case RTTI_AIRCRAFT: attref = Aircraft.ID((AircraftClass*)attached); break;
                case RTTI_BUILDING: attref = Buildings.ID((BuildingClass*)attached); break;
                default: break;
                }
            }
        }
        printf("EFX|ANIM|id=%d|type=%d|name=%s|lx=%d|ly=%d|stage=%d|start=%d|stages=%d|"
               "loops=%d|size=%d|ground=%d|attref=%d|attkind=%d|owner=%d|biggest=%d\n",
               Anims.ID(anim), (int)(AnimType)*anim, type.IniName,
               (int)Coord_X(center), (int)Coord_Y(center), anim->Fetch_Stage(),
               type.Start, type.Stages, (int)anim->Loops, type.Size,
               (int)type.IsGroundLayer, attref, attkind,
               (int)anim->OwnerHouse, type.Biggest);
        total++;
    }
    /*
    **	OpenCNC3D: the shots fired since the last dump (OpenCNC3D_Note_Fire), then the list
    **	is emptied; and the bullets in flight, with Tiberian Dawn's fields. type is the
    **	BulletType, name its IniName ("Cannon", "HeatSeeker"); img, appended, is the picture
    **	it flies as ("120MM", "DRAGON"), which is what Tiberian Dawn's names are. lx/ly is its
    **	ground point and alt its height above it (ObjectClass::Height, in leptons), face its
    **	PrimaryFacing 0..255. invis is the type's IsInvisible: the 1996 game never drew those.
    **	OpenCNC3D: tlx/tly is where its fuse is set to go off (FuseClass::Fuse_Target, the
    **	target point after any scatter), -1 for none, so a renderer can bring a projectile
    **	it draws on a path of its own down where the engine's will burst.
    */
    for (int i = 0; i < OpenCNC3D_FireCount; i++) {
        const OpenCNC3DFireRecord& r = OpenCNC3D_Fires[i];
        printf("EFX|FIRE|id=%d|kind=%d|type=%s|which=%d|second=%d|bullet=%d|btype=%d|bname=%s|lx=%d|ly=%d\n",
               r.id, r.kind, r.type, r.which, r.second, r.bullet, r.btype, r.bname, r.lx, r.ly);
        total++;
    }
    OpenCNC3D_FireCount = 0;
    for (int i = 0; i < Bullets.Count(); i++) {
        BulletClass* b = Bullets.Ptr(i);
        if (b == NULL || !b->IsActive || b->IsInLimbo)
            continue;
        COORDINATE where = b->Center_Coord();
        const COORDINATE fuse = b->Fuse_Target();
        printf("EFX|BULLET|id=%d|type=%d|name=%s|lx=%d|ly=%d|face=%d|alt=%d|invis=%d|"
               "arcing=%d|homing=%d|img=%s|tlx=%d|tly=%d\n",
               Bullets.ID(b), (int)b->Class->Type, b->Class->IniName,
               (int)Coord_X(where), (int)Coord_Y(where),
               b->OpenCNC3D_Facing(), (int)b->Height,
               (int)b->Class->IsInvisible, (int)b->Class->IsArcing, (int)(b->Class->ROT != 0),
               b->Class->GraphicName, fuse ? (int)Coord_X(fuse) : -1, fuse ? (int)Coord_Y(fuse) : -1);
        total++;
    }
    for (int i = 0; i < Houses.Count(); ++i) {
        HouseClass* house = Houses.Ptr(i);
        if (house == NULL || !house->IsActive)
            continue;
        printf("HOUSE|%s|credits=%d|human=%d|player=%d|units=%d|buildings=%d|tiberium=%d|capacity=%d\n",
               house->Class->IniName, (int)house->Credits, (int)house->IsHuman,
               (int)(house == PlayerPtr), (int)house->CurUnits, (int)house->CurBuildings,
               (int)house->Tiberium, (int)house->Capacity);
    }
    printf("VIEW|%d|%d\n", Cell_X(Scen.Views[0]), Cell_Y(Scen.Views[0]));
    printf("OBJDUMP-END count=%d frame=%d\n", total, (int)Frame);
    fflush(stdout);
    return total;
}

/* Local effect probes use the same hooks as the TD brain. Normal play never
   calls them; the anim and damage still run through Red Alert's own classes. */
extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Spawn_Anim(int type, int x, int y)
{
    if (type < ANIM_FIRST || type >= ANIM_COUNT || x < 0 || y < 0)
        return false;
    return new AnimClass((AnimType)type, XY_Coord(x, y)) != NULL;
}

extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Hurt_Object(int rtti, int id, int damage)
{
    TechnoClass* victim = NULL;
    if (rtti == RTTI_AIRCRAFT && id >= 0 && id < Aircraft.Length()) {
        AircraftClass* air = Aircraft.Raw_Ptr(id);
        if (air != NULL && air->IsActive) victim = air;
    } else if (rtti == RTTI_UNIT && id >= 0 && id < Units.Length()) {
        UnitClass* unit = Units.Raw_Ptr(id);
        if (unit != NULL && unit->IsActive) victim = unit;
    } else if (rtti == RTTI_INFANTRY && id >= 0 && id < Infantry.Length()) {
        InfantryClass* man = Infantry.Raw_Ptr(id);
        if (man != NULL && man->IsActive) victim = man;
    } else if (rtti == RTTI_VESSEL && id >= 0 && id < Vessels.Length()) {
        /* OpenCNC3D: a ship too, for the tests of the remastered ships' damage and wrecks */
        VesselClass* ship = Vessels.Raw_Ptr(id);
        if (ship != NULL && ship->IsActive) victim = ship;
    }
    if (victim == NULL || victim->Strength <= 0 || damage <= 0)
        return false;
    int dealt = damage;
    victim->Take_Damage(dealt, 0, WARHEAD_HE, 0);
    return true;
}

/*
**	OpenCNC3D: A TEST HOOK FOR THE ORE SILO'S FILL. Puts `amount` credits of ore into a house's
**	store as a harvest unloads it (HouseClass::Harvested: up to the house's capacity), or with a
**	negative amount spends that much of the stored ore (Spend_Money, never more than is stored).
**	Returns the house's stored ore after it, -1 for no such house. GATE ONLY: it changes the
**	world, as OpenCNC3D_Hurt_Object does.
*/
extern "C" __declspec(dllexport) int __cdecl OpenCNC3D_Store_Ore(const char* house, int amount)
{
    if (house == NULL)
        return -1;
    for (int i = 0; i < Houses.Count(); ++i) {
        HouseClass* h = Houses.Ptr(i);
        if (h == NULL || !h->IsActive || strcmp(h->Class->IniName, house) != 0)
            continue;
        if (amount > 0) {
            h->Harvested((unsigned)amount);
        } else if (amount < 0) {
            int take = -amount;
            if (take > (int)h->Tiberium)
                take = (int)h->Tiberium;
            h->Spend_Money((unsigned)take);
        }
        return (int)h->Tiberium;
    }
    return -1;
}

/* Tactical coordinates use the same 11-int renderer contract as the TD brain.
   Keep the conversion in this brain so input follows Red Alert's own viewport. */
extern "C" __declspec(dllexport) void __cdecl OpenCNC3D_Get_View(int* out)
{
    if (out == NULL)
        return;
    out[0] = Map.TacPixelX;
    out[1] = Map.TacPixelY;
    out[2] = Map.TacLeptonWidth;
    out[3] = Map.TacLeptonHeight;
    out[4] = (int)Coord_X(Map.TacticalCoord);
    out[5] = (int)Coord_Y(Map.TacticalCoord);
    out[6] = Map.MapCellX;
    out[7] = Map.MapCellY;
    out[8] = Map.MapCellWidth;
    out[9] = Map.MapCellHeight;
    out[10] = DLLExportClass::Legacy_Render_Enabled() ? 1 : 0;
}

extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Cell_To_Pixel(int cell_x, int cell_y, int* px, int* py)
{
    int lx = Cell_To_Lepton(cell_x) + CELL_LEPTON_W / 2 - (int)Coord_X(Map.TacticalCoord);
    int ly = Cell_To_Lepton(cell_y) + CELL_LEPTON_H / 2 - (int)Coord_Y(Map.TacticalCoord);
    int x = Lepton_To_Pixel(lx) + Map.TacPixelX;
    int y = Lepton_To_Pixel(ly) + Map.TacPixelY;
    if (px != NULL)
        *px = x;
    if (py != NULL)
        *py = y;
    COORDINATE coord = Map.Pixel_To_Coord(x, y);
    return coord != 0 && Coord_Cell(coord) == XY_Cell(cell_x, cell_y);
}

/* Selection reports use Red Alert's active player context, including vessels. */
extern "C" __declspec(dllexport) int __cdecl OpenCNC3D_Dump_Selection(void)
{
    printf("SEL-BEGIN count=%d frame=%d\n", CurrentObject.Count(), (int)Frame);
    for (int i = 0; i < CurrentObject.Count(); ++i) {
        ObjectClass* obj = CurrentObject[i];
        if (obj == NULL)
            continue;
        const char* kind = "OTHER";
        int id = -1;
        switch (obj->What_Am_I()) {
        case RTTI_INFANTRY: kind = "INFANTRY"; id = Infantry.ID((InfantryClass*)obj); break;
        case RTTI_UNIT: kind = "UNIT"; id = Units.ID((UnitClass*)obj); break;
        case RTTI_VESSEL: kind = "VESSEL"; id = Vessels.ID((VesselClass*)obj); break;
        case RTTI_AIRCRAFT: kind = "AIRCRAFT"; id = Aircraft.ID((AircraftClass*)obj); break;
        case RTTI_BUILDING: kind = "BUILDING"; id = Buildings.ID((BuildingClass*)obj); break;
        default: break;
        }
        int nav = -1, tar = -1;
        const char* mission = "-";
        if (obj->Is_Foot()) {
            FootClass* foot = (FootClass*)obj;
            if (Target_Legal(foot->NavCom))
                nav = (int)::As_Cell(foot->NavCom);
        }
        if (obj->Is_Techno()) {
            TechnoClass* techno = (TechnoClass*)obj;
            if (Target_Legal(techno->TarCom))
                tar = (int)::As_Cell(techno->TarCom);
            mission = MissionClass::Mission_Name(techno->Get_Mission());
        }
        CELL cell = Coord_Cell(obj->Coord);
        printf("SEL|%s|%s|id=%d|cell=%d|x=%d|y=%d|mission=%s|navcell=%d|tarcell=%d\n",
               kind, obj->Class_Of().IniName, id, (int)cell, Cell_X(cell), Cell_Y(cell),
               mission ? mission : "-", nav, tar);
    }
    printf("SEL-END\n");
    fflush(stdout);
    return CurrentObject.Count();
}

extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Sel_Info(int index, int* out)
{
    if (out == NULL || index < 0 || index >= CurrentObject.Count())
        return false;
    ObjectClass* obj = CurrentObject[index];
    if (obj == NULL)
        return false;
    CELL cell = Coord_Cell(obj->Coord);
    out[0] = (int)cell;
    out[1] = Cell_X(cell);
    out[2] = Cell_Y(cell);
    out[3] = (int)Coord_X(obj->Coord);
    out[4] = (int)Coord_Y(obj->Coord);
    out[5] = obj->Is_Techno() ? (int)((TechnoClass*)obj)->Get_Mission() : -1;
    out[6] = -1;
    out[7] = -1;
    if (obj->Is_Foot()) {
        FootClass* foot = (FootClass*)obj;
        if (Target_Legal(foot->NavCom))
            out[6] = (int)::As_Cell(foot->NavCom);
    }
    if (obj->Is_Techno()) {
        TechnoClass* techno = (TechnoClass*)obj;
        if (Target_Legal(techno->TarCom))
            out[7] = (int)::As_Cell(techno->TarCom);
    }
    return true;
}

extern "C" __declspec(dllexport) const char* __cdecl OpenCNC3D_Mission_Name(int mission)
{
    return MissionClass::Mission_Name((MissionType)mission);
}

/* Ask Red Alert's own hit test and action rules what the pointer would do here.
   The query leaves selection and orders untouched. */
extern "C" __declspec(dllexport) int __cdecl OpenCNC3D_Probe_Object_At(int x, int y)
{
    COORDINATE coord = Map.Pixel_To_Coord(x, y);
    if (coord == 0) {
        printf("PROBE|px=%d,%d|REJECTED (outside tactical view)\n", x, y);
        fflush(stdout);
        return -1;
    }
    CELL cell = Coord_Cell(coord);
    ObjectClass* object = Map.Close_Object(coord);
    ActionType action = ACTION_NONE;
    if (CurrentObject.Count() != 0) {
        action = object != NULL ? Best_Object_Action(object) : Best_Object_Action(cell);
    } else if (Map.IsTargettingMode == SPC_CHRONO2) {
        /*
        ** The Chronosphere's destination, by the rule of DisplayClass::Mouse_Left_Up
        ** (display.cpp): refused over ground the player has not mapped, and where the unit
        ** chosen by the first click could not be teleported (Can_Teleport_Here: never an
        ** infantryman, never off the playfield or onto ground it could not enter). The
        ** placement itself checks none of this, so the front end must not send a refused
        ** click. A query only: the mode is left as it is.
        */
        action = ACTION_CHRONO2;
        if (!Map[cell].Is_Mapped(PlayerPtr) && !Debug_Unshroud)
            action = ACTION_NOMOVE;
        ObjectClass const* tobject = As_Object(PlayerPtr->UnitToTeleport);
        if (tobject == NULL || !tobject->Is_Techno() || !((TechnoClass const*)tobject)->Can_Teleport_Here(cell))
            action = ACTION_NOMOVE;
    }
    printf("PROBE|px=%d,%d|coord=%d,%d|cell=%d|cx=%d|cy=%d|obj=%s|action=%d\n",
           x, y, (int)Coord_X(coord), (int)Coord_Y(coord), (int)cell,
           Cell_X(cell), Cell_Y(cell), object ? object->Class_Of().IniName : "(none)",
           (int)action);
    fflush(stdout);
    return (int)action;
}

/* Use Red Alert's own house verdict flags. The engine finishes the mission on
   its next simulation tick and emits the normal game-over event with its score
   and movie list; the caller does not manufacture a result. Player ID zero is
   the campaign. Multiplayer IDs are resolved through the same session table
   used for the other exported player actions. */
extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Force_Verdict(uint64 player_id, bool win)
{
    HouseClass* house = NULL;
    if (GAME_TO_PLAY == GAME_NORMAL) {
        if (player_id != 0)
            return false;
        house = PlayerPtr;
    } else {
        for (int i = 0; i < MULTIPLAYER_COUNT; ++i) {
            if (DLLExportClass::GlyphxPlayerIDs[i] == player_id && Session.Players[i] != NULL) {
                house = HouseClass::As_Pointer(Session.Players[i]->Player.ID);
                break;
            }
        }
    }
    if (house == NULL)
        return false;
    return win ? house->Flag_To_Win() : house->Flag_To_Lose();
}

/**************************************************************************************************
 * THE OPENCNC3D EXPORTS THE TIBERIAN DAWN BRAIN CARRIES, FOR RED ALERT (project OpenCNC3D).
 *
 * Same names, same signatures and the same meaning as the Tiberian Dawn brain's, so one renderer
 * and one app drive either brain through one set of calls. Where Red Alert's own rules differ
 * the difference is said beside the export. Nothing below runs unless a host calls it.
 **************************************************************************************************/

/*
**	Which house does a GlyphX player id own? This repeats the lookup inside
**	DLLExportClass::Set_Player_Context rather than calling it, on purpose: Set_Player_Context
**	MOVES the engine's local player and leaves it moved, which is wrong for a cheat toggle, a
**	takeover or a query that must be able to ask about a computer's house. GAME_NORMAL has
**	exactly one local house and does not consult the id at all. HOUSE_NONE when the id is in
**	neither.
*/
static HousesType OpenCNC3D_House_Of_Player(uint64 player_id)
{
    if (GAME_TO_PLAY == GAME_NORMAL) {
        return (PlayerPtr != NULL) ? PlayerPtr->Class->House : HOUSE_NONE;
    }
    for (int i = 0; i < MULTIPLAYER_COUNT; i++) {
        if (DLLExportClass::GlyphxPlayerIDs[i] == player_id && Session.Players[i] != NULL) {
            return Session.Players[i]->Player.ID;
        }
    }
    return HOUSE_NONE;
}

/**************************************************************************************************
 * OpenCNC3D_Event_ABI -- fingerprint the order wire, so a mismatched peer is refused at the door
 *
 * EventClass IS the wire unit: lockstep copies it whole. Its size alone cannot say whether two
 * builds lay it out the same way, so the fingerprint hashes EventLength[], which is computed per
 * type from the real structure. Red Alert's order wire is not Tiberian Dawn's (other event types,
 * other arms, other bit widths), so the fingerprint of one brain never matches the other's and a
 * Red Alert peer and a Tiberian Dawn peer refuse each other before a tick runs. The slots are the
 * Tiberian Dawn brain's, in the same order; slot 3 and 4 carry Red Alert's own Frame and ID widths.
 **************************************************************************************************/
extern "C" __declspec(dllexport) int __cdecl OpenCNC3D_Event_ABI(unsigned int* out, int count)
{
    unsigned int slot[OPENCNC3D_EVENT_ABI_SLOTS];
    int i;

    unsigned int hash = 2166136261u;
    for (i = 0; i < (int)EventClass::LAST_EVENT; i++) {
        hash ^= (unsigned int)EventClass::EventLength[i];
        hash *= 16777619u;
    }
    /* The game is part of the fingerprint too, so a coincidence of tables between the two
       brains could still never pair them. */
    hash ^= 0x52413936u; /* "RA96" */
    hash *= 16777619u;

    slot[0] = 1;                                        /* fingerprint format version */
    slot[1] = (unsigned int)sizeof(EventClass);
    slot[2] = (unsigned int)offsetof(EventClass, Data); /* where the payload starts */
    slot[3] = 26;                                       /* EventClass::Frame bit width */
    slot[4] = 5;                                        /* EventClass::ID bit width */
    slot[5] = 1;                                        /* EventClass::IsExecuted bit width */
    slot[6] = (unsigned int)sizeof(COORDINATE);
    slot[7] = (unsigned int)sizeof(CELL);
    slot[8] = (unsigned int)sizeof(TARGET);
    slot[9] = (unsigned int)EventClass::LAST_EVENT;     /* number of event types */
    slot[10] = hash;                                    /* the deciding value */

    for (i = 0; out != NULL && i < count && i < OPENCNC3D_EVENT_ABI_SLOTS; i++) {
        out[i] = slot[i];
    }
    return (OPENCNC3D_EVENT_ABI_SLOTS);
}

/**************************************************************************************************
 * OpenCNC3D_Drain_Events -- take this peer's pending orders out for the wire.
 *
 * Copies up to `max` events out of OutList and REMOVES them, stamping each with the frame it is
 * to execute on (now plus `frame_delay`) and the originating house. Removing the local copy is
 * correct ONLY because the host sends these bytes to every peer INCLUDING ITSELF and posts them
 * back through OpenCNC3D_Post_Event; so the drain refuses (-1) unless lockstep is on, rather than
 * handing a host that never asked for lockstep an empty queue that reads as "no orders".
 *
 * Returns the number written, -1 if lockstep is off, -2 on a bad argument.
 **************************************************************************************************/
extern "C" __declspec(dllexport) int __cdecl OpenCNC3D_Drain_Events(void* out, int max, int frame_delay)
{
    if (!OpenCNC3D_Lockstep) {
        return (-1);
    }
    if (out == NULL || max <= 0 || frame_delay < 0 || PlayerPtr == NULL) {
        return (-2);
    }

    unsigned char* dest = (unsigned char*)out;
    int written = 0;

    while (OutList.Count && written < max) {
        OutList.First().Frame = (unsigned)Frame + (unsigned)frame_delay;
        OutList.First().ID = PlayerPtr->ID;
        OutList.First().IsExecuted = 0;
        memcpy(dest + ((size_t)written * sizeof(EventClass)), &OutList.First(), sizeof(EventClass));
        OutList.Next();
        written++;
    }

    return (written);
}

/**************************************************************************************************
 * OpenCNC3D_Post_Event -- inject a received order at its stamped frame.
 *
 * Every peer posts every peer's orders, its own included, so all of them put the same set into
 * DoList and execute it at the same frame. These bytes came off a wire, so they are checked: an
 * event stamped for a frame already executed can never run, an unknown event type or a house
 * index outside the house table would index past the end of a table inside Execute.
 *
 * Returns 1 on success, 0 if DoList is full, -1 if lockstep is off, -2 on a bad argument or an
 * unknown event type, -3 if the frame has already passed, -4 if the house index is out of range.
 **************************************************************************************************/
extern "C" __declspec(dllexport) int __cdecl OpenCNC3D_Post_Event(const void* in)
{
    if (!OpenCNC3D_Lockstep) {
        return (-1);
    }
    if (in == NULL) {
        return (-2);
    }

    EventClass event;
    memcpy(&event, in, sizeof(EventClass));

    if ((int)event.Type < 0 || (int)event.Type >= (int)EventClass::LAST_EVENT) {
        return (-2);
    }
    if ((unsigned)event.Frame < (unsigned)Frame) {
        return (-3);
    }
    if ((int)event.ID >= (int)HOUSE_COUNT) {
        return (-4);
    }

    event.IsExecuted = 0;
    if (!DoList.Add(event)) {
        return (0);
    }
    return (1);
}

/**************************************************************************************************
 * OpenCNC3D_Set_Lockstep -- put this brain into deterministic lockstep mode.
 *
 * False until a host calls this, so the campaign and the skirmish take the 1996 path and nothing
 * behind the flag can move them. One global rather than a per-player flag: lockstep is a
 * property of the whole simulation.
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl OpenCNC3D_Set_Lockstep(bool on)
{
    OpenCNC3D_Lockstep = on;
}

extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Get_Lockstep(void)
{
    return OpenCNC3D_Lockstep;
}

/**************************************************************************************************
 * OpenCNC3D_Set_Seed -- the seed the next scenario start uses (Red Alert only).
 *
 * Red Alert reseeds its synchronised stream at every scenario start, from the wall clock unless
 * a seed was given. This is that seed, the same variable the -SEED= command line option writes.
 * Zero restores the 1996 behaviour (the clock) outside a match; in a lockstep match zero is used
 * as the seed itself, so peers that agreed on nothing still agree. A host that wants a skirmish
 * it can repeat, or a match whose peers agreed a seed, sets it before the start.
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl OpenCNC3D_Set_Seed(int seed)
{
    CustomSeed = seed;
}

/**************************************************************************************************
 * OpenCNC3D_Get_Build_Potential -- what this player's tree could eventually offer.
 *
 * Closes the potential prerequisite tree through the SAME legality test production uses
 * (HouseClass::Can_Build_With_Prerequisites), so nothing the mission's rules forbid is ever
 * listed. Red Alert's sides are countries rather than two factions, so the producer sets are
 * masks of countries: the house's own country always (a missing own Construction Yard is
 * replaceable), plus the country each owned factory acts like (a captured factory builds what
 * its original owner could). Red Alert's Construction Yard, the prerequisite of every other
 * building, counts once the house owns an MCV or the tree can build one. Buildings an engineer
 * could capture count once one is owned or can be trained. No game state is changed.
 *
 * Returns the number of entries (written up to `capacity`), or -1 when the player is unknown.
 **************************************************************************************************/
static int OpenCNC3D_Production_Category(RTTIType type)
{
    switch (type) {
    case RTTI_BUILDINGTYPE:
        return 0;
    case RTTI_INFANTRYTYPE:
        return 1;
    case RTTI_UNITTYPE:
        return 2;
    case RTTI_AIRCRAFTTYPE:
        return 3;
    case RTTI_VESSELTYPE:
        return 4;
    default:
        return -1;
    }
}

static bool OpenCNC3D_Any_Can_Build(HouseClass const* house,
                                    ObjectTypeClass const* type,
                                    unsigned int countries,
                                    unsigned int prerequisites)
{
    for (int c = HOUSE_FIRST; c < HOUSE_COUNT && c < 32; c++) {
        if ((countries & (1u << c)) && house->Can_Build_With_Prerequisites(type, (HousesType)c, prerequisites)) {
            return true;
        }
    }
    return false;
}

extern "C" __declspec(dllexport) int __cdecl OpenCNC3D_Get_Build_Potential(uint64 player_id,
                                                                           OpenCNC3D_BuildPotentialEntry* entries,
                                                                           int capacity)
{
    const HousesType owner = OpenCNC3D_House_Of_Player(player_id);
    HouseClass* house = owner != HOUSE_NONE ? HouseClass::As_Pointer(owner) : NULL;
    if (house == NULL || capacity < 0 || (capacity && entries == NULL)) {
        return -1;
    }

    unsigned int producers[5] = {0, 0, 0, 0, 0};
    if (house->ActLike >= HOUSE_FIRST && house->ActLike < 32) {
        producers[0] = 1u << house->ActLike;
    }
    unsigned int prerequisites = house->ActiveBScan;
    for (int i = 0; i < Buildings.Count(); ++i) {
        BuildingClass const* b = Buildings.Ptr(i);
        if (b == NULL || b->Owner() != owner || b->IsInLimbo || b->Mission == MISSION_DECONSTRUCTION) {
            continue;
        }
        const int category = OpenCNC3D_Production_Category(b->Class->ToBuild);
        if (category >= 0 && b->ActLike >= HOUSE_FIRST && b->ActLike < 32) {
            producers[category] |= 1u << b->ActLike;
        }
    }
    /* Red Alert puts its Construction Yard in front of every other building (FACT is the
       prerequisite of the whole tree), and the yard comes from an MCV rather than from a
       factory, so an MCV the house owns is the yard it will have. */
    for (int i = 0; i < Units.Count(); ++i) {
        UnitClass const* u = Units.Ptr(i);
        if (u != NULL && u->Owner() == owner && !u->IsInLimbo && u->Class->Type == UNIT_MCV) {
            prerequisites |= STRUCTF_CONST;
        }
    }

    bool changed;
    do {
        changed = false;
        /* Every building prerequisite bit is in the first 32 type slots (STRUCT_TESLA is 31);
           the fakes and civilian buildings past them are never prerequisites. */
        for (int i = STRUCT_FIRST; i < STRUCT_COUNT && i < 32; ++i) {
            BuildingTypeClass const& type = BuildingTypeClass::As_Reference((StructType)i);
            for (int c = HOUSE_FIRST; c < HOUSE_COUNT && c < 32; c++) {
                if (!(producers[0] & (1u << c))
                    || !house->Can_Build_With_Prerequisites(&type, (HousesType)c, prerequisites)) {
                    continue;
                }
                const unsigned int before = prerequisites;
                prerequisites |= 1u << i;
                changed = changed || before != prerequisites;
                const int category = OpenCNC3D_Production_Category(type.ToBuild);
                if (category >= 0) {
                    /* A new building acts like the house that builds it. */
                    const unsigned int was = producers[category];
                    if (house->ActLike >= HOUSE_FIRST && house->ActLike < 32) {
                        producers[category] |= 1u << house->ActLike;
                    }
                    changed = changed || was != producers[category];
                }
            }
        }
        bool can_capture = false;
        for (int i = 0; i < Infantry.Count(); ++i) {
            InfantryClass const* inf = Infantry.Ptr(i);
            if (inf && inf->Owner() == owner && !inf->IsInLimbo && inf->Class->Type == INFANTRY_RENOVATOR) {
                can_capture = true;
            }
        }
        if (OpenCNC3D_Any_Can_Build(
                house, &InfantryTypeClass::As_Reference(INFANTRY_RENOVATOR), producers[1], prerequisites)) {
            can_capture = true;
        }
        /* An MCV the tree can build is a Construction Yard the house can have. */
        if (!(prerequisites & STRUCTF_CONST)
            && OpenCNC3D_Any_Can_Build(house, &UnitTypeClass::As_Reference(UNIT_MCV), producers[2], prerequisites)) {
            prerequisites |= STRUCTF_CONST;
            changed = true;
        }
        for (int i = 0; can_capture && i < Buildings.Count(); ++i) {
            BuildingClass const* b = Buildings.Ptr(i);
            if (b == NULL || b->IsInLimbo || b->Mission == MISSION_DECONSTRUCTION || !b->Class->IsCaptureable) {
                continue;
            }
            const int type = (int)b->Class->Type;
            if (type >= 0 && type < 32) {
                const unsigned int before = prerequisites;
                prerequisites |= 1u << type;
                changed = changed || before != prerequisites;
            }
        }
    } while (changed);

    const RTTIType kinds[5] = {RTTI_BUILDINGTYPE, RTTI_INFANTRYTYPE, RTTI_UNITTYPE, RTTI_AIRCRAFTTYPE, RTTI_VESSELTYPE};
    const int counts[5] = {STRUCT_COUNT, INFANTRY_COUNT, UNIT_COUNT, AIRCRAFT_COUNT, VESSEL_COUNT};
    int count = 0;
    for (int kind = 0; kind < 5; ++kind) {
        for (int id = 0; id < counts[kind]; ++id) {
            TechnoTypeClass const* type = Fetch_Techno_Type(kinds[kind], id);
            if (type == NULL || !OpenCNC3D_Any_Can_Build(house, type, producers[kind], prerequisites)) {
                continue;
            }
            if (count < capacity) {
                memset(&entries[count], 0, sizeof entries[count]);
                strncpy(entries[count].name, type->IniName, sizeof entries[count].name - 1);
            }
            ++count;
        }
    }
    return count;
}

/**************************************************************************************************
 * OpenCNC3D_Player_Blowup -- destroys one player's house, the way a disconnect does.
 *
 * Flag_To_Die is the multiplayer-aware ending: a moment later HouseClass::AI blows the house up
 * and reports it defeated on every player's screen, on the same turn on every machine because
 * every machine calls this from the same turn. Returns whatever the flag returns: false when the
 * house is already winning, losing or dying.
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Player_Blowup(uint64 player_id)
{
    HousesType house = OpenCNC3D_House_Of_Player(player_id);
    HouseClass* hptr = (house != HOUSE_NONE) ? HouseClass::As_Pointer(house) : NULL;
    if (hptr == NULL) {
        return false;
    }
    return hptr->Flag_To_Die();
}

/**************************************************************************************************
 * OpenCNC3D_Player_Is_AI -- 1 for the computer, 0 for a person, -1 when the id names nobody.
 **************************************************************************************************/
extern "C" __declspec(dllexport) int __cdecl OpenCNC3D_Player_Is_AI(uint64 player_id)
{
    HousesType house = OpenCNC3D_House_Of_Player(player_id);
    HouseClass* hptr = (house != HOUSE_NONE) ? HouseClass::As_Pointer(house) : NULL;
    if (hptr == NULL) {
        return -1;
    }
    return hptr->IsHuman ? 0 : 1;
}

/**************************************************************************************************
 * OpenCNC3D_Player_To_AI -- hands one player's house to the computer.
 *
 * When somebody leaves a match and the room's AI TAKEOVER option is on, their base carries on
 * under Red Alert's own expert system. This is the #else arm of CNC_Handle_Player_Switch_To_AI,
 * with the house NAMED rather than made current, because every peer applies it to the same house
 * on the same frame and Set_Player_Context is per-machine state. WasHuman keeps the score screen
 * reporting the person who started the match. An MCV is told to deploy, as the original does.
 *
 * Returns false when the id names nobody, or the house is already the computer's.
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Player_To_AI(uint64 player_id)
{
    HousesType house = OpenCNC3D_House_Of_Player(player_id);
    HouseClass* hptr = (house != HOUSE_NONE) ? HouseClass::As_Pointer(house) : NULL;
    if (hptr == NULL || !hptr->IsHuman) {
        return false;
    }

    hptr->WasHuman = true;
    hptr->IsHuman = false;
    hptr->IsPlayerControl = false;
    hptr->IsStarted = true;
    hptr->IQ = Rule.MaxIQ;
    hptr->IsBaseBuilding = true;
    FairAI_RA_Assign(hptr, -1);

    for (int index = 0; index < Units.Count(); index++) {
        UnitClass* obj = Units.Ptr(index);
        if (obj && !obj->IsInLimbo && obj->House == hptr && *obj == UNIT_MCV) {
            obj->Assign_Mission(MISSION_GUARD);
            obj->Assign_Target(TARGET_NONE);
            obj->Assign_Destination(TARGET_NONE);
            obj->Assign_Mission(MISSION_UNLOAD);
            obj->Commence();
        }
    }
    return true;
}

/**************************************************************************************************
 * OpenCNC3D_Set_Invincible / OpenCNC3D_Get_Invincible -- damage immunity for one player, for the
 * cheat menu. The enemy still targets and fires; ordinary damage simply does not reduce
 * strength (ObjectClass::Take_Damage). Cleared with the scenario. false when the id names nobody.
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Set_Invincible(uint64 player_id, bool on)
{
    HousesType house = OpenCNC3D_House_Of_Player(player_id);
    if (house == HOUSE_NONE) {
        return false;
    }
    ObjectClass::OpenCNC3D_Set_Invincible(house, on);
    return true;
}

extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Get_Invincible(uint64 player_id)
{
    HousesType house = OpenCNC3D_House_Of_Player(player_id);
    if (house == HOUSE_NONE || house >= HOUSE_COUNT) {
        return false;
    }
    return ((ObjectClass::OpenCNC3D_InvincibleHouses & (1u << house)) != 0);
}

/**************************************************************************************************
 * OpenCNC3D_Set_Build_Anywhere / OpenCNC3D_Get_Build_Anywhere -- lifts the base adjacency rule for
 * one player, for the cheat menu. The ground must still take a foundation: rivers, rock and
 * cliffs refuse one exactly as before. Cleared with the scenario.
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Set_Build_Anywhere(uint64 player_id, bool on)
{
    HousesType house = OpenCNC3D_House_Of_Player(player_id);
    if (house == HOUSE_NONE) {
        return false;
    }
    DisplayClass::OpenCNC3D_Set_Build_Anywhere(house, on);
    return true;
}

extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Get_Build_Anywhere(uint64 player_id)
{
    return DisplayClass::OpenCNC3D_Build_Anywhere(OpenCNC3D_House_Of_Player(player_id));
}

/**************************************************************************************************
 * OpenCNC3D_Set_Wall_Fill / OpenCNC3D_Get_Wall_Fill -- how far a placed wall reaches to join its
 * own kind (BuildingClass::Unlimbo). 0 is the 1996 rule. Clamped to 16. A lockstep match must set
 * it the same on every peer before the first tick.
 **************************************************************************************************/
extern "C" __declspec(dllexport) void __cdecl OpenCNC3D_Set_Wall_Fill(int reach)
{
    OpenCNC3D_WallFill = (reach < 0) ? 0 : ((reach > 16) ? 16 : reach);
}

extern "C" __declspec(dllexport) int __cdecl OpenCNC3D_Get_Wall_Fill(void)
{
    return OpenCNC3D_WallFill;
}

/**************************************************************************************************
 * OpenCNC3D_Proximity_Ok -- would this building type pass the proximity check on this cell?
 *
 * A pure query of the routine the placement cursor answers, DisplayClass::Passes_Proximity_Check,
 * with the type's own occupy list. In Red Alert that is also the only adjacency rule a placement
 * meets: HouseClass::Place_Object checks the ground, not the base. False for an unknown type
 * name or an unknown player.
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Proximity_Ok(uint64 player_id,
                                                                     const char* type_name,
                                                                     int cell_x,
                                                                     int cell_y)
{
    HousesType house = OpenCNC3D_House_Of_Player(player_id);
    if (house == HOUSE_NONE || type_name == NULL) {
        return false;
    }
    StructType st = BuildingTypeClass::From_Name(type_name);
    if (st == STRUCT_NONE) {
        return false;
    }
    BuildingTypeClass const& btype = BuildingTypeClass::As_Reference(st);
    return Map.Passes_Proximity_Check(&btype, house, btype.Occupy_List(true), XY_Cell(cell_x, cell_y));
}

/**************************************************************************************************
 * OpenCNC3D_Grant_Superweapons -- gives one player Red Alert's super weapons, ready now, for the
 * cheat menu.
 *
 * Granted ONE-TIME, which is what keeps them: HouseClass::AI removes a repeating super weapon
 * whose building the house does not own, and its unforced Remove spares a one-time one. A
 * one-time nuclear missile in Red Alert also needs no silo to launch from; it comes in from the
 * north. The host re-asserts this every tick, so a spent weapon is granted straight back and the
 * recharge is defeated, which is what the cheat promises; Enable refuses a weapon already present
 * and the sidebar add is made only when Enable granted, so nothing is duplicated. The GPS
 * satellite and the sonar pulse are left out: the satellite fires itself the moment it is ready
 * and the sonar pulse belongs to a spied enemy sub pen. Turning the cheat off removes them.
 **************************************************************************************************/
static void OpenCNC3D_Add_Special(HouseClass* hptr, SpecialWeaponType spc)
{
    if (GAME_TO_PLAY == GAME_GLYPHX_MULTIPLAYER) {
        if (hptr->IsHuman) {
            Sidebar_Glyphx_Add(RTTI_SPECIAL, spc, hptr);
        }
    } else if (hptr == PlayerPtr) {
        Map.Add(RTTI_SPECIAL, spc);
        Map.Column[1].Flag_To_Redraw();
    }
}

extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Grant_Superweapons(uint64 player_id, bool on)
{
    static SpecialWeaponType const _granted[] = {SPC_NUCLEAR_BOMB,
                                                 SPC_CHRONOSPHERE,
                                                 SPC_IRON_CURTAIN,
                                                 SPC_PARA_BOMB,
                                                 SPC_PARA_INFANTRY,
                                                 SPC_SPY_MISSION};
    HousesType house = OpenCNC3D_House_Of_Player(player_id);
    HouseClass* hptr = (house != HOUSE_NONE) ? HouseClass::As_Pointer(house) : NULL;
    if (hptr == NULL) {
        return false;
    }

    bool changed = false;
    for (unsigned i = 0; i < sizeof(_granted) / sizeof(_granted[0]); i++) {
        SuperClass& super = hptr->SuperWeapon[_granted[i]];
        if (on) {
            if (super.Enable(true, hptr == PlayerPtr, true)) {
                changed = true;
                OpenCNC3D_Add_Special(hptr, _granted[i]);
            }
            super.Forced_Charge(hptr == PlayerPtr);
        } else {
            changed |= super.Remove(true);
        }
    }

    if (changed) {
        if (hptr == PlayerPtr) {
            Map.Column[1].Flag_To_Redraw();
        }
        hptr->IsRecalcNeeded = true;
    }
    return true;
}

/**************************************************************************************************
 * OpenCNC3D_Spring_Trigger -- fires one named trigger's actions on demand.
 *
 * Red Alert's triggers already have the forced spring this needs: TriggerClass::Spring with
 * `forced` performs the actions whatever the events say and uses the trigger's own cell, and its
 * persistence rules are the engine's (a volatile trigger removes itself after it fires). The
 * first live trigger of that name is sprung. False when no live trigger has the name.
 **************************************************************************************************/
static TriggerClass* OpenCNC3D_Trigger_By_Name(const char* name)
{
    if (name == NULL || *name == '\0') {
        return NULL;
    }
    for (int i = 0; i < Triggers.Count(); i++) {
        TriggerClass* trig = Triggers.Ptr(i);
        if (trig != NULL && trig->IsActive && trig->Class.Is_Valid() && stricmp(trig->Class->Name(), name) == 0) {
            return trig;
        }
    }
    return NULL;
}

extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Spring_Trigger(const char* name)
{
    TriggerClass* trig = OpenCNC3D_Trigger_By_Name(name);
    if (trig == NULL) {
        return false;
    }
    return trig->Spring(TEVENT_ANY, NULL, 0, true);
}

/**************************************************************************************************
 * OpenCNC3D_Trigger_Info -- reads one trigger's live state: its first event and first action
 * (Red Alert's triggers carry two of each), the first event's data and its persistence. Any
 * out-parameter may be NULL. True if a live trigger has the name.
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Trigger_Info(const char* name,
                                                                     int* event,
                                                                     int* action,
                                                                     int* data,
                                                                     int* persist)
{
    TriggerClass* trig = OpenCNC3D_Trigger_By_Name(name);
    if (trig == NULL) {
        return false;
    }
    if (event != NULL) {
        *event = (int)trig->Class->Event1.Event;
    }
    if (action != NULL) {
        *action = (int)trig->Class->Action1.Action;
    }
    if (data != NULL) {
        *data = trig->Class->Event1.Data.Value;
    }
    if (persist != NULL) {
        *persist = (int)trig->Class->IsPersistant;
    }
    return true;
}

/**************************************************************************************************
 * OpenCNC3D_Force_Fire -- makes one vehicle fire one of its weapons now. A test hook for the
 * effect recordings: TechnoClass's own Fire_At, at the vehicle's target or the nearest enemy,
 * whatever the range or the facing. Nothing in a normal game calls this.
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Force_Fire(int heapid, int which)
{
    if (heapid < 0 || heapid >= Units.Length()) {
        return false;
    }
    UnitClass* u = Units.Raw_Ptr(heapid);
    if (u == NULL || !u->IsActive || u->IsInLimbo) {
        return false;
    }
    TARGET target = u->TarCom;
    if (!Target_Legal(target)) {
        TechnoClass* best = NULL;
        int bestd = 0x7fffffff;
        for (int i = 0; i < Logic.Count(); i++) {
            ObjectClass* o = Logic[i];
            if (o == NULL || !o->Is_Techno() || o->IsInLimbo || o == u) {
                continue;
            }
            TechnoClass* t2 = (TechnoClass*)o;
            if (u->House->Is_Ally(t2->House)) {
                continue;
            }
            int d = u->Distance(t2);
            if (d < bestd) {
                bestd = d;
                best = t2;
            }
        }
        if (best == NULL) {
            return false;
        }
        target = best->As_Target();
    }
    return ((TechnoClass*)u)->TechnoClass::Fire_At(target, which) != NULL;
}

/**************************************************************************************************
 * OpenCNC3D_Building_By_Heapid -- resolves the id the object dump hands out (Buildings.ID, the
 * RAW slot in the pool) by walking the ACTIVE list, so only live objects are dereferenced. The
 * active index and the raw id part company the moment a building leaves the game.
 **************************************************************************************************/
static BuildingClass* OpenCNC3D_Building_By_Heapid(int heapid)
{
    if (heapid < 0) {
        return NULL;
    }
    for (int index = 0; index < Buildings.Count(); index++) {
        BuildingClass* p = Buildings.Ptr(index);
        if (p != NULL && Buildings.ID(p) == heapid) {
            return p;
        }
    }
    return NULL;
}

/**************************************************************************************************
 * OpenCNC3D_Set_Rally -- sets or clears a factory's rally point.
 *
 * Posts the SAME ARCHIVE event a human click posts (BuildingClass::Active_Click_With), so there is
 * one way a rally point is ever set, and under lockstep it crosses the wire like any order. A
 * CELL, never an object target: nothing clears a building's ArchiveTarget when its target dies.
 * cell_x < 0 clears it. The house is resolved without moving the engine's local player. False
 * when the player, the building or the cell does not resolve, in which case nothing was posted.
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Set_Rally(uint64 player_id,
                                                                  int building_id,
                                                                  int cell_x,
                                                                  int cell_y)
{
    HousesType house = OpenCNC3D_House_Of_Player(player_id);
    if (house == HOUSE_NONE) {
        return false;
    }
    BuildingClass* b = OpenCNC3D_Building_By_Heapid(building_id);
    if (b == NULL || !b->House.Is_Valid() || b->House->Class->House != house) {
        return false;
    }
    if (!b->OpenCNC3D_Can_Rally()) {
        return false;
    }
    if (cell_x < 0) {
        OutList.Add(EventClass(EventClass::ARCHIVE, TargetClass(b), TargetClass(TARGET_NONE)));
        return true;
    }
    if (cell_x >= MAP_CELL_W || cell_y < 0 || cell_y >= MAP_CELL_H) {
        return false;
    }
    OutList.Add(EventClass(EventClass::ARCHIVE, TargetClass(b), TargetClass(::As_Target(XY_Cell(cell_x, cell_y)))));
    return true;
}

/**************************************************************************************************
 * OpenCNC3D_Get_Rally -- reads a factory's rally cell back, or -1,-1. A pure query.
 **************************************************************************************************/
extern "C" __declspec(dllexport) bool __cdecl OpenCNC3D_Get_Rally(int building_id, int* cell_x, int* cell_y)
{
    if (cell_x) {
        *cell_x = -1;
    }
    if (cell_y) {
        *cell_y = -1;
    }
    BuildingClass* b = OpenCNC3D_Building_By_Heapid(building_id);
    if (b == NULL || !Target_Legal(b->ArchiveTarget) || !Is_Target_Cell(b->ArchiveTarget)) {
        return false;
    }
    CELL cell = ::As_Cell(b->ArchiveTarget);
    if (cell_x) {
        *cell_x = Cell_X(cell);
    }
    if (cell_y) {
        *cell_y = Cell_Y(cell);
    }
    return true;
}

/* Fair per-roster-seat skill ABI. Call before Set_Multiplayer_Scenario. */
extern "C" __declspec(dllexport) void __cdecl OpenCNC3D_Set_AI_Difficulties(const unsigned char* levels, int count)
{ FairAI_RA_Set_Levels(levels, count); }
extern "C" __declspec(dllexport) unsigned int __cdecl OpenCNC3D_AI_Policy(void)
{ return fair_ai::PolicyHash(); }
extern "C" __declspec(dllexport) unsigned int __cdecl OpenCNC3D_AI_State_Hash(void)
{ return FairAI_RA_State_Hash(); }
extern "C" __declspec(dllexport) int __cdecl OpenCNC3D_AI_Diagnostics(char* out, int size)
{ return FairAI_RA_Diagnostics(out, size); }
