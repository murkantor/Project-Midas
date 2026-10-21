/**
 * funguns - the Portal Gun, as a mod.
 *
 * It takes a free item slot (host->claim_item: ITEM_SILVERWPPK, which no level
 * places), so weapon cycling, "all guns" and the setup scripts keep working,
 * re-skinned with the Moonraker laser model. Fire (Z) shoots the blue portal,
 * the aim button (R / right click) the orange one (WEAPON_AIM, instead of aim
 * mode). Walking (or falling, or flying) into one puts Bond out of the other,
 * facing out of it, with his momentum turned through: the bhop mod's air
 * velocity horizontally when it is loaded, field_7C up/down.
 *
 * [input] portal_gun=1 (default) gives it at solo mission start; console
 * `give portalgun` adds and equips it any time. GE_FUNGUNDBG=1 logs
 * placement and teleports.
 */
#include <ultra64.h>
#include <math.h>
#include <bondgame.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "bg.h"
#include "bondinv.h"
#include "bondview.h"
#include "chr.h"
#include "chrai.h"
#include "chraction.h"
#include "gun.h"
#include "loadobjectmodel.h"
#include "lv.h"
#include "math_atan2f.h"
#include "matrixmath.h"
#include "player.h"
#include "propobj.h"
#include "stan.h"
#include "midas_mod.h"
#include "decals.h"

extern float sqrtf(float);
extern float sinf(float);
extern float cosf(float);
extern float fabsf(float);
extern char *getenv(const char *);

extern f32 g_GlobalTimerDelta;
extern GunModelFileRecord gitem_structs[];
extern ModelFileHeader laser_header;
extern WeaponStats laser_stats;

extern s32 getPlayerCount(void);
extern void bullet_path_from_screen_center(coord3d *origin, coord3d *dir, enum GUNHAND hand);
extern s32 bgCopyDrawnRoomsToList(s32 *rooms, s32 max);
extern s32 bgTestBulletHitBackground(coord3d *from, coord3d *to, s32 room, HitThing *hit);
extern f32 get_room_data_float2(void);
extern void change_player_pos_to_target(struct collision434 *col, coord3d *pos, StandTile *stan);
extern void bondviewApplyVertaTheta(void);
extern void bondviewUpdatePlayerRoom(struct player *player);
extern f32 bondviewYPositionRelated(StandTile *tile, f32 x, f32 z);
extern StandTile *stanFindTileBelowPos(coord3d *pos, u8 *rooms, f32 *yRtn);
extern Mtxf *currentPlayerGetViewToWorldMtxf(void);
extern f32 atan2f(f32 y, f32 x);

MIDAS_MOD_ABI_EXPORT

#define PORTAL_HALF_W     35.0f
#define PORTAL_HALF_H     60.0f
#define PORTAL_COOLDOWN   20.0f    /* ticks after a trip before either portal works */
#define PORTAL_MIN_SPEED  3.0f     /* exit speed floor along the exit normal, units/tick */

static const MidasHost *H;
static s32 s_item = -1;            /* the Portal Gun's ITEM_* */
static const s32 *s_portalGunOpt;  /* [input] portal_gun */
static u16 s_strName;
static u16 s_strBlank;

/* the bhop mod's air velocity, when it is loaded */
static s32 (*s_bhopGet)(f32 *vx, f32 *vz);
static void (*s_bhopSet)(f32 vx, f32 vz);

typedef struct
{
    s32 active;
    coord3d pos;      /* world, on the surface */
    coord3d normal;   /* unit, out of the surface */
    s32 room;
} Portal;

static Portal s_portal[2];
static f32 s_portalCooldown;

static WeaponStats s_portalStats;
static s32 s_patched;

static s32 funDbg(void)
{
    static s32 dbg = -1;

    if (dbg < 0)
    {
        dbg = getenv("GE_FUNGUNDBG") != NULL;
    }
    return dbg;
}

/* The item row, re-pointed once: the asset tables stay N64-exact. */
static void funPatchItems(void)
{
    GunModelFileRecord *portal = &gitem_structs[s_item];

    if (s_patched)
    {
        return;
    }
    s_patched = 1;

    s_portalStats = laser_stats;
    s_portalStats.Inaccuracy = 0.0f;
    s_portalStats.BitFlags &= ~WEAPONSTATBITFLAG_HAS_AUTO_AIM;
    s_portalStats.BitFlags |= WEAPONSTATBITFLAG_HIDE_AMMO_DISPLAY;

    portal->item_header = &laser_header;
    portal->item_file_name = "GlaserZ";
    portal->item_weapon_stats = &s_portalStats;
    portal->watch_pos_x = gitem_structs[ITEM_LASER].watch_pos_x;
    portal->watch_pos_y = gitem_structs[ITEM_LASER].watch_pos_y;
    portal->watch_pos_z = gitem_structs[ITEM_LASER].watch_pos_z;
    portal->equip_watch_x = gitem_structs[ITEM_LASER].equip_watch_x;
    portal->equip_watch_y = gitem_structs[ITEM_LASER].equip_watch_y;
    portal->equip_watch_z = gitem_structs[ITEM_LASER].equip_watch_z;
    portal->upper_watch_text = s_strBlank;
    portal->lower_watch_text = s_strName;
    portal->weapon_of_choice_text = s_strName;
    portal->watch_equipment_text = s_strName;
    H->item_model_dir(s_item, "portalgun");
}

/** LEVEL_START, after the fist is added. */
static void funLevelStart(void)
{
    s32 i;

    funPatchItems();
    for (i = 0; i < 2; i++)
    {
        s_portal[i].active = 0;
        funDecalClear(i);
    }
    s_portalCooldown = 0.0f;
    if (*s_portalGunOpt && (getPlayerCount() == 1))
    {
        bondinvAddInvItem(s_item);
    }
}

/** Console `give portalgun`: adds and equips. */
static s32 funGive(const char *what)
{
    if ((what[0] != 'p') && (what[0] != 'P'))
    {
        return 0;
    }
    funPatchItems();
    bondinvAddInvItem(s_item);
    currentPlayerEquipWeaponWrapper(GUNRIGHT, s_item);
    H->log("gave portalgun\n");
    return 1;
}

static s32 funOn(void)
{
    return s_patched && (getPlayerCount() == 1);
}

/** 1 while the Portal Gun is in the right hand. */
static s32 funHeld(void)
{
    return funOn() && (getCurrentPlayerWeaponId(GUNRIGHT) == s_item);
}

/* ---- aiming -------------------------------------------------------------- */

/* The crosshair ray in world space (the bullet's; the gun has 0 spread). */
static void funAimRay(coord3d *eye, coord3d *dir)
{
    coord3d o;
    coord3d d;

    bullet_path_from_screen_center(&o, &d, GUNRIGHT);
    *eye = o;
    mtx4TransformVecInPlace(currentPlayerGetViewToWorldMtxf(), eye);
    *dir = d;
    mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), dir);
}

/**
 * Closest level-geometry hit along eye + dir * [0, maxdist] over the rooms
 * drawn this frame. bgTestBulletHitBackground reports the whole line in
 * scaled units, so hits are scaled back and kept to the forward segment.
 */
static s32 funTrace(coord3d *eye, coord3d *dir, f32 maxdist, coord3d *hitpos, coord3d *normal, s32 *hitroom)
{
    s32 rooms[MAXROOMCOUNT];
    s32 n = bgCopyDrawnRoomsToList(rooms, MAXROOMCOUNT);
    const f32 scale = get_room_data_float2();
    coord3d to;
    f32 best = maxdist;
    s32 found = 0;
    s32 i;

    to.x = eye->x + (dir->x * maxdist);
    to.y = eye->y + (dir->y * maxdist);
    to.z = eye->z + (dir->z * maxdist);

    for (i = 0; i < n; i++)
    {
        HitThing hit;
        coord3d p;
        f32 t;

        if ((rooms[i] <= 0) || !bgTestBulletHitBackground(eye, &to, rooms[i], &hit))
        {
            continue;
        }
        p.x = hit.hitpos.x * scale;
        p.y = hit.hitpos.y * scale;
        p.z = hit.hitpos.z * scale;
        t = ((p.x - eye->x) * dir->x) + ((p.y - eye->y) * dir->y) + ((p.z - eye->z) * dir->z);
        if ((t <= 0.0f) || (t >= best))
        {
            continue;
        }
        best = t;
        *hitpos = p;
        normal->x = hit.normal.x;
        normal->y = hit.normal.y;
        normal->z = hit.normal.z;
        *hitroom = rooms[i];
        found = 1;
    }
    if (found)
    {
        f32 len = sqrtf((normal->x * normal->x) + (normal->y * normal->y) + (normal->z * normal->z));

        if (len <= 0.0f)
        {
            return 0;
        }
        normal->x /= len;
        normal->y /= len;
        normal->z /= len;
        /* face the shooter */
        if (((normal->x * dir->x) + (normal->y * dir->y) + (normal->z * dir->z)) > 0.0f)
        {
            normal->x = -normal->x;
            normal->y = -normal->y;
            normal->z = -normal->z;
        }
    }
    return found;
}

/* ---- Portal Gun ---------------------------------------------------------- */

static void portalPlace(s32 which)
{
    coord3d eye, dir, pos, normal;
    s32 room;
    Portal *p = &s_portal[which];
    const Portal *other = &s_portal[!which];

    funAimRay(&eye, &dir);
    if (!funTrace(&eye, &dir, 20000.0f, &pos, &normal, &room))
    {
        return;
    }
    if (other->active)
    {
        const f32 dx = pos.x - other->pos.x;
        const f32 dy = pos.y - other->pos.y;
        const f32 dz = pos.z - other->pos.z;

        if (((dx * dx) + (dy * dy) + (dz * dz)) < (2.0f * PORTAL_HALF_W) * (2.0f * PORTAL_HALF_W))
        {
            return;   /* on top of the other one */
        }
    }
    p->active = 1;
    p->pos = pos;
    p->normal = normal;
    p->room = room;
    /* lifted off the surface a touch so it never z-fights the wall */
    {
        coord3d draw;

        draw.x = pos.x + (normal.x * 0.5f);
        draw.y = pos.y + (normal.y * 0.5f);
        draw.z = pos.z + (normal.z * 0.5f);
        if (which == 0)
        {
            funDecalSet(0, &draw, &normal, room, PORTAL_HALF_W, PORTAL_HALF_H, 0x20, 0x80, 0xFF);
        }
        else
        {
            funDecalSet(1, &draw, &normal, room, PORTAL_HALF_W, PORTAL_HALF_H, 0xFF, 0x80, 0x10);
        }
    }
    if (funDbg())
    {
        H->log("portal %s at (%.0f %.0f %.0f) n=(%.2f %.2f %.2f) room %d\n", which ? "orange" : "blue",
               pos.x, pos.y, pos.z, normal.x, normal.y, normal.z, room);
    }
}

static s32 portalIsWall(const Portal *p)
{
    return fabsf(p->normal.y) < 0.7f;
}

/* Bond is in portal `p`'s mouth this tick. */
static s32 portalTouching(const Portal *p, f32 radius)
{
    const coord3d *c = &g_CurrentPlayer->field_488.collision_position;
    const f32 feet = g_CurrentPlayer->field_70;
    const f32 dx = c->x - p->pos.x;
    const f32 dz = c->z - p->pos.z;

    if (portalIsWall(p))
    {
        const f32 h = sqrtf((p->normal.x * p->normal.x) + (p->normal.z * p->normal.z));
        const f32 nx = p->normal.x / h;
        const f32 nz = p->normal.z / h;
        const f32 depth = (dx * nx) + (dz * nz);
        const f32 side = (dx * -nz) + (dz * nx);

        /* his body (feet up to chest) overlaps the portal's height */
        return (depth < radius + 8.0f) && (depth > -radius) && (fabsf(side) < PORTAL_HALF_W - 5.0f)
            && (feet < p->pos.y + PORTAL_HALF_H) && ((feet + (g_CurrentPlayer->eyeheight * 0.7f)) > p->pos.y - PORTAL_HALF_H);
    }
    if (((dx * dx) + (dz * dz)) > (PORTAL_HALF_W * PORTAL_HALF_W))
    {
        return 0;
    }
    if (p->normal.y > 0.0f)
    {
        return (feet < p->pos.y + 4.0f) && (feet > p->pos.y - 60.0f);
    }
    return (feet + g_CurrentPlayer->eyeheight + 15.0f) > p->pos.y;
}

/* Bond out of `b`, having gone into `a`. */
static void portalTeleport(const Portal *a, const Portal *b)
{
    struct collision434 *col = &g_CurrentPlayer->field_488;
    const f32 radius = col->collision_radius;
    const f32 eyeh = g_CurrentPlayer->eyeheight;
    coord3d vin;
    coord3d vout;
    coord3d exitpos;
    coord3d probe;
    StandTile *tile;
    f32 floory;
    f32 feet;
    f32 speed;
    f32 yawdelta = 0.0f;

    /* incoming velocity: the bhop air velocity, else what he just moved */
    if ((s_bhopGet == NULL) || !s_bhopGet(&vin.x, &vin.z))
    {
        const f32 d = (g_GlobalTimerDelta > 0.0f) ? g_GlobalTimerDelta : 1.0f;

        vin.x = (col->collision_position.x - g_CurrentPlayer->bondprevpos.x) / d;
        vin.z = (col->collision_position.z - g_CurrentPlayer->bondprevpos.z) / d;
    }
    vin.y = g_CurrentPlayer->field_7C;
    speed = sqrtf((vin.x * vin.x) + (vin.y * vin.y) + (vin.z * vin.z));

    if (portalIsWall(a) && portalIsWall(b))
    {   /* turn everything (view and velocity) by the yaw between the portals */
        const f32 ain = atan2f(-a->normal.x, -a->normal.z);
        const f32 bout = atan2f(b->normal.x, b->normal.z);
        const f32 r = bout - ain;
        const f32 cr = cosf(r);
        const f32 sr = sinf(r);

        vout.x = (vin.x * cr) + (vin.z * sr);
        vout.z = (vin.z * cr) - (vin.x * sr);
        vout.y = vin.y;
        yawdelta = -r * (180.0f / M_PI_F);
    }
    else
    {   /* floor / ceiling: the speed goes straight out of the exit */
        const f32 out = (speed > PORTAL_MIN_SPEED) ? speed : PORTAL_MIN_SPEED;

        vout.x = b->normal.x * out;
        vout.y = b->normal.y * out;
        vout.z = b->normal.z * out;
        if (portalIsWall(b))
        {   /* face out of a wall exit */
            const f32 theta = atan2f(-b->normal.x, b->normal.z);

            yawdelta = (theta * (180.0f / M_PI_F)) - g_CurrentPlayer->vv_theta;
        }
    }

    if (portalIsWall(b))
    {
        const f32 h = sqrtf((b->normal.x * b->normal.x) + (b->normal.z * b->normal.z));

        exitpos.x = b->pos.x + ((b->normal.x / h) * (radius + 12.0f));
        exitpos.z = b->pos.z + ((b->normal.z / h) * (radius + 12.0f));
        feet = b->pos.y - PORTAL_HALF_H + 10.0f;
    }
    else if (b->normal.y > 0.0f)
    {
        exitpos.x = b->pos.x;
        exitpos.z = b->pos.z;
        feet = b->pos.y + 2.0f;
    }
    else
    {
        exitpos.x = b->pos.x;
        exitpos.z = b->pos.z;
        feet = b->pos.y - eyeh - 25.0f;
    }

    probe.x = exitpos.x;
    probe.y = (portalIsWall(b) ? b->pos.y : feet) + 10.0f;
    probe.z = exitpos.z;
    tile = stanFindTileBelowPos(&probe, NULL, &floory);
    if (tile == NULL)
    {
        if (funDbg())
        {
            H->log("portal exit has no floor under it\n");
        }
        return;
    }
    if (feet < floory)
    {
        feet = floory;
    }

    exitpos.y = feet + eyeh;
    change_player_pos_to_target(col, &exitpos, tile);
    col->collision_radius = radius;   /* that reset it to 30 */
    g_CurrentPlayer->field_70 = feet;
    g_CurrentPlayer->stanHeight = bondviewYPositionRelated(tile, exitpos.x, exitpos.z);
    g_CurrentPlayer->field_6C = feet / 0.17000002f;
    g_CurrentPlayer->field_7C = vout.y;
    g_CurrentPlayer->field_2A6C = 0;
    g_CurrentPlayer->field_2A70 = NULL;

    g_CurrentPlayer->vv_theta += yawdelta;
    while (g_CurrentPlayer->vv_theta < 0.0f)
    {
        g_CurrentPlayer->vv_theta += 360.0f;
    }
    while (g_CurrentPlayer->vv_theta >= 360.0f)
    {
        g_CurrentPlayer->vv_theta -= 360.0f;
    }
    bondviewApplyVertaTheta();

    g_CurrentPlayer->prop->pos = exitpos;
    g_CurrentPlayer->bondprevpos = exitpos;
    g_CurrentPlayer->prop->stan = tile;
    g_CurrentPlayer->field_3B8.x = col->pos.x / 0.100000024f;
    g_CurrentPlayer->field_3B8.y = col->pos.y / 0.100000024f;
    g_CurrentPlayer->field_3B8.z = col->pos.z / 0.100000024f;
    bondviewUpdatePlayerRoom(g_CurrentPlayer);

    if (s_bhopSet != NULL)
    {
        s_bhopSet(vout.x, vout.z);
    }
    s_portalCooldown = PORTAL_COOLDOWN;

    if (funDbg())
    {
        H->log("portal trip -> (%.0f %.0f %.0f) v=(%.2f %.2f %.2f) yaw%+.0f\n",
               exitpos.x, feet, exitpos.z, vout.x, vout.y, vout.z, yawdelta);
    }
}

static void portalTick(void)
{
    const f32 radius = g_CurrentPlayer->field_488.collision_radius;
    s32 i;

    if (s_portalCooldown > 0.0f)
    {
        s_portalCooldown -= g_GlobalTimerDelta;
        return;
    }
    if (!s_portal[0].active || !s_portal[1].active)
    {
        return;
    }
    for (i = 0; i < 2; i++)
    {
        if (portalTouching(&s_portal[i], radius))
        {
            portalTeleport(&s_portal[i], &s_portal[!i]);
            return;
        }
    }
}

/* ---- hooks --------------------------------------------------------------- */

/** WEAPON_FIRE: the blue portal. */
static s32 funFire(s32 hand, s32 item)
{
    if (!funOn() || (hand != GUNRIGHT) || (item != s_item))
    {
        return 0;
    }
    portalPlace(0);
    return 1;
}

/** WEAPON_AIM: the aim button is the orange portal while the gun is out. */
static s32 funAim(s32 pressed)
{
    if (!funHeld())
    {
        return 0;
    }
    if (pressed)
    {
        portalPlace(1);
    }
    return 1;
}

/** PLAYER_MOVED, after the bhop mod's (priority 10): portal trips. */
static void funMoved(f32 dx, f32 dz)
{
    (void) dx;
    (void) dz;
    if (funOn())
    {
        portalTick();
    }
}

MIDAS_EXPORT s32 midas_mod_init(const MidasHost *host)
{
    H = host;

    s_item = H->claim_item("portalgun");
    s_portalGunOpt = H->option_bool("portal_gun", "Portal Gun", 1);
    if (s_item < 0 || s_portalGunOpt == NULL)
    {
        return 0;
    }
    s_strName = H->string("Portal Gun\n");
    s_strBlank = H->string("\n");
    s_bhopGet = (s32 (*)(f32 *, f32 *)) H->symbol("bhop", "bhop_get_velocity");
    s_bhopSet = (void (*)(f32, f32)) H->symbol("bhop", "bhop_set_velocity");
    if (s_bhopGet == NULL)
    {
        H->log("bhop mod not loaded: portals carry walk speed only\n");
    }

    H->hook(MIDAS_HOOK_LEVEL_START, (void *) funLevelStart, 0);
    H->hook(MIDAS_HOOK_WEAPON_FIRE, (void *) funFire, 0);
    H->hook(MIDAS_HOOK_WEAPON_AIM, (void *) funAim, 0);
    H->hook(MIDAS_HOOK_PLAYER_MOVED, (void *) funMoved, 10);
    H->hook(MIDAS_HOOK_RENDER_WORLD, (void *) funDecalsRender, 0);
    H->command("give", funGive, "give portalgun");
    return 1;
}
