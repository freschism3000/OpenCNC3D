/* Worker-only Windows metadata. No OpenGL/WMI, commands, network calls or identity
   fields. Clock rates are OS reports, not effective-clock or thermal measurements.
   Include globally: windows.h + psapi.h. Included inside namespace game_diag. */
#ifndef OPENCNC3D_DIAGNOSTIC_WININFO_H
#define OPENCNC3D_DIAGNOSTIC_WININFO_H
static bool native_write(State& s,const std::string& text) {
 if(put(s,s.hardware,text,s.eventBytes,s.limits.event))return true;
 s.hardwareDropped++;return false;
}
static void native_item(State& s,const char* key,const std::string& value) {
 auto o=stream();o<<"# hardware,"<<key<<",\""<<safe_text(value.c_str())<<"\"\n";native_write(s,o.str());
}
#ifdef _WIN32
// This is the public PROCESSOR_POWER_INFORMATION ABI (six 32-bit ULONG fields).
// Read only: CallNtPowerInformation(ProcessorInformation=11,NULL,0,...).
struct NativePowerInfo { ULONG number,maxMhz,currentMhz,mhzLimit,maxIdleState,currentIdleState; };
typedef BOOL (WINAPI *NativeMemoryFn)(HANDLE,PROCESS_MEMORY_COUNTERS*,DWORD);
typedef LONG (WINAPI *NativePowerFn)(int,void*,ULONG,void*,ULONG);
struct NativeSamples {
 Uint64 counter=0,cpu100ns=0,mhzAt=0,threadsAt=0,threadErrors=0;DWORD threads=0,powerThreads=0,registryMhz=0,processThreads=0;
 bool cpuSeen=false,frequencySeen=false,powerValid=false,threadCountValid=false,threadCountSeen=false;
 double reportedMean=0;DWORD reportedMin=0,reportedMax=0;
 NativeMemoryFn memory=NULL;NativePowerFn power=NULL;
 HMODULE psapi=NULL,powrprof=NULL;
};
static thread_local NativeSamples nativeSamples;
static Uint64 native_filetime(const FILETIME& x){return (Uint64(x.dwHighDateTime)<<32)|x.dwLowDateTime;}
static std::string native_utf8(const wchar_t* value) {
 if(!value)return "";const int n=WideCharToMultiByte(CP_UTF8,0,value,-1,NULL,0,NULL,NULL);
 if(n<=1||n>4096)return "";std::vector<char> text((size_t)n);
 if(WideCharToMultiByte(CP_UTF8,0,value,-1,text.data(),n,NULL,NULL)!=n)return "";
 return text.data();
}
static std::wstring native_wide(const char* value) {
 if(!value)return L"";const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,NULL,0);
 if(length<=0||length>32768)return L"";std::vector<wchar_t> text((size_t)length);
 if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,text.data(),length)!=length)return L"";
 return text.data();
}
static int native_mkdir(const char* path) {
 const std::wstring wide=native_wide(path);if(wide.empty()){errno=EINVAL;return -1;}return _wmkdir(wide.c_str());
}
static FILE* native_fopen(const char* path,const char* mode) {
 const std::wstring wide=native_wide(path),wideMode=native_wide(mode);
 if(wide.empty()||wideMode.empty()){errno=EINVAL;return NULL;}return _wfopen(wide.c_str(),wideMode.c_str());
}
static int native_replace(const char* from,const char* to) {
 const std::wstring source=native_wide(from),target=native_wide(to);
 if(source.empty()||target.empty()){errno=EINVAL;return -1;}
 if(MoveFileExW(source.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return 0;
 errno=EACCES;return -1;
}
static std::string native_fallback_logs() {
 const DWORD length=GetEnvironmentVariableW(L"LOCALAPPDATA",NULL,0);if(length<=1||length>32000)return "";
 std::vector<wchar_t> buffer(length);const DWORD written=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer.data(),length);
 if(!written||written>=length)return "";std::wstring parent(buffer.data());
 if(parent.back()!=L'\\'&&parent.back()!=L'/')parent+=L'\\';parent+=L"OpenCNC3D";
 if(_wmkdir(parent.c_str())!=0){const DWORD attributes=GetFileAttributesW(parent.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY))return "";}
 parent+=L"\\Logs";return native_utf8(parent.c_str());
}
static bool native_reg_dword(HKEY hive,const wchar_t* path,const wchar_t* value,DWORD& result) {
 HKEY key=NULL;if(RegOpenKeyExW(hive,path,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return false;
 DWORD type=0,bytes=sizeof result,resultTmp=0;const LONG error=RegQueryValueExW(key,value,NULL,&type,(BYTE*)&resultTmp,&bytes);RegCloseKey(key);
 if(error!=ERROR_SUCCESS||type!=REG_DWORD||bytes!=sizeof result)return false;result=resultTmp;return true;
}
static std::string native_reg_string(HKEY hive,const wchar_t* path,const wchar_t* value) {
 HKEY key=NULL;if(RegOpenKeyExW(hive,path,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return "";
 wchar_t text[512]={};DWORD type=0,bytes=sizeof(text)-sizeof(wchar_t);
 const LONG error=RegQueryValueExW(key,value,NULL,&type,(BYTE*)text,&bytes);RegCloseKey(key);
 if(error!=ERROR_SUCCESS||(type!=REG_SZ&&type!=REG_EXPAND_SZ)||bytes%sizeof(wchar_t))return "";
 text[511]=0;return native_utf8(text);
}
// Use an absolute OS system directory, so a DLL beside the game cannot be loaded.
// This temporary path is never written to the report.
static HMODULE native_system_library(const wchar_t* name) {
 wchar_t dir[MAX_PATH+64]={};const UINT length=GetSystemDirectoryW(dir,MAX_PATH);
 if(!length||length>=MAX_PATH)return NULL;
 const size_t extra=wcslen(name);if(length+1+extra>=sizeof(dir)/sizeof(dir[0]))return NULL;
 dir[length]=L'\\';memcpy(dir+length+1,name,(extra+1)*sizeof(wchar_t));return LoadLibraryW(dir);
}
static void native_frequency(Uint64 clock) {
 auto& n=nativeSamples;n.mhzAt=clock;n.frequencySeen=true;n.registryMhz=0;n.powerValid=false;
 native_reg_dword(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",L"~MHz",n.registryMhz);
 if(!n.power||!n.powerThreads||n.powerThreads>256)return;
 std::vector<NativePowerInfo> values(n.powerThreads);
 if(n.power(11,NULL,0,values.data(),(ULONG)(values.size()*sizeof(NativePowerInfo)))!=0)return;
 Uint64 sum=0;DWORD smallest=~DWORD(0),largest=0;size_t valid=0;
 for(const auto& p:values)if(p.currentMhz&&p.currentMhz<100000){sum+=p.currentMhz;smallest=std::min(smallest,p.currentMhz);largest=std::max(largest,p.currentMhz);valid++;}
 if(!valid)return;n.reportedMin=smallest;n.reportedMax=largest;n.reportedMean=double(sum)/double(valid);n.powerValid=true;
}
static void native_thread_count(Uint64 clock) {
 auto& n=nativeSamples;n.threadsAt=clock;n.threadCountSeen=true;n.threadCountValid=false;
 const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
 if(snapshot==INVALID_HANDLE_VALUE){n.threadErrors++;return;}
 THREADENTRY32 entry={};entry.dwSize=sizeof entry;DWORD count=0;const DWORD process=GetCurrentProcessId();
 if(!Thread32First(snapshot,&entry)){n.threadErrors++;CloseHandle(snapshot);return;}
 do {
  if(entry.th32OwnerProcessID==process)count++;
  entry.dwSize=sizeof entry;
 } while(Thread32Next(snapshot,&entry));
 const DWORD error=GetLastError();CloseHandle(snapshot);
 if(error!=ERROR_NO_MORE_FILES){n.threadErrors++;return;}
 n.processThreads=count;n.threadCountValid=true;
}
static void native_hardware(State& s) {
 auto& n=nativeSamples;n=NativeSamples();
 SYSTEM_INFO info={};GetNativeSystemInfo(&info);n.threads=info.dwNumberOfProcessors;
 SYSTEM_INFO powerInfo={};GetSystemInfo(&powerInfo);n.powerThreads=powerInfo.dwNumberOfProcessors;
 HMODULE kernel=GetModuleHandleW(L"kernel32.dll");
 typedef DWORD (WINAPI *CountFn)(WORD);CountFn count=kernel?(CountFn)GetProcAddress(kernel,"GetActiveProcessorCount"):NULL;
 if(count){const DWORD all=count(0xffff);if(all)n.threads=all;}
 if(kernel)n.memory=(NativeMemoryFn)GetProcAddress(kernel,"K32GetProcessMemoryInfo");
 if(!n.memory){n.psapi=native_system_library(L"psapi.dll");if(n.psapi)n.memory=(NativeMemoryFn)GetProcAddress(n.psapi,"GetProcessMemoryInfo");}
 n.powrprof=native_system_library(L"powrprof.dll");if(n.powrprof)n.power=(NativePowerFn)GetProcAddress(n.powrprof,"CallNtPowerInformation");
 std::string cpu=native_reg_string(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",L"ProcessorNameString");
 // '@ 3.60GHz' is a CPU brand suffix, not an email address. The frequency columns
 // describe clocks separately; all other '@' strings retain ordinary redaction.
 const size_t clockSuffix=cpu.find(" @ ");if(clockSuffix!=std::string::npos)cpu.resize(clockSuffix);
 native_item(s,"cpu_model",cpu.empty()?"unavailable":cpu);
 native_item(s,"logical_processors",n.threads?std::to_string(n.threads):"unavailable");
 native_item(s,"process_bits",std::to_string(sizeof(void*)*8));
 const char* arch=info.wProcessorArchitecture==9?"x86_64":info.wProcessorArchitecture==0?"x86":info.wProcessorArchitecture==12?"arm64":"other";native_item(s,"native_architecture",arch);
 typedef LONG (WINAPI *VersionFn)(OSVERSIONINFOW*);HMODULE nt=GetModuleHandleW(L"ntdll.dll");VersionFn version=nt?(VersionFn)GetProcAddress(nt,"RtlGetVersion"):NULL;
 OSVERSIONINFOW os={};os.dwOSVersionInfoSize=sizeof os;
 native_item(s,"os_version",version&&version(&os)==0?std::to_string(os.dwMajorVersion)+"."+std::to_string(os.dwMinorVersion)+"."+std::to_string(os.dwBuildNumber):"unavailable");
 MEMORYSTATUSEX ram={};ram.dwLength=sizeof ram;native_item(s,"physical_ram_bytes",GlobalMemoryStatusEx(&ram)?std::to_string(ram.ullTotalPhys):"unavailable");
 for(DWORD i=0;i<16;i++) {
  DISPLAY_DEVICEW gpu={};gpu.cb=sizeof gpu;if(!EnumDisplayDevicesW(NULL,i,&gpu,0))break;
  if(gpu.StateFlags&DISPLAY_DEVICE_MIRRORING_DRIVER)continue;
  const std::string prefix="gpu_"+std::to_string(i)+"_";native_item(s,(prefix+"model").c_str(),native_utf8(gpu.DeviceString));
  native_item(s,(prefix+"attached").c_str(),gpu.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP?"1":"0");native_item(s,(prefix+"primary").c_str(),gpu.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE?"1":"0");
  const wchar_t root[]=L"\\Registry\\Machine\\";std::string driver;
  if(wcsncmp(gpu.DeviceKey,root,(sizeof(root)/sizeof(root[0]))-1)==0)driver=native_reg_string(HKEY_LOCAL_MACHINE,gpu.DeviceKey+(sizeof(root)/sizeof(root[0]))-1,L"DriverVersion");
  native_item(s,(prefix+"driver_version").c_str(),driver.empty()?"unavailable":driver);
 }
 native_item(s,"reported_clock_semantics","OS power-manager reports; not measured effective clock or temperature");
 native_write(s,"at_ms,cpu_one_core_percent,cpu_all_logical_processors_percent,logical_processors,working_set_bytes,private_commit_bytes,page_fault_count,physical_available_bytes,registry_reported_mhz,os_reported_current_mhz_min,os_reported_current_mhz_max,os_reported_current_mhz_mean,clock_report_age_ms,process_threads,thread_snapshot_errors,thread_report_age_ms\n");
 native_frequency(now());native_thread_count(now());
}
static void native_process_sample(State& s,Uint64 clock) {
 auto& n=nativeSamples;auto out=stream();out<<ms(s,s.origin,clock)<<',';
 FILETIME created={},exited={},kernel={},user={};const bool cpuOk=GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=0;
 const Uint64 cpu=cpuOk?native_filetime(kernel)+native_filetime(user):0;
 if(cpuOk&&n.cpuSeen&&clock>n.counter&&cpu>=n.cpu100ns){const double one=double(cpu-n.cpu100ns)*double(s.freq)*100.0/(10000000.0*double(clock-n.counter));out<<one<<',';if(n.threads)out<<one/n.threads;}else out<<',';
 out<<',';if(n.threads)out<<n.threads;out<<',';
 if(cpuOk){n.counter=clock;n.cpu100ns=cpu;n.cpuSeen=true;}else n.cpuSeen=false;
 PROCESS_MEMORY_COUNTERS_EX memory={};memory.cb=sizeof memory;
 if(n.memory&&n.memory(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&memory,sizeof memory))out<<memory.WorkingSetSize<<','<<memory.PrivateUsage<<','<<memory.PageFaultCount;
 else out<<",,";out<<',';
 MEMORYSTATUSEX ram={};ram.dwLength=sizeof ram;if(GlobalMemoryStatusEx(&ram))out<<ram.ullAvailPhys;out<<',';
 if(!n.frequencySeen||clock<n.mhzAt||clock-n.mhzAt>=5*s.freq)native_frequency(clock);
 if(n.registryMhz)out<<n.registryMhz;out<<',';if(n.powerValid)out<<n.reportedMin;out<<',';if(n.powerValid)out<<n.reportedMax;out<<',';if(n.powerValid)out<<n.reportedMean;out<<',';if(n.frequencySeen)out<<ms(s,n.mhzAt,clock);
 if(!n.threadCountSeen||clock<n.threadsAt||clock-n.threadsAt>=5*s.freq)native_thread_count(clock);
 out<<',';if(n.threadCountValid)out<<n.processThreads;out<<','<<n.threadErrors<<',';if(n.threadCountSeen)out<<ms(s,n.threadsAt,clock);out<<'\n';native_write(s,out.str());
}
static void native_close() {
 auto& n=nativeSamples;if(n.psapi)FreeLibrary(n.psapi);if(n.powrprof)FreeLibrary(n.powrprof);n=NativeSamples();
}
#else
static int native_mkdir(const char* path) {return mkdir(path,0700);}
static FILE* native_fopen(const char* path,const char* mode) {return fopen(path,mode);}
static int native_replace(const char* from,const char* to) {return rename(from,to);}
static std::string native_fallback_logs() {return "";}
static void native_hardware(State& s) {native_item(s,"platform","Windows counters unavailable on this host");}
static void native_process_sample(State&,Uint64) {}
static void native_close() {}
#endif
#endif
