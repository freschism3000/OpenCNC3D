"""Dry coast feathers continue across icons without changing land, alpha or the floor."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WaterCoastFeather(unittest.TestCase):
    def test_td_atlas_scale_and_variant_offsets(self):
        source = (ROOT / 'game/water_mod.h').read_text()
        edt = source[source.index('#define WATER_EDT_INF'):
                     source.index('/* One separable box blur')]
        gutter = source[source.index('static void water_coast_gutter'):
                        source.index('static int    g_waterSoftUW')]
        wiring = source[source.index('static size_t water_soft_material_td'):
                        source.index('static bool water_soft_sync_for')]
        code = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>
#define TT_COLS 32
#define TT_PITCH 26
#define TT_TS 24
#define TT_GUTTER 1
#define EDIT_TEMPLATE_COUNT 4
struct TtSlot { int tmpl,icon,slot; };
struct PackCell { int slot; bool holes; };
struct PackTex { int w,h,uw,uh; };
struct EnhVar { int has; float du[3],dv[3]; };
struct { const char* theater="test"; std::vector<PackCell> cell;
         std::vector<EnhVar> enhVar; } g_pack;
struct { int w,h; } EDIT_TEMPLATES[4]={{1,1},{1,1},{1,1},{2,1}};
static TtSlot table[2]={{3,0,0},{3,1,31}};
static const TtSlot* tt_table(const char*,int* count){*count=2;return table;}
static int ts_slot_of_cell(const PackCell& cell,int){return cell.slot;}
static bool water_cell_wet(const PackCell& cell){return cell.holes;}
static bool coast_mask_apply(unsigned char*,int,int,size_t,int,const char*,int,int){return false;}
''' + edt + '\n#include "' + str(ROOT / 'game/water_coast_feather.h') + '"\n' + gutter + wiring + r'''
int main() {
    for(int scale:{1,5,6}) {
        const int pitch=26*scale,cell=24*scale,gutter=scale;
        PackTex at={32*pitch,2*pitch,32*pitch,2*pitch};
        std::vector<unsigned char> atlas((size_t)at.w*at.h*4,71);
        g_pack.cell={{0,true}};g_pack.enhVar.assign(32,EnhVar());
        // Base0->alternate63 shifts right, base31->alternate32 shifts left.
        // Both move down by one row. This is EnhVar's normalized source law.
        g_pack.enhVar[0].has=2;g_pack.enhVar[0].du[1]=31.0f/32;
        g_pack.enhVar[0].dv[1]=0.5f;
        g_pack.enhVar[31].has=2;g_pack.enhVar[31].du[1]=-31.0f/32;
        g_pack.enhVar[31].dv[1]=0.5f;
        for(int variant=0;variant<2;++variant)for(int icon=0;icon<2;++icon) {
            const int slot=variant?(icon?32:63):(icon?31:0);
            const int ox=(slot%32)*pitch+gutter,oy=(slot/32)*pitch+gutter;
            for(int y=0;y<cell;++y)for(int x=0;x<cell;++x) {
                auto p=&atlas[((size_t)(oy+y)*at.w+ox+x)*4];
                p[0]=5;p[1]=90;p[2]=220;p[3]=160;
                if(!icon && x<cell/2) {
                    p[0]=variant?80:110;p[1]=variant?30:70;p[2]=variant?20:40;p[3]=255;
                }
            }
        }
        const auto before=atlas;
        assert(water_soft_material_td(atlas,at,true)==(size_t)3*cell*cell);
        // Opaque and fractional coverage remain exact in each base/alternate
        // icon. Right icons obtain their material across logical cell joins.
        for(int variant=0;variant<2;++variant)for(int icon=0;icon<2;++icon) {
            const int slot=variant?(icon?32:63):(icon?31:0);
            const int ox=(slot%32)*pitch+gutter,oy=(slot/32)*pitch+gutter;
            for(int y=0;y<cell;++y)for(int x=0;x<cell;++x) {
                const size_t i=((size_t)(oy+y)*at.w+ox+x)*4;
                assert(atlas[i+3]==before[i+3]);
                assert(atlas[i]==(variant?80:110));
                assert(atlas[i+1]==(variant?30:70));
                assert(atlas[i+2]==(variant?20:40));
            }
        }
        // An unrelated atlas slot and the original effects-off source survive.
        const size_t unrelated=((size_t)gutter*at.w+pitch+gutter)*4;
        assert(!memcmp(&atlas[unrelated],&before[unrelated],4));
        auto fallback=before;g_pack.cell.clear();
        assert(water_soft_material_td(fallback,at,true)==0);
        assert(fallback==before);
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source_file = directory / 'test.cpp'
            executable = directory / 'test'
            source_file.write_text(code)
            subprocess.run(['clang++', '-std=c++14', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source_file), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_cross_icon_material_and_immutable_coverage(self):
        source = (ROOT / 'game/water_mod.h').read_text()
        edt = source[source.index('#define WATER_EDT_INF'):
                     source.index('/* One separable box blur')]
        code = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
''' + edt + '\n#include "' + str(ROOT / 'game/water_coast_feather.h') + '"\n' + r'''
int main() {
    // The right icon has no dry donor of its own. It must get the actual bank
    // across the logical template join, never a neighbouring atlas slot's blue.
    const int w=8,h=4,row=40;
    std::vector<unsigned char> pixels(row*h,37);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        auto p=pixels.data()+y*row+x*4;
        p[0]=5;p[1]=100;p[2]=220;p[3]=x==0?255:(y==3?0:160);
        if(x==0){p[0]=110;p[1]=70;p[2]=40;}
    }
    const auto before=pixels;
    WaterCoastFeatherRegion left={pixels.data(),pixels.data()+3,row,row,4,4,0,0,4,h};
    WaterCoastFeatherRegion right={pixels.data()+16,pixels.data()+19,row,row,4,4,4,0,4,h};
    assert(water_coast_feather_template({right},w,h)==0);
    assert(pixels==before);
    assert(water_coast_feather_template({left,right},w,h)==21);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const int i=y*row+x*4;
        assert(pixels[i+3]==before[i+3]);
        if(before[i+3]==0||before[i+3]==255)assert(!memcmp(&pixels[i],&before[i],4));
        else {assert(pixels[i]==110);assert(pixels[i+1]==70);assert(pixels[i+2]==40);}
    }
    for(int y=0;y<h;++y)for(int x=w*4;x<row;++x)assert(pixels[y*row+x]==37);

    // Smooth between two actual dry materials instead of making a hard nearest-
    // donor/Voronoi line inside the transparent band.
    std::vector<unsigned char> line(8*4,99);
    for(int x=0;x<8;++x) {
        line[x*4]=x==7?210:0;line[x*4+1]=0;line[x*4+2]=0;
        line[x*4+3]=x==0||x==7?255:127;
    }
    assert(water_coast_feather_rgb(line.data(),8,1,32,4,line.data()+3,32,4)==6);
    for(int x=1;x<8;++x) {
        assert(line[x*4]>=line[(x-1)*4]);
        assert(line[x*4]-line[(x-1)*4]<40);
    }
    std::vector<unsigned char> missing(8,0);
    const auto frozen=line;
    assert(water_coast_feather_rgb(line.data(),8,1,32,4,line.data()+3,32,4,missing.data())==0);
    assert(line==frozen);
    assert(water_coast_feather_rgb(nullptr,8,1,32,4,line.data()+3,32,4)==0);
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source_file = directory / 'test.cpp'
            executable = directory / 'test'
            source_file.write_text(code)
            subprocess.run(['clang++', '-std=c++14', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(source_file), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    unittest.main()
