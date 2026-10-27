/* Public modules protocol, implemented with lmodule.c so all launchers share it.
 * Catalogs and ZIPs are remote data. A catalog can select only a named release
 * asset; its file manifest can select only immutable runtime paths below. */
#include "lcfg.h"
#include "ljson.h"
#include "lnet.h"
#include <ctype.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define LMOD_PLATFORM "windows"
#define LMOD_MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define LMOD_PLATFORM "macos"
#define LMOD_MKDIR(p) mkdir(p,0755)
#endif
#define LMOD_CAP (4*1024*1024)
#define LMOD_FILES 20000

static char *lmod_line(char *text,char **state)
{
    char *p=text?text:*state,*end;if(!p||!*p)return NULL;end=strchr(p,'\n');
    if(end){*end=0;*state=end+1;}else *state=p+strlen(p);return p;
}
static int lmod_hex(const char *p)
{ int i;if(strlen(p)!=64)return 0;for(i=0;i<64;i++)if(!strchr("0123456789abcdef",p[i]))return 0;return 1; }
static int lmod_equal(const char *a,const char *b)
{ while(*a&&*b){if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++))return 0;}return !*a&&!*b; }
static const char *lmod_version(const char *s){return s&&s[0]=='v'?s+1:s;}
static const LJ_Value *lmod_release(const LJ_Value *root,const char *build)
{
    const LJ_Value *r=lj_get(root,"latest"),*p;const char *tag=lj_str(r,"tag");
    if(tag&&!strcmp(lmod_version(tag),lmod_version(build)))return r;
    for(p=lj_first(lj_get(root,"releases"));p;p=lj_next(p)){
        tag=lj_str(p,"tag");if(tag&&!strcmp(lmod_version(tag),lmod_version(build)))return p;
    }
    return NULL;
}
static const LJ_Value *lmod_asset(const LJ_Value *release,const char *name)
{
    const LJ_Value *a,*found=NULL;
    for(a=lj_first(lj_get(release,"assets"));a;a=lj_next(a)){
        const char *n=lj_str(a,"name");if(n&&!strcmp(n,name)){if(found)return NULL;found=a;}
    }
    return found;
}
int lmod_catalog_read(const char *build,const char *json,const char *catalog,LMOD_Catalog *out,char *err,int len)
{
    LJ_Value *root;const LJ_Value *release,*asset;char *copy,*line,*save=NULL;
    LMOD_Catalog result;int header=0,version=0,count=0;
    memset(&result,0,sizeof result);memset(out,0,sizeof *out);
    if(!build||!json||!catalog||strlen(catalog)>=LMOD_CAP)goto bad;
    root=lj_parse(json,err,len);if(!root)return 0;
    if(!lj_bool(root,"ok",1)){lj_free(root);goto bad;}
    release=lmod_release(root,build);if(!release){lj_free(root);goto bad;}
    copy=strdup(catalog);if(!copy){lj_free(root);goto bad;}
    for(line=lmod_line(copy,&save);line;line=lmod_line(NULL,&save)){
        char key[32],id[16],platform[24],name[256],sha[80],extra;long long size;int target;
        while(*line==' '||*line=='\t')line++;
        if(!*line||*line=='#'||*line=='\r')continue;
        if(sscanf(line,"modules %d %c",&target,&extra)==1){if(header++||target!=1)goto parsebad;continue;}
        if(sscanf(line,"version %99s %c",result.version,&extra)==1){if(version++||strcmp(lmod_version(build),result.version))goto parsebad;continue;}
        if(sscanf(line,"%31s %15s %23s %255s %lld %79s %c",key,id,platform,name,&size,sha,&extra)!=6||strcmp(key,"module"))goto parsebad;
        if(strcmp(id,"ra")&&strcmp(id,"td"))goto parsebad;
        if(strcmp(platform,"macos")&&strcmp(platform,"windows"))goto parsebad;
        if(strcmp(platform,LMOD_PLATFORM))continue;
        target=!strcmp(id,"ra");
        {char expected[256];snprintf(expected,sizeof expected,"OpenCNC3D-module-%s-%s-v%s.zip",id,platform,lmod_version(build));if(strcmp(name,expected))goto parsebad;}
        asset=lmod_asset(release,name);
        if(result.module[target].available||!lmod_hex(sha)||size<=0||size>4000000000LL||!asset||
           lj_num(asset,"id",0)<=0||lj_num(asset,"size",-1)!=size)goto parsebad;
        result.module[target].available=1;result.module[target].id=lj_num(asset,"id",0);
        result.module[target].size=size;snprintf(result.module[target].name,256,"%s",name);
        snprintf(result.module[target].sha,65,"%s",sha);count++;
    }
    free(copy);lj_free(root);
    if(header!=1||version!=1||!count)goto bad;
    *out=result;return 1;
parsebad:free(copy);lj_free(root);
bad:snprintf(err,(size_t)len,"No complete, verified module catalog matches this launcher build.");return 0;
}
static int lmod_site_ok(const char *site)
{ return site&&(!strncmp(site,"https://",8)||!strncmp(site,"http://127.0.0.1:",17)||!strncmp(site,"http://localhost:",17)); }
static void lmod_asset_url(const char *site,long long id,char *out,size_t n)
{snprintf(out,n,"%s/api/download?asset=%lld",site,id);}
int lmod_catalog_fetch_at(const char *site,const char *build,LMOD_Catalog *out,char *err,int len)
{
    char url[1400],name[256];char *json=NULL,*text=NULL;LJ_Value *root=NULL;
    const LJ_Value *release,*asset;long long id,size;int ok=0;
    memset(out,0,sizeof *out);err[0]=0;if(!lmod_site_ok(site))goto done;
    snprintf(url,sizeof url,"%s/api/builds",site);json=ln_get_text(url,LCFG_KEY,LMOD_CAP,err,len);
    if(!json||strlen(json)>=LMOD_CAP)goto done;
    root=lj_parse(json,err,len);if(!root)goto done;release=lmod_release(root,build);
    snprintf(name,sizeof name,"OpenCNC3D-v%s-modules.txt",lmod_version(build));asset=lmod_asset(release,name);
    id=lj_num(asset,"id",0);size=lj_num(asset,"size",-1);
    if(id<=0||size<=0||size>=LMOD_CAP){snprintf(err,(size_t)len,"This build has no public module catalog yet.");goto done;}
    lmod_asset_url(site,id,url,sizeof url);text=ln_get_text(url,LCFG_KEY,LMOD_CAP,err,len);
    if(!text||strlen(text)!=(size_t)size)goto done;
    ok=lmod_catalog_read(build,json,text,out,err,len);if(ok)snprintf(out->site,sizeof out->site,"%s",site);
done:if(!ok&&!err[0])snprintf(err,(size_t)len,"The public module catalog could not be checked. Reopen the browser to retry.");
    lj_free(root);free(json);free(text);return ok;
}
int lmod_catalog_fetch(const char *build,LMOD_Catalog *out,char *err,int len)
{return lmod_catalog_fetch_at(LCFG_BASE[0]?LCFG_BASE:"https://opencnc3d.com",build,out,err,len);}

/* These names describe immutable shipped runtime files. The signed-by-release
 * list is still exact, and neither it nor an archive may name player data. */
static int lmod_runtime_name(int target,const char *name)
{
    static const char *const dirs[]={"content/","missions/","dosdata/","models3d/","n64fix/","ebld/","eplc/","evhc/","einf/","ecur/","vfx3d/","cinematic/","foley/","ambience/","newaudio/","desert-details/","stone-materials/",NULL};
    static const char *const ra_dirs[]={"data/","audio/","missions/","einf/","3d/","previews/","ebld/","eplc/","evhc/","efx/","ecur/","ore/",NULL};
    static const char *const ra_packs[]={"clearblend_RA_SNOW.pack","groundmask_RA_SNOW.pack","waterbase_RA_TEMPERAT.pack",
        "terrainmaterial_RA_SNOW.pack","terrainmaterial_RA_TEMPERAT.pack","terrainsurface_RA_SNOW.pack","terrainsurface_RA_TEMPERAT.pack",
        "terrainenh_RA_SNOW.pack","terrainenh_RA_TEMPERAT.pack","terrainseam_RA_SNOW.pack","terrainseam_RA_TEMPERAT.pack",NULL};
    static const char *const ra_local_packs[]={"terrainenh_RA_SNOW.pack","terrainenh_RA_TEMPERAT.pack",
        "terrainseam_RA_SNOW.pack","terrainseam_RA_TEMPERAT.pack",NULL};
    static const char *const root[]={"opencnc3d","opencnc3d-game.exe","cnc_eyes","cnc_eyes.exe","TiberianDawn.dylib","TiberianDawn.dll","TiberianDawnXL.dylib","TiberianDawnXL.dll","RedAlert.dylib","RedAlert.dll","SDL2.dll","libSDL2-2.0.0.dylib","libSDL3.dylib","CONQUER.MIX","temperate_grass_rgb.bin","netcheck","netcheck.exe","OpenCNC3D-Diagnostics.exe","skirmish-maps.txt","LICENSE","NOTICE.md",NULL};
    const char *p=name,*base,*dot;int i,ra_local=0;char lower[1024],*part,*slash;
    static const char *const protected_parts[]={"movies","music","saves","savegames","settings","user_maps","logs","shots","screenshots",".git",NULL};
    if(!name||strlen(name)>900||!lz_name_safe(name)||strchr(name,'\\')||name[0]=='.')return 0;
    if(target&&!strncmp(name,"ra3d/",5)){p=name+5;ra_local=1;}
    else if(!strncmp(name,"ra3d/",5))return 0;
    for(base=p;*base;base++)if(isspace((unsigned char)*base)||*base==':'||(unsigned char)*base>=128)return 0;
    for(i=0;p[i];i++)lower[i]=(char)tolower((unsigned char)p[i]);
    lower[i]=0;
    for(part=lower;part&&*part;part=slash?slash+1:NULL){
        slash=strchr(part,'/');if(slash)*slash=0;
        if(part[0]=='.')return 0;
        for(i=0;protected_parts[i];i++)if(!strcmp(part,protected_parts[i]))return 0;
        if(slash)*slash='/';
    }
    base=strrchr(p,'/');base=base?base+1:p;dot=strrchr(base,'.');
    if(!*base||base[0]=='.'||strstr(lower,"save")||strstr(lower,"user_maps")||strstr(lower,".opencnc3d")||
       (dot&&(lmod_equal(dot,".cfg")||lmod_equal(dot,".sav")||lmod_equal(dot,".vqa")||lmod_equal(dot,".log")||lmod_equal(dot,".ocpart")))||
       (lmod_equal(base,"SCORES.MIX")||lmod_equal(base,"TRANSIT.MIX")))return 0;
    if(!strcmp(name,"CHANGELOG.txt"))return 1;
    if(target&&!ra_local){
        if(!strchr(p,'/')){for(i=0;ra_packs[i];i++)if(!strcmp(p,ra_packs[i]))return 1;}
        if(!strchr(p,'/'))return dot&&!strcmp(dot,".pack")&&strncmp(p,"SC",2)&&strncmp(p,"sidebar",7)&&strcmp(p,"campaign.pack")&&strcmp(p,"dosclassic.pack")&&!strstr(p,"_RA_");
        if(!strncmp(p,"missions/",9)||!strncmp(p,"content/",8)||!strncmp(p,"dosdata/",8))return 0;
        for(i=0;dirs[i];i++)if(!strncmp(p,dirs[i],strlen(dirs[i])))return 1;
        return 0;
    }
    if(!strchr(p,'/')){
        for(i=0;root[i];i++)if(!strcmp(p,root[i]))return target?strncmp(p,"TiberianDawn",12)!=0:strncmp(p,"RedAlert",8)!=0;
        if(ra_local)for(i=0;ra_local_packs[i];i++)if(!strcmp(p,ra_local_packs[i]))return 1;
        return dot&&!strcmp(dot,".pack")&&(!strstr(p,"_RA_")||!strcmp(p,"waterbase_RA_TEMPERAT.pack"));
    }
    for(i=0;(ra_local?ra_dirs:dirs)[i];i++)if(!strncmp(p,(ra_local?ra_dirs:dirs)[i],strlen((ra_local?ra_dirs:dirs)[i])))return 1;
    return 0;
}
typedef struct {char **owned;int count;} LMOD_Files;
static void lmod_files_free(LMOD_Files *f){int i;for(i=0;i<f->count;i++)free(f->owned[i]);free(f->owned);memset(f,0,sizeof *f);}
static int lmod_files_parse(const char *text,int target,const char *build,LMOD_Files *out,char *err,int len)
{
    char *copy,*line,*save=NULL,manifest[80];int header=0,module=0,version=0,platform=0;
    memset(out,0,sizeof *out);if(!text||strlen(text)>=LMOD_CAP)goto bad;
    out->owned=(char **)calloc(LMOD_FILES+2,sizeof(char *));copy=strdup(text);
    if(!out->owned||!copy){free(copy);goto bad;}
    for(line=lmod_line(copy,&save);line;line=lmod_line(NULL,&save)){
        char key[32],value[100],sha[80],name[1024],extra;long long size;int fmt,i;
        if(!*line||*line=='#'||*line=='\r')continue;
        if(sscanf(line,"module-files %d %c",&fmt,&extra)==1){if(header++||fmt!=1)goto parsebad;continue;}
        if(sscanf(line,"%31s %99s %c",key,value,&extra)==2){
            if(!strcmp(key,"module")){if(module++||strcmp(value,target?"ra":"td"))goto parsebad;}
            else if(!strcmp(key,"version")){if(version++||(build&&strcmp(lmod_version(build),value)))goto parsebad;}
            else if(!strcmp(key,"platform")){if(platform++||strcmp(value,LMOD_PLATFORM))goto parsebad;}
            else goto parsebad;
            continue;
        }
        if(sscanf(line,"file %79s %lld %1023s %c",sha,&size,name,&extra)!=3||!lmod_hex(sha)||size<0||
           size>4000000000LL||!lmod_runtime_name(target,name)||out->count>=LMOD_FILES)goto parsebad;
        for(i=0;i<out->count;i++)if(lmod_equal(name,out->owned[i]))goto parsebad;
        out->owned[out->count]=strdup(name);if(!out->owned[out->count])goto parsebad;out->count++;
    }
    free(copy);if(header!=1||module!=1||version!=1||platform!=1||!out->count)goto bad;
    snprintf(manifest,sizeof manifest,"opencnc3d-module-%s.txt",target?"ra":"td");
    out->owned[out->count]=strdup(manifest);if(!out->owned[out->count])goto bad;out->count++;return 1;
parsebad:free(copy);
bad:lmod_files_free(out);snprintf(err,(size_t)len,"The module file manifest is invalid or names protected player data.");return 0;
}
static int lmod_install_public(const char *dir,int target,const char *build,const char *zip,
                                int (*cb)(void *,int,int),void *user,char *err,int len)
{
    char manifest[80],record[512];char *text;LMOD_Files files;const char *last[2];LZ_File extra[2];int ok;
#ifdef _WIN32
    static const char *const td_required[]={"TiberianDawn.dll","opencnc3d-game.exe","CONQUER.MIX"};
#else
    static const char *const td_required[]={"TiberianDawn.dylib","opencnc3d","CONQUER.MIX"};
#endif
    if(lz_is_private_developer(dir)){snprintf(err,(size_t)len,"This private source build is updated from its source checkout, not public game packages.");return 0;}
    if(!lmod_settle(dir,err,len))return 0;
    snprintf(manifest,sizeof manifest,"opencnc3d-module-%s.txt",target?"ra":"td");
    text=lz_read_text(zip,manifest,LMOD_CAP,err,len);if(!text)return 0;
    ok=lmod_files_parse(text,target,build,&files,err,len);free(text);if(!ok)return 0;
    last[0]=manifest;last[1]=NULL;memset(extra,0,sizeof extra);
    /* Bootstrap install record is committed after every module byte, under the
     * same lock and rollback journal. Existing install/settings records stay. */
    if(!lz_is_install(dir)){
        snprintf(record,sizeof record,"version %s\nplatform %s\nmodule_install 1\n",lmod_version(build),LMOD_PLATFORM);
        extra[0].name="opencnc3d-install.txt";extra[0].bytes=record;extra[0].len=strlen(record);
    }
    {int i,j,n=target?lmod_ra_nneeds:3;const char *const *needs=target?lmod_ra_needs:td_required;
     for(i=0;i<n;i++){for(j=0;j<files.count;j++)if(!strcmp(needs[i],files.owned[j]))break;
         if(j==files.count){snprintf(err,(size_t)len,"The public module manifest is missing %.120s",needs[i]);lmod_files_free(&files);return 0;}}
    }
    /* Every listed file must exist in the archive: a phantom ownership entry
     * must never become permission to remove a player's existing file later. */
    ok=lz_install_owned_extra(dir,zip,(const char *const *)files.owned,
                              (const char *const *)files.owned,files.count,last,extra,cb,user,err,len);
    lmod_files_free(&files);return ok;
}
static int lmod_removable(int target,const char *name)
{
    int i;if(!strncmp(name,"opencnc3d-module-",17))return 1;
    if(target)return !strncmp(name,"ra3d/",5);
    if(!strcmp(name,"TiberianDawnXL.dylib")||!strcmp(name,"TiberianDawnXL.dll"))return 1;
    if(!strncmp(name,"content/",8)||!strncmp(name,"missions/",9)||!strncmp(name,"dosdata/",8))return 1;
    if(!strcmp(name,"opencnc3d")||!strcmp(name,"opencnc3d-game.exe")||!strcmp(name,"cnc_eyes")||!strcmp(name,"cnc_eyes.exe")||
       !strcmp(name,"netcheck")||!strcmp(name,"netcheck.exe")||!strcmp(name,"OpenCNC3D-Diagnostics.exe"))return 1;
    for(i=0;lmod_td_files[i];i++)if(!strcmp(name,lmod_td_files[i]))return 1;
    return 0;
}
static const char *const *lmod_owned_files(const char *dir,int target)
{
    static LMOD_Files memory[2];char rel[100],path[1400],err[256];char *text;FILE *f;long n;
    snprintf(rel,sizeof rel,"opencnc3d-module-%s.txt.module-removing",target?"ra":"td");
    if(!lz_inside(dir,rel,path,sizeof path)||lz_is_symlink(path))return NULL;
    f=fopen(path,"rb");if(!f){
        snprintf(rel,sizeof rel,"opencnc3d-module-%s.txt",target?"ra":"td");
        if(!lz_inside(dir,rel,path,sizeof path)||lz_is_symlink(path))return NULL;
        f=fopen(path,"rb");
    }
    if(!f)return target?lmod_ra_files:lmod_td_files;
    if(fseek(f,0,SEEK_END)||(n=ftell(f))<=0||n>=LMOD_CAP||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    text=(char *)malloc((size_t)n+1);if(!text){fclose(f);return NULL;}
    if(fread(text,1,(size_t)n,f)!=(size_t)n||memchr(text,0,(size_t)n)){fclose(f);free(text);return NULL;}fclose(f);text[n]=0;
    lmod_files_free(&memory[target]);
    if(!lmod_files_parse(text,target,NULL,&memory[target],err,sizeof err)){free(text);return NULL;}free(text);
    {int i,nout=0;LMOD_Files *m=&memory[target];for(i=0;i<m->count;i++){
        if(lmod_removable(target,m->owned[i]))m->owned[nout++]=m->owned[i];else free(m->owned[i]);
    }m->count=nout;m->owned[nout]=NULL;}
    return (const char *const *)memory[target].owned;
}

int lmod_install_file(const char *dir,int target,const char *build,const char *zip,const char *expected,
                       int (*cb)(void *,int,int),void *user,char *err,int len)
{
    LZ_Lock lock;int own=0,ok;char sha[65];
    if(lz_is_private_developer(dir)){snprintf(err,(size_t)len,"This private source build is updated from its source checkout, not public game packages.");return 0;}
    if(target<0||target>1||!expected||!lmod_hex(expected)){snprintf(err,(size_t)len,"The module checksum is missing or invalid.");return 0;}
    if(!lz_sha256_file(zip,sha,err,len))return 0;
    if(strcmp(sha,expected)){snprintf(err,(size_t)len,"The module failed its checksum. No game files were changed.");return 0;}
    if(!lz_held_here(dir)){if(lz_lock_new(dir,&lock)!=1){snprintf(err,(size_t)len,"Another launcher is using this installation.");return 0;}own=1;}
    ok=lmod_install_public(dir,target,build,zip,cb,user,err,len);if(own)lz_unlock(&lock);return ok;
}

/* A module-owned record is authoritative. Legacy TD full packages may use their
 * global record; legacy RA has no verified release identity and offers Update. */
int lmod_module_current(const char *dir,int target,const char *build)
{
    char rel[100],path[1400],line[1200],value[100],extra;FILE *f;int current=0;
    snprintf(rel,sizeof rel,"opencnc3d-module-%s.txt",target?"ra":"td");
    if(!lz_inside(dir,rel,path,sizeof path)||lz_is_symlink(path))return 0;
    f=fopen(path,"rb");
    if(!f&&!target){if(!lz_inside(dir,"opencnc3d-install.txt",path,sizeof path)||lz_is_symlink(path))return 0;f=fopen(path,"rb");}
    if(!f)return 0;
    while(fgets(line,sizeof line,f))if(sscanf(line,"version %99s %c",value,&extra)==1){current=!strcmp(lmod_version(build),lmod_version(value));break;}
    fclose(f);return current;
}

/* The asset identity in the part name prevents a different release/hash ever
 * resuming over old bytes. HTTP Range verifies offsets; SHA verifies the whole. */
typedef struct {int (*cb)(void *,int,int);void *user;} LMOD_Download;
static int lmod_download_progress(void *user,long long done,long long total)
{LMOD_Download *p=(LMOD_Download *)user;return !p->cb||p->cb(p->user,total>0?(int)(done*700/total):0,1000);}
static int lmod_extract_progress(void *user,int done,int total)
{LMOD_Download *p=(LMOD_Download *)user;return !p->cb||p->cb(p->user,700+(total>0?done*300/total:0),1000);}
static int lmod_replace(const char *from,const char *to)
{
#ifdef _WIN32
    return MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)?0:-1;
#else
    return rename(from,to);
#endif
}
static int lmod_write_side(const char *dir,int target,const char *suffix,const char *text,char *err,int len)
{
    char path[1400],temp[1420];FILE *f;int ok;
    if(!lmod_cache_path(dir,target,suffix,path))return 0;
    snprintf(temp,sizeof temp,"%s.new",path);if(lz_is_symlink(temp))return 0;
    f=fopen(temp,"wb");if(!f)return 0;ok=fputs(text,f)>=0;if(fclose(f))ok=0;
    if(!ok){remove(temp);return 0;}if(lmod_replace(temp,path)){remove(temp);snprintf(err,(size_t)len,"The verified package could not be cached.");return 0;}return 1;
}
int lmod_install_remote(const char *dir,int target,const char *build,const LMOD_Catalog *catalog,
                         int (*cb)(void *,int,int),void *user,char *err,int len)
{
    const LMOD_Asset *asset;LMOD_Download progress;LZ_Lock lock;int own=0,ok=0,rc;long status;
    char cache[1400],part[1400],rel[160],zip[1400],url[1400],sha[65],side[128];
    err[0]=0;
    if(lz_is_private_developer(dir)){snprintf(err,(size_t)len,"This private source build is updated from its source checkout, not public game packages.");return 0;}
    if(!catalog||target<0||target>1||strcmp(lmod_version(build),catalog->version)||!lmod_site_ok(catalog->site))goto invalid;
    asset=&catalog->module[target];if(!asset->available||asset->id<=0||asset->size<=0||!lmod_hex(asset->sha))goto invalid;
    if(!lz_held_here(dir)){if(lz_lock_new(dir,&lock)!=1){snprintf(err,(size_t)len,"Another launcher is using this installation.");return 0;}own=1;}
    if(!lz_inside(dir,".opencnc3d-modules",cache,sizeof cache)||lz_is_symlink(cache))goto done;
    if(LMOD_MKDIR(cache)&&errno!=EEXIST)goto done;
    snprintf(rel,sizeof rel,".opencnc3d-modules/%s-%lld-%.64s.ocpart",target?"ra":"td",asset->id,asset->sha);
    if(!lz_inside(dir,rel,part,sizeof part)||lz_is_symlink(part)||!lmod_cache_path(dir,target,"",zip))goto done;
    progress.cb=cb;progress.user=user;
    if(!lz_sha256_file(zip,sha,err,len)||strcmp(sha,asset->sha)||ln_file_size(zip)!=asset->size){
        lmod_asset_url(catalog->site,asset->id,url,sizeof url);
        rc=ln_get_range(url,LCFG_KEY,part,0,asset->size,30,NULL,lmod_download_progress,&progress,&status,err,len);
        if(rc!=LN_RANGE_OK)goto done;
        if(ln_file_size(part)!=asset->size||!lz_sha256_file(part,sha,err,len)||strcmp(sha,asset->sha)){
            remove(part);snprintf(err,(size_t)len,"The downloaded module failed its checksum. No game files were changed.");goto done;
        }
        if(lmod_replace(part,zip)){snprintf(err,(size_t)len,"The verified module package could not take its cache name.");goto done;}
    }
    snprintf(side,sizeof side,"%s\n",asset->sha);if(!lmod_write_side(dir,target,".sha256",side,err,len))goto done;
    snprintf(side,sizeof side,"%s\n",catalog->version);if(!lmod_write_side(dir,target,".version",side,err,len))goto done;
    if(cb&&!cb(user,700,1000))goto done;
    ok=lmod_install_public(dir,target,build,zip,lmod_extract_progress,&progress,err,len);
done:if(!ok&&!err[0])snprintf(err,(size_t)len,"The module installation stopped before completion. No partial game was installed.");if(own)lz_unlock(&lock);return ok;
invalid:snprintf(err,(size_t)len,"A verified public package is not available for this launcher build.");return 0;
}
