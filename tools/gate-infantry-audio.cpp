/* Actual mixer signals for soldier foley, including isolated slot pools. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../game/infantry_audio_math.h"
#include "../audio/mixer.h"
static void close_to(float a,float b){assert(fabsf(a-b)<0.00001f);}
int main()
{
    close_to(infantry_audio_zoom_gain(1),1);
    close_to(infantry_audio_zoom_gain(.75f),.625f);
    close_to(infantry_audio_zoom_gain(.5f),.25f);
    close_to(infantry_audio_zoom_gain(.25f),.25f);
    close_to(infantry_audio_zoom_gain(0),.25f);
    close_to(infantry_audio_zoom_gain(ambience_zoom_fraction(3000,1000,9000)),.625f);
    const char* types[]={"E1","E2","E3","E4","E5","E6","RMBO","E1TR","E7","MEDI","SHOK"};
    for(int i=0;i<11;++i)assert(infantry_audio_is_soldier(types[i]));
    assert(!infantry_audio_is_soldier("C1") && !infantry_audio_is_soldier("DOG"));
    short pcm[]={1000,2000,3000,4000,5000,6000}; short out[1024*2];
    Mixer* m=mixer_create(); assert(m);
    int h=mixer_play_foley(m,pcm,3,0);assert((h&255)>=MIX_VOICES+MIX_VEHICLE_VOICES);
    mixer_voice_gain(m,h,1000,0);mixer_render(m,out,1);assert(out[0]==1000 && out[1]==2000);
    mixer_voice_gain(m,h,250,0);mixer_render(m,out,1);assert(out[0]==750 && out[1]==1000);
    mixer_voice_gain(m,h,1000,-1000);mixer_render(m,out,1);assert(out[0]>0 && out[1]==0);
    mixer_render(m,out,1);assert(!mixer_voice_active(m,h)); // one shot, never loops
    int vehicles[MIX_VEHICLE_VOICES];
    for(int i=0;i<MIX_VEHICLE_VOICES;++i)vehicles[i]=mixer_play_vehicle_loop(m,pcm,3,0,i);
    assert(mixer_play_vehicle_loop(m,pcm,3,0,0)==-1);
    int bed=mixer_play_stereo(m,MIX_BUS_AMBIENCE,pcm,3,1000,0,255,1);
    int eva=mixer_play(m,MIX_BUS_SPEECH,pcm,6,1000,0,255);
    for(int i=0;i<MIX_FOLEY_VOICES+16;++i)assert(mixer_play_foley(m,pcm,3,0)>=0);
    assert(mixer_voice_active(m,bed) && mixer_voice_active(m,eva));
    for(int i=0;i<MIX_VEHICLE_VOICES;++i)assert(mixer_voice_active(m,vehicles[i]));
    mixer_stop_bus(m,MIX_BUS_AMBIENCE);mixer_stop_bus(m,MIX_BUS_SPEECH);mixer_stop_bus(m,MIX_BUS_FX);
    h=mixer_play_foley(m,pcm,3,1000);mixer_voice_gain(m,h,1000,1000);
    mixer_bus_gain(m,MIX_BUS_FX,0,0);mixer_render(m,out,4);
    for(int i=0;i<8;++i)assert(out[i]==0);
    assert(!mixer_voice_active(m,h));
    assert(INFANTRY_AUDIO_VARIANTS==10);
    close_to((float)infantry_audio_gain(1,1,900),900);
    close_to((float)infantry_audio_gain(1,1,1100),1100);
    close_to((float)infantry_audio_gain(.5f,1,1100),275);
    close_to((float)infantry_audio_gain(0,.5f,900),113);
    mixer_bus_gain(m,MIX_BUS_FX,1000,0);
    h=mixer_play_foley_pitched(m,pcm,3,0,900);mixer_voice_gain(m,h,1000,0);
    mixer_render(m,out,2);assert(out[0]==1000&&out[1]==2000);
    assert(abs(out[2]-2800)<=1 && abs(out[3]-3800)<=1);
    mixer_render(m,out,2);assert(!mixer_voice_active(m,h));
    h=mixer_play_foley_pitched(m,pcm,3,0,1100);mixer_voice_gain(m,h,1000,0);
    mixer_render(m,out,2);assert(abs(out[2]-3200)<=1&&abs(out[3]-4200)<=1);
    mixer_render(m,out,1);assert(!mixer_voice_active(m,h));
    // Duration scales with pitch; the fractional phase survives block boundaries,
    // and remains identical whether rendered in large blocks or one frame at a time.
    short longpcm[2002],whole[2400],split[2400];
    for(int i=0;i<1001;++i){longpcm[i*2]=(short)(i*3);longpcm[i*2+1]=(short)(-i*2);}
    const int pitches[]={800,900,937,1000,1073,1100,1200};
    for(int i=0;i<7;++i){
        const int pitch=pitches[i]<900?900:pitches[i]>1100?1100:pitches[i];
        const int rate=(65536*pitch+500)/1000;
        const int frames=(1001*65536+rate-1)/rate;
        h=mixer_play_foley_pitched(m,longpcm,1001,0,pitches[i]);mixer_voice_gain(m,h,1000,0);
        mixer_render(m,whole,frames-1);assert(mixer_voice_active(m,h));
        mixer_render(m,whole+(frames-1)*2,1);assert(!mixer_voice_active(m,h));
        h=mixer_play_foley_pitched(m,longpcm,1001,0,pitches[i]);mixer_voice_gain(m,h,1000,0);
        for(int j=0;j<frames;++j)mixer_render(m,split+j*2,1);
        assert(!mixer_voice_active(m,h));assert(!memcmp(whole,split,frames*2*sizeof(short)));
        h=mixer_play_foley_pitched(m,longpcm,1001,1000,pitches[i]);mixer_voice_gain(m,h,1100,1000);
        mixer_render(m,out,2);assert(out[0]==0&&out[2]==0&&out[3]<0);mixer_voice_stop(m,h);
        h=mixer_play_foley_pitched(m,longpcm,1001,0,pitches[i]);mixer_voice_gain(m,h,1000,0);
        mixer_bus_gain(m,MIX_BUS_FX,0,0);mixer_render(m,whole,frames);
        assert(!mixer_voice_active(m,h));for(int j=0;j<frames*2;++j)assert(whole[j]==0);
        mixer_bus_gain(m,MIX_BUS_FX,1000,0);
    }
    // THE CROWD LIMIT: three walkers untouched, then sqrt(3/n) to a floor of one half;
    // four steps ringing at most and one started a tick, the loudest first, ties to the
    // lower id.
    close_to(infantry_audio_crowd_gain(0),1);close_to(infantry_audio_crowd_gain(1),1);
    close_to(infantry_audio_crowd_gain(3),1);close_to(infantry_audio_crowd_gain(4),sqrtf(.75f));
    close_to(infantry_audio_crowd_gain(12),.5f);close_to(infantry_audio_crowd_gain(40),.5f);
    for(int n=4;n<40;++n)assert(infantry_audio_crowd_gain(n+1)<=infantry_audio_crowd_gain(n));
    assert(INFANTRY_AUDIO_MAX_VOICES==4&&INFANTRY_AUDIO_STARTS_PER_TICK==1);
    assert(infantry_audio_budget(0)==1&&infantry_audio_budget(3)==1);
    assert(infantry_audio_budget(4)==0&&infantry_audio_budget(60)==0);
    {
        const int gain[]={300,900,500,900,100},id[]={7,9,2,4,1};int out[5];
        assert(infantry_audio_pick(gain,id,5,2,out)==2&&out[0]==3&&out[1]==1);
        assert(infantry_audio_pick(gain,id,5,0,out)==0);
        assert(infantry_audio_pick(gain,id,5,9,out)==5&&out[2]==2&&out[3]==0&&out[4]==4);
        assert(infantry_audio_pick(gain,id,0,2,out)==0);
    }
    // A crowd's summed level: n walkers each at crowd gain, uncorrelated, add as sqrt(n);
    // past three it stays within a squad of three's, up to the floor.
    for(int n=4;n<=12;++n)assert(sqrtf((float)n)*infantry_audio_crowd_gain(n)<=sqrtf(3.f)+1e-4f);
    mixer_destroy(m);
    puts("INFANTRY AUDIO PASS: crowd limit (level, voices, starts per tick, loudest first), live zoom curve, soldier filter, one-shot stereo/gain, pitch interpolation/duration/block continuity, persistent volume, mute retirement, separate pools");
}
