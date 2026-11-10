#ifndef _UNK_0C0A70_H_
#define _UNK_0C0A70_H_
#include <ultra64.h>

extern s32 lastFrameCounter;
extern s32 currentFrameCounter;
extern s32 speedgraphframes;

#if defined (BUGFIX_R1)
extern f32 jpD_800484CC;
extern f32 jpD_800484D0;
#endif

extern s32 previousFrameCounter;
extern s32 halfFrameCounter;
extern s32 isFrameCounterOdd;
extern s32 halfMinusPreviousCounter;
extern u32 copy_of_osgetcount_value_0;
extern u32 copy_of_osgetcount_value_1;
extern s32 frameDelay;

#ifndef TARGET_N64
/* GE_TICKHZ sub-field ticking (contract in frametiming.c): portTickDelta is the
 * fractional field delta for this tick (0.5 at 120Hz), portTickCadence the last
 * whole-field delta held across 0-delta sub-ticks, and portTickHz() the
 * configured rate (60 = classic, 0 = uncapped, forced to 60 while a demo is
 * recording). */
extern f32 portTickDelta;
extern s32 portTickCadence;
/* Smoothed presentation stats, written by gfx_sdl2's title-stat pass
 * (500ms window) for the midas.ini show_fps in-game overlay. 0 headless. */
extern s32 g_PortStatFps;
extern s32 g_PortStatTps;
s32 portTickHz(void);           /* 60 = classic, 0 = uncapped, else that literal rate */
u32 portTickGateCounts(void);   /* boss.c tick admission threshold, in counts */
u32 portTickFieldGateCounts(void); /* gate that yields exactly 60 ticks/s (front end) */
s32 portPaceDue(u32 *stamp, u32 counts); /* real-time pacing for display-list work */
s32 portTickIntroPaceDue(s32 fields);    /* held front-end tick due (advancing deadline) */
s32 portGunbarrelBloodDiv(void);         /* blood-drip divisor (1=30fps, 2=15fps
                                          * half - drip only) */
void portTickIntroPaceReset(void);
void portTickSetHz(s32 hz);     /* midas.ini [display] tickhz; GE_TICKHZ still wins */
void portTickSetHwPace(s32 on); /* tickhz=hw: the console's frame pacing on the 60 Hz sim (frametiming.c) */
s32 portTickHwPace(void);       /* 1 = hold frames for the modelled N64 fields (not during demos / recording) */
s32 portHwPaceFields(void);     /* the fields the last frame would have cost the console, 1..6 */
s32 portFastTickFixOn(void);    /* port collision fixes: tick under two fields, no demo, no Hardware pacing */
/* Sub-field smoothing (see frametiming.c "Sub-field SMOOTHING"):
 * portAnimTicks() = fractional anim ticks this tick (== g_ClockTimer when the
 * delta is whole); portLagStep advances `x = x*d + in` by delta fields exactly;
 * GE_NOSMOOTH=1 restores field stepping at a raised rate. */
s32 portSmoothOn(void);
f32 portAnimTicks(void);
f32 portTickFieldPhase(void); /* sub-field phase [0,1) banked by the field carry */
f32 portRenderFieldPhase(void); /* that phase for render-only particle smoothing, held while paused */
void portLagStep(f32 *x, f32 d, f32 in, f32 delta);
void portDecayStep(f32 *x, f32 d, f32 delta);
/* tickhz=hw stage-load work (frametiming.c "texture decode and stage setup"):
 * calls and steps of the setup code the N64 bench ROM timed, counted where
 * the code runs and priced at the end of each stage load */
enum
{
    PORTHW_MATCH_CALLS,     /* stanMatchTileName */
    PORTHW_MATCH_STEPS,     /* ...tiles it walked */
    PORTHW_NEAR_CALLS,      /* sub_GAME_7F0AFB78 (nearest tile to a point) */
    PORTHW_NEAR_TILES,
    PORTHW_NEAR_POINTS,     /* ...points it tested */
    PORTHW_WALK_CALLS,      /* sub_GAME_7F0B0914 */
    PORTHW_SETUPIDX_STEPS,  /* setupGetPtrToCommandByIndex: propdefs walked */
    PORTHW_CHRFIND_STEPS,   /* chrFindByLiteralId: slots walked */
    PORTHW_GUARDS,          /* proplvreset2 propdefs */
    PORTHW_OBJECTS,
    PORTHW_TEXTURES,        /* textures decoded */
    PORTHW_WORK_COUNT
};
extern u32 g_PortHwWork[PORTHW_WORK_COUNT];
void portHwTexInfo(s32 format, s32 width, s32 height, s32 method, s32 numcolours, s32 numimages);
void portHwTexDecoded(s32 texnum, s32 zlib, s32 lod, s32 explicitlods);
void portHwNoteTexture(s32 texnum, u32 romBytes);
void portHwNotePropdef(s32 type);
#endif

void waitForNextFrame(void);


void updateFrameCounters(s32 deltaFrames);
void store_osgetcount(void);

#endif
