/* Runs real engine mission/match state through the production sidebar adapters.
   No window, audio output, installed settings, or copied legality implementation. */
#include <cstdio>
#include <cstdint>
typedef uint64_t uint64;
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include "function.h"
#include "dllinterface.h"
#include "sidebar_build_potential.h"
#include "hud_enhanced_catalog.h"
struct DB_Surface;struct DB_Pack;struct SbRosterRow;
#include "production-adapters.inc"
typedef void (*CNC_Event_Callback_Type)(const EventCallbackStruct&);
extern "C" void CNC_Init(const char*,CNC_Event_Callback_Type);
extern "C" void CNC_Config(const CNCRulesDataStruct&);
extern "C" bool CNC_Start_Custom_Instance(const char*,const char*,const char*,int,bool);
extern "C" bool CNC_Get_Game_State(GameStateRequestEnum,uint64,unsigned char*,unsigned int);
extern "C" bool CNC_Advance_Instance(uint64);
extern "C" bool CNC_Set_Multiplayer_Data(int,CNCMultiplayerOptionsStruct&,int,CNCPlayerInfoStruct*,int);
extern "C" int OpenCNC3D_Get_Build_Potential(uint64,OpenCNC3D_BuildPotentialEntry*,int);
static int checks=0,failures=0;
static void check(bool pass,const char* name){++checks;if(!pass){++failures;printf("FAIL|%s\n",name);}}
static void events(const EventCallbackStruct&){}
static std::vector<unsigned char> raw_sidebar(){
 std::vector<unsigned char>b(1<<20);check(CNC_Get_Game_State(GAME_STATE_SIDEBAR,0,b.data(),b.size()),"actual sidebar export");
 return b;
}
static std::vector<int> current_legal(){
 std::vector<int>v;RTTIType kinds[]={RTTI_BUILDINGTYPE,RTTI_INFANTRYTYPE,RTTI_UNITTYPE,RTTI_AIRCRAFTTYPE};
 int counts[]={STRUCT_COUNT,INFANTRY_COUNT,UNIT_COUNT,AIRCRAFT_COUNT};
 for(int k=0;k<4;++k)for(int i=0;i<counts[k];++i){
  const TechnoTypeClass*t=Fetch_Techno_Type(kinds[k],i);v.push_back(PlayerPtr->Can_Build(t,HOUSE_GOOD));v.push_back(PlayerPtr->Can_Build(t,HOUSE_BAD));
 }
 return v;
}
static void snapshot(const char* tag){
 auto before=raw_sidebar();auto legal=current_legal();unsigned scan=PlayerPtr->ActiveBScan,bs=PlayerPtr->BScan;int level=BuildLevel;
 OpenCNC3D_BuildPotentialEntry entries[OPENCNC3D_BUILD_POTENTIAL_MAX];int n=OpenCNC3D_Get_Build_Potential(0,entries,OPENCNC3D_BUILD_POTENTIAL_MAX);
 check(n>=0&&n<=OPENCNC3D_BUILD_POTENTIAL_MAX,"bounded potential batch");
 check(OpenCNC3D_Get_Build_Potential(0,0,0)==n,"capacity-zero count query");
 struct{OpenCNC3D_BuildPotentialEntry v[2];unsigned canary;}bounded;memset(&bounded,0xa5,sizeof bounded);
 check(OpenCNC3D_Get_Build_Potential(0,bounded.v,1)==n,"limited buffer returns complete count");
 bool guard=bounded.canary==0xa5a5a5a5u;for(size_t k=0;k<sizeof bounded.v[1];++k)guard=guard&&((unsigned char*)&bounded.v[1])[k]==0xa5;
 check(guard,"capacity bounds preserve adjacent records/canary");
 check(OpenCNC3D_Get_Build_Potential(0,0,-1)==-1,"invalid capacity rejected");
 check(sizeof(OpenCNC3D_BuildPotentialEntry)==16,"cross-platform16-byte record");
 auto after=raw_sidebar();check(before==after,"live sidebar unchanged by potential query");
 check(legal==current_legal()&&scan==PlayerPtr->ActiveBScan&&bs==PlayerPtr->BScan&&level==BuildLevel,"production legality and mission level unchanged");
 printf("CASE|%s|level=%d|scenario=%d|mode=%d|house=%d|actlike=%d|potential=%d\n",tag,BuildLevel,Scen.Scenario,GameToPlay,PlayerPtr->Class->House,PlayerPtr->ActLike,n);
 for(int i=0;i<n;++i)printf("POTENTIAL|%s|%s\n",tag,entries[i].name);
 sb_poll();check(g_sbState.valid,"actual sidebar adapter state valid");
 for(int category=0;category<4;++category){
  g_ehCategory=category;eh_refresh();
  for(size_t i=0;i<g_ehItems.size();++i){
   const EH_Display d=g_ehItems[i];const EH_Display*hit=eh_display(i%2,i/2);
   check(hit&& !strcmp(hit->name,d.name)&&hit->engine==d.engine,"native drawn/hit display identity agrees");
   if(d.engine>=0)check(!strcmp(g_sbState.entry[d.engine].name,d.name),"live item identity remains authoritative");
   for(size_t j=0;j<i;++j)check(strcasecmp(g_ehItems[j].name,d.name)!=0,"no duplicated own/captured cameo");
   printf("DISPLAY|%s|category=%d|%s|state=%s|engine=%d\n",tag,category,d.name,d.engine<0?"grey":"live",d.engine);
  }
 }
 for(size_t i=0;i<g_sbState.entry.size();++i)printf("LIVE|%s|%s|type=%d|id=%d\n",tag,g_sbState.entry[i].name,g_sbState.entry[i].btype,g_sbState.entry[i].bid);
}
static void advance(int n){for(int i=0;i<n;++i)CNC_Advance_Instance(0);}
static BuildingClass* building(StructType type,HousesType house){for(int i=0;i<Buildings.Count();++i){auto*b=Buildings.Ptr(i);if(b&&!b->IsInLimbo&&b->Class->Type==type&&b->House->Class->House==house)return b;}return 0;}
int main(int argc,char**argv){
 setvbuf(stdout,0,_IONBF,0);if(argc<7)return 2;
 const char*content=argv[1],*dir=argv[2],*scen=argv[3],*tag=argv[6];int level=atoi(argv[4]),side=atoi(argv[5]);bool mp=strstr(tag,"mp-")!=0;
 CNC_Init("",events);CNCRulesDataStruct rules;memset(&rules,0,sizeof rules);
 for(int i=0;i<3;++i){auto&r=rules.Difficulties[i];r.FirepowerBias=r.GroundspeedBias=r.AirspeedBias=r.ArmorBias=r.ROFBias=r.CostBias=r.BuildSpeedBias=1;r.RepairDelay=.02;r.BuildDelay=.03;r.IsWallDestroyer=true;r.IsContentScan=true;}CNC_Config(rules);
 if(mp){CNCMultiplayerOptionsStruct o;memset(&o,0,sizeof o);o.MPlayerCount=2;o.MPlayerBases=1;o.MPlayerCredits=5000;o.MPlayerSolo=1;o.MPlayerTiberium=1;o.EnableSuperweapons=true;o.DestroyStructures=true;
  CNCPlayerInfoStruct p[8];memset(p,0,sizeof p);for(int i=0;i<2;++i){snprintf(p[i].Name,sizeof p[i].Name,"P%d",i);p[i].House=i?1-side:side;p[i].ColorIndex=i;p[i].GlyphxPlayerID=i;p[i].Team=i;p[i].StartLocationIndex=i;}check(CNC_Set_Multiplayer_Data(1,o,2,p,8),"real multiplayer lobby arm");}
 if (!CNC_Start_Custom_Instance(content,dir,scen,level,mp)) {printf("FAIL|real mission/match start\n");return 3;}advance(5);
 if(!strcmp(tag,"capture")||!strcmp(tag,"restore")||!strcmp(tag,"fallback")){for(int i=0;i<Buildings.Count();++i){auto*b=Buildings.Ptr(i);if(b&&b->House==PlayerPtr&&!b->IsInLimbo)b->Revealed(PlayerPtr);}advance(4);}
 memset(&g_sb,0,sizeof g_sb);g_sb.GetState=CNC_Get_Game_State;g_sb.BuildPotential=OpenCNC3D_Get_Build_Potential;g_sb.nod=side;
 if(!strcmp(tag,"fallback"))g_sb.BuildPotential=0;
 snapshot(tag);
 if(!strcmp(tag,"gdi-01")||!strcmp(tag,"gdi-15")||!strcmp(tag,"mp-gdi-7")){
  OpenCNC3D_BuildPotentialEntry entries[OPENCNC3D_BUILD_POTENTIAL_MAX];std::vector<double>us;us.reserve(20000);int invalid=0;
  for(int i=0;i<20200;++i){auto t=std::chrono::steady_clock::now();int n=OpenCNC3D_Get_Build_Potential(0,entries,OPENCNC3D_BUILD_POTENTIAL_MAX);auto e=std::chrono::steady_clock::now();if(i>=200)us.push_back(std::chrono::duration<double,std::micro>(e-t).count());invalid+=n<0;}
  check(invalid==0,"timing queries all valid");std::sort(us.begin(),us.end());printf("TIMING|%s|samples=%zu|median_us=%.3f|p95_us=%.3f|p99_us=%.3f\n",tag,us.size(),us[us.size()/2],us[us.size()*95/100],us[us.size()*99/100]);
 }
 if(!strcmp(tag,"restore")){
  auto*b=building(STRUCT_BARRACKS,PlayerPtr->Class->House);check(b!=0,"original barracks exists");if(b){check(b->Limbo(),"original barracks removed from map");delete b;advance(4);snapshot("restore-destroyed");
   auto*replacement=(BuildingClass*)BuildingTypeClass::As_Reference(STRUCT_BARRACKS).Create_One_Of(PlayerPtr);
   bool placed=false;for(int y=4;replacement&&!placed&&y<58;++y)for(int x=4;!placed&&x<57;++x)placed=replacement->Unlimbo(Cell_Coord(XY_Cell(x,y)));
   check(placed,"actual barracks reconstruction at legal cell");if(placed)replacement->Revealed(PlayerPtr);advance(4);snapshot("restore-rebuilt");}
 }
 if(!strcmp(tag,"capture")){
  auto*b=building(STRUCT_HAND,HOUSE_BAD);check(b!=0,"enemy Nod producer exists");if(b){check(b->Captured(PlayerPtr),"actual Nod factory capture");advance(4);snapshot("captured-hand");}
  auto*yard=building(STRUCT_CONST,HOUSE_BAD);check(yard!=0,"enemy Nod yard exists");if(yard){check(yard->Captured(PlayerPtr),"actual Nod yard capture");advance(4);snapshot("captured-yard");}
 }
 printf("RESULT|checks=%d|failures=%d\n",checks,failures);return failures?1:0;
}
