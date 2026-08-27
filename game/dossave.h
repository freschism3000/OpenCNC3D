/*
 * dossave.h -- the save-slot index. Plain C89: no GL, no SDL, no engine header, so the
 * Win98 build takes it unchanged. Same rule dosbar.c and hud640.c follow.
 *
 * WHAT THIS OWNS AND WHAT IT DOES NOT.
 *
 * The ENGINE writes the game. saveload.cpp's Save_Game codes every pointer in the world
 * into a target id, writes the fifteen object heaps, the map, the score, the base and
 * Save_Misc_Values (PlayerPtr, scenario, difficulty, Frame, the win/lose movie names, the
 * selection list, the waypoints, the build level), and DLLExportClass::Save adds the
 * per-player build queues and factory progress on top. None of that is ours and none of it
 * is reimplemented here: CNC_Save_Load is handed an absolute path and does the work.
 *
 * This file owns the INDEX beside it, and it exists for one measured reason: the engine's
 * own header is not host-readable. In a file written by this build the description string
 * sits at offset 10,558, behind a DLL block that explicitly reserves 4095 bytes "for save
 * game expansion", and the one API that could read it (Get_Savefile_Info, saveload.cpp:992)
 * takes an integer id and rebuilds SAVEGAME.%03d itself rather than accepting a path. A
 * host that parsed that header would be parsing a moving target.
 *
 * So each slot is two files: SAVEGAME.nnn, which is 1995's own 8.3-safe name and is the
 * engine's, and one CNC3DSAV.IDX holding sixteen fixed 128-byte little-endian records,
 * written with plain fopen/fwrite exactly the way app/campaign.c writes HALLFAME.DAT.
 *
 * A slot is offered to the player only when BOTH its record is valid AND its payload file
 * is on disk. A row you can select and cannot load is worse than a row that is not there.
 */

#ifndef DOSSAVE_H
#define DOSSAVE_H

#ifdef __cplusplus
extern "C" {
#endif

#define DS_SLOTS 16
#define DS_REC 128       /* bytes per index record, fixed for ever */
#define DS_DESCR 44      /* DESCRIP_MAX, defines.h:2026 */

typedef struct DS_Slot
{
    int used;
    int slot;                    /* the %03d in SAVEGAME.nnn */
    char descr[DS_DESCR + 1];
    char scen[12];               /* "SCG02EA": which mission, so a load can refuse a
                                    slot saved in a different one */
    char pack[24];               /* "SCG01EA.pack": the terrain, models and tiberium */
    unsigned char camp_active;   /* the campaign record the shell owns */
    unsigned char camp_side;
    unsigned char camp_scenario;
    unsigned char camp_dir;
    unsigned char camp_var;
    unsigned char build;         /* --build, the sidebar's build level */
    unsigned char cam_mode;
    int camx_m, camz_m;          /* camera, in thousandths of a cell */
    int dist_m, zoom_m;
    int music;                   /* ThemeType playing, or -1 */
    int frame;                   /* engine frame, for the list row */
    unsigned int bytes;          /* payload size, for the list row */
} DS_Slot;

/* Where the two files live. Called once by the shell with SDL_GetPrefPath's answer, or
 * with --savedir so a gate owns a scratch directory and never touches the player's.
 * A NULL or empty directory degrades to "no persistence", the same way the hall of fame
 * does, rather than writing into the working directory by accident. */
void ds_set_dir(const char *dir);
const char *ds_get_dir(void);

/* "<dir>/SAVEGAME.%03d", absolute. CDFileClass::Open short-circuits on an absolute path
 * for both read and write (cdfile.cpp:485-517), so this is exact and portable and the
 * engine's own user-path redirection never has to be reasoned about. */
const char *ds_payload_path(int slot);

/* The index. Both return the number of live records, or -1 when there is no directory. */
int ds_read_index(DS_Slot out[DS_SLOTS]);
int ds_write_index(const DS_Slot in[DS_SLOTS]);

/* Lowest unused slot, or -1 when all sixteen are taken. */
int ds_first_free(const DS_Slot t[DS_SLOTS]);

/* Does slot's payload file actually exist? */
int ds_payload_exists(int slot);

#ifdef __cplusplus
}
#endif

#endif /* DOSSAVE_H */
