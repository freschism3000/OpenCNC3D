/* Deterministic fair skirmish controller. Included by house.cpp after function.h.
 * All opponent information enters through Knows; TD's explored cells remain
 * visible under its original shroud rules. No live hidden target is retained.
 */
#include "fair_ai_td.h"
#include "../../fair_ai.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern unsigned int RandNumb;
namespace {
struct TDPolicyState {
    uint32_t level, rng, next_think, next_tactic, next_scout, next_attack;
    uint32_t thoughts, orders, scouting, attacks, cursor;
    uint32_t scout[2], scout_goal[2], scout_position[2], objective[3], attacking;
    uint32_t known_cells, contacts, infantry, armor, air;
};
static_assert(sizeof(TDPolicyState)==26u*sizeof(uint32_t), "TD fair AI save layout must remain explicit words");
TDPolicyState td_policy[HOUSE_COUNT];
unsigned char td_pending[fair_ai::Seats] = {1,1,1,1,1,1,1,1};
TDPolicyState& Policy(HouseClass const* h) { return td_policy[(int)h->Class->House]; }
bool Live(TechnoClass const* o) { return o && o->IsActive && !o->IsInLimbo && o->Strength > 0; }
bool Friendly(HouseClass const* h, TechnoClass const* o) { return Live(o) && o->House == h; }
int BuildingCount(HouseClass const* h, StructType type) {
    int n=0; for(int i=0;i<Buildings.Count();++i) { BuildingClass* b=Buildings.Ptr(i);
        if(Friendly(h,b) && b->Class->Type==type && b->Mission!=MISSION_CONSTRUCTION && b->Mission!=MISSION_DECONSTRUCTION) ++n; } return n;
}
int UnitCount(HouseClass const* h, UnitType type) {
    int n=0; for(int i=0;i<Units.Count();++i) { UnitClass* u=Units.Ptr(i); if(u && u->IsActive && u->House==h && u->Strength>0 && u->Class->Type==type) ++n; } return n;
}
int Planned(HouseClass const* h,StructType type) {
    int n=0;for(int i=0;i<Buildings.Count();++i) { BuildingClass* b=Buildings.Ptr(i);
        if(b && b->IsActive && b->House==h && b->Strength>0 && b->Class->Type==type && b->Mission!=MISSION_DECONSTRUCTION) ++n; }return n;
}
bool BuildingQueue(HouseClass const* h) {
    for(int i=0;i<Buildings.Count();++i) { BuildingClass* b=Buildings.Ptr(i);if(b && b->House==h && ((b->Factory && b->Class->ToBuild==RTTI_BUILDINGTYPE) || b->Mission==MISSION_CONSTRUCTION)) return true; }return false;
}
bool Build(HouseClass* h, StructType t, bool urgent=false) {
    BuildingTypeClass const& b=BuildingTypeClass::As_Reference(t);
    if(h->Can_Build(&b,h->ActLike) && (urgent || h->Available_Money() >= b.Cost_Of())) { h->BuildStructure=t; return true; } return false;
}
int CellDistance(CELL a, CELL b) { return abs(Cell_X(a)-Cell_X(b))+abs(Cell_Y(a)-Cell_Y(b)); }
CELL Home(HouseClass* h) {
    if(h->Center) return Coord_Cell(h->Center);
    for(int i=0;i<Units.Count();++i) if(Friendly(h,Units.Ptr(i))) return Coord_Cell(Units.Ptr(i)->Center_Coord());
    return XY_Cell(Map.MapCellX+Map.MapCellWidth/2,Map.MapCellY+Map.MapCellHeight/2);
}
void OwnSight(HouseClass* h) {
    // Legacy Revealed uses one shared computer bit and may skip the owner's
    // look when another computer already saw the object. Refresh legitimate
    // own-unit sight per house; sight radii/terrain rules remain engine-owned.
    for(int i=0;i<Buildings.Count();++i) if(Friendly(h,Buildings.Ptr(i))) Buildings.Ptr(i)->Look(false);
    for(int i=0;i<Units.Count();++i) if(Friendly(h,Units.Ptr(i))) Units.Ptr(i)->Look(false);
    for(int i=0;i<Infantry.Count();++i) if(Friendly(h,Infantry.Ptr(i))) Infantry.Ptr(i)->Look(false);
}
void Observe(HouseClass* h) {
    TDPolicyState& s=Policy(h); s.known_cells=s.contacts=s.infantry=s.armor=s.air=0;
    for(int y=Map.MapCellY;y<Map.MapCellY+Map.MapCellHeight;++y)
        for(int x=Map.MapCellX;x<Map.MapCellX+Map.MapCellWidth;++x) if(Map[XY_Cell(x,y)].Is_Mapped(h)) ++s.known_cells;
    h->Enemy=HOUSE_NONE; int nearest=0x7fffffff;
    for(int i=0;i<Buildings.Count();++i) { BuildingClass* b=Buildings.Ptr(i); if(!TD_Fair_AI_Knows(h,b)) continue;
        ++s.contacts; int d=CellDistance(Home(h),Coord_Cell(b->Center_Coord())); if(d<nearest) { nearest=d;h->Enemy=b->Owner(); } }
    for(int i=0;i<Units.Count();++i) if(TD_Fair_AI_Knows(h,Units.Ptr(i))) { ++s.armor; ++s.contacts; }
    for(int i=0;i<Infantry.Count();++i) if(TD_Fair_AI_Knows(h,Infantry.Ptr(i))) { ++s.infantry; ++s.contacts; }
    for(int i=0;i<Aircraft.Count();++i) if(TD_Fair_AI_Knows(h,Aircraft.Ptr(i))) { ++s.air; ++s.contacts; }
}
int RemainingCost(FactoryClass* factory) {
    TechnoClass* product=factory->Get_Object();if(!product) return 0;
    // The legacy public interface exposes stage and purchase price. Replay the
    // same integer installments as FactoryClass::AI/Cost_Per_Tick to preserve
    // already-paid money exactly without changing its saved object layout.
    int balance=product->PurchasePrice;
    for(int stage=1;stage<=min(108,factory->Completion());++stage) {
        int remaining=108-stage;balance-=remaining ? balance/remaining : balance;
    }
    return max(0,balance);
}
bool RecoverIncome(HouseClass* h,TDPolicyState& s) {
    int refs=BuildingCount(h,STRUCT_REFINERY), harvesters=UnitCount(h,UNIT_HARVESTER);
    int war=BuildingCount(h,STRUCT_WEAP)+BuildingCount(h,STRUCT_AIRSTRIP);
    int power=BuildingCount(h,STRUCT_POWER)+BuildingCount(h,STRUCT_ADVANCED_POWER);
    int queued_harvesters=0;
    for(int i=0;i<Buildings.Count();++i) { BuildingClass* b=Buildings.Ptr(i);
        if(b && b->House==h && b->Factory && b->Factory->Get_Object()
            && b->Factory->Get_Object()->What_Am_I()==RTTI_UNIT
            && ((UnitClass*)b->Factory->Get_Object())->Class->Type==UNIT_HARVESTER) ++queued_harvesters;
    }
    if(refs && harvesters>queued_harvesters) return false;
    h->BuildStructure=STRUCT_NONE;h->BuildUnit=UNIT_NONE;
    h->BuildInfantry=INFANTRY_NONE;h->BuildAircraft=AIRCRAFT_NONE;
    int needed=0,pending_refund=0,cancelled=0;
    bool queued_income=false,queued_power=false;
    for(int i=0;i<Buildings.Count();++i) { BuildingClass* b=Buildings.Ptr(i);
        if(!b || !b->IsActive || b->House!=h || b->Strength<=0) continue;
        if(b->Mission==MISSION_DECONSTRUCTION || b->MissionQueue==MISSION_DECONSTRUCTION) pending_refund+=b->Refund_Amount();
        if(!b->Factory) continue;
        TechnoClass* product=b->Factory->Get_Object();if(!product) continue;
        bool income=product->What_Am_I()==RTTI_UNIT && ((UnitClass*)product)->Class->Type==UNIT_HARVESTER;
        bool essential=income;
        if(product->What_Am_I()==RTTI_BUILDING) {
            StructType type=((BuildingClass*)product)->Class->Type;
            if(type==STRUCT_REFINERY) income=essential=true;
            if(type==STRUCT_POWER || type==STRUCT_ADVANCED_POWER) {
                essential=!power || h->Power<h->Drain+20;
                if(essential) queued_power=true;
            }
            if(!war && (type==STRUCT_WEAP || type==STRUCT_AIRSTRIP)) essential=true;
        }
        if(essential) { needed+=RemainingCost(b->Factory);queued_income|=income; }
        else if(cancelled<max(1,fair_ai::Get(s.level).max_orders-1)) {
            b->Factory->Abandon();delete b->Factory;b->Factory=NULL;
            ++cancelled;++s.orders;
        }
    }
    StructType structure=STRUCT_NONE;UnitType unit=UNIT_NONE;
    bool growing_refinery=Planned(h,STRUCT_REFINERY)>refs;
    if(!queued_income && !growing_refinery) {
        if(refs && war && h->Can_Build(UNIT_HARVESTER,h->ActLike)) {
            unit=UNIT_HARVESTER;needed+=(int)(UnitTypeClass::As_Reference(unit).Cost_Of()*h->CostBias);
        } else if(BuildingCount(h,STRUCT_CONST)) {
            structure=STRUCT_REFINERY;
            if(!h->Can_Build(structure,h->ActLike)) {
                structure=h->Can_Build(STRUCT_ADVANCED_POWER,h->ActLike) ? STRUCT_ADVANCED_POWER:STRUCT_POWER;
            }
            if(!h->Can_Build(structure,h->ActLike) || (queued_power && (structure==STRUCT_POWER || structure==STRUCT_ADVANCED_POWER))) structure=STRUCT_NONE;
            if(structure!=STRUCT_NONE) needed+=(int)(BuildingTypeClass::As_Reference(structure).Cost_Of()*h->CostBias);
        }
    }
    bool recoverable=queued_income || growing_refinery || queued_power || structure!=STRUCT_NONE || unit!=UNIT_NONE;
    if(recoverable && h->Available_Money()+pending_refund<needed) {
        BuildingClass* sale=NULL;int value=-1;
        for(int i=0;i<Buildings.Count();++i) { BuildingClass* b=Buildings.Ptr(i);
            if(!Friendly(h,b) || b->Mission==MISSION_CONSTRUCTION || b->Mission==MISSION_DECONSTRUCTION
                || b->MissionQueue==MISSION_DECONSTRUCTION) continue;
            StructType type=b->Class->Type;
            if(type==STRUCT_CONST || type==STRUCT_REFINERY || ((type==STRUCT_WEAP || type==STRUCT_AIRSTRIP) && war<=1)) continue;
            if((type==STRUCT_POWER || type==STRUCT_ADVANCED_POWER) && (power<=1 || h->Power-b->Power_Output()<h->Drain)) continue;
            int score=b->Refund_Amount();if(type==STRUCT_BARRACKS || type==STRUCT_HAND) score/=4;
            if(score>value) { value=score;sale=b; }
        }
        if(sale) { sale->Sell_Back(1);++s.orders; }
    }
    // Starting a normally paid essential queue may consume available cash in
    // installments; no subsequent discretionary queue can take its reserve.
    if(structure!=STRUCT_NONE && !queued_income && !BuildingQueue(h)) h->BuildStructure=structure;
    if(unit!=UNIT_NONE && !queued_income) h->BuildUnit=unit;
    return true;
}
void Economy(HouseClass* h) {
    TDPolicyState& s=Policy(h); fair_ai::Profile const& p=fair_ai::Get(s.level);
    int ref=BuildingCount(h,STRUCT_REFINERY), harv=UnitCount(h,UNIT_HARVESTER);
    int barr=BuildingCount(h,STRUCT_BARRACKS)+BuildingCount(h,STRUCT_HAND);
    int war=BuildingCount(h,STRUCT_WEAP)+BuildingCount(h,STRUCT_AIRSTRIP);
    int power=BuildingCount(h,STRUCT_POWER)+BuildingCount(h,STRUCT_ADVANCED_POWER);
    int money=h->Available_Money();
    // Pending requests are ordinary proposed commands. A lost prerequisite or
    // captured faction factory must not leave a now-illegal request wedged.
    if(h->BuildStructure!=STRUCT_NONE && !h->Can_Build(h->BuildStructure,h->ActLike)) h->BuildStructure=STRUCT_NONE;
    if(h->BuildUnit!=UNIT_NONE && !h->Can_Build(h->BuildUnit,h->ActLike)) h->BuildUnit=UNIT_NONE;
    if(h->BuildInfantry!=INFANTRY_NONE && !h->Can_Build(h->BuildInfantry,h->ActLike)) h->BuildInfantry=INFANTRY_NONE;
    if(h->BuildAircraft!=AIRCRAFT_NONE && !h->Can_Build(h->BuildAircraft,h->ActLike)) h->BuildAircraft=AIRCRAFT_NONE;
    if(RecoverIncome(h,s)) return;
    // Evaluate a new plan only after production consumed the preceding request.
    // Essential rebuilds may spend the reserve; discretionary queues may not.
    if(h->BuildStructure==STRUCT_NONE && !BuildingQueue(h)) {
        if(!power || h->Power<h->Drain+20) { if(!Build(h,STRUCT_ADVANCED_POWER)) Build(h,STRUCT_POWER,true); }
        else if(!Planned(h,STRUCT_REFINERY)) Build(h,STRUCT_REFINERY,true);
        else if(!Planned(h,STRUCT_BARRACKS) && !Planned(h,STRUCT_HAND)
            && (h->Can_Build(STRUCT_BARRACKS,h->ActLike) || h->Can_Build(STRUCT_HAND,h->ActLike))) { if(!Build(h,STRUCT_BARRACKS)) Build(h,STRUCT_HAND,true); }
        else if(!Planned(h,STRUCT_WEAP) && !Planned(h,STRUCT_AIRSTRIP)
            && (h->Can_Build(STRUCT_WEAP,h->ActLike) || h->Can_Build(STRUCT_AIRSTRIP,h->ActLike))) { if(!Build(h,STRUCT_WEAP)) Build(h,STRUCT_AIRSTRIP,true); }
        else if(ref<p.expansion_refineries && money>2200+ref*700 && harv>=ref && h->Can_Build(STRUCT_REFINERY,h->ActLike)) Build(h,STRUCT_REFINERY);
        else if(s.air && !BuildingCount(h,STRUCT_SAM) && h->ActLike==HOUSE_BAD && h->Can_Build(STRUCT_SAM,h->ActLike)) Build(h,STRUCT_SAM);
        else if(h->ActLike==HOUSE_GOOD && !BuildingCount(h,STRUCT_REPAIR) && h->Can_Build(STRUCT_REPAIR,h->ActLike)) Build(h,STRUCT_REPAIR);
        else if(!BuildingCount(h,STRUCT_RADAR) && h->Can_Build(STRUCT_RADAR,h->ActLike)) Build(h,STRUCT_RADAR);
        else if(BuildingCount(h,STRUCT_GTOWER)+BuildingCount(h,STRUCT_TURRET)<1+(int)s.level/2 && money>p.cash_reserve+600
            && (h->Can_Build(STRUCT_GTOWER,h->ActLike) || h->Can_Build(STRUCT_TURRET,h->ActLike))) {
            if(!Build(h,STRUCT_GTOWER)) Build(h,STRUCT_TURRET);
        } else if(!BuildingCount(h,STRUCT_EYE) && !BuildingCount(h,STRUCT_TEMPLE) && s.level>0 && money>2500+p.cash_reserve
            && (h->Can_Build(STRUCT_EYE,h->ActLike) || h->Can_Build(STRUCT_TEMPLE,h->ActLike))) {
            if(!Build(h,STRUCT_EYE)) Build(h,STRUCT_TEMPLE);
        } else if(!BuildingCount(h,STRUCT_HELIPAD) && s.level>0 && money>2200+p.cash_reserve && h->Can_Build(STRUCT_HELIPAD,h->ActLike)) Build(h,STRUCT_HELIPAD);
        else if(s.air && !BuildingCount(h,STRUCT_ATOWER) && h->ActLike==HOUSE_GOOD && h->Can_Build(STRUCT_ATOWER,h->ActLike)) Build(h,STRUCT_ATOWER);
    }
    bool critical=(!ref || !power || !war || !harv);
    int reserve=critical ? 0 : p.cash_reserve;
    if(h->BuildUnit==UNIT_NONE && ref && harv<ref+(s.level>=2 ? 1:0) && h->Can_Build(UNIT_HARVESTER,h->ActLike)) {
        h->BuildUnit=UNIT_HARVESTER;
    } else if(h->BuildUnit==UNIT_NONE && war && h->CurUnits<h->MaxUnit && money>reserve+400) {
        int score[UNIT_COUNT]; int total=0;
        for(int j=UNIT_FIRST;j<UNIT_COUNT;++j) {
            UnitType t=(UnitType)j; UnitTypeClass const& u=UnitTypeClass::As_Reference(t); score[j]=0;
            if(t==UNIT_HARVESTER || t==UNIT_MCV || u.Primary==WEAPON_NONE || !h->Can_Build(&u,h->ActLike) || u.Cost_Of()>money-reserve) continue;
            int w=20; // Mixed armies on all modes, increasingly evidence-led counters.
            if(t==UNIT_MTANK || t==UNIT_LTANK || t==UNIT_HTANK) w+=20+(s.armor ? p.counter_weight/2:0);
            if((t==UNIT_FTANK || t==UNIT_BUGGY || t==UNIT_JEEP) && s.infantry>s.armor) w+=p.counter_weight;
            if((t==UNIT_MLRS || t==UNIT_BIKE) && s.air) w+=p.counter_weight;
            if(t==UNIT_ARTY || t==UNIT_MSAM) w = s.level>=2 ? 18:5;
            w=w*4/(2+UnitCount(h,t)); score[j]=max(1,w); total+=score[j];
        }
        unsigned n=fair_ai::Random(s.rng,total); for(int j=UNIT_FIRST;total>0 && j<UNIT_COUNT;++j) { if(n<(unsigned)score[j]) { h->BuildUnit=(UnitType)j;break; } n-=score[j]; }
    }
    if(h->BuildInfantry==INFANTRY_NONE && barr && harv && money>reserve+150 && h->CurInfantry<8+4*ref+4*(int)s.level) {
        InfantryType t=INFANTRY_E1;
        unsigned pick=fair_ai::Random(s.rng,100);
        if((s.armor || s.air) && pick<(unsigned)p.counter_weight) t=INFANTRY_E3;
        else if(pick<25) t=INFANTRY_E2;
        InfantryTypeClass const& i=InfantryTypeClass::As_Reference(t);
        if(!h->Can_Build(&i,h->ActLike)) t=INFANTRY_E1;
        if(h->Can_Build(t,h->ActLike)) h->BuildInfantry=t;
    }
    if(h->BuildAircraft==AIRCRAFT_NONE && s.level>0 && harv && money>reserve+1400 && h->CurAircraft<BuildingCount(h,STRUCT_HELIPAD)) {
        AircraftType t=h->ActLike==HOUSE_GOOD ? AIRCRAFT_ORCA:AIRCRAFT_HELICOPTER;
        if(h->Can_Build(t,h->ActLike)) h->BuildAircraft=t;
    }
    // Normal repair consumes normal money and observes the same strength cap.
    int repaired=0;
    for(int i=0;i<Buildings.Count() && repaired<1+(int)s.level;++i) { BuildingClass* b=Buildings.Ptr(i);
        if(Friendly(h,b) && b->Strength<b->Class->MaxStrength && money>reserve+200 && b->Mission!=MISSION_DECONSTRUCTION) { b->Repair(1); ++repaired; } }
}
int Army(HouseClass* h,FootClass** army,int maxn) {
    int n=0;
    for(int i=0;i<Units.Count() && n<maxn;++i) { UnitClass* u=Units.Ptr(i); if(Friendly(h,u) && u->Is_Weapon_Equipped() && u->Class->Type!=UNIT_HARVESTER && u->Class->Type!=UNIT_MCV && !u->Team && !u->IsTethered) army[n++]=u; }
    for(int i=0;i<Infantry.Count() && n<maxn;++i) { InfantryClass* u=Infantry.Ptr(i); if(Friendly(h,u) && u->Is_Weapon_Equipped() && !u->Team && !u->IsTethered) army[n++]=u; }
    return n;
}
void MoveArea(FootClass* u,CELL c) {
    u->Assign_Target(TARGET_NONE);u->ArchiveTarget=::As_Target(c);
    u->Assign_Mission(MISSION_GUARD_AREA);u->Assign_Destination(::As_Target(c));
}
CELL Frontier(HouseClass* h,TDPolicyState& s,CELL from) {
    // Choose unexplored destinations using explored topology and map dimensions
    // only. Pathing learns obstructions through ordinary movement.
    CELL best=0; int score=-0x7fffffff;
    // Candidate start locations belong to the static map, not enemy live
    // houses. Scout the possible starts before sweeping unexplored terrain.
    for(int k=0;k<8;++k) {
        CELL c=Scen.Waypoint[k];
        if(c<=0 || !Map.In_Radar(c) || Map[c].Is_Mapped(h)) continue;
        bool assigned=false;for(int z=0;z<2;++z) if(s.scout_goal[z]==(unsigned)c) assigned=true;
        if(assigned) continue;
        int value=-CellDistance(from,c);if(value>score) { score=value;best=c; }
    }
    if(best) return best;
    score=-0x7fffffff;
    int w=Map.MapCellWidth, height=Map.MapCellHeight, total=w*height;
    if(total<=0) return 0;
    for(int k=0;k<total;++k) { int idx=(k+(int)s.cursor)%total;
        CELL c=XY_Cell(Map.MapCellX+idx%w,Map.MapCellY+idx/w);
        if(Map[c].Is_Mapped(h)) continue;
        int d=CellDistance(from,c); int merit=-d;
        if(merit>score) { score=merit;best=c; }
    }
    s.cursor=(s.cursor+137u)%(unsigned)total; return best;
}
void Tactics(HouseClass* h) {
    TDPolicyState& s=Policy(h); fair_ai::Profile const& p=fair_ai::Get(s.level);
    FootClass* army[512]; int count=Army(h,army,512), orders=0; CELL home=Home(h);
    // Clear stale scouts by owned live identity, never by an enemy reference.
    for(int z=0;z<2;++z) if(s.scout[z]) { bool exists=false; for(int i=0;i<count;++i) if(army[i]->As_Target()==s.scout[z]) exists=true; if(!exists) s.scout[z]=s.scout_goal[z]=0; }
    if((uint32_t)Frame>=s.next_scout) {
        s.next_scout=Frame+p.scout_ticks;
        for(int z=0;z<p.scouts;++z) {
            FootClass* scout=NULL;
            for(int i=0;i<count;++i) if(army[i]->As_Target()==s.scout[z]) scout=army[i];
            if(!scout) {
                int cheapest=0x7fffffff;
                for(int i=0;i<count;++i) { bool used=false;for(int j=0;j<2;++j) if(army[i]->As_Target()==s.scout[j]) used=true;
                    int cost=army[i]->Techno_Type_Class()->Cost_Of(); if(!used && cost<cheapest && army[i]->Mission!=MISSION_ATTACK) { cheapest=cost;scout=army[i]; } }
            }
            if(scout && orders<p.max_orders) {
                CELL position=Coord_Cell(scout->Center_Coord());
                CELL previous=(CELL)s.scout_position[z];s.scout_position[z]=position;
                CELL current=(CELL)s.scout_goal[z];
                if(current && !Map[current].Is_Mapped(h) && Target_Legal(scout->NavCom)
                    && position!=previous && CellDistance(position,current)>2) continue;
                CELL goal=Frontier(h,s,position);
                if(goal) { s.scout[z]=scout->As_Target();s.scout_goal[z]=goal;scout->Assign_Target(TARGET_NONE);scout->Assign_Mission(MISSION_MOVE);scout->Assign_Destination(::As_Target(goal));++s.scouting;++orders; }
            }
        }
    }
    bool launch=(uint32_t)Frame>=s.next_attack && count>=p.squad_min;
    if(launch) {
        s.next_attack=Frame+p.attack_ticks;
        for(int z=0;z<3;++z) s.objective[z]=0;
        int n=0;
        // Select objectives from currently observed buildings. Distinct groups
        // pressure separate objectives while the cheapest forces remain home.
        for(int z=0;z<p.attack_groups;++z) {
            int best=-0x7fffffff; CELL chosen=0;
            for(int i=0;i<Buildings.Count();++i) { BuildingClass* b=Buildings.Ptr(i);if(!TD_Fair_AI_Knows(h,b)) continue;
                CELL c=Coord_Cell(b->Center_Coord());bool used=false;for(int j=0;j<z;++j) if(s.objective[j]==(unsigned)c) used=true;
                if(used) continue; int value=b->Class->Cost_Of()/20-CellDistance(home,c);
                if(s.level>=2 && (b->Class->Type==STRUCT_REFINERY || b->Class->Type==STRUCT_WEAP || b->Class->Type==STRUCT_AIRSTRIP)) value+=100;
                if(value>best) { best=value;chosen=c; }
            }
            if(!chosen) {
                // Finish mobile survivors and punish exposed harvesters even
                // when the opponent has never deployed a construction yard.
                for(int i=0;i<Units.Count();++i) { UnitClass* u=Units.Ptr(i);if(!TD_Fair_AI_Knows(h,u)) continue;
                    CELL c=Coord_Cell(u->Center_Coord());bool used=false;for(int j=0;j<z;++j) if(s.objective[j]==(unsigned)c) used=true;
                    if(used) continue;int value=u->Class->Cost_Of()/20-CellDistance(home,c);
                    if(value>best) { best=value;chosen=c; }
                }
                for(int i=0;i<Infantry.Count() && !chosen;++i) { InfantryClass* u=Infantry.Ptr(i);if(!TD_Fair_AI_Knows(h,u)) continue;
                    CELL c=Coord_Cell(u->Center_Coord());bool used=false;for(int j=0;j<z;++j) if(s.objective[j]==(unsigned)c) used=true;
                    if(!used) chosen=c;
                }
            }
            if(chosen) { s.objective[z]=chosen;++n; }
        }
        if(!n) { s.objective[0]=Frontier(h,s,home);n=s.objective[0]?1:0; }
        s.attacking=n;
        if(n) ++s.attacks;
    }
    int reserve=max(1,count*p.reserve_percent/100);
    for(int q=0;q<count && orders<p.max_orders;++q) {
        // Rotate the command budget through the full army. Large armies get
        // bounded attention rather than starving high-index units.
        int i=(q+(int)(s.orders%max(1,count)))%count; FootClass* u=army[i];
        bool scout=false;for(int z=0;z<2;++z) if(u->As_Target()==s.scout[z]) scout=true;
        if(scout) continue;
        bool hurt=u->Strength*100 < u->Techno_Type_Class()->MaxStrength*p.retreat_percent;
        if(hurt && s.level>0 && CellDistance(home,Coord_Cell(u->Center_Coord()))>4) { MoveArea(u,home);++orders;continue; }
        TechnoClass* threat=NULL;int nearest=0x7fffffff;
        // Defenders react to observed threats to the economy, using a bounded
        // range. Attacking units focus their nearest visible local opponent.
        for(int j=0;j<Units.Count();++j) { UnitClass* e=Units.Ptr(j);if(!TD_Fair_AI_Knows(h,e)) continue;
            int d=CellDistance(Coord_Cell(u->Center_Coord()),Coord_Cell(e->Center_Coord()));
            if(d<nearest && d<=6 && (i>=reserve || CellDistance(home,Coord_Cell(e->Center_Coord()))<12)) { nearest=d;threat=e; } }
        if(threat && s.level>0 && u->TarCom!=threat->As_Target()) { u->Assign_Mission(MISSION_ATTACK);u->Assign_Target(threat->As_Target());++orders;continue; }
        if(!hurt && s.attacking && i>=reserve) {
            CELL goal=(CELL)s.objective[(i-reserve)%s.attacking];
            if(goal && u->ArchiveTarget!=::As_Target(goal) && !Target_Legal(u->TarCom)) { MoveArea(u,goal);++orders; }
        } else if(!Target_Legal(u->NavCom) && !Target_Legal(u->TarCom) && CellDistance(home,Coord_Cell(u->Center_Coord()))>8) { MoveArea(u,home);++orders; }
    }
    // Aircraft use known targets and ordinary ammunition/landing missions.
    for(int i=0;i<Aircraft.Count() && orders<p.max_orders;++i) { AircraftClass* a=Aircraft.Ptr(i); if(!Friendly(h,a) || !a->Is_Weapon_Equipped() || a->Ammo<=0 || Target_Legal(a->TarCom) || a->Mission==MISSION_ENTER) continue;
        TechnoClass* target=NULL;
        for(int j=0;j<Buildings.Count() && !target;++j) if(TD_Fair_AI_Knows(h,Buildings.Ptr(j))) target=Buildings.Ptr(j);
        for(int j=0;j<Units.Count() && !target;++j) if(TD_Fair_AI_Knows(h,Units.Ptr(j))) target=Units.Ptr(j);
        if(target) { a->Assign_Mission(MISSION_ATTACK);a->Assign_Target(target->As_Target());++orders; }
    }
    s.orders+=orders;
}
}
bool TD_Fair_AI_Knows(HouseClass const* h,TechnoClass const* o) {
    if(!h || !Live(o) || h->Is_Ally(o) || o->Is_Cloaked(h)) return false;
    CELL c=Coord_Cell(o->Center_Coord());return c>=0 && c<MAP_CELL_TOTAL && Map[c].Is_Mapped(h->Class->House);
}
void TD_Fair_AI_Reset() {
    memset(td_policy,0,sizeof(td_policy));
    for(int i=0;i<HOUSE_COUNT;++i) { td_policy[i].level=1;td_policy[i].rng=0x51f15e5du ^ (uint32_t)(i+1)*2654435761u; }
}
void TD_Fair_AI_Set_Levels(unsigned char const* levels,int count) { for(int i=0;i<fair_ai::Seats;++i) td_pending[i]=(levels && i<count)?fair_ai::Clamp(levels[i]):fair_ai::Normal; }
void TD_Fair_AI_Assign(HouseClass* h,int seat) { if(h) {
    TDPolicyState& s=Policy(h);memset(&s,0,sizeof(s));
    s.rng=0x51f15e5du ^ (uint32_t)(h->Class->House+1)*2654435761u;
    s.level=seat>=0 && seat<fair_ai::Seats?td_pending[seat]:fair_ai::Normal;
} }
void TD_Fair_AI_Tick(HouseClass* h) {
    if(!h || !h->IsActive || h->IsHuman || GameToPlay==GAME_NORMAL || h->IsDefeated
        || h->Class->House<HOUSE_MULTI1 || h->Class->House>HOUSE_MULTI8) return;
    TDPolicyState& s=Policy(h); fair_ai::Profile const& p=fair_ai::Get(s.level);
    // Restore the legal faction statistics, independently from difficulty INIs.
    // Assignment itself preserves faction identity; no skill changes statistics.
    if((uint32_t)Frame>=s.next_think) { s.next_think=Frame+p.think_ticks;++s.thoughts;h->Recalc_Center();OwnSight(h);Observe(h);Economy(h); }
    if((uint32_t)Frame>=s.next_tactic) { s.next_tactic=Frame+p.tactical_ticks;Tactics(h); }
}
unsigned int TD_Fair_AI_Hash() {
    uint32_t hash=fair_ai::PolicyHash();
    for(int i=0;i<HOUSE_COUNT;++i) { uint32_t const* words=(uint32_t const*)&td_policy[i];for(unsigned j=0;j<sizeof(TDPolicyState)/4;++j) hash=fair_ai::HashWord(hash,words[j]); } return hash;
}
bool TD_Fair_AI_Save(FileClass& file) {
    uint32_t header[4]={0x54444149u,3u,sizeof(td_policy)+4u*sizeof(uint32_t),fair_ai::PolicyHash()};
    uint32_t random[4]={Scen.RandomNumber.Seed,NonCriticalRandomNumber.Seed,RandNumb,(uint32_t)Seed};
    return file.Write(header,sizeof(header))==sizeof(header) && file.Write(td_policy,sizeof(td_policy))==sizeof(td_policy)
        && file.Write(random,sizeof(random))==sizeof(random);
}
bool TD_Fair_AI_Load(FileClass& file) {
    TD_Fair_AI_Reset(); uint32_t header[4];
    int got=file.Read(header,sizeof(header));
    if(got==0) return true; // Old saves end here and start at Normal.
    if(got!=sizeof(header) || header[0]!=0x54444149u || header[1]!=3u || header[2]!=sizeof(td_policy)+4u*sizeof(uint32_t) || header[3]!=fair_ai::PolicyHash()) return false;
    TDPolicyState state[HOUSE_COUNT];
    if(file.Read(state,sizeof(state))!=sizeof(state)) return false;
    for(int i=0;i<HOUSE_COUNT;++i) {
        if(state[i].level>3 || state[i].attacking>3 || state[i].cursor>=(uint32_t)MAP_CELL_TOTAL) return false;
        for(int j=0;j<2;++j) if(state[i].scout_goal[j]>=(uint32_t)MAP_CELL_TOTAL || state[i].scout_position[j]>=(uint32_t)MAP_CELL_TOTAL) return false;
        for(int j=0;j<3;++j) if(state[i].objective[j]>=(uint32_t)MAP_CELL_TOTAL) return false;
        // Scout handles are compared against owned live army IDs on the next
        // tactic tick; they are never dereferenced from this external state.
    }
    uint32_t random[4];if(file.Read(random,sizeof(random))!=sizeof(random)) return false;
    memcpy(td_policy,state,sizeof(state));
    Scen.RandomNumber.Seed=random[0];NonCriticalRandomNumber.Seed=random[1];RandNumb=random[2];Seed=(int)random[3];
    return true;
}
int TD_Fair_AI_Diagnostics(char* out,int size) {
    if(!out || size<=0) return 0;int used=0;out[0]=0;
    for(int i=HOUSE_MULTI1;i<HOUSE_COUNT && used<size-1;++i) { HouseClass* h=HouseClass::As_Pointer((HousesType)i);if(!h || !h->IsActive) continue;
        int n=snprintf(out+used,size-used,"FAIRRULE|game=TD|house=%d|act=%d|human=%d|difficulty=%d|fire=%d|ground=%d|air=%d|armor=%d|rof=%d|cost=%d|build=%d|harvest=%d\n",i,(int)h->ActLike,(int)h->IsHuman,(int)h->Difficulty,(int)(h->FirepowerBias*1000),(int)(h->GroundspeedBias*1000),(int)(h->AirspeedBias*1000),(int)(h->ArmorBias*1000),(int)(h->ROFBias*1000),(int)(h->CostBias*1000),(int)(h->BuildSpeedBias*1000),UnitTypeClass::FULL_LOAD_CREDITS);
        if(n<0) break;used+=min(n,size-used-1);
    }
    for(int i=0;i<HOUSE_COUNT && used<size-1;++i) { HouseClass* h=HouseClass::As_Pointer((HousesType)i); if(!h || !h->IsActive || h->IsHuman || i<HOUSE_MULTI1) continue;
        TDPolicyState const& s=td_policy[i];int n=snprintf(out+used,size-used,"FAIRAI|game=TD|house=%d|level=%u|name=%s|think=%u|orders=%u|scouts=%u|attacks=%u|known_cells=%u|contacts=%u|hash=%08x\n",i,s.level,fair_ai::Name(s.level),s.thoughts,s.orders,s.scouting,s.attacks,s.known_cells,s.contacts,TD_Fair_AI_Hash());
        if(n<0) break;used+=min(n,size-used-1);
    } return used;
}
