/* Native updater/parser/controller fixture. The transport is replaced by a
 * counting failure stub: no actual HTTP, renderer or window is possible. */
#define SDL_MAIN_HANDLED
#include "../../launcher/lnet.h"
#define main launcher_entry
#include "../../launcher/launcher.c"
#undef main
static int fixture_get_file(const char *,const char *,const char *,LN_Progress,void *,char *,int);
static int fixture_get_range(const char *,const char *,const char *,long long,long long,int,volatile int *,LN_Progress,void *,long *,char *,int);
static char *fixture_get_text(const char *,const char *,long,char *,int);
static char *fixture_get_text_reach(const char *,const char *,long,int *,char *,int);
#define ln_get_file fixture_get_file
#define ln_get_range fixture_get_range
#define ln_get_text fixture_get_text
#define ln_get_text_reach fixture_get_text_reach
#include "../../launcher/lupdate.c"
#undef ln_get_file
#undef ln_get_range
#undef ln_get_text
#undef ln_get_text_reach
#include <assert.h>
static int fixture_calls,fixture_asset;
static const char *fixture_builds;
static char *fixture_get_text(const char *url,const char *key,long cap,char *err,int len)
{(void)key;(void)cap;(void)err;(void)len;return strdup(strstr(url,"/api/builds")?fixture_builds:"{\"entries\":[]}");}
static char *fixture_get_text_reach(const char *url,const char *key,long cap,int *unreached,char *err,int len)
{if(unreached)*unreached=0;return fixture_get_text(url,key,cap,err,len);}
static void fixture_request(const char *url,char *err,int len)
{const char *p=strrchr(url,'=');fixture_calls++;fixture_asset=p?atoi(p+1):-1;snprintf(err,(size_t)len,"fixture transport stopped before download");}
static int fixture_get_file(const char *url,const char *key,const char *path,LN_Progress cb,void *user,char *err,int len)
{(void)key;(void)path;(void)cb;(void)user;fixture_request(url,err,len);return 0;}
static int fixture_get_range(const char *url,const char *key,const char *path,long long offset,long long length,int stall,volatile int *cancel,LN_Progress cb,void *user,long *status,char *err,int len)
{(void)key;(void)path;(void)offset;(void)length;(void)stall;(void)cancel;(void)cb;(void)user;(void)status;fixture_request(url,err,len);return LN_RANGE_DISK;}

static void check_fixture(const char *json,const char *installed,int fresh,const char *pinned,
                          LU_Phase phase,const char *latest,long long asset)
{
    LU_State u;
    memset(&u,0,sizeof u);u.lock=SDL_CreateMutex();assert(u.lock);
    snprintf(u.installed,sizeof u.installed,"%s",installed);u.fresh=fresh;
    if(pinned)snprintf(u.want,sizeof u.want,"%s",pinned);
    fixture_builds=json;u.phase=LU_CHECKING;assert(!lu_thread(&u));
    assert(u.phase==phase);
    if(latest)assert(!strcmp(u.latest,latest));
    if(asset>0)assert(u.full.id==asset);
    else if(!asset)assert(u.full.id<=0);
    if(phase==LU_FAILED)assert(strstr(u.status,"package")||strstr(u.status,"does not list"));
    free(u.notes);SDL_DestroyMutex(u.lock);
}

static void platform_checks(void)
{
    const char *preview="{\"ok\":true,\"latest\":{\"tag\":\"v0.8.6\",\"assets\":["
        "{\"id\":86,\"name\":\"OpenCNC3D-windows-v0.8.6-preview.zip\",\"size\":100}]},\"releases\":["
        "{\"tag\":\"v0.8.5\",\"assets\":[{\"id\":85,\"name\":\"OpenCNC3D-macos-v0.8.5.zip\",\"size\":100},"
        "{\"id\":185,\"name\":\"OpenCNC3D-windows-v0.8.5.zip\",\"size\":100}]},"
        "{\"tag\":\"v0.8.6\",\"assets\":[{\"id\":86,\"name\":\"OpenCNC3D-windows-v0.8.6-preview.zip\",\"size\":100}]}]}";
    const char *windows_only="{\"latest\":{\"tag\":\"v0.9.1\",\"assets\":["
        "{\"id\":191,\"name\":\"OpenCNC3D-windows-v0.9.1.zip\",\"size\":100}]},\"releases\":["
        "{\"tag\":\"v0.9.0\",\"assets\":[{\"id\":90,\"name\":\"OpenCNC3D-macos-v0.9.0.zip\",\"size\":100},"
        "{\"id\":190,\"name\":\"OpenCNC3D-windows-v0.9.0.zip\",\"size\":100}]},"
        "{\"tag\":\"v0.9.1\",\"assets\":[{\"id\":191,\"name\":\"OpenCNC3D-windows-v0.9.1.zip\",\"size\":100}]}]}";
    const char *no_full="{\"latest\":{\"tag\":\"v0.9.1\",\"assets\":["
        "{\"id\":9,\"name\":\"RA3DComplete-macos-v0.9.1.pkg\",\"kind\":\"full\"}]},\"releases\":["
        "{\"tag\":\"v0.9.1\",\"assets\":[]}]}";
    LJ_Value *root;const LJ_Value *mac,*win;char err[256];
    root=lj_parse(windows_only,err,sizeof err);assert(root);
    mac=lu_platform_release(root,"macos");win=lu_platform_release(root,"windows");
    assert(mac&&win&&!strcmp(lj_str(mac,"tag"),"v0.9.0")&&!strcmp(lj_str(win,"tag"),"v0.9.1"));
    assert(lu_release_has_full(mac,"macos")&&!lu_release_has_full(win,"macos"));lj_free(root);
#ifdef _WIN32
    check_fixture(windows_only,"0.8.7",0,NULL,LU_AVAILABLE,"0.9.1",191);
    check_fixture(windows_only,"0.9.1",0,NULL,LU_UPTODATE,"0.9.1",191);
    check_fixture(preview,"0.8.4",0,NULL,LU_AVAILABLE,"0.8.5",185);
    check_fixture(preview,"0.8.7+source",0,NULL,LU_UPTODATE,"0.8.5",185);
#else
    check_fixture(windows_only,"0.8.7",0,NULL,LU_AVAILABLE,"0.9.0",90);
    check_fixture(windows_only,"0.9.1",0,NULL,LU_UPTODATE,"0.9.0",90);
    check_fixture(windows_only,"?",1,"0.9.1",LU_FAILED,"0.9.1",0);
    check_fixture(preview,"0.8.4",0,NULL,LU_AVAILABLE,"0.8.5",85);
    check_fixture(preview,"0.8.7+source",0,NULL,LU_UPTODATE,"0.8.5",85);
#endif
    check_fixture(no_full,"0.9.0",0,NULL,LU_FAILED,"0.9.1",0);
    check_fixture(no_full,"0.9.1",0,NULL,LU_UPTODATE,"0.9.1",0);
    check_fixture(no_full,"0.9.2+source",0,NULL,LU_UPTODATE,"0.9.1",0);
    check_fixture(no_full,"0.9.2",1,NULL,LU_FAILED,"0.9.1",0);
    check_fixture(no_full,"0.9.2",1,"0.9.1",LU_FAILED,"0.9.1",0);
    check_fixture(preview,"?",1,"0.8.6",LU_FAILED,"0.8.6",0);
    check_fixture(preview,"?",1,"0.9.0",LU_FAILED,NULL,0);
    puts("UPDATE-PLATFORM|PASS|approved_feed_only=1|mac_windows_exact_identity=1|windows_only_latest=1|preview_skipped=1|current_and_newer=1|true_missing_fails=1|pinned_strict=1");
}

static void apply_policy(const char *dir,const char *mode)
{
    LU_State u;L_App a;char err[512];int blocked=!strcmp(mode,"blocked"),fresh=!strcmp(mode,"fresh-full"),before_launch;
    memset(&u,0,sizeof u);u.lock=SDL_CreateMutex();assert(u.lock);snprintf(u.dir,sizeof u.dir,"%s",dir);u.fresh=fresh;
    u.full.id=1;u.full.size=100;snprintf(u.full.name,sizeof u.full.name,"TD full fixture");
    u.bins.id=2;u.bins.size=20;snprintf(u.bins.name,sizeof u.bins.name,"TD binary fixture");
    snprintf(u.latest_data,sizeof u.latest_data,"matching-data");snprintf(u.latest,sizeof u.latest,"0.9.1");
    fixture_calls=fixture_asset=0;
    assert(!lu_do_apply(&u,err,sizeof err));
    if(blocked){assert(!fixture_calls);assert(!u.locked);assert(strstr(err,"OpenCNC3D Installer"));}
    else{assert(fixture_calls==1);assert(fixture_asset==(!strcmp(mode,"td-bins")?2:1));assert(strstr(err,"fixture transport"));}

    /* A future platform update must not steal RA's Launch action. TD installs
     * still expose Update; missing/empty installs still have disabled Launch. */
    memset(&a,0,sizeof a);snprintf(a.dir,sizeof a.dir,"%s",dir);a.up=&u;u.phase=LU_AVAILABLE;
    a.mod[0].name="OpenCNC3D";a.mod[0].installed=lmod_td_present(dir);
    a.mod[1].name="RA3D";a.mod[1].installed=lmod_ra_present(dir,NULL,0);a.mod_sel=a.mod[1].installed?1:0;
    l_set_view(&a,L_VIEW_MODULES);
    assert(!strcmp(a.btn[L_BTN_PLAY].label,a.mod[0].installed&&a.mod_sel==0?"Update":"Launch"));
    if(a.mod[1].installed){
        before_launch=fixture_calls;
        assert(!a.btn[L_BTN_PLAY].disabled);l_activate(&a,L_BTN_PLAY);
        assert(a.play_after==1&&a.quit==1&&!u.worker&&fixture_calls==before_launch);
        if(a.mod[0].installed){a.mod_sel=0;a.play_after=a.quit=0;l_set_buttons(&a);assert(!strcmp(a.btn[L_BTN_PLAY].label,"Update")&&!a.btn[L_BTN_PLAY].disabled);}
    }else if(!a.mod[0].installed)assert(a.btn[L_BTN_PLAY].disabled);
    a.media_add_only=1;a.view=L_VIEW_MODULE_OPTIONS;a.want_movies=a.want_music=0;a.any_movies=a.any_music=1;
    assert(!strcmp(l_m_note(&a,0),"KEPT")&&!strcmp(l_m_note(&a,1),"KEPT"));
    a.media_add_only=0;a.view=L_VIEW_SETTINGS;
    assert(!strcmp(l_m_note(&a,0),"WILL BE REMOVED")&&!strcmp(l_m_note(&a,1),"WILL BE REMOVED"));
    if(u.locked)lz_unlock(&u.lk);SDL_DestroyMutex(u.lock);
    printf("UPDATE-MODULE-POLICY|PASS|mode=%s|transport_calls=%d|asset=%d|RA_launch_preserved=1|notes_distinct=1\n",mode,fixture_calls,fixture_asset);
}

int main(int argc,char **argv)
{
    LU_State u;
    char err[256];
    const char *json = "{\"ok\":true,\"latest\":{\"tag\":\"v0.9.0\",\"assets\":["
        "{\"id\":1,\"name\":\"OpenCNC3D-" LU_PLATFORM "-v0.9.0.zip\",\"platform\":\"" LU_PLATFORM "\",\"kind\":\"full\",\"size\":100},"
        "{\"id\":2,\"name\":\"OpenCNC3D-" LU_PLATFORM "-v0.9.0-bins.zip\",\"kind\":\"binaries\",\"size\":20},"
        "{\"id\":3,\"name\":\"OpenCNC3D-v0.9.0-manifest.txt\",\"size\":10},"
        "{\"id\":4,\"name\":\"OpenCNC3D-module-ra-macos-v0.9.0.zip\",\"platform\":\"macos\",\"kind\":\"full\",\"size\":90},"
        "{\"id\":5,\"name\":\"OpenCNC3D-module-td-macos-v0.9.0.zip\",\"platform\":\"macos\",\"kind\":\"binaries\",\"size\":80},"
        "{\"id\":6,\"name\":\"OpenCNC3D-v0.9.0-modules.txt\",\"kind\":\"full\",\"size\":5},"
        "{\"id\":7,\"name\":\"OpenCNC3DInstaller-macos-v0.9.0.pkg\",\"platform\":\"macos\",\"kind\":\"full\",\"size\":9},"
        "{\"id\":8,\"name\":\"OpenCNC3DComplete-macos-v0.9.0.pkg\",\"platform\":\"macos\",\"kind\":\"full\",\"size\":9},"
        "{\"id\":9,\"name\":\"RA3DComplete-macos-v0.9.0.pkg\",\"platform\":\"macos\",\"kind\":\"full\",\"size\":9},"
        "{\"id\":10,\"name\":\"OpenCNC3DBinaries-macos-v0.9.0.zip\",\"platform\":\"macos\",\"kind\":\"full\",\"size\":9},"
        "{\"id\":11,\"name\":\"RA3DBinaries-macos-v0.9.0.zip\",\"platform\":\"macos\",\"kind\":\"full\",\"size\":9},"
        "{\"id\":12,\"name\":\"OpenCNC3DInstaller-windows-v0.9.0.exe\",\"platform\":\"windows\",\"kind\":\"full\",\"size\":9},"
        "{\"id\":13,\"name\":\"OpenCNC3DComplete-windows-v0.9.0.exe\",\"platform\":\"windows\",\"kind\":\"full\",\"size\":9},"
        "{\"id\":14,\"name\":\"RA3DComplete-windows-v0.9.0.exe\",\"platform\":\"windows\",\"kind\":\"full\",\"size\":9},"
        "{\"id\":15,\"name\":\"OpenCNC3DBinaries-windows-v0.9.0.zip\",\"platform\":\"windows\",\"kind\":\"full\",\"size\":9},"
        "{\"id\":16,\"name\":\"RA3DBinaries-windows-v0.9.0.zip\",\"platform\":\"windows\",\"kind\":\"full\",\"size\":9}]}}";
    memset(&u, 0, sizeof u);
    u.lock = SDL_CreateMutex();
    assert(u.lock);
    assert(lu_read_builds(&u, json, err, sizeof err));
    assert(u.full.id == 1 && u.manifest.id == 3);
#ifdef _WIN32
    assert(u.bins.id == 15);
#else
    assert(u.bins.id == 10);
#endif
    assert(!strcmp(u.latest, "0.9.0"));
    /* A module-only catalog must not pretend it has a complete update. */
    assert(!lu_read_builds(&u,
        "{\"latest\":{\"tag\":\"v0.9.0\",\"assets\":[{\"id\":9,\"name\":\"OpenCNC3D-module-ra-macos-v0.9.0.zip\",\"platform\":\"macos\",\"kind\":\"full\"}]}}",
        err, sizeof err));
    assert(u.full.id == -1);
    SDL_DestroyMutex(u.lock);
    platform_checks();
    puts("UPDATE-MODULE-SELECTION|PASS|legacy_full=1|new_td_bins_named=1|all_ten_misclassified_products_safe=1|module_assets_ignored=1");
    assert(argc==3);apply_policy(argv[1],argv[2]);
    return 0;
}
