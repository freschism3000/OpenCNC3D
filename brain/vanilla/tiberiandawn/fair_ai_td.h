/* Fair skirmish policy state is external to legacy objects/save ABI. */
#ifndef OPENCNC3D_TD_FAIR_AI_H
#define OPENCNC3D_TD_FAIR_AI_H
class HouseClass;
class TechnoClass;
class FileClass;
void TD_Fair_AI_Reset();
void TD_Fair_AI_Set_Levels(const unsigned char*, int);
void TD_Fair_AI_Assign(HouseClass*, int);
void TD_Fair_AI_Tick(HouseClass*);
bool TD_Fair_AI_Knows(HouseClass const*, TechnoClass const*);
bool TD_Fair_AI_Save(FileClass&);
bool TD_Fair_AI_Load(FileClass&);
unsigned int TD_Fair_AI_Hash();
int TD_Fair_AI_Diagnostics(char*, int);
#endif
