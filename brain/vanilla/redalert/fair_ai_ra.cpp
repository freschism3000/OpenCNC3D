/* MODIFIED for OpenCNC3D, 19 October 2026. Fair per-seat skirmish skill.
 * The controller issues ordinary game orders and purchases through existing factories.
 * Enemy inspection is behind the same house-specific shroud and cloak checks used by
 * players. A remembered objective is a cell snapshot, never a hidden live object handle.
 * Campaign expert-system behavior is untouched. */
#include "function.h"
#include "fair_ai_ra.h"
#include "../../fair_ai.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

namespace {
struct State {
    uint32_t level, initialized, rng, next_think, next_tactical, next_scout, next_attack;
    uint32_t objective, objective_owner, objective_value, objective_seen;
    uint32_t scout[2], scout_cell[2], scout_last[2], scout_stall[2], scout_cursor;
    uint32_t observed_infantry, observed_armor, observed_air, observed_navy;
    uint32_t thinks, orders, scouts, attacks, retreats, contacts;
    uint32_t attack_until;
};
static_assert(sizeof(State) == 31 * sizeof(uint32_t), "Fair AI state must contain only packed 32-bit fields");
static State states[HOUSE_COUNT];
static unsigned char requested[fair_ai::Seats] = {1,1,1,1,1,1,1,1};
static State& Get(HouseClass const* h) { return states[(int)h->Class->House]; }
static bool Live(TechnoClass const* t) { return t && t->IsActive && !t->IsInLimbo && t->Strength > 0; }
static int TypeCount(HouseClass const* h, StructType type, bool complete = false)
{
    int count = 0;
    for (int i = 0; i < Buildings.Count(); ++i) {
        BuildingClass const* b = Buildings.Ptr(i);
        if (b && b->IsActive && b->Strength > 0 && b->House == h && *b == type
            && b->Mission != MISSION_DECONSTRUCTION && b->MissionQueue != MISSION_DECONSTRUCTION
            && (!complete || (!b->IsInLimbo && b->Mission != MISSION_CONSTRUCTION))) ++count;
    }
    return count;
}
static int UnitCount(HouseClass const* h, UnitType type)
{
    int count = 0;
    for (int i = 0; i < Units.Count(); ++i) {
        UnitClass const* u = Units.Ptr(i);
        if (u && u->IsActive && u->Strength > 0 && u->House == h && *u == type) ++count;
    }
    return count;
}
static bool IsScout(State const& s, TechnoClass const* t)
{
    TARGET target = t->As_Target();
    return s.scout[0] == target || s.scout[1] == target;
}
static FootClass* Scout(HouseClass* h, uint32_t target)
{
    TechnoClass* t = target ? As_Techno(target) : NULL;
    return Live(t) && t->House == h && t->Is_Foot() && t->What_Am_I() != RTTI_AIRCRAFT ? (FootClass*)t : NULL;
}
static CELL Home(HouseClass const* h)
{
    if (h->Center && Map.In_Radar(Coord_Cell(h->Center))) return Coord_Cell(h->Center);
    for (int i = 0; i < Units.Count(); ++i) {
        UnitClass* u = Units.Ptr(i);
        if (Live(u) && u->House == h) return Coord_Cell(u->Center_Coord());
    }
    return 0;
}
static bool LegalType(HouseClass* h, TechnoTypeClass const& t)
{
    return h->Can_Build(&t, h->ActLike) && t.Level <= (unsigned)h->Control.TechLevel;
}
static bool Income(HouseClass* h)
{
    if (TypeCount(h, STRUCT_REFINERY, true) == 0) return false;
    for (int i=0;i<Units.Count();++i) {
        UnitClass* u=Units.Ptr(i);
        if (!u || !u->IsActive || u->Strength<=0 || u->House!=(HouseClass const*)h || *u!=UNIT_HARVESTER) continue;
        bool unfinished=false;
        for (int b=0;b<Buildings.Count();++b) {
            BuildingClass* factory=Buildings.Ptr(b);
            if (factory && factory->House==h && factory->Factory.Is_Valid() && factory->Factory->Get_Object()==u) { unfinished=true; break; }
        }
        if (!unfinished) return true;
    }
    return false;
}
static bool Cash(HouseClass* h, TechnoTypeClass const& t, int reserve)
{
    int cost = t.Cost_Of() * h->CostBias;
    return h->Available_Money() >= cost + reserve;
}
static bool ChooseBuilding(HouseClass* h, StructType type, int desired, int reserve = 0)
{
    BuildingTypeClass const& t = BuildingTypeClass::As_Reference(type);
    if (TypeCount(h, type) >= desired || !LegalType(h, t) || !Cash(h, t, reserve)) return false;
    h->BuildStructure = type;
    return true;
}
static int KnownOre(HouseClass* h)
{
    int count = 0;
    for (int cell = 0; cell < MAP_CELL_TOTAL; ++cell) {
        if (Map.In_Radar(cell) && Map[(CELL)(cell)].Is_Mapped(h) && Map[(CELL)(cell)].Land_Type() == LAND_TIBERIUM) ++count;
    }
    return count;
}
static bool Coastal(HouseClass* h)
{
    CELL home = Home(h);
    if (!home) return false;
    for (int y = -10; y <= 10; ++y) for (int x = -10; x <= 10; ++x) {
        int xx = Cell_X(home) + x, yy = Cell_Y(home) + y;
        if (xx < 0 || yy < 0 || xx >= MAP_CELL_W || yy >= MAP_CELL_H) continue;
        CELL cell = XY_Cell(xx, yy);
        if (Map.In_Radar(cell) && Map[(CELL)(cell)].Is_Mapped(h) && Map[(CELL)(cell)].Land_Type() == LAND_WATER) return true;
    }
    return false;
}
static bool RecoverIncome(HouseClass* h, State& s)
{
    if (Income(h)) return false;
    bool yard=TypeCount(h,STRUCT_CONST,true)>0;
    bool war=TypeCount(h,STRUCT_WEAP,true)>0;
    bool refinery=TypeCount(h,STRUCT_REFINERY,true)>0;
    int pending_refund=0, queued_balance=0, cancelled=0;
    bool queued_income=false, queued_power=false, queued_war=false;
    int cancellation_budget=max(1,fair_ai::Get(s.level).max_orders-1);
    for (int i=0;i<Buildings.Count();++i) {
        BuildingClass* b=Buildings.Ptr(i);
        if (!b || !b->IsActive || b->House!=(HouseClass const*)h || b->Strength<=0) continue;
        if (b->Mission==MISSION_DECONSTRUCTION || b->MissionQueue==MISSION_DECONSTRUCTION) pending_refund+=b->Refund_Amount();
        if (!b->Factory.Is_Valid()) continue;
        TechnoClass* product=b->Factory->Get_Object(); if (!product) continue;
        bool harvester=product->What_Am_I()==RTTI_UNIT && *(UnitClass*)product==UNIT_HARVESTER;
        bool newref=product->What_Am_I()==RTTI_BUILDING && *(BuildingClass*)product==STRUCT_REFINERY;
        bool essential=harvester || newref;
        if (product->What_Am_I()==RTTI_BUILDING) {
            StructType type=*(BuildingClass*)product;
            essential=essential || type==STRUCT_POWER || type==STRUCT_ADVANCED_POWER || (!war && type==STRUCT_WEAP);
            if (type==STRUCT_POWER || type==STRUCT_ADVANCED_POWER) queued_power=true;
            if (type==STRUCT_WEAP) queued_war=true;
        }
        if (essential) {
            queued_balance+=b->Factory->Remaining_Cost();
            if (harvester || newref) queued_income=true;
        } else if (cancelled<cancellation_budget) {
            // Abandon uses the engine's normal refund of installments already paid.
            b->Factory->Abandon(); delete (FactoryClass*)b->Factory; b->Factory=0;
            ++cancelled; ++s.orders;
        }
    }
    StructType structure=STRUCT_NONE; UnitType unit=UNIT_NONE;
    int needed=queued_balance;
    bool growing_refinery=TypeCount(h,STRUCT_REFINERY)>TypeCount(h,STRUCT_REFINERY,true);
    if (!queued_income && !growing_refinery) {
        if (refinery && war && LegalType(h,UnitTypeClass::As_Reference(UNIT_HARVESTER))) {
            unit=UNIT_HARVESTER;
            needed+=(int)(UnitTypeClass::As_Reference(unit).Cost_Of()*h->CostBias);
        } else if (yard) {
            structure=STRUCT_REFINERY; // A refinery supplies its normal free harvester.
            if (!LegalType(h,BuildingTypeClass::As_Reference(structure))) {
                structure=LegalType(h,BuildingTypeClass::As_Reference(STRUCT_ADVANCED_POWER)) ? STRUCT_ADVANCED_POWER : STRUCT_POWER;
            }
            if (!LegalType(h,BuildingTypeClass::As_Reference(structure))) structure=STRUCT_NONE;
            if (queued_power && (structure==STRUCT_POWER || structure==STRUCT_ADVANCED_POWER)) structure=STRUCT_NONE;
            if (structure!=STRUCT_NONE) needed+=(int)(BuildingTypeClass::As_Reference(structure).Cost_Of()*h->CostBias);
        }
    }
    // Do not liquidate the base when its surviving production cannot rebuild income.
    bool recoverable=queued_income || growing_refinery || queued_power || queued_war || structure!=STRUCT_NONE || unit!=UNIT_NONE;
    if (recoverable && h->Available_Money()+pending_refund<needed) {
        BuildingClass* sale=NULL; int best=-1;
        for (int i=0;i<Buildings.Count();++i) {
            BuildingClass* b=Buildings.Ptr(i);
            if (!Live(b)||b->House!=(HouseClass const*)h||!b->Can_Demolish()||b->MissionQueue==MISSION_DECONSTRUCTION) continue;
            StructType type=*b;
            if (type==STRUCT_CONST || type==STRUCT_REFINERY || (type==STRUCT_WEAP && TypeCount(h,STRUCT_WEAP,true)<=1)) continue;
            if (b->Power_Output()>0 && h->Power-b->Power_Output()<h->Drain) continue;
            int score=b->Refund_Amount();
            if (type==STRUCT_BARRACKS || type==STRUCT_TENT) score/=4;
            if (score>best) { best=score; sale=b; }
        }
        if (sale) { sale->Sell_Back(1); ++s.orders; }
    }
    if (structure!=STRUCT_NONE && Cash(h,BuildingTypeClass::As_Reference(structure),0)) h->BuildStructure=structure;
    if (unit!=UNIT_NONE && Cash(h,UnitTypeClass::As_Reference(unit),0)) h->BuildUnit=unit;
    return true;
}
static void Economy(HouseClass* h, State& s)
{
    fair_ai::Profile const& p = fair_ai::Get(s.level);
    // Recommendations are revalidated before every factory starts; destroyed prerequisites
    // cannot leave a stale high-tech request blocking economic recovery.
    h->BuildStructure = STRUCT_NONE;
    h->BuildUnit = UNIT_NONE;
    h->BuildInfantry = INFANTRY_NONE;
    h->BuildAircraft = AIRCRAFT_NONE;
    h->BuildVessel = VESSEL_NONE;
    // Keep the same paid recovery path on every skill level before discretionary spending.
    if (RecoverIncome(h,s)) return;
    int refs = TypeCount(h, STRUCT_REFINERY);
    int harvesters = UnitCount(h, UNIT_HARVESTER);
    bool income = Income(h);
    int money = h->Available_Money();
    int reserve = income ? p.cash_reserve : 0;
    if (h->Power <= h->Drain + 40) {
        if (!ChooseBuilding(h, STRUCT_ADVANCED_POWER, 99)) ChooseBuilding(h, STRUCT_POWER, 99);
    }
    if (h->BuildStructure == STRUCT_NONE && refs == 0) ChooseBuilding(h, STRUCT_REFINERY, 1);
    if (h->BuildStructure == STRUCT_NONE && !TypeCount(h, STRUCT_BARRACKS) && !TypeCount(h, STRUCT_TENT)) {
        if (!ChooseBuilding(h, STRUCT_BARRACKS, 1, reserve)) ChooseBuilding(h, STRUCT_TENT, 1, reserve);
    }
    if (h->BuildStructure == STRUCT_NONE) ChooseBuilding(h, STRUCT_WEAP, 1, reserve);
    // Expand from observed resources and the house's own demand. No enemy economy reads.
    int desired_refs = 1;
    if (income && KnownOre(h) >= 12 && (Frame > TICKS_PER_MINUTE * 3 || money > 4500)) {
        desired_refs = min(p.expansion_refineries, 2 + (Frame > TICKS_PER_MINUTE * 8) + (money > 6500));
    }
    if (h->BuildStructure == STRUCT_NONE && refs < desired_refs) ChooseBuilding(h, STRUCT_REFINERY, desired_refs, reserve);
    if (h->BuildStructure == STRUCT_NONE) ChooseBuilding(h, STRUCT_RADAR, 1, reserve + 600);
    if (h->BuildStructure == STRUCT_NONE && s.observed_air) {
        if (!ChooseBuilding(h, STRUCT_SAM, 1 + (s.level >= 2), reserve)) ChooseBuilding(h, STRUCT_AAGUN, 1 + (s.level >= 2), reserve);
    }
    if (h->BuildStructure == STRUCT_NONE && Frame > TICKS_PER_MINUTE * (4 - min((int)s.level, 2))) {
        if (!ChooseBuilding(h, STRUCT_TURRET, 1 + (s.level >= 1), reserve)) ChooseBuilding(h, STRUCT_PILLBOX, 1 + (s.level >= 1), reserve);
    }
    if (h->BuildStructure == STRUCT_NONE && s.level >= 1 && money > reserve + 1800) ChooseBuilding(h, STRUCT_REPAIR, 1, reserve);
    if (h->BuildStructure == STRUCT_NONE && Frame > TICKS_PER_MINUTE * (7 - (int)s.level)) {
        if (!ChooseBuilding(h, STRUCT_ADVANCED_TECH, 1, reserve + 500)) ChooseBuilding(h, STRUCT_SOVIET_TECH, 1, reserve + 500);
    }
    if (h->BuildStructure == STRUCT_NONE && Frame > TICKS_PER_MINUTE * (6 - (int)s.level) && money > 2200) {
        if (!ChooseBuilding(h, STRUCT_HELIPAD, 1 + (s.level >= 2), reserve + 600)) ChooseBuilding(h, STRUCT_AIRSTRIP, 1 + (s.level >= 2), reserve + 600);
    }
    if (h->BuildStructure == STRUCT_NONE && s.level >= 1 && money > 3500 && Coastal(h)) {
        if (!ChooseBuilding(h, STRUCT_SHIP_YARD, 1, reserve + 1000)) ChooseBuilding(h, STRUCT_SUB_PEN, 1, reserve + 1000);
    }
    if (h->BuildStructure == STRUCT_NONE && s.level >= 2 && money > 6000) {
        ChooseBuilding(h, STRUCT_MSLO, 1, reserve + 2000);
    }
    // Build one economic replacement before discretionary military spending.
    if (refs && harvesters < refs * (s.level >= 2 ? 2 : 1) && !h->IsTiberiumShort) {
        UnitTypeClass const& t = UnitTypeClass::As_Reference(UNIT_HARVESTER);
        if (LegalType(h, t) && Cash(h, t, 0)) h->BuildUnit = UNIT_HARVESTER;
    }
    if (!income || h->Power_Fraction() < fixed::_3_4) return;
    // Reserve capital for the next economic/technology step. Continuous infantry
    // purchases must not permanently consume the savings needed for a refinery,
    // radar or technology center. This is a spending plan, not a credit bonus.
    StructType saving = STRUCT_NONE;
    if (refs < desired_refs) saving = STRUCT_REFINERY;
    else if (!TypeCount(h,STRUCT_RADAR)) saving = STRUCT_RADAR;
    else if (s.level >= 1 && !TypeCount(h,STRUCT_REPAIR) && Frame > TICKS_PER_MINUTE*4) saving = STRUCT_REPAIR;
    else if (!TypeCount(h,STRUCT_ADVANCED_TECH) && !TypeCount(h,STRUCT_SOVIET_TECH) && Frame > TICKS_PER_MINUTE*(7-(int)s.level)) {
        saving = LegalType(h,BuildingTypeClass::As_Reference(STRUCT_ADVANCED_TECH)) ? STRUCT_ADVANCED_TECH : STRUCT_SOVIET_TECH;
    } else if (!TypeCount(h,STRUCT_HELIPAD) && !TypeCount(h,STRUCT_AIRSTRIP) && Frame > TICKS_PER_MINUTE*(6-(int)s.level)) {
        saving = LegalType(h,BuildingTypeClass::As_Reference(STRUCT_HELIPAD)) ? STRUCT_HELIPAD : STRUCT_AIRSTRIP;
    }
    if (saving != STRUCT_NONE) {
        BuildingTypeClass const& next=BuildingTypeClass::As_Reference(saving);
        if (LegalType(h,next)) reserve=max(reserve,(int)(next.Cost_Of()*h->CostBias)+p.cash_reserve+600);
    }
    // Score legal purchases against observed composition. A small separate RNG stream
    // supplies variety without altering weapon, resource, or other houses' random draws.
    if (h->BuildUnit == UNIT_NONE && h->CurUnits < h->Control.MaxUnit) {
        int best = -1; UnitType choice = UNIT_NONE;
        for (UnitType type = UNIT_FIRST; type < UNIT_COUNT; type++) {
            UnitTypeClass const& t = UnitTypeClass::As_Reference(type);
            if (!t.PrimaryWeapon || !LegalType(h, t) || !Cash(h, t, reserve)) continue;
            int score = 30 + fair_ai::Random(s.rng, 30);
            if (type == UNIT_MTANK || type == UNIT_MTANK2 || type == UNIT_LTANK || type == UNIT_HTANK) score += 45;
            if (type == UNIT_JEEP || type == UNIT_APC) score += (s.observed_infantry > s.observed_armor ? p.counter_weight : 0) - UnitCount(h, type) * 15;
            if (type == UNIT_V2_LAUNCHER || type == UNIT_ARTY) score += (s.objective ? p.counter_weight / 2 : 0) - UnitCount(h, type) * 18;
            if (t.PrimaryWeapon->Bullet->IsAntiAircraft && s.observed_air) score += p.counter_weight;
            if (score > best) { best = score; choice = type; }
        }
        h->BuildUnit = choice;
    }
    if (h->CurInfantry < h->Control.MaxInfantry && h->CurInfantry < 6 + (int)h->CurUnits * (s.level ? 2 : 1)) {
        int best = -1; InfantryType choice = INFANTRY_NONE;
        for (InfantryType type = INFANTRY_FIRST; type < INFANTRY_COUNT; type++) {
            InfantryTypeClass const& t = InfantryTypeClass::As_Reference(type);
            if (!t.PrimaryWeapon || type == INFANTRY_DOG || !LegalType(h, t) || !Cash(h, t, reserve)) continue;
            int score = 20 + fair_ai::Random(s.rng, 40);
            if (type == INFANTRY_E1) score += 20;
            if (type == INFANTRY_E3 && (s.observed_armor || s.observed_air)) score += p.counter_weight;
            if ((type == INFANTRY_E2 || type == INFANTRY_E4) && s.observed_infantry > s.observed_armor) score += p.counter_weight / 2;
            if (type == INFANTRY_TANYA) score -= 10;
            if (score > best) { best = score; choice = type; }
        }
        h->BuildInfantry = choice;
    }
    if (h->CurAircraft < h->Control.MaxAircraft) {
        int pads = TypeCount(h, STRUCT_HELIPAD, true), strips = TypeCount(h, STRUCT_AIRSTRIP, true);
        int helis = 0, jets = 0;
        for (int i = 0; i < Aircraft.Count(); ++i) {
            AircraftClass* a = Aircraft.Ptr(i);
            if (a && a->IsActive && a->Strength > 0 && a->House == h) {
                if (*a == AIRCRAFT_LONGBOW || *a == AIRCRAFT_HIND) ++helis; else ++jets;
            }
        }
        const AircraftType types[] = {AIRCRAFT_LONGBOW, AIRCRAFT_HIND, AIRCRAFT_MIG, AIRCRAFT_YAK};
        for (unsigned i = 0; i < sizeof(types)/sizeof(types[0]); ++i) {
            AircraftTypeClass const& t = AircraftTypeClass::As_Reference(types[i]);
            if ((i < 2 ? pads > helis : strips > jets) && LegalType(h, t) && Cash(h, t, reserve + 300)) { h->BuildAircraft = types[i]; break; }
        }
    }
    if (h->CurVessels < h->Control.MaxVessel && h->CurVessels < 2 + (int)s.level * 2) {
        int best = -1; VesselType choice = VESSEL_NONE;
        for (VesselType type = VESSEL_FIRST; type < VESSEL_COUNT; type++) {
            VesselTypeClass const& t = VesselTypeClass::As_Reference(type);
            if (!t.PrimaryWeapon || !LegalType(h, t) || !Cash(h, t, reserve + 600)) continue;
            int score = 20 + fair_ai::Random(s.rng, 30);
            if (type == VESSEL_DD || type == VESSEL_PT) score += 30;
            if ((type == VESSEL_SS || type == VESSEL_MISSILESUB) && s.observed_navy) score += p.counter_weight;
            if (score > best) { best = score; choice = type; }
        }
        h->BuildVessel = choice;
    }
}
static void ObserveOne(HouseClass* h, State& s, TechnoClass* t)
{
    if (!FairAI_RA_Observed(h, t) || h->Is_Ally(t)) return;
    ++s.contacts;
    switch (t->What_Am_I()) {
    case RTTI_INFANTRY: ++s.observed_infantry; break;
    case RTTI_UNIT: ++s.observed_armor; break;
    case RTTI_AIRCRAFT: ++s.observed_air; break;
    case RTTI_VESSEL: ++s.observed_navy; break;
    default: break;
    }
    int value = t->What_Am_I() == RTTI_BUILDING ? 300 + t->Techno_Type_Class()->Cost_Of() / 10 : 80;
    if (t->What_Am_I() == RTTI_UNIT && *(UnitClass*)t == UNIT_HARVESTER) value += 150;
    CELL cell = Coord_Cell(t->Center_Coord());
    int dist = Distance(Cell_Coord(Home(h)), t->Center_Coord()) / CELL_LEPTON_W;
    value = max(1, value - dist);
    // Higher skills can choose valuable economic targets; Easy prefers proximity.
    if (s.level == 0) value = max(1, 200 - dist);
    if (!s.objective || value > (int)s.objective_value || cell == s.objective) {
        s.objective = cell; s.objective_owner = (int)t->Owner();
        s.objective_value = value; s.objective_seen = Frame;
    }
}
static void Observe(HouseClass* h, State& s)
{
    s.contacts = s.observed_infantry = s.observed_armor = s.observed_air = s.observed_navy = 0;
    if (s.objective && Map.In_Radar((CELL)s.objective) && Map[(CELL)(s.objective)].Is_Mapped(h)) {
        TechnoClass* there = Map[(CELL)(s.objective)].Cell_Techno();
        if (!FairAI_RA_Observed(h, there) || h->Is_Ally(there)) s.objective = s.objective_value = 0;
    }
    if (s.objective && Frame - s.objective_seen > TICKS_PER_MINUTE * 4) s.objective = s.objective_value = 0;
    // Re-evaluate values each observation pass; no permanently preferred destroyed target.
    s.objective_value = 0;
    for (int i=0;i<Buildings.Count();++i) ObserveOne(h,s,Buildings.Ptr(i));
    for (int i=0;i<Units.Count();++i) ObserveOne(h,s,Units.Ptr(i));
    for (int i=0;i<Infantry.Count();++i) ObserveOne(h,s,Infantry.Ptr(i));
    for (int i=0;i<Aircraft.Count();++i) ObserveOne(h,s,Aircraft.Ptr(i));
    for (int i=0;i<Vessels.Count();++i) ObserveOne(h,s,Vessels.Ptr(i));
    h->Enemy = s.objective ? (HousesType)s.objective_owner : HOUSE_NONE;
}
static CELL Explore(HouseClass* h, State& s, FootClass* scout)
{
    CELL bestcell = 0; int best = -100000;
    int width = Map.MapCellWidth, height = Map.MapCellHeight;
    if (width < 1 || height < 1) return 0;
    // Only use known/explored status and geometric distance while choosing unseen cells.
    // Terrain and enemy occupancy remain the ordinary movement system's responsibility.
    for (int n = 0; n < 96; ++n) {
        s.scout_cursor = (s.scout_cursor + 37) % (width * height);
        int cell = XY_Cell(Map.MapCellX + s.scout_cursor % width, Map.MapCellY + s.scout_cursor / width);
        if (Map[(CELL)(cell)].Is_Mapped(h)) continue;
        int distance = Distance(scout->Center_Coord(), Cell_Coord(cell)) / CELL_LEPTON_W;
        int score = 200 + min(distance, 50) * 2 + fair_ai::Random(s.rng, 80);
        if (distance < 12) score -= 100;
        if (score > best) { best = score; bestcell = cell; }
    }
    return bestcell;
}
static void Command(FootClass* f, CELL cell, MissionType mission, State& s)
{
    f->Assign_Target(TARGET_NONE);
    f->Assign_Mission(mission);
    f->Assign_Destination(::As_Target(cell));
    f->Commence();
    ++s.orders;
}
static void Scouts(HouseClass* h, State& s)
{
    fair_ai::Profile const& p = fair_ai::Get(s.level);
    for (int j = 0; j < p.scouts; ++j) {
        FootClass* scout = Scout(h, s.scout[j]);
        if (!scout) {
            int best = 1000000;
            for (int i=0;i<Units.Count();++i) {
                UnitClass* u = Units.Ptr(i);
                if (!Live(u) || u->House != (HouseClass const*)h || !u->Is_Weapon_Equipped() || IsScout(s,u) || u->Team.Is_Valid()) continue;
                if (u->Techno_Type_Class()->Cost_Of() < best) { scout=u; best=u->Techno_Type_Class()->Cost_Of(); }
            }
            if (!scout) for (int i=0;i<Infantry.Count();++i) {
                InfantryClass* u=Infantry.Ptr(i);
                if (Live(u) && u->House==h && u->Is_Weapon_Equipped() && !IsScout(s,u) && !u->Team.Is_Valid()) { scout=u; break; }
            }
            s.scout[j] = scout ? scout->As_Target() : 0;
        }
        if (scout) {
            CELL current = Coord_Cell(scout->Center_Coord());
            s.scout_stall[j] = current == s.scout_last[j] ? s.scout_stall[j] + 1 : 0;
            s.scout_last[j] = current;
            bool unfinished = s.scout_cell[j] && !Map[(CELL)s.scout_cell[j]].Is_Mapped(h);
            if (unfinished && s.scout_stall[j] < 2 && Target_Legal(scout->NavCom)) continue;
            CELL cell = Explore(h,s,scout);
            if (cell) { Command(scout,cell,MISSION_MOVE,s); s.scout_cell[j]=cell; s.scout_stall[j]=0; ++s.scouts; }
        }
    }
}
static void Collect(HouseClass* h, State const& s, FootClass* f, FootClass** list, int& count, int capacity)
{
    if (count >= capacity || !Live(f) || f->House != (HouseClass const*)h || !f->Is_Weapon_Equipped() || IsScout(s,f) || f->Team.Is_Valid()) return;
    // Harvesters and MCVs are never recruited, even if a rules mod equips them.
    if (f->What_Am_I()==RTTI_UNIT && (*(UnitClass*)f==UNIT_HARVESTER || *(UnitClass*)f==UNIT_MCV)) return;
    list[count++]=f;
}
static void Naval(HouseClass* h, State& s, int& budget)
{
    for (int i=0;i<Vessels.Count() && budget>0;++i) {
        VesselClass* ship=Vessels.Ptr(i);
        if (!Live(ship) || ship->House!=(HouseClass const*)h || !ship->Is_Weapon_Equipped() || ship->Team.Is_Valid()) continue;
        TechnoClass* best=NULL; int bestscore=-100000;
        BulletTypeClass const* bullet=ship->Techno_Type_Class()->PrimaryWeapon->Bullet;
        for (int o=0;o<Logic.Count();++o) {
            ObjectClass* object=Logic[o]; if(!object || !object->Is_Techno()) continue;
            TechnoClass* enemy=(TechnoClass*)object;
            if (!FairAI_RA_Observed(h,enemy) || h->Is_Ally(enemy)) continue;
            bool air=enemy->What_Am_I()==RTTI_AIRCRAFT && enemy->Height>0;
            if (air ? !bullet->IsAntiAircraft : !bullet->IsAntiGround) continue;
            if (*ship==VESSEL_SS && enemy->What_Am_I()!=RTTI_VESSEL) continue;
            int score=300-Distance(ship->Center_Coord(),enemy->Center_Coord())/CELL_LEPTON_W;
            if(enemy->What_Am_I()==RTTI_VESSEL) score+=100;
            if(score>bestscore) { bestscore=score; best=enemy; }
        }
        if(best) {
            if(ship->TarCom!=best->As_Target()) {
                ship->Assign_Mission(MISSION_ATTACK); ship->Assign_Target(best->As_Target()); ship->Commence(); ++s.orders; --budget;
            }
            continue;
        }
        if(Target_Legal(ship->NavCom)) continue;
        // Follow the edge of explored water. No unseen terrain or unseen unit positions
        // enter destination selection, and land objectives cannot strand the fleet.
        CELL frontier=0; int bestfront=-100000;
        for(int c=0;c<MAP_CELL_TOTAL;++c) {
            CELL cell=(CELL)c;
            if(!Map.In_Radar(cell)||!Map[cell].Is_Mapped(h)||Map[cell].Land_Type()!=LAND_WATER) continue;
            int unknown=0;
            for(FacingType dir=FACING_FIRST;dir<FACING_COUNT;dir++) {
                CELL next=Adjacent_Cell(cell,dir);
                if(Map.In_Radar(next)&&!Map[next].Is_Mapped(h)) ++unknown;
            }
            if(!unknown) continue;
            int distance=Distance(ship->Center_Coord(),Cell_Coord(cell))/CELL_LEPTON_W;
            if(distance<3) continue;
            int score=unknown*30-distance;
            if(score>bestfront) { bestfront=score; frontier=cell; }
        }
        if(frontier) { Command(ship,frontier,MISSION_MOVE,s); --budget; }
    }
}
static void Tactics(HouseClass* h, State& s)
{
    fair_ai::Profile const& p=fair_ai::Get(s.level);
    FootClass* army[512]; int count=0;
    for(int i=0;i<Units.Count();++i) Collect(h,s,Units.Ptr(i),army,count,512);
    for(int i=0;i<Infantry.Count();++i) Collect(h,s,Infantry.Ptr(i),army,count,512);

    CELL home=Home(h); if(!home) return;
    TechnoClass* threat=NULL; int closest=13*CELL_LEPTON_W;
    // Base and harvester defense are restricted to currently observable opponents.
    for(int i=0;i<Logic.Count();++i) {
        ObjectClass* object=Logic[i];
        if(!object || !object->Is_Techno()) continue;
        TechnoClass* t=(TechnoClass*)object;
        if(!FairAI_RA_Observed(h,t) || h->Is_Ally(t) || !t->Is_Weapon_Equipped()) continue;
        int dist=Distance(Cell_Coord(home),t->Center_Coord());
        for(int u=0;u<Units.Count();++u) {
            UnitClass* harv=Units.Ptr(u);
            if(Live(harv) && harv->House==h && *harv==UNIT_HARVESTER) dist=min(dist,Distance(harv->Center_Coord(),t->Center_Coord()));
        }
        if(dist<closest) { closest=dist; threat=t; }
    }
    int budget=p.max_orders;
    int reserve=max(1,count*p.reserve_percent/100);
    bool launch=s.objective && count-reserve>=p.squad_min && Frame>=s.next_attack;
    if(launch) { s.next_attack=Frame+p.attack_ticks; s.attack_until=Frame+TICKS_PER_MINUTE*2; ++s.attacks; }
    bool attacking=s.objective && Frame<s.attack_until;
    int groups=p.attack_groups;
    for(int i=0;i<count && budget>0;++i) {
        FootClass* f=army[i];
        bool damaged=f->Health_Ratio()<fixed(p.retreat_percent,100);
        CELL current=Coord_Cell(f->Center_Coord());
        if(damaged && s.level>=1 && Distance(f->Center_Coord(),Cell_Coord(home))>4*CELL_LEPTON_W) {
            if(f->Mission!=MISSION_MOVE || !Target_Legal(f->NavCom)) { Command(f,home,MISSION_MOVE,s); ++s.retreats; --budget; }
            continue;
        }
        if(threat && (i<reserve || !attacking)) {
            if(f->TarCom!=threat->As_Target() && f->Is_Weapon_Equipped()) {
                f->Assign_Mission(MISSION_ATTACK); f->Assign_Target(threat->As_Target()); f->Commence(); ++s.orders; --budget;
            }
            continue;
        }
        // Let local combat finish. Regroup idle survivors and launch a synchronized wave.
        if(Target_Legal(f->TarCom)) continue;
        CELL goal=home;
        if(attacking && i>=reserve) {
            goal=(CELL)s.objective;
            // Multiple approach groups use nearby known cells, retaining legal pathing.
            if(groups>1 && i%groups) {
                int x=Cell_X(goal)+(i%groups==1?3:-3), y=Cell_Y(goal);
                if(x>=0 && x<MAP_CELL_W) { CELL flank=XY_Cell(x,y); if(Map.In_Radar(flank) && Map[(CELL)(flank)].Is_Mapped(h)) goal=flank; }
            }
        }
        if(Distance(f->Center_Coord(),Cell_Coord(goal))<=3*CELL_LEPTON_W) {
            if(f->Mission==MISSION_MOVE) { f->Assign_Mission(MISSION_GUARD_AREA); f->Commence(); }
            continue;
        }
        if(!Target_Legal(f->NavCom) || Coord_Cell(As_Coord(f->NavCom))!=goal || launch) {
            // Sea units only accept sea cells; land armies retain normal pathfinder rules.
            if(f->What_Am_I()==RTTI_VESSEL && (!Map[(CELL)(goal)].Is_Mapped(h) || Map[(CELL)(goal)].Land_Type()!=LAND_WATER)) continue;
            Command(f,goal,MISSION_MOVE,s); --budget;
        }
    }
    Naval(h,s,budget);
    // Aircraft use legal attack missions and currently visible targets; no global Hunt.
    if(attacking && budget>0) for(int i=0;i<Aircraft.Count() && budget>0;++i) {
        AircraftClass* a=Aircraft.Ptr(i);
        if(!Live(a)||a->House!=(HouseClass const*)h||!a->Is_Weapon_Equipped()||Target_Legal(a->TarCom)||a->Ammo==0) continue;
        TechnoClass* target=Map[(CELL)(s.objective)].Cell_Techno();
        if(FairAI_RA_Observed(h,target) && !h->Is_Ally(target)) {
            a->Assign_Mission(MISSION_ATTACK); a->Assign_Target(target->As_Target()); a->Commence(); ++s.orders; --budget;
        }
    }
}
}

bool FairAI_RA_Enabled(HouseClass const* h)
{
    return h && h->Class && Session.Type != GAME_NORMAL && !h->IsHuman && h->Class->House >= HOUSE_MULTI1 && h->Class->House <= HOUSE_MULTI8;
}
bool FairAI_RA_Observed(HouseClass const* h, TechnoClass const* t)
{
    if(!Live(t) || !h) return false;
    if(h->Is_Ally(t)) return true;
    CELL cell=Coord_Cell(t->Center_Coord());
    if (!Map.In_Radar(cell) || !Map[cell].Is_Mapped((HouseClass*)h) || t->Is_Cloaked(h,true)) return false;
    // A spy's disguise is part of the rules. Only nearby attack dogs identify it.
    if (t->What_Am_I()==RTTI_INFANTRY && *(InfantryClass const*)t==INFANTRY_SPY) {
        for (int i=0;i<Infantry.Count();++i) {
            InfantryClass* dog=Infantry.Ptr(i);
            if (Live(dog) && dog->House==h && dog->Class->IsDog
                && Distance(dog->Center_Coord(),t->Center_Coord()) <= dog->Techno_Type_Class()->SightRange*CELL_LEPTON_W) return true;
        }
        return false;
    }
    return true;
}
unsigned int FairAI_RA_Prerequisites(HouseClass const* h)
{
    unsigned int flags=0;
    for(int i=0;i<Buildings.Count();++i) {
        BuildingClass* b=Buildings.Ptr(i);
        if(Live(b)&&b->House==h&&b->Mission!=MISSION_CONSTRUCTION&&b->Mission!=MISSION_DECONSTRUCTION&&b->MissionQueue!=MISSION_DECONSTRUCTION)
            if ((int)b->Class->Type < 32) flags|=1u<<(int)b->Class->Type;
    }
    return flags;
}
void FairAI_RA_Set_Levels(unsigned char const* levels,int count)
{
    for(int i=0;i<fair_ai::Seats;++i) requested[i]=(unsigned char)fair_ai::Clamp(levels&&i<count?levels[i]:1);
}
void FairAI_RA_Reset(void)
{
    memset(states,0,sizeof(states));
    for(int i=0;i<HOUSE_COUNT;++i) states[i].level=1;
}
void FairAI_RA_Assign(HouseClass* h,int seat)
{
    if(!h||!h->Class) return;
    State& s=Get(h); memset(&s,0,sizeof(s));
    s.level=seat>=0&&seat<fair_ai::Seats?requested[seat]:1;
    s.rng=0x6d2b79f5u ^ ((uint32_t)CustomSeed * 747796405u) ^ ((uint32_t)h->Class->House*2891336453u);
    s.next_attack=fair_ai::Get(s.level).attack_ticks;
}
void FairAI_RA_Post_Load(void)
{
    if (Session.Type==GAME_NORMAL) return;
    for (int i=HOUSE_MULTI1;i<=HOUSE_MULTI8;++i) {
        HouseClass* h=HouseClass::As_Pointer((HousesType)i);
        if (h && h->IsActive) h->Assign_Handicap(DIFF_NORMAL);
    }
}
void FairAI_RA_Tick(HouseClass* h)
{
    // Legacy skirmish saves can contain the old difficulty stat handicaps. Restore
    // the same normal country statistics for every occupied seat on their first tick.
    if(h && h->Class && Session.Type!=GAME_NORMAL && h->Class->House>=HOUSE_MULTI1
        && h->Class->House<=HOUSE_MULTI8 && h->Difficulty!=DIFF_NORMAL) h->Assign_Handicap(DIFF_NORMAL);
    if(!FairAI_RA_Enabled(h)||h->IsDefeated) return;
    State& s=Get(h); fair_ai::Profile const& p=fair_ai::Get(s.level);
    if(!s.initialized) { s.initialized=1; h->IQ=Rule.MaxIQ; }
    if(Frame>=(int)s.next_think) {
        Observe(h,s); Economy(h,s); ++s.thinks; s.next_think=Frame+p.think_ticks;
        // Repairs use the existing paid repair mechanism and the same power/cash limits.
        int repairs=0;
        for(int i=0;i<Buildings.Count()&&repairs<p.max_orders;++i) {
            BuildingClass* b=Buildings.Ptr(i);
            if(Live(b)&&b->House==h&&b->Health_Ratio()<fixed(3,4)&&!b->IsRepairing&&h->Available_Money()>p.cash_reserve) { b->Repair(1); ++repairs; }
        }
    }
    if(Frame>=(int)s.next_scout) { Scouts(h,s); s.next_scout=Frame+p.scout_ticks; }
    if(Frame>=(int)s.next_tactical) { Tactics(h,s); s.next_tactical=Frame+p.tactical_ticks; }
}
bool FairAI_RA_Production_Allowed(BuildingClass const* factory,TechnoTypeClass const* type)
{
    HouseClass* h=factory?(HouseClass*)factory->House:NULL;
    if(!FairAI_RA_Enabled(h)) return true;
    if(!type||factory->IsPowerOff||!LegalType(h,*type)||!Cash(h,*type,0)) return false;
    // Human players have one production queue per category. More factories accelerate
    // that queue through the existing Build_Time rules; they do not mint extra queues.
    for(int i=0;i<Buildings.Count();++i) {
        BuildingClass* b=Buildings.Ptr(i);
        if(b!=factory&&Live(b)&&b->House==h&&b->Class->ToBuild==factory->Class->ToBuild&&b->Factory.Is_Valid()) return false;
    }
    return true;
}
void FairAI_RA_Special(HouseClass* h,int id)
{
    if(!FairAI_RA_Enabled(h)) return;
    State& s=Get(h);
    if(id==SPC_SPY_MISSION) {
        CELL home=Home(h); if(!home) return;
        // Reconnaissance targets unexplored geography, without reading enemy positions.
        int width=Map.MapCellWidth,height=Map.MapCellHeight;
        for(int n=0;n<96;++n) {
            s.scout_cursor=(s.scout_cursor+37)%(width*height);
            CELL c=XY_Cell(Map.MapCellX+s.scout_cursor%width,Map.MapCellY+s.scout_cursor/width);
            if(!Map[(CELL)(c)].Is_Mapped(h)) { if(h->Place_Special_Blast((SpecialWeaponType)id,c)) ++s.orders; return; }
        }
    }
    if(s.objective && Map.In_Radar((CELL)s.objective) && Map[(CELL)(s.objective)].Is_Mapped(h)) {
        TechnoClass* t=Map[(CELL)(s.objective)].Cell_Techno();
        if(FairAI_RA_Observed(h,t)&&!h->Is_Ally(t)) if(h->Place_Special_Blast((SpecialWeaponType)id,(CELL)s.objective)) ++s.orders;
    }
}
uint32_t FairAI_RA_State_Hash(void)
{
    uint32_t hash=fair_ai::HashWord(2166136261u,fair_ai::PolicyHash());
    for(int i=0;i<HOUSE_COUNT;++i) {
        uint32_t const* words=(uint32_t const*)&states[i];
        for(unsigned w=0;w<sizeof(State)/4;++w) hash=fair_ai::HashWord(hash,words[w]);
    }
    return hash;
}
int FairAI_RA_Diagnostics(char* out,int size)
{
    int used=0;
    for(int i=0;i<HOUSE_COUNT;++i) {
        HouseClass* h=HouseClass::As_Pointer((HousesType)i); if(!FairAI_RA_Enabled(h)) continue;
        State const& s=Get(h); int mapped=0;
        for(int c=0;c<MAP_CELL_TOTAL;++c) if(Map.In_Radar(c)&&Map[(CELL)(c)].Is_Mapped(h)) ++mapped;
        char line[320];
        int n=snprintf(line,sizeof(line),"FAIRAI|house=%d|level=%u|name=%s|thinks=%u|orders=%u|scouts=%u|attacks=%u|retreats=%u|known_cells=%d|contacts=%u|objective=%u|rng=%u\n",i,s.level,fair_ai::Name(s.level),s.thinks,s.orders,s.scouts,s.attacks,s.retreats,mapped,s.contacts,s.objective,s.rng);
        if(out&&size>used+1) { int copy=min(n,size-used-1); memcpy(out+used,line,copy); out[used+copy]=0; }
        used+=n;
    }
    for(int i=HOUSE_MULTI1;i<=HOUSE_MULTI8;++i) {
        HouseClass* h=HouseClass::As_Pointer((HousesType)i);
        if(!h||!h->IsActive||(!h->IsHuman&&!Get(h).initialized)) continue;
        char line[384];
        int n=snprintf(line,sizeof(line),"FAIRRULE|house=%d|country=%d|human=%d|difficulty=%d|fire=%d|ground=%d|air=%d|armor=%d|rof=%d|cost=%d|build=%d|repair=%d|delay=%d\n",i,(int)h->ActLike,(int)h->IsHuman,(int)h->Difficulty,(int)(h->FirepowerBias*1000),(int)(h->GroundspeedBias*1000),(int)(h->AirspeedBias*1000),(int)(h->ArmorBias*1000),(int)(h->ROFBias*1000),(int)(h->CostBias*1000),(int)(h->BuildSpeedBias*1000),(int)h->RepairDelay,(int)h->BuildDelay);
        if(out&&size>used+1) { int copy=min(n,size-used-1); memcpy(out+used,line,copy); out[used+copy]=0; }
        used+=n;
    }
    return used;
}
int FairAI_RA_Save(unsigned char* out,int size)
{
    int bytes=16+(int)sizeof(states);
    if(!out||size<bytes) return -1;
    const uint32_t head[4]={0x49414652u,1u,(uint32_t)sizeof(states),fair_ai::PolicyHash()};
    memcpy(out,head,sizeof(head)); memcpy(out+16,states,sizeof(states)); return bytes;
}
bool FairAI_RA_Load(unsigned char const* in,int size)
{
    FairAI_RA_Reset();
    if(!in||size<16) return false;
    uint32_t head[4]; memcpy(head,in,16);
    if(head[0]==0) return true; // Saves preceding fair skill start at Normal.
    if(head[0]!=0x49414652u||head[1]!=1||head[2]!=sizeof(states)||size<16+(int)sizeof(states)||head[3]!=fair_ai::PolicyHash()) return false;
    State candidate[HOUSE_COUNT]; memcpy(candidate,in+16,sizeof(candidate));
    for(int i=0;i<HOUSE_COUNT;++i) {
        State const& s=candidate[i];
        if(s.level>=fair_ai::LevelCount||s.initialized>1||s.objective>=MAP_CELL_TOTAL||s.objective_owner>=HOUSE_COUNT||s.scout_cursor>MAP_CELL_TOTAL) return false;
        for(int scout=0;scout<2;++scout) {
            if(s.scout_cell[scout]>=MAP_CELL_TOTAL || s.scout_last[scout]>=MAP_CELL_TOTAL) return false;
            TARGET target=(TARGET)s.scout[scout];
            if(target!=TARGET_NONE && !((Is_Target_Unit(target) && Target_Value(target)<Units.Length())
                || (Is_Target_Infantry(target) && Target_Value(target)<Infantry.Length()))) return false;
        }
    }
    memcpy(states,candidate,sizeof(states)); return true;
}
