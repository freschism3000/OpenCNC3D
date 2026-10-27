#ifndef CNC_IOS_TOUCH_LOGIC_H
#define CNC_IOS_TOUCH_LOGIC_H
#include <cmath>
#include <cstdint>

namespace cnc_touch {
// Screen-space sectors: up Advance, right Focus, down Stop, left Move.
inline int direction(float dx,float dy,int previous=-1) {
    if(dx*dx+dy*dy<28.f*28.f)return -1;
    const float angle=std::atan2(dy,dx)*180.f/3.14159265358979323846f;
    const float centers[4]={-90,0,90,180};
    if(previous>=0&&previous<4){
        float delta=std::remainder(angle-centers[previous],360.f);
        if(std::fabs(delta)<=55.f)return previous;
    }
    if(angle>=-135&&angle< -45)return 0;
    if(angle>=-45&&angle<45)return 1;
    if(angle>=45&&angle<135)return 2;
    return 3;
}

// A confirmation belongs to one building, cell and camera/layout revision.
// A gesture interruption or navigation invalidates it, without a time limit.
struct PlacementChoice {
    bool selected=false;
    int type=-1,id=-1,x=0,y=0;
    uint64_t revision=0;
    void clear(){selected=false;}
    bool matches(int t,int i,int cx,int cy,uint64_t view) const {
        return selected&&type==t&&id==i&&x==cx&&y==cy&&revision==view;
    }
    void select(int t,int i,int cx,int cy,uint64_t view){
        selected=true;type=t;id=i;x=cx;y=cy;revision=view;
    }
};
}
#endif
