/* Tiny real module install/cache fixture; no window, game or network is used. */
#ifndef MODULE_SOURCE
#define MODULE_SOURCE "../../launcher/lmodule.c"
#endif
#include MODULE_SOURCE
#include <assert.h>
#include <unistd.h>
#include RELEASE_NOTES_READER

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"notes fixture failed at line %d: %s\n",__LINE__,#x);exit(1); } } while (0)

static void notes_equal(const char *dir,const char *expected)
{
    char path[1400];char *text;long length;
    snprintf(path,sizeof path,"%s/CHANGELOG.txt",dir);
    text=l_read_file(path,&length);CHECK(text);
    CHECK((size_t)length==strlen(expected)&&!memcmp(text,expected,(size_t)length));free(text);
}
static int same_file(const struct stat *a,const struct stat *b)
{
    if(a->st_dev!=b->st_dev||a->st_ino!=b->st_ino||a->st_size!=b->st_size||a->st_mode!=b->st_mode)return 0;
#ifdef __APPLE__
    return a->st_mtimespec.tv_sec==b->st_mtimespec.tv_sec&&a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec;
#else
    return a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec;
#endif
}
static void install(const char *dir,int target,const char *zip)
{
    char hash[65],error[512];
    CHECK(lz_sha256_file(zip,hash,error,sizeof error));
    if(!lmod_install_file(dir,target,"0.9.0",zip,hash,NULL,NULL,error,sizeof error)){
        fprintf(stderr,"module install failed: %s\n",error);exit(1);
    }
}
static void owned(const char *dir,int target)
{
    char path[1400],error[512];char *text;LMOD_Files files;int i,found=0;
    snprintf(path,sizeof path,"%s/opencnc3d-module-%s.txt",dir,target?"ra":"td");
    text=l_read_file(path,NULL);CHECK(text);
    CHECK(lmod_files_parse(text,target,"0.9.0",&files,error,sizeof error));free(text);
    for(i=0;i<files.count;i++)if(!strcmp(files.owned[i],"CHANGELOG.txt"))found++;
    CHECK(found==1);lmod_files_free(&files);
}
int main(int argc,char **argv)
{
    if(argc==4&&!strcmp(argv[1],"allowed")){
        printf("%d\n",lmod_runtime_name(atoi(argv[2]),argv[3]));return 0;
    }
    if(argc==5&&!strcmp(argv[1],"reject")){
        char hash[65],error[512];CHECK(lz_sha256_file(argv[4],hash,error,sizeof error));
        CHECK(!lmod_install_file(argv[2],atoi(argv[3]),"0.9.0",argv[4],hash,NULL,NULL,error,sizeof error));
        CHECK(strstr(error,"protected player data"));puts("MODULE-NOTES-REFUSAL|PASS");return 0;
    }
    if(argc==6&&!strcmp(argv[1],"exercise")){
        const char *dir=argv[2];char path[1400],error[512];char *expected;
        struct stat before,first,after;int existing;LZ_Lock lock;
        expected=l_read_file(argv[5],NULL);CHECK(expected);
        snprintf(path,sizeof path,"%s/CHANGELOG.txt",dir);existing=!stat(path,&before);
        install(dir,1,argv[4]);notes_equal(dir,expected);owned(dir,1);CHECK(lmod_module_current(dir,1,"0.9.0"));
        CHECK(!stat(path,&first));CHECK((first.st_mode&0777)==0644);
        if(existing)CHECK(same_file(&before,&first));
        install(dir,0,argv[3]);notes_equal(dir,expected);owned(dir,0);CHECK(lmod_td_present(dir));
        CHECK(!stat(path,&after)&&same_file(&first,&after));
        CHECK(lz_lock(dir,&lock)==1);CHECK(lmod_remove(dir,1,error,sizeof error));notes_equal(dir,expected);
        CHECK(lmod_td_present(dir));CHECK(lmod_install_cached(dir,1,"0.9.0",NULL,NULL,error,sizeof error));
        owned(dir,1);notes_equal(dir,expected);CHECK(!stat(path,&after)&&same_file(&first,&after));
        CHECK(lmod_remove(dir,0,error,sizeof error));CHECK(lmod_ra_present(dir,NULL,0));notes_equal(dir,expected);
        CHECK(lmod_remove(dir,1,error,sizeof error));notes_equal(dir,expected);
        CHECK(!stat(path,&after)&&same_file(&first,&after));lz_unlock(&lock);free(expected);
        puts("MODULE-RELEASE-NOTES|PASS|direct_RA_TD=1|cached_RA=1|owned=1|actual_launcher_reader=1|shared_retention=1|same_existing_inode_mode_mtime=1");return 0;
    }
    fprintf(stderr,"usage: gate allowed TARGET PATH | reject DIR TARGET ZIP | exercise DIR TDZIP RAZIP NOTES\n");return 2;
}
