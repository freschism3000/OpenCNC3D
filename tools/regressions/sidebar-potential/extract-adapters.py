from pathlib import Path
import sys
repo=Path(sys.argv[1]);out=Path(sys.argv[2]);s=(repo/'game/cnc_sidebar.h').read_text()
def block(needle):
 start=s.index(needle);brace=s.index('{',start);depth=1;i=brace+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 if s[i:i+1]==';':i+=1
 return s[start:i]+'\n'
need=['struct SbHooks {','struct SbEntry {','struct SbState {']
first=''.join(block(n) for n in need)
first+='static SbHooks g_sb;\nstatic SbState g_sbState;\nstatic bool g_sbSpectator=false, g_sbPowerForce=false, g_sbReady=false, g_sbPlacing=false;\nstatic int g_sbPowerForceP=0,g_sbPowerForceD=0,g_sbPlaceIdx=-1,g_sbPlaceBType=-1,g_sbPlaceBId=-1;\nstatic std::vector<unsigned char> g_sbBuf,g_sbPlaceProx,g_sbPlaceClear;\n'
first+=block('static void sb_poll(void)')
first+='static int g_ehCategory=0,g_ehTop[4]={0,0,0,0},g_h6Rows=6;\n'+block('struct EH_Display {')+'static std::vector<EH_Display> g_ehItems;\n'
first+=block('static int eh_category(const SbEntry& e)')
if 'static bool eh_potential(' in s:first+=block('static bool eh_potential(const char* name)')
first+=block('static void eh_refresh(void)')+block('static const EH_Display* eh_display(int c,int row)')
out.write_text(first)
