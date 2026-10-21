/**
 * bhop - jumping, ledge jumping, bunnyhopping and a speedometer, as a mod.
 *
 * Jump (Space): an upward kick to field_70 / field_7C while Bond is on the
 * ground; the game's own fall code does the rest. A jump can step onto
 * objects (PLAYER_FLOOR, PLAYER_STEP_WINDOW) and cross tile edges onto
 * ledges or down past railings (PLAYER_BLOCKED). Airborne, Quake/CS air
 * control replaces the walk animation's motion (PLAYER_MOVE).
 *
 * Options ([input]): jumping, jump_height, jump_ledges, bhop, auto_bhop,
 * speedometer. GE_BHOPDBG=1 logs speed, GE_LEDGEDBG=1 ledge attempts.
 *
 * Exports bhop_get_velocity / bhop_set_velocity for other mods (funguns
 * carries momentum through portals with them).
 */
#include <ultra64.h>
#include <bondtypes.h>
#include <options.h>
#include "bg.h"
#include "bondview.h"
#include "chr.h"
#include "chrai.h"
#include "player.h"
#include "propobj.h"
#include "stan.h"
#include "textrelated.h"
#include "midas_mod.h"

/* The tree's IDO math.h hides its float declarations from GCC; an implicit
 * one returns garbage and leaks an x87 stack slot. */
extern float sqrtf(float);
extern float fabsf(float);
extern char *getenv(const char *);
extern s32 sprintf(char *, const char *, ...);

extern f32 g_GlobalTimerDelta;
extern s32 g_GlobalTimer;
extern s32 g_PlayerIsInTank;
extern struct PropRecord *g_PlayerTankProp;
extern s16 *ptr_list_object_lookup_indices;
extern PropRecord g_Props[];
extern s32 obj_collision_flag;
extern vec3d g_ForceBondMoveOffset;
extern struct fontchar *ptrFontBankGothicChars;
extern struct font *ptrFontBankGothic;

extern s32 get_cur_playernum(void);
extern s32 getPlayerCount(void);
extern s32 get_debug_fast_bond_flag(void);
extern s32 sub_GAME_7F0B21B0(StandTile **tileStack, f32 target_x, f32 target_z, f32 radius, s32 *rooms, s32 *count_rtn, s32 bufMax);
extern void roomGetProps(s32 *rooms);
extern s32 propIsOfCdType(PropRecord *prop, s32 cdtypes);
extern void chraiGetCollisionBounds(PropRecord *prop, struct rect4f **polygon, s32 *edges, f32 *top, f32 *bottom);
extern s32 chrpropTestPointInPolygon(coord3d *point, struct rect4f *polygon, s32 edges);
extern s32 chrobjTestPointPolygonCollision(struct coord3d *point, f32 collision_radius, struct rect4f *polygon, s32 edges);
extern s32 bgTestBulletHitBackground(coord3d *from, coord3d *to, s32 roomnum, struct HitThing *hit);
extern f32 get_room_data_float2(void);
extern f32 stanGetPositionYValue(StandTile *tile, f32 p_x, f32 p_z);
extern void bondviewGetCollisionRadius(PropRecord *prop, f32 *collision_radius, f32 *height, f32 *always_30);
extern StandTile *stanFindTileBelowPos(coord3d *pos, u8 *rooms, f32 *yRtn);
extern f32 bondviewGetPlayerDuckingHeightRelated(struct player *player);
extern s32 getTileRoom(StandTile *tile);
extern void sub_GAME_7F03D058(PropRecord *prop, s32 unset);
extern s32 stanTestVolume(StandTile **tile, f32 x, f32 z, f32 radius, s32 cdtypes, f32 height, f32 always_30);
extern s16 viGetViewLeft(void);
extern s16 viGetViewTop(void);
extern s16 viGetX(void);
extern s16 viGetY(void);
extern s32 portFullHeightActive(void);
extern s32 portBondControlsLive(void);
extern s32 portVideoShowFps(void);
extern Gfx *microcode_constructor(Gfx *gdl);
extern Gfx *combiner_bayer_lod_perspective(Gfx *gdl);

MIDAS_MOD_ABI_EXPORT

static const MidasHost *H;

static const s32 *s_jumping;
static const s32 *s_jumpHeight;
static const s32 *s_jumpLedges;
static const s32 *s_bhop;
static const s32 *s_autoBhop;
static const s32 *s_speedometer;
static s32 s_jumpAct;

/** 0 with jumping off, so every jump feature keyed on it switches off too. */
static f32 jumpHeight(void)
{
    return *s_jumping ? (f32) *s_jumpHeight : 0.0f;
}

/* Bunnyhop state per player: world velocity (units per tick) that replaces
 * the walk animation's motion while airborne. */
static f32 s_bhopVel[4][2];
static f32 s_bhopGround[4];   /* ticks on the ground since landing */
static f32 s_bhopWalkMax[4];  /* fastest walk-animation speed seen on the ground */
static s32 s_bhopActive[4];
static f32 s_hudSpeed[4];     /* horizontal speed actually moved, units per second */

/* Quake/CS air control: sv_airaccelerate 10, and the 30-of-250 air wish cap. */
#define BHOP_AIR_ACCEL   10.0f
#define BHOP_AIR_CAP     0.12f
#define BHOP_MAX_MULT    5.0f   /* speed cap in walk speeds, so collision can't be tunnelled */
#define BHOP_GRACE_TICKS 4.0f   /* on the ground this long keeps full speed (the re-jump window) */
#define BHOP_FRICTION    0.2f   /* per tick, toward walk speed, once the grace is over */
#define BHOP_GROUND_ACCEL 20.0f /* sv_accelerate-style, on ground ticks */

static s32 bhopAirborne(void)
{
    return (g_CurrentPlayer->field_7C > 0.0f)
        || (g_CurrentPlayer->field_70 > g_CurrentPlayer->stanHeight + 1.0f);
}

/**
 * PLAYER_MOVE: `wx`/`wz` is the walk animation's world velocity per tick
 * already in `move`. Airborne (and for a short grace after landing) that is
 * swapped for s_bhopVel, accelerated Quake-style toward the strafe/forward
 * input.
 */
static void bhopMove(struct coord3d *move, f32 wx, f32 wz)
{
    static s32 dbg = -1;
    const s32 p = get_cur_playernum() & 3;
    const f32 d = g_GlobalTimerDelta;
    f32 *v = s_bhopVel[p];
    const f32 t0 = g_CurrentPlayer->field_488.theta_transform.f[0];
    const f32 t2 = g_CurrentPlayer->field_488.theta_transform.f[2];
    const f32 wspd = sqrtf((wx * wx) + (wz * wz));
    f32 vmax;
    f32 spd;

    if (dbg < 0)
    {
        dbg = (getenv("GE_BHOPDBG") != NULL);
    }
    if (!(*s_jumping && *s_bhop) || (d <= 0.0f) || g_PlayerIsInTank || (g_PlayerTankProp != NULL))
    {
        s_bhopActive[p] = 0;
        return;
    }

    if (!bhopAirborne())
    {
        if (wspd > s_bhopWalkMax[p])
        {
            s_bhopWalkMax[p] = wspd;
        }
        if (!s_bhopActive[p])
        {
            return;
        }
        s_bhopGround[p] += d;
        if (wspd > 0.0f)
        {   /* Quake ground accelerate toward the walk velocity: a one-tick
             * landing can win back speed lost to a wall, never beyond walk speed */
            const f32 add = wspd - (((v[0] * wx) + (v[1] * wz)) / wspd);

            if (add > 0.0f)
            {
                f32 acc = BHOP_GROUND_ACCEL * wspd * d / 60.0f;

                if (acc > add)
                {
                    acc = add;
                }
                v[0] += acc * wx / wspd;
                v[1] += acc * wz / wspd;
            }
        }
        if (s_bhopGround[p] > BHOP_GRACE_TICKS)
        {
            f32 k = BHOP_FRICTION * d;

            if (k > 1.0f)
            {
                k = 1.0f;
            }
            v[0] += (wx - v[0]) * k;
            v[1] += (wz - v[1]) * k;
            if ((fabsf(v[0] - wx) + fabsf(v[1] - wz)) < (0.05f * s_bhopWalkMax[p]) + 0.01f)
            {
                s_bhopActive[p] = 0;
                return;
            }
        }
    }
    else
    {
        f32 fx;
        f32 fz;
        f32 mag;
        f32 sf = g_CurrentPlayer->speedforwards;
        f32 ss = g_CurrentPlayer->speedsideways;

        if (!s_bhopActive[p])
        {
            s_bhopActive[p] = 1;
            v[0] = wx;
            v[1] = wz;
        }
        s_bhopGround[p] = 0.0f;

        /* the same forward/strafe basis the debug fast-bond move uses */
        fx = (t0 * sf) - (t2 * ss);
        fz = (t2 * sf) + (t0 * ss);
        mag = sqrtf((fx * fx) + (fz * fz));
        if ((mag > 0.0f) && (s_bhopWalkMax[p] > 0.0f))
        {
            const f32 wishspeed = s_bhopWalkMax[p] * ((mag > 1.0f) ? 1.0f : mag);
            const f32 cap = wishspeed * BHOP_AIR_CAP;
            f32 add;
            f32 acc;

            fx /= mag;
            fz /= mag;
            add = cap - ((v[0] * fx) + (v[1] * fz));
            if (add > 0.0f)
            {
                acc = BHOP_AIR_ACCEL * wishspeed * d / 60.0f;
                if (acc > add)
                {
                    acc = add;
                }
                v[0] += acc * fx;
                v[1] += acc * fz;
            }
        }
    }

    vmax = s_bhopWalkMax[p] * BHOP_MAX_MULT;
    spd = sqrtf((v[0] * v[0]) + (v[1] * v[1]));
    if ((vmax > 0.0f) && (spd > vmax))
    {
        v[0] *= vmax / spd;
        v[1] *= vmax / spd;
    }

    move->f[0] += (v[0] - wx) * d;
    move->f[2] += (v[1] - wz) * d;

    if (dbg)
    {
        static s32 n;

        if ((n++ % 15) == 0)
        {
            H->log("spd=%.2f walk=%.2f (x%.2f) %s v=(%.2f %.2f) w=(%.2f %.2f) mv=(%.2f %.2f) pos=(%.1f %.1f %.1f) d=%.2f\n",
                   spd, s_bhopWalkMax[p], (s_bhopWalkMax[p] > 0.0f) ? spd / s_bhopWalkMax[p] : 0.0f,
                   bhopAirborne() ? "air" : "gnd", v[0], v[1], wx, wz, move->f[0], move->f[2],
                   g_CurrentPlayer->field_488.collision_position.f[0], g_CurrentPlayer->field_70,
                   g_CurrentPlayer->field_488.collision_position.f[2], d);
        }
    }
}

/**
 * PLAYER_MOVED: a wall or slide shortens the air velocity to what was
 * actually moved, so hitting something kills the speed.
 */
static void bhopMoved(f32 movedx, f32 movedz)
{
    const s32 p = get_cur_playernum() & 3;
    const f32 d = g_GlobalTimerDelta;
    f32 *v = s_bhopVel[p];
    f32 ax;
    f32 az;

    if (d <= 0.0f)
    {
        return;
    }
    ax = movedx / d;
    az = movedz / d;
    /* smoothed so the speedometer doesn't flicker with collision jitter */
    s_hudSpeed[p] += ((sqrtf((ax * ax) + (az * az)) * 60.0f) - s_hudSpeed[p]) * 0.3f;
    if (!s_bhopActive[p])
    {
        return;
    }
    if (((ax * ax) + (az * az)) < ((v[0] * v[0]) + (v[1] * v[1])))
    {
        v[0] = ax;
        v[1] = az;
    }
}

/** The air velocity (units per tick) while bhop motion is active; 0 = none. */
MIDAS_EXPORT s32 bhop_get_velocity(f32 *vx, f32 *vz)
{
    const s32 p = get_cur_playernum() & 3;

    if (!s_bhopActive[p])
    {
        return 0;
    }
    *vx = s_bhopVel[p][0];
    *vz = s_bhopVel[p][1];
    return 1;
}

/** Carry this velocity from now on (needs bhop on to take effect). */
MIDAS_EXPORT void bhop_set_velocity(f32 vx, f32 vz)
{
    const s32 p = get_cur_playernum() & 3;

    s_bhopVel[p][0] = vx;
    s_bhopVel[p][1] = vz;
    s_bhopActive[p] = 1;
    s_bhopGround[p] = 0.0f;
}

/**
 * PLAYER_FLOOR: the top of the highest object under Bond that he can step
 * onto from his current feet height (field_70), or `floor` if there is none.
 * Only objects that block at floor level count (top above floor + 30, the
 * step allowance), so low props stay walk-through as on N64.
 */
static f32 jumpObjectFloor(f32 floor)
{
    StandTile *tile = g_CurrentPlayer->field_488.current_tile_ptr;
    struct coord3d *pos = &g_CurrentPlayer->field_488.collision_position;
    const f32 radius = g_CurrentPlayer->field_488.collision_radius;
    const f32 feet = g_CurrentPlayer->field_70;
    f32 best = floor;
    s32 rooms[21];
    s32 count = 0;
    s16 *idx;

    if (tile == NULL || jumpHeight() <= 0.0f)
    {
        return floor;
    }
    sub_GAME_7F0B21B0(&tile, pos->f[0], pos->f[2], radius, rooms, &count, 20);
    rooms[(count > 20) ? 20 : count] = -1;
    roomGetProps(rooms);

    for (idx = ptr_list_object_lookup_indices; *idx >= 0; idx++)
    {
        PropRecord *prop = &g_Props[*idx];
        struct rect4f *polygon;
        s32 edges;
        f32 top;
        f32 bottom;

        if ((prop->type != PROP_TYPE_OBJ) || !propIsOfCdType(prop, CDTYPE_OBJS)
            || (((struct TankRecord *) prop->obj)->type == PROPDEF_TANK))
        {
            continue;
        }
        chraiGetCollisionBounds(prop, &polygon, &edges, &top, &bottom);
        if ((edges <= 0) || (top <= floor + 30.0f) || (top > feet + 30.0f) || (top <= best))
        {
            continue;
        }
        if (chrpropTestPointInPolygon(pos, polygon, edges)
            || chrobjTestPointPolygonCollision(pos, radius * 0.5f, polygon, edges))
        {
            best = top;
        }
    }
    return best;
}

/**
 * PLAYER_STEP_WINDOW: the stan prop tests measure this window from the
 * floor, so lift it by how far Bond's feet are above the floor: mid-jump or
 * on top of a crate he clears what is below his feet + 30, as a step on the
 * floor.
 */
static void jumpStepWindow(StandTile *floor, f32 *height, f32 *always_30)
{
    f32 lift;

    if (jumpHeight() <= 0.0f)
    {
        return;
    }
    lift = g_CurrentPlayer->field_70 - stanGetPositionYValue(floor,
        g_CurrentPlayer->field_488.collision_position.f[0],
        g_CurrentPlayer->field_488.collision_position.f[2]);
    if (lift > 0.0f)
    {
        *height += lift;
        *always_30 += lift;
    }
}

static HitThing s_ledgeHit;  /* last hit, for GE_LEDGEDBG */

/* 1 = level geometry crosses the horizontal segment from `from` to `to` at
 * height y in `room`. A room with no geometry loaded counts as a hit, so an
 * unknown wall is never walked through. walls_only ignores floor-like hits
 * (normal mostly vertical), which a ray skimming a ledge top can graze. */
static s32 ledgeBgHit(struct coord3d *from, struct coord3d *to, f32 y, s32 room, s32 walls_only)
{
    struct coord3d a;
    struct coord3d b;
    f32 n;

    if ((room < 0) || (g_BgRoomInfo[room].vtx_batch_bounds == NULL))
    {
        s_ledgeHit.normal.f[1] = -99.0f; /* marks "room not loaded" in the log */
        return 1;
    }
    a.f[0] = from->f[0]; a.f[1] = y; a.f[2] = from->f[2];
    b.f[0] = to->f[0];   b.f[1] = y; b.f[2] = to->f[2];
    if (!bgTestBulletHitBackground(&a, &b, room, &s_ledgeHit))
    {
        return 0;
    }
    {   /* the bullet test reports the nearest hit on the whole line, not just
         * the segment, so a far wall would count; keep hits within the segment */
        const f32 ws = get_room_data_float2();
        const f32 hx = (s_ledgeHit.hitpos.f[0] * ws) - a.f[0];
        const f32 hz = (s_ledgeHit.hitpos.f[2] * ws) - a.f[2];
        const f32 sx = b.f[0] - a.f[0];
        const f32 sz = b.f[2] - a.f[2];

        if (((hx * sx) + (hz * sz) < 0.0f) || ((hx * hx) + (hz * hz) > (sx * sx) + (sz * sz) + 1.0f))
        {
            return 0;
        }
    }
    n = sqrtf((s_ledgeHit.normal.f[0] * s_ledgeHit.normal.f[0])
              + (s_ledgeHit.normal.f[1] * s_ledgeHit.normal.f[1])
              + (s_ledgeHit.normal.f[2] * s_ledgeHit.normal.f[2]));
    return !(walls_only && (n > 0.0f) && (fabsf(s_ledgeHit.normal.f[1]) > 0.7f * n));
}

/**
 * PLAYER_BLOCKED: a move refused by a tile edge while Bond is off the floor.
 * Stan edges have no height, so ledges and railings look like walls; this
 * crosses when there is floor behind the edge no higher than a step above his
 * feet (a ledge to climb, or lower ground past a railing), he fits there, and
 * the room geometry is clear at max(feet, ledge) and at head height.
 * Bond is placed one collision radius past the edge so his circle clears it.
 */
static s32 jumpLedgeMove(struct coord3d *next_pos)
{
    static s32 dbg = -1;
    StandTile *cur = g_CurrentPlayer->field_488.current_tile_ptr;
    struct coord3d *pos = &g_CurrentPlayer->field_488.collision_position;
    const f32 feet = g_CurrentPlayer->field_70;
    struct coord3d target;
    StandTile *ledge;
    StandTile *fit;
    f32 curFloor;
    f32 ledgeY = 0.0f;
    f32 radius;
    f32 height;
    f32 always_30;
    f32 dx;
    f32 dz;
    f32 len;
    s32 cdtypes;
    s32 ok;
    const char *why = NULL;
    const char *ray = "";
    static s32 lastLog;

    if (dbg < 0)
    {
        dbg = (getenv("GE_LEDGEDBG") != NULL);
    }
    if ((cur == NULL) || (jumpHeight() <= 0.0f) || g_PlayerIsInTank || (g_PlayerTankProp != NULL))
    {
        return 0;
    }
    curFloor = stanGetPositionYValue(cur, pos->f[0], pos->f[2]);
    if (feet <= curFloor + 1.0f)
    {
        return 0;
    }

    bondviewGetCollisionRadius(g_CurrentPlayer->prop, &radius, &height, &always_30);
    dx = next_pos->f[0] - pos->f[0];
    dz = next_pos->f[2] - pos->f[2];
    len = sqrtf((dx * dx) + (dz * dz));
    if (len <= 0.0f)
    {
        return 0;
    }
    target.f[0] = next_pos->f[0] + ((dx / len) * (radius + 2.0f));
    target.f[1] = feet + 30.0f;
    target.f[2] = next_pos->f[2] + ((dz / len) * (radius + 2.0f));

    ledge = stanFindTileBelowPos(&target, NULL, &ledgeY);
    if (ledge == NULL)
    {
        why = "no floor";
    }
    else if (ledge == cur)
    {
        why = "same tile";
    }
    else
    {
        const f32 base = (feet > ledgeY) ? feet : ledgeY;
        const f32 heads = base + bondviewGetPlayerDuckingHeightRelated(g_CurrentPlayer);
        const s32 r0 = getTileRoom(cur);
        const s32 r1 = getTileRoom(ledge);

        if (feet > ledgeY)
        {
            height += feet - ledgeY;
            always_30 += feet - ledgeY;
        }
        cdtypes = obj_collision_flag ? (CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER) : 0;
        fit = ledge;
        sub_GAME_7F03D058(g_CurrentPlayer->prop, 0);
        ok = stanTestVolume(&fit, target.f[0], target.f[2], radius, cdtypes, height, always_30) < 0;
        sub_GAME_7F03D058(g_CurrentPlayer->prop, 1);

        if (!ok)
        {
            why = "no room";
        }
        else if (ledgeBgHit(pos, &target, base + 10.0f, r0, 1)) { why = "wall"; ray = "low/from-room"; }
        else if (ledgeBgHit(pos, &target, heads, r0, 0))        { why = "wall"; ray = "head/from-room"; }
        else if ((r1 != r0) && ledgeBgHit(pos, &target, base + 10.0f, r1, 1)) { why = "wall"; ray = "low/to-room"; }
        else if ((r1 != r0) && ledgeBgHit(pos, &target, heads, r1, 0))        { why = "wall"; ray = "head/to-room"; }
    }

    if (dbg && ((why == NULL) || ((s32) g_GlobalTimer - lastLog >= 10)))
    {
        struct coord3d high = target;
        f32 topY = 0.0f;
        StandTile *top;

        lastLog = (s32) g_GlobalTimer;
        high.f[1] = 100000.0f;
        top = stanFindTileBelowPos(&high, NULL, &topY);
        H->log("ledge %s: feet=%.1f floor=%.1f ledge=%.1f top=%.1f at (%.1f %.1f) rooms %d->%d\n",
               why ? why : "CROSS", feet, curFloor, ledge ? ledgeY : 0.0f, top ? topY : -99999.0f,
               target.f[0], target.f[2], getTileRoom(cur), ledge ? getTileRoom(ledge) : -1);
        if (ray[0] != '\0')
        {
            const f32 ws = get_room_data_float2();

            H->log("ledge   ray %s hit=(%.1f %.1f %.1f) normal=(%.2f %.2f %.2f)\n", ray,
                   s_ledgeHit.hitpos.f[0] * ws, s_ledgeHit.hitpos.f[1] * ws, s_ledgeHit.hitpos.f[2] * ws,
                   s_ledgeHit.normal.f[0], s_ledgeHit.normal.f[1], s_ledgeHit.normal.f[2]);
        }
    }
    if (why != NULL)
    {
        return 0;
    }

    g_CurrentPlayer->field_488.current_tile_ptr = ledge;
    pos->f[0] = target.f[0];
    pos->f[2] = target.f[2];
    return 1;
}

/** Only asked while grounded, so a press just before landing still jumps
 * (the latch lives 0.25s); auto_bhop counts a held key as a press. */
static s32 jumpPressed(void)
{
    if (jumpHeight() <= 0.0f)
    {
        return 0;
    }
    return H->action_pressed(s_jumpAct) || (*s_autoBhop && H->action_held(s_jumpAct));
}

/** PLAYER_Y: end a rise into a floor above his feet, and launch a jump. */
static void jumpPlayerY(s32 use_stanHeight)
{
    if (jumpHeight() <= 0.0f)
    {
        return;
    }
    /* Rising into a floor above his feet (a ledge or crate top reached
     * mid-jump): the jump is over, and the game's step-up smoothing lifts him. */
    if ((g_CurrentPlayer->field_7C > 0.0f) && (g_CurrentPlayer->field_70 < g_CurrentPlayer->stanHeight))
    {
        g_CurrentPlayer->field_7C = 0.0f;
    }

    if ((use_stanHeight == 0) && (g_CurrentPlayer->field_2A6C == 0)
        && (g_CurrentPlayer->field_7C <= 0.0f)
        && (g_CurrentPlayer->field_70 - g_CurrentPlayer->stanHeight < 1.0f)
        && jumpPressed())
    {
        /* Same gravity as the game's fall; peak = v^2 / 2g. Lifting field_70
         * off the floor lets that fall code integrate it. */
        const f32 g = ((get_debug_fast_bond_flag() != 0) && (g_ForceBondMoveOffset.f[0] == 0.0f) && (g_ForceBondMoveOffset.f[2] == 0.0f))
            ? 1.388889f : 0.27777779f;

        g_CurrentPlayer->field_7C = sqrtf(2.0f * g * jumpHeight());
        if (g_CurrentPlayer->field_70 < g_CurrentPlayer->stanHeight)
        {
            g_CurrentPlayer->field_70 = g_CurrentPlayer->stanHeight;
        }
        g_CurrentPlayer->field_70 += 0.01f;
    }
}

/** RENDER_HUD: horizontal speed (units/s) in the top-left corner, under the
 * FPS counter when that is on, while Bond is under player control. */
static Gfx *speedometerHud(Gfx *gdl)
{
    static char sptext[16];
    s32 tw = 0;
    s32 th = 0;
    s32 sx;
    s32 sy;

    if (!*s_speedometer || !portBondControlsLive() || g_PlayerIsInTank)
    {
        return gdl;
    }
    sx = viGetViewLeft() + 3;
    sy = viGetViewTop() + 3;
    if (getPlayerCount() == 1 && portFullHeightActive())
    {
        sy = 3;
    }
    sprintf(sptext, "%d", (int) (s_hudSpeed[get_cur_playernum() & 3] + 0.5f));
    if (portVideoShowFps() && (getPlayerCount() == 1 || get_cur_playernum() == 0))
    {
        textMeasure(&th, &tw, sptext, ptrFontBankGothicChars, ptrFontBankGothic, 0);
        sy += th + 2;
    }
    gdl = microcode_constructor(gdl);
    PORT_HUD_ANCHOR_FULL(gdl, PORT_HUD_ANCHOR_LEFT);
    gdl = textRenderOutlined(gdl, &sx, &sy, sptext, ptrFontBankGothicChars, ptrFontBankGothic, -1, 0x646464FFU, viGetX(), viGetY(), 0, 0);
    PORT_HUD_ANCHOR_OFF(gdl);
    gdl = combiner_bayer_lod_perspective(gdl);
    return gdl;
}

MIDAS_EXPORT s32 midas_mod_init(const MidasHost *host)
{
    H = host;

    s_jumping     = H->option_bool("jumping", "Jumping", 1);
    s_jumpHeight  = H->option_int("jump_height", "Jump Height", 100, 50, 200, 10);
    s_jumpLedges  = H->option_bool("jump_ledges", "Ledge Jumping", 1);
    s_bhop        = H->option_bool("bhop", "Bunnyhopping", 1);
    s_autoBhop    = H->option_bool("auto_bhop", "Auto Bunnyhop", 1);
    s_speedometer = H->option_bool("speedometer", "Speedometer", 1);
    s_jumpAct     = H->action("jump", "Jump", "Space");
    if (!s_jumping || !s_jumpHeight || !s_jumpLedges || !s_bhop || !s_autoBhop || !s_speedometer || s_jumpAct < 0)
    {
        return 0;
    }

    H->hook(MIDAS_HOOK_PLAYER_MOVE, (void *) bhopMove, 0);
    H->hook(MIDAS_HOOK_PLAYER_MOVED, (void *) bhopMoved, 0);
    H->hook(MIDAS_HOOK_PLAYER_STEP_WINDOW, (void *) jumpStepWindow, 0);
    H->hook(MIDAS_HOOK_PLAYER_BLOCKED, (void *) jumpLedgeMove, 0);
    H->hook(MIDAS_HOOK_PLAYER_FLOOR, (void *) jumpObjectFloor, 0);
    H->hook(MIDAS_HOOK_PLAYER_Y, (void *) jumpPlayerY, 0);
    H->hook(MIDAS_HOOK_RENDER_HUD, (void *) speedometerHud, 0);
    return 1;
}
