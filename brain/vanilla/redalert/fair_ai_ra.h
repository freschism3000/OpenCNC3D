/* OpenCNC3D fair skirmish controller. State is external to legacy save-layout classes. */
#ifndef OPENCNC3D_FAIR_AI_RA_H
#define OPENCNC3D_FAIR_AI_RA_H
#include <stdint.h>
class HouseClass;
class TechnoClass;
class TechnoTypeClass;
class BuildingClass;
bool FairAI_RA_Enabled(HouseClass const* house);
bool FairAI_RA_Observed(HouseClass const* house, TechnoClass const* object);
unsigned int FairAI_RA_Prerequisites(HouseClass const* house);
void FairAI_RA_Set_Levels(unsigned char const* levels, int count);
void FairAI_RA_Reset(void);
void FairAI_RA_Post_Load(void);
void FairAI_RA_Assign(HouseClass* house, int seat);
void FairAI_RA_Tick(HouseClass* house);
bool FairAI_RA_Production_Allowed(BuildingClass const* factory, TechnoTypeClass const* type);
void FairAI_RA_Special(HouseClass* house, int special);
uint32_t FairAI_RA_State_Hash(void);
int FairAI_RA_Diagnostics(char* out, int size);
int FairAI_RA_Save(unsigned char* out, int size);
bool FairAI_RA_Load(unsigned char const* in, int size);
#endif
