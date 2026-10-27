#!/usr/bin/env python3
from pathlib import Path
import argparse,subprocess,json,re
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).parent/'prototype/game/ebld_mod.h');p.add_argument('--out',type=Path,default=Path(__file__).parent/'decode-test');a=p.parse_args();a.out.mkdir(exist_ok=True)
s=a.source.read_text();st=s.index('static bool ebld_inflate_texture(');beg=s.index('{',st);end=beg+1;depth=1
while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
code=r'''#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <zlib.h>
struct EbldTex{int w,h,house;long csize;};
'''+s[st:end]+r'''
int main(){unsigned checks=0;unsigned seed=987654321;
 auto check=[&](bool b,const char* name){checks++;if(!b){printf("FAIL %s\n",name);return false;}return true;};
 for(int side: {2,16,128,512,2048})for(int house:{0,1,3})for(int pattern:{0,1,2}) {
  const size_t sz=(size_t)side*side*4,want=sz+(house?sz:0)+(house==3?sz/4:0);
  std::vector<unsigned char> raw(want);for(size_t i=0;i<want;i++){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;raw[i]=pattern==0?0:pattern==1?(unsigned char)(i*7):(unsigned char)seed;}
  uLongf zsize=compressBound((uLong)want);std::vector<unsigned char> z(zsize);if(compress2(z.data(),&zsize,raw.data(),raw.size(),6)!=Z_OK)return 2;z.resize(zsize);
  auto run=[&](std::vector<unsigned char> data,size_t length,bool expected){FILE*f=tmpfile();fwrite(data.data(),1,data.size(),f);rewind(f);EbldTex t={side,side,house,(long)length};std::vector<unsigned char> rgba(sz),nod,mask;bool ok=ebld_inflate_texture(f,t,rgba,nod,mask);fclose(f);if(!check(ok==expected,"status")){printf("case side=%d house=%d pattern=%d data=%zu length=%zu ok=%d expected=%d\n",side,house,pattern,data.size(),length,ok,expected);return false;}if(ok){if(!check(std::equal(rgba.begin(),rgba.end(),raw.begin()),"RGBA exact"))return false;if(house&&!check(std::equal(nod.begin(),nod.end(),raw.begin()+sz),"Nod exact"))return false;if(house==3&&!check(std::equal(mask.begin(),mask.end(),raw.begin()+sz*2),"mask exact"))return false;}return true;};
  if(!run(z,z.size(),true))return 3;
  auto trailing=z;trailing.insert(trailing.end(),7,0xab);if(!run(trailing,trailing.size(),true))return 4;
  for(size_t drop:{size_t(1),size_t(4),size_t(8)})if(z.size()>drop){auto trunc=z;trunc.resize(z.size()-drop);if(!run(trunc,z.size(),false)||!run(trunc,trunc.size(),false))return 5;}
  auto bad=z;bad.back()^=0x80;if(!run(bad,bad.size(),false))return 6;
  std::vector<unsigned char> extra=raw;extra.push_back(55);uLongf n=compressBound(extra.size());std::vector<unsigned char> over(n);if(compress2(over.data(),&n,extra.data(),extra.size(),6)!=Z_OK)return 7;over.resize(n);if(!run(over,over.size(),false))return 8;
 }
 printf("DECODE|checks=%u|failures=0|max=2048|house=0,1,3|patterns=3|truncated_corrupt_overlong=refused\n",checks);return 0;
}
'''
(a.out/'test.cpp').write_text(code);subprocess.run(['clang++','-std=c++14','-O2',str(a.out/'test.cpp'),'-lz','-o',str(a.out/'test')],check=True);q=subprocess.run([str(a.out/'test')],text=True,capture_output=True);print(q.stdout,end='');assert q.returncode==0,q.stderr+q.stdout
v={'passed':True,'actual_helper':True,'checks':int(re.search(r'checks=(\d+)',q.stdout)[1]),'exact_rgba_nod_mask':True,'max_atlas':2048,'corruption_truncation_overflow_refused':True,'no_game_or_UI':True};(a.out/'verification.json').write_text(json.dumps(v,indent=2)+'\n')
