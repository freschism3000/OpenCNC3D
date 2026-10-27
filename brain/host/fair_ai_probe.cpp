/* Headless TD/RA fair-AI integration probe. Compile once per game's ABI header.
 * Usage: probe dylib scenario-dir content-dir scenario level ticks seed [bots] [save-tick]
 * level -1 gives successive AI seats Easy/Normal/Hard/Expert; otherwise 0..3.
 * All output/state is written to the caller's scratch directory; no game window.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <dlfcn.h>
#include <unistd.h>

#define _MAX_FNAME 256
#define _MAX_EXT 256
#define __declspec(x)
#define __cdecl
typedef uint64_t uint64;
#ifdef FAIR_AI_RED_ALERT
#include "../vanilla/redalert/dllinterface.h"
#else
#define TIBERIAN_DAWN 1
#define MEGAMAPS 1
#include "../vanilla/tiberiandawn/dllinterface.h"
#endif

static FILE* report;
static int game_over;
static void callback(EventCallbackStruct const& event)
{
    if (event.EventType == CALLBACK_EVENT_GAME_OVER) {
        game_over = 1;
        fprintf(report,"PROBE|result|human=%d|win=%d|credits=%d\n",event.GameOver.IsHuman?1:0,event.GameOver.PlayerWins?1:0,event.GameOver.RemainingCredits);
        for (int i=0;i<event.GameOver.MultiPlayerTotalPlayers&&i<8;++i) {
            GameOverMultiPlayerStatsStruct const& p=event.GameOver.MultiPlayerPlayersData[i];
            fprintf(report,"PROBE|winner|seat=%llu|win=%d|kills=%d|gathered=%d\n",(unsigned long long)p.GlyphXPlayerID,p.IsWinner?1:0,p.TotalUnitsKilled,p.ResourcesGathered);
        }
    }
}
template<class T> static T symbol(void* library, char const* name, bool required = true)
{
    T result = reinterpret_cast<T>(dlsym(library, name));
    if (!result && required) { fprintf(report, "FAIL missing %s\n", name); exit(2); }
    return result;
}
static uint32_t hash_text(std::string const& value)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < value.size(); ++i) hash = (hash ^ (unsigned char)value[i]) * 16777619u;
    return hash;
}
static std::string dump(int (*fn)())
{
    fflush(stdout);
    FILE* scratch = tmpfile();
    if (!scratch) exit(2);
    int previous = dup(STDOUT_FILENO);
    dup2(fileno(scratch), STDOUT_FILENO);
    fn();
    fflush(stdout);
    dup2(previous, STDOUT_FILENO);
    close(previous);
    rewind(scratch);
    std::string result;
    char line[4096];
    while (fgets(line, sizeof line, scratch)) {
        /* Camera/player-context and selection are local presentation state. */
        if (!strncmp(line,"VIEW|",5)) continue;
        /* FIRE is a consumed renderer receipt, not the current projectile heap.
         * Its pending history is intentionally absent from engine saves. */
        if (!strncmp(line,"EFX|FIRE|",9)) continue;
        if (!strncmp(line,"OBJDUMP-END",11)) {
            const char* frame=strstr(line,"frame=");
            if (frame) result += std::string("FRAME|") + frame;
            continue;
        }
        std::string row(line);
        static char const* fields[] = {"|sel=","|player=","|group=","|flashme="};
        for (unsigned f = 0; f < sizeof fields / sizeof fields[0]; ++f) {
            size_t at = 0;
            while ((at = row.find(fields[f], at)) != std::string::npos) {
                size_t begin = at + strlen(fields[f]);
                size_t end = row.find_first_of("|\n\r", begin);
                if (end == std::string::npos) end = row.size();
                row.replace(begin, end - begin, "0");
                at = begin + 1;
            }
        }
        result += row;
    }
    fclose(scratch);
    return result;
}
int main(int argc, char** argv)
{
    report = fdopen(dup(STDOUT_FILENO), "w");
    setvbuf(report, NULL, _IOLBF, 0);
    if (argc < 8) { fprintf(report,"usage: probe dylib scenario-dir content-dir scenario level ticks seed [bots] [save-tick]\n"); return 2; }
    int level = atoi(argv[5]), ticks = atoi(argv[6]), seed = atoi(argv[7]);
    int bots = argc > 8 ? atoi(argv[8]) : 1;
    int save_tick = argc > 9 ? atoi(argv[9]) : 0;
    int perturb = argc > 10 ? atoi(argv[10]) : 0;
    const uint64 context = getenv("FAIR_AI_CONTEXT") ? (uint64)atoi(getenv("FAIR_AI_CONTEXT")) : 0;
    char save_path[2048];
    if (!getcwd(save_path, sizeof save_path - 32)) return 2;
    strcat(save_path, "/fair-ai-save.sav");
    if (level < -1 || level > 3 || ticks < 1 || bots < 1 || bots > 7) return 2;
    FILE* engine_log = freopen("engine.log", "w", stdout);
    if (!engine_log) return 2;
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(report,"FAIL dlopen %s\n",dlerror()); return 2; }
    auto init = symbol<void (*)(char const*, void (*)(EventCallbackStruct const&))>(library,"CNC_Init");
    auto add_path = symbol<void (*)(char const*)>(library,"CNC_Add_Mod_Path",false);
    auto configure = symbol<void (*)(CNCRulesDataStruct const&)>(library,"CNC_Config");
    auto set_mp = symbol<bool (*)(int,CNCMultiplayerOptionsStruct&,int,CNCPlayerInfoStruct*,int)>(library,"CNC_Set_Multiplayer_Data");
    auto set_levels = symbol<void (*)(unsigned char const*,int)>(library,"OpenCNC3D_Set_AI_Difficulties");
    auto policy = symbol<unsigned (*)()>(library,"OpenCNC3D_AI_Policy");
    auto state = symbol<unsigned (*)()>(library,"OpenCNC3D_AI_State_Hash");
    auto diagnostics = symbol<int (*)(char*,int)>(library,"OpenCNC3D_AI_Diagnostics");
    auto start = symbol<bool (*)(char const*,char const*,char const*,int,bool)>(library,"CNC_Start_Custom_Instance");
    auto advance = symbol<bool (*)(uint64)>(library,"CNC_Advance_Instance");
    auto objects = symbol<int (*)()>(library,"OpenCNC3D_Dump_Objects");
    auto set_seed = symbol<void (*)(int)>(library,"OpenCNC3D_Set_Seed",false);
    auto lockstep = symbol<void (*)(bool)>(library,"OpenCNC3D_Set_Lockstep");
    auto save_load = symbol<bool (*)(bool,char const*,char const*)>(library,"CNC_Save_Load");
    auto debug = symbol<void (*)(DebugRequestEnum,uint64,char const*,int,int,bool,bool)>(library,"CNC_Handle_Debug_Request");
    if (add_path) add_path(argv[2]);
    init("-HEADLESS", callback);
#ifndef FAIR_AI_RED_ALERT
    CNCRulesDataStruct rules = {};
    for (int i = 0; i < 3; ++i) {
        CNCDifficultyDataStruct& d = rules.Difficulties[i];
        d.FirepowerBias = d.GroundspeedBias = d.AirspeedBias = d.ArmorBias = 1.0f;
        d.ROFBias = d.CostBias = d.BuildSpeedBias = 1.0f;
        d.RepairDelay=.02f; d.BuildDelay=.03f;
        d.IsWallDestroyer=d.IsContentScan=true;
    }
    configure(rules);
#else
    (void)configure;
#endif
    lockstep(true);
    if (set_seed) set_seed(seed);
    CNCMultiplayerOptionsStruct options = {};
    options.MPlayerCount = bots + 1;
    options.MPlayerBases = 1; options.MPlayerCredits = 10000;
    options.MPlayerTiberium = 1; options.MPlayerGhosts = 1; options.MPlayerSolo = 1;
    options.EnableSuperweapons = true; options.DestroyStructures = true;
    options.MPlayerShadowRegrow = (perturb & 4) != 0;
    CNCPlayerInfoStruct players[8] = {};
    unsigned char levels[8];
    for (int i = 0; i < 8; ++i) {
        levels[i] = (unsigned char)(level < 0 ? (i ? (i - 1) % 4 : 1) : level);
        snprintf(players[i].Name,sizeof players[i].Name,"seat%d",i);
#ifdef FAIR_AI_RED_ALERT
        const int countries[8] = {1,2,3,4,5,6,7,0};
        players[i].House=(unsigned char)countries[i];
#else
        players[i].House=(unsigned char)(i % 2);
#endif
        /* Deliberately reorder colors; level belongs to a seat, not sorted house. */
        players[i].ColorIndex=(i * 3) % 8;
        players[i].GlyphxPlayerID=(uint64)i;
        players[i].Team=i; players[i].StartLocationIndex=i;
        players[i].IsAI=(i != 0);
        if (perturb & 2) players[i].House = players[0].House;
        if (perturb & 8) players[i].IsAI = true;
    }
    if (const char* specified = getenv("FAIR_AI_LEVELS")) {
        for (int i=0;*specified && i<8;++i) {
            levels[i]=(unsigned char)atoi(specified);
            if (levels[i]>3) return 2;
            while (*specified && *specified!=',') ++specified;
            if (*specified==',') ++specified;
        }
    }
    set_levels(levels, bots + 1);
    if (!set_mp(1,options,bots+1,players,8)) { fprintf(report,"FAIL setup\n"); return 1; }
#ifdef FAIR_AI_RED_ALERT
    int tech=10;
#else
    int tech=7;
#endif
    if(const char* selected=getenv("FAIR_AI_TECH")) tech=atoi(selected);
    if(tech<1 || tech>10) { fprintf(report,"FAIL invalid tech\n");return 2; }
    if (!start(argv[3],argv[2],argv[4],tech,true)) { fprintf(report,"FAIL start\n"); return 1; }
    fprintf(report,"PROBE|policy=%08x|level=%d|bots=%d|seed=%d|tech=%d\n",policy(),level,bots,seed,tech);
    int done=0;
    for (int tick=0; tick < ticks; ++tick) {
        const bool active=advance(context);
        done=tick+1;
        if ((perturb & 1) && done == 100) debug(DEBUG_REQUEST_ADD_RESOURCES,0,"1000000",0,0,false,false);
        if (done == save_tick) {
            const unsigned saved_state=state();
            const std::string saved_world=dump(objects);
            if (!save_load(true,save_path,NULL)) { fprintf(report,"FAIL save\n"); return 1; }
            for (int extra=0;extra<37;++extra) advance(context);
            if (!save_load(false,save_path,"GAME_GLYPHX_MULTIPLAYER")) { fprintf(report,"FAIL load\n"); return 1; }
            const std::string restored_world=dump(objects);
            if (state()!=saved_state || restored_world!=saved_world) {
                FILE* before=fopen("roundtrip-before.txt","w");
                FILE* after=fopen("roundtrip-after.txt","w");
                if (before) { fwrite(saved_world.data(),1,saved_world.size(),before); fclose(before); }
                if (after) { fwrite(restored_world.data(),1,restored_world.size(),after); fclose(after); }
                fprintf(report,"FAIL roundtrip|before=%08x|after=%08x\n",saved_state,state()); return 1;
            }
            fprintf(report,"PROBE|roundtrip=pass|tick=%d|state=%08x\n",done,state());
            game_over=0;
        }
        if (done % 1500 == 0 || !active || done==ticks) {
            std::string world=dump(objects);
            fprintf(report,"PROBE|tick=%d|world=%08x|state=%08x|active=%d\n",done,hash_text(world),state(),active?1:0);
            char diagnostic[16384]={}; diagnostics(diagnostic,sizeof diagnostic);
            fprintf(report,"%s",diagnostic);
            char filename[64]; snprintf(filename,sizeof filename,"world-%06d.txt",done);
            FILE* saved=fopen(filename,"w"); if (saved) { fwrite(world.data(),1,world.size(),saved); fclose(saved); }
        }
        if (!active) {
            if (!game_over) { fprintf(report,"FAIL advance without game over\n"); return 1; }
            break;
        }
    }
    fprintf(report,"PROBE|pass=1|ticks=%d\n",done);
    return 0;
}
