/*
 * sfxtable.h -- the engine's own sound tables, lifted from the GPL Tiberian Dawn
 * source so the audio side never has to guess a filename.
 *
 * sfx_voc[] is indexed by the SFXIndex the sound callback hands over; the name is
 * the base filename with no extension, and `where` says which extension rule
 * applies (see sfx_voc_filename).
 * sfx_vox[] is indexed by SpeechIndex and the file is always NAME.AUD.
 */

#ifndef SFXTABLE_H
#define SFXTABLE_H

#define SFX_VOC_COUNT 109
#define SFX_VOX_COUNT 46
#define SFX_THEME_COUNT 37

typedef enum
{
    SFX_NOVAR = 0, /* NAME.AUD, always                                    */
    SFX_JUV,       /* NAME.JUV when the juvenile option is on, else .AUD  */
    SFX_VAR        /* NAME.V00 .. .V03 chosen by the variation number     */
} SfxWhere;

typedef struct
{
    const char *name;
    int priority;
    SfxWhere where;
    int varuse; /* bit 0: asked with a positive variation (infantry .V01/.V03)  */
               /* bit 1: asked with a negative variation (vehicles .V00/.V02) */
               /* 0 means no response table ever names it                     */
} SfxEntry;

typedef struct
{
    const char *name;
    int scenario;  /* first scenario the track becomes available in */
    int duration;  /* seconds, as the 1995 table claims             */
    int normal;    /* allowed in ordinary play                      */
    int variation; /* a NAME.VAR alternate exists                   */
    int repeat;    /* always loops                                  */
    const char *fullname; /* the 1995 score name, CONQUER.ENG via TXT_THEME_* */
} ThemeEntry;

extern const SfxEntry sfx_voc[SFX_VOC_COUNT];
extern const char *const sfx_vox[SFX_VOX_COUNT];
extern const ThemeEntry sfx_theme[SFX_THEME_COUNT];

/* Builds the filename the 1995 engine would have asked MFCD for. `variation` is
 * the value from the sound callback; `juvenile` is Special.IsJuvenile. */
void sfx_voc_filename(int index, int variation, int juvenile, char *out, int outlen);

#endif /* SFXTABLE_H */
