/*
 * t1_anim.h -- which frame of which clip an object is on, this tick.
 *
 * The cartridge animates a model by driving its scene-graph nodes, and the pack bakes
 * those tracks onto a fixed frame grid (see t1_mesh.h). This file is the other half:
 * the DRIVERS that say where in a clip a particular object currently is.
 *
 * NOTHING HERE IS INVENTED. Every rule below was decoded from the cartridge's own draw
 * arms and the 1995 engine's animation tables by the desktop build, and is a plain C89
 * transcription of game/cnc_eyes.cpp's structure_anim_frame, construction_frac,
 * mcvrig_for and procrig_for, with the same ROM citations kept in place so the two
 * cannot drift apart silently. Where the desktop marks something as OURS rather than a
 * decode -- the free-running idle counter, the 2x video-rate hypothesis, the MCV rig's
 * stage-domain normalisation, a sold yard running the deploy backwards -- the note comes
 * across with it.
 *
 * THE CLOCK IS THE ENGINE TICK, NEVER THE WALL CLOCK. Every driver takes `ticks` and
 * nothing here reads a timer, so two scripted runs of the same script produce the same
 * pixels. That is not a nicety on this branch: the regression is a set of screenshots.
 */

#ifndef T1_ANIM_H
#define T1_ANIM_H

#include "w98_brain.h"
#include "t1_mesh.h"

/* BuildingClass::BState, as the brain reports it in `doing` for a building. */
#define T1_BSTATE_CONSTRUCTION 0
#define T1_BSTATE_IDLE         1
#define T1_BSTATE_ACTIVE       2

/* The clip frame for a structure whose type has a decoded draw arm, or -1 for
 * "this type does not animate", which is most of them. `frames` is the mesh's baked
 * frame count. */
float t1_anim_structure(const W98_Object *o, int frames, long ticks);

/* The generic fallback: loop clip 0 over the whole mesh, at one baked frame per tick.
 * Returns -1 when the mesh carries no clip. */
float t1_anim_default(const T1_MeshBank *b, int mi, long ticks);

/* WHAT AN OBJECT'S CLIP FRAME ACTUALLY IS: the decoded structure arm if its type has
 * one, otherwise the generic loop. -1 means "hold the rest pose", which is the picture
 * this renderer drew before any of this existed. Use this, not the two above. */
float t1_anim_object(const T1_MeshBank *b, int mi, const W98_Object *o, long ticks);

/* ---- the buildup clock, and why it is OURS on this build ----------------------------
 *
 * The 1995 engine gives every structure the SAME construction duration, and it is not a
 * taste value: BDATA.CPP:3845 sets `timedelay = (5 * TICKS_PER_SECOND) / count` and then
 * `Init_Anim(BSTATE_CONSTRUCTION, 0, count, timedelay)`, so count * timedelay is five
 * seconds whatever the MAKE.SHP frame count happens to be.
 *
 * ON THIS BUILD THE ENGINE CANNOT TELL US WHERE IN THAT SPAN A BUILDING IS. That whole
 * block is guarded by `if (dataptr)`, i.e. by MAKE.SHP having loaded out of a MIX, and
 * our headless brain has no 1995 shape art: measured on the box, every building reports
 * makecnt = 1 (the default at BDATA.CPP:3740) and its dostage never leaves 0 while
 * BState is CONSTRUCTION. With Count = 1 the engine's own construction state also ends
 * almost immediately, which is why a placed building simply appeared, whole, and why the
 * MCV deploy rig flickered past in a handful of frames.
 *
 * So the renderer keeps the clock: a building assembles over the engine's own five
 * seconds from the moment it is first seen, and only if the engine said CONSTRUCTION when
 * it was first seen -- otherwise every structure already standing at mission start would
 * build itself again. The DURATION is the engine's; the COUNTER is ours. Registered in
 * known-gap notes, and the thing that deletes it is a brain that can load MAKE.SHP.
 * ---------------------------------------------------------------------------------- */
#define T1_BUILDUP_TICKS 75            /* 5 * TICKS_PER_SECOND, BDATA.CPP:3845 */

/* Call once per object dump, before anything asks for a build fraction. */
void t1_anim_track(const W98_Object *objs, int n, long ticks);

/* TICKS WITHOUT MOTION BEFORE A UNIT COUNTS AS STANDING. Ported from cnc_eyes.cpp's
 * AIM_STILL_TICKS with its reasoning intact: NOT 1, because a driving unit's speed
 * accumulator emits isolated zero-progress ticks (one in five, measured on the gunboat
 * at cruise), and a single such tick read as "standing" snaps a whole turretless mesh
 * to its turret facing for one frame and back -- the boat spin bug. */
#define T1_AIM_STILL_TICKS 4

/* Has this object been motionless for T1_AIM_STILL_TICKS engine ticks. Fed by
 * t1_anim_track, so it is only as fresh as the last object dump. */
int t1_anim_still(const W98_Object *o);

/* THE FACING AUDIT, cumulative over the run. `sum/n` is the mean distance in DirType
 * units between the direction a unit MOVED and the facing the engine reported. The drawn
 * nose is that facing by construction, so a mean near zero is the proof that the art
 * points where the thing is going; 128 is backwards and 64 is sideways. */
void t1_anim_face_audit(long *n, long *sum, long *max);
int  t1_anim_face_audit_type(int i, const char **name, long *n, long *sum, long *max);

/* How much of a building currently EXISTS, 0..1, for the section-by-section assembly.
 * 1.0 for anything that is not mid-construction. A building being SOLD sheds sections in
 * reverse, which is what the console does. */
float t1_anim_build_frac(const W98_Object *o, long ticks);

/* The MCV deploy rig: the mesh to draw INSTEAD of a Construction Yard that is still
 * unfolding, or -1. Writes the clip frame to *animT. */
int t1_anim_mcvrig(const T1_MeshBank *b, const W98_Object *o, long ticks, float *animT);

/* The refinery's SECOND model: the mesh to draw ALONGSIDE a PROC, or -1. The refinery's
 * own tracks are flat on every frame; its animation lives entirely in this rig. */
int t1_anim_procrig(const T1_MeshBank *b, const W98_Object *o, float *animT);

#endif /* T1_ANIM_H */
