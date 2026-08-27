/* sfxname.c -- the extension rule from Sound_Effect(), kept out of the generated
 * table so regenerating sfxtable.c cannot lose it.
 *
 * The rule (tiberiandawn/audio.cpp Sound_Effect, mirrored in dllinterface.cpp
 * On_Sound_Effect, which is the path that actually runs in our build):
 *
 *   IN_JUV  and Special.IsJuvenile  ->  NAME.JUV
 *   IN_VAR                          ->  variation < 0 ? (|v| odd ? .V00 : .V02)
 *                                                     : (  v  odd ? .V01 : .V03)
 *   otherwise                       ->  NAME.AUD
 *
 * Note the asymmetry: the DLL path leaves the extension EMPTY for the plain case
 * and appends ".AUD" nowhere, so the name that arrives in SoundEffectName is bare.
 * We append ".AUD" here, which is what MFCD::Retrieve was asked for in 1995.
 */

#include "sfxtable.h"

#include <stdio.h>
#include <string.h>

void sfx_voc_filename(int index, int variation, int juvenile, char *out, int outlen)
{
    const char *ext = ".AUD";

    if (index < 0 || index >= SFX_VOC_COUNT) {
        snprintf(out, (size_t)outlen, "BADINDEX");
        return;
    }

    if (juvenile && sfx_voc[index].where == SFX_JUV) {
        ext = ".JUV";
    } else if (sfx_voc[index].where == SFX_VAR) {
        if (variation < 0) {
            ext = ((-variation) % 2) ? ".V00" : ".V02";
        } else {
            ext = (variation % 2) ? ".V01" : ".V03";
        }
    }
    snprintf(out, (size_t)outlen, "%s%s", sfx_voc[index].name, ext);
}
