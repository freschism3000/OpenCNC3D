/* Actual updater with counting response stubs; no HTTP, window or install lock. */
#define SDL_MAIN_HANDLED
#include "../../launcher/lupdate.c"
#include <assert.h>

static char fixture_builds[1400], fixture_manifest[600];
static int builds_calls, manifest_calls, notes_calls;
static const char local_notes[] = "## v0.9.0 Alpha\n\nExpanded local RA and private-build notes.\n";

static char *response(const char *text)
{
    char *copy=(char *)malloc(strlen(text)+1);
    assert(copy);strcpy(copy,text);return copy;
}

char *ln_get_text(const char *url,const char *key,long cap,char *err,int len)
{
    (void)key;(void)cap;(void)err;(void)len;
    if(strstr(url,"/api/builds")){builds_calls++;return response(fixture_builds);}
    if(strstr(url,"/api/download?asset=3")){manifest_calls++;return response(fixture_manifest);}
    if(strstr(url,"/api/changelog")){
        notes_calls++;
        return response("{\"entries\":[{\"title\":\"Approved public notes\",\"body\":\"Current public update body.\"}]}");
    }
    assert(!"unexpected request");return NULL;
}
char *ln_get_text_reach(const char *url,const char *key,long cap,int *unreached,char *err,int len)
{if(unreached)*unreached=0;return ln_get_text(url,key,cap,err,len);}
char *ln_post_text(const char *url,const char *key,const char *body,const char *type,long cap,char *err,int len)
{(void)url;(void)key;(void)body;(void)type;(void)cap;(void)err;(void)len;assert(!"unexpected POST");return NULL;}
int ln_get_file(const char *url,const char *key,const char *path,LN_Progress cb,void *user,char *err,int len)
{(void)url;(void)key;(void)path;(void)cb;(void)user;(void)err;(void)len;assert(!"unexpected file download");return 0;}
int ln_get_range(const char *url,const char *key,const char *path,long long off,long long count,
                 int stall,volatile int *cancel,LN_Progress cb,void *user,long *status,char *err,int len)
{(void)url;(void)key;(void)path;(void)off;(void)count;(void)stall;(void)cancel;(void)cb;(void)user;(void)status;(void)err;(void)len;assert(!"unexpected range download");return LN_RANGE_DISK;}
long long ln_file_size(const char *path)
{struct stat st;return stat(path,&st)?-1:(long long)st.st_size;}

int main(int argc,char **argv)
{
    LU_State state;char err[512];const char *latest;int fetch,empty;
    assert(argc==6);assert(!SDL_Init(SDL_INIT_TIMER));
    latest=argv[3];fetch=atoi(argv[4]);empty=atoi(argv[5]);
    snprintf(fixture_builds,sizeof fixture_builds,
        "{\"latest\":{\"tag\":\"v%s\",\"assets\":["
        "{\"id\":1,\"name\":\"OpenCNC3D-" LU_PLATFORM "-v%s.zip\",\"size\":100},"
        "{\"id\":3,\"name\":\"OpenCNC3D-v%s-manifest.txt\",\"size\":200}]}}",latest,latest,latest);
    snprintf(fixture_manifest,sizeof fixture_manifest,
        LU_PLATFORM "_full OpenCNC3D-" LU_PLATFORM "-v%s.zip 100 "
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
        LU_PLATFORM "_data_id fixture-approved-data\n",latest);
    memset(&state,0,sizeof state);state.lock=SDL_CreateMutex();assert(state.lock);
    snprintf(state.dir,sizeof state.dir,"%s",argv[1]);snprintf(state.installed,sizeof state.installed,"%s",argv[2]);
    if(!empty)state.notes=response(local_notes);
    assert(lu_do_check(&state,err,sizeof err));
    assert(builds_calls==1&&manifest_calls==1&&notes_calls==fetch);
    assert(!strcmp(state.latest,latest)&&state.full.id==1&&state.manifest.id==3);
    assert(!strcmp(state.latest_data,"fixture-approved-data")&&strlen(state.full.sha)==64);
    assert(!state.locked&&!strcmp(state.installed,argv[2]));
    if(fetch){assert(state.notes&&!strcmp(state.notes,"## Approved public notes\n\nCurrent public update body.\n\n"));assert(state.notes_dirty==1);}
    else{assert(empty?!state.notes:state.notes&&!strcmp(state.notes,local_notes));assert(state.notes_dirty==0);}
    printf("LOCAL-CHANGELOG|PASS|installed=%s|approved=%s|notes_requests=%d|metadata_and_manifest_read=1|no_runtime_apply=1\n",argv[2],latest,notes_calls);
    free(state.notes);SDL_DestroyMutex(state.lock);SDL_Quit();return 0;
}
