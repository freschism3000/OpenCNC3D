"""Source palette selection and explicit-mask reference regression without a GL window."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RaInfantryColour(unittest.TestCase):
    def test_source_palette_material_reference_and_unchanged_remaps(self):
        source = (ROOT / "game/cnc_eyes.cpp").read_text()
        marker = "static bool ra_infantry_campaign_palette(void)\n{"
        start = source.index(marker)
        end = source.index("\n}", start) + 2
        campaign_policy = source[start:end]
        code = r'''
#include <cassert>
#include <cmath>
#include <cstring>
#include "ra_infantry_colour.h"
#include "model_cloth_reference.h"
static bool g_skirmishRunning = false;
static int network_active = 0;
static int nm_active() { return network_active; }
CAMPAIGN_POLICY
static double linear(double v) {
    v /= 255.0;
    return v <= .04045 ? v / 12.92 : pow((v + .055) / 1.055, 2.4);
}
int main() {
    // The source PALETTE.CPS row1's column6 in TEMPERAT.PAL is104,116,160.
    // Keep the full eight-row order, including RA grey5/blue6, separate from TD.
    const unsigned char source[8][3] = {
        {144,136,76},{104,116,160},{176,0,0},{120,152,100},
        {212,120,16},{148,124,112},{64,132,116},{152,76,56}};
    assert(memcmp(source,RA_RADAR_BAR,sizeof source)==0);
    for(int row=0;row<8;++row) for(int c=0;c<3;++c)
        assert(fabs(RA_SOLDIER_CLOTH[row][c]-linear(source[row][c]))<.00006);
    // Use the actual running-mode guard, including transitions through the same
    // process. A new campaign or loaded campaign must not inherit a previous match.
    assert(ra_infantry_campaign_palette());
    g_skirmishRunning=true;
    assert(!ra_infantry_campaign_palette());
    g_skirmishRunning=false;network_active=1;
    assert(!ra_infantry_campaign_palette());
    g_skirmishRunning=true;network_active=1;
    assert(!ra_infantry_campaign_palette());
    g_skirmishRunning=false;network_active=0;
    assert(ra_infantry_campaign_palette());
    for(int campaign=0;campaign<2;++campaign)
      for(int enhanced=0;enhanced<2;++enhanced) for(int row=0;row<8;++row) {
        float team[3]={.95f,.82f,.30f},cloth[3]={1,1,1},amount=0;
        ra_infantry_colours(row,"E1",enhanced,campaign,team,cloth,&amount);
        assert(amount==1);
        // Campaign France's blue slot uses the existing Allied-blue source donor;
        // a multiplayer player's teal slot6 remains exactly that chosen colour.
        const int uniform=campaign&&row==6?1:row;
        assert(ra_infantry_uniform_remap(row,campaign)==uniform);
        const bool normalized=enhanced&&uniform==1;
        assert(ra_infantry_source_cloth(row,"E1",enhanced,campaign)==normalized);
        for(int c=0;c<3;++c) {
            assert(team[c]==RA_SOLDIER_CLOTH[uniform][c]);
            const float previous=RA_SOLDIER_CLOTH[uniform][c]*RA_SOLDIER_CLOTH_SCALE;
            assert(cloth[c]==(normalized?RA_SOLDIER_CLOTH[uniform][c]:previous));
        }
    }
    // An engineer has no source remapped cloth; invalid owner data keeps the caller's
    // original fallback. Nobody silently becomes Allied blue from a default integer0.
    for(int row=-1;row<=8;++row) {
        float team[3]={.2f,.3f,.4f},cloth[3]={1,1,1},amount=0;
        ra_infantry_colours(row,"E6",true,true,team,cloth,&amount);
        assert(amount==0&&cloth[0]==1&&cloth[1]==1&&cloth[2]==1);
        assert(!ra_infantry_source_cloth(row,"E6",true,true));
        if(row<0||row>=8) assert(team[0]==.2f&&team[1]==.3f&&team[2]==.4f);
    }
    // Base alpha is deliberately unrelated. The sole owner is paired ORM alpha;
    // an unmasked red pixel must not contaminate the cloth reference.
    const unsigned char base[]={64,64,64,0, 192,192,192,255, 255,0,0,255};
    const unsigned char orm[] ={0,0,0,255, 0,0,0,128, 0,0,0,0};
    const float ref=m3_cloth_reference_rgba(base,orm,3);
    const double expected=(linear(64)+linear(192)*128.0/255.0)/(1+128.0/255.0);
    assert(fabs(ref-expected)<1e-7);
    assert(m3_cloth_reference_rgba(nullptr,orm,3)==0);
    assert(m3_cloth_reference_rgba(base,nullptr,3)==0);
    assert(m3_cloth_reference_rgba(base,orm,0)==0);
    const unsigned char empty[]={0,0,0,0};
    assert(m3_cloth_reference_rgba(base,empty,1)==0);
    // Normalizing two genuinely different atlases gives the same source blue mean,
    // while preserving each atlas's own light/dark ratio rather than flattening it.
    const unsigned char base2[]={32,32,32,255, 96,96,96,0};
    const unsigned char full[] ={0,0,0,255, 0,0,0,255};
    const float ref2=m3_cloth_reference_rgba(base2,full,2);
    for(int c=0;c<3;++c) {
        const double target=RA_SOLDIER_CLOTH[1][c];
        const double mean=(target*linear(32)/ref2+target*linear(96)/ref2)/2;
        assert(fabs(mean-target)<1e-7);
        assert(fabs((target*linear(96)/ref2)/(target*linear(32)/ref2)
                    -linear(96)/linear(32))<1e-10);
    }
}
'''.replace("CAMPAIGN_POLICY", campaign_policy)
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            cpp = folder / "test.cpp"
            cpp.write_text(code)
            binary = folder / "test"
            subprocess.run(["clang++", "-std=c++14", "-I", str(ROOT / "game"),
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
