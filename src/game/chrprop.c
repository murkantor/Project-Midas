#include <ultra64.h>
#include "game/bondview.h"
#include "game/lightfixture.h"
#include <assert.h>
#include <bondgame.h>
#include <bondtypes.h>
#include <bondaicommands.h>
#include <boss.h>
#include <limits.h>
#include <music.h>
#include <random.h>
#include <snd.h>
#include "bg.h"
#include "bgfog.h"
#include "bondview.h"
#include "cheat.h"
#include "chr.h"
#include "chrai.h"
#include "chraidata.h"
#include "chraction.h"
#include "explosion.h"
#include "file.h"
#include "glass.h"
#include "gun.h"
#include "initanitable.h"
#include "language.h"
#include "loadobjectmodel.h"
#include "lv.h"
#include "math.h"
#include "math_atan2f.h"
#include "math_ceil.h"
#include "math_floor.h"
#include "model.h"
#include "mp_music.h"
#include "player.h"
#include "propobj.h"
#include "objecthandler.h"
#include "objective_status.h"
#include "stan.h"
#include "tex.h"


// bss


//CODE.bss:80069C30
s16 * ptr_list_object_lookup_indices;

//CODE.bss:80069C34 canonically roompropsnum
u32 num_obj_position_data_entries;

/**
 * Address 0x80069C38.
 * 
 * This 600 length prop pool provides storage for every PropRecord in the game.
 * Props are never allocated anywhere else. init_load_objpos_table threads
 * all 600 slots onto the free list, and chrpropAllocate/chrpropFree pop and push to that list.
 * 
 * This is g_Vars.props in PD.
*/
PropRecord g_Props[MAX_PROPS];

//CODE.bss:80071618
s16 *RoomPropListBlockIndices;

//CODE.bss:8007161C
struct roomproplistblock *RoomPropListBlocks;

/**
 * Array of pointers, containing onscreen props.
 *
 * Address 0x80071620.
*/
PropRecord *g_OnScreenPropList[ONSCREEN_PROP_LIST_LEN];

/**
 * Pointer to last onscreen prop.
 * Address 0x80071DF0.
*/
PropRecord **g_LastOnScreenProp;

/**
 * Count of onscreen props.
 * Address 0x80071DF4.
 * canonically propznum
*/
s32 g_OnScreenPropCount;

//CODE.bss:80071DF8
PropRecord *g_InteractProp;
//CODE.bss:80071DFC
u32 dword_CODE_bss_80071DFC;
//CODE.bss:80071E00
WeaponObjRecord* proxy_mine_table[30];

//CODE.bss:80071E78
f32 gasTimeToFullOpacity;
//CODE.bss:80071E7C
u32 gasDoesDamageFlag;

/**
 * Address 0x80071E80.
*/
WeaponObjRecord g_WeaponSlots[MAX_WEAPON_SLOTS];

/**
 * Address 0x80072E70.
*/
HatRecord g_HatSlots[MAX_HAT_SLOTS];

/**
 * Address 0x80073370.
*/
AmmoCrateRecord g_AmmoCrates[MAX_AMMO_CRATES];

/**
 * Address 0x80073DC0.
*/
Projectile g_Projectiles[PROJECTILES_ARR_MAX];

/**
 * Address 0x80075030.
*/
Embedment g_Embedments[EMBEDMENT_ARR_MAX];

//CODE.bss:80075B70
struct Model *g_CurrentProjectileModel;
//CODE.bss:80075B74
struct ModelNode * dword_CODE_bss_80075B74;
//CODE.bss:80075B78
coord3d flt_CODE_bss_80075B78;
//CODE.bss:80075B84
f32 flt_CODE_bss_80075B84;
//CODE.bss:80075B88
coord3d flt_CODE_bss_80075B88;
//CODE.bss:80075B94
f32 flt_CODE_bss_80075B94;

/**
 * Address 0x80075B98.
*/
MonitorRecord g_MonitorAnimController;

/**
 * Unused / unreferenced (from padding / align?)
 * Address 0x80075C0C.
*/
s32 bss_80075C0C;

/**
 * Address 0x80075C10.
*/
struct object_animation_controller g_UnknownAnimController;

/**
 * Unused / unreferenced (from padding / align?)
 * Address 0x80075C84.
*/
s32 bss_80075C84;

/**
 * Unused / unreferenced (from padding / align?)
 * Address 0x80075C88.
*/
struct object_animation_controller g_TaserAnimController;

/**
 * Address 0x80075CFC.
*/
s32 bss_80075CFC;



//CODE.bss:80075D00 - 80075D24
stagesetup g_CurrentSetup; //Public Working Setup

//CODE.bss:80075D28
stagesetup                        *g_ptrStageSetupFile;

PropRecord *g_ActivePropsTail = 0;
PropRecord *g_ActivePropsHead = 0;
PropRecord *g_FreeProps = 0;

f32 difficulty = 1.0;
struct coord2d g_DefaultAutoAimCoord = { 0 };

// forward declarations

Gfx *chrpropRender(Gfx *arg0, PropRecord *arg1, s32 withalpha);
void chraiCheckUseHeldItem(s32 hand);
void chraiDefaultWeaponFireHandler(s32);
void chraiFistAttackHandler(s32 hand, s32 item_id);
void modelGetAxisExtents(Model* model, f32* max, f32* min, s32 axis);

// end forward declarations


/**
 * Counts onscreen props.
 *
 * Address 0x7F03A240.
*/
void chraiUpdateOnscreenPropCount(void)
{
    s32 i;
    s32 j;
    s32 count;
    PropRecord *prop;
    s32 phi_a0;
    f32 phi_f12;

    i = 0;
    count = 0;
    prop = chrpropGetActiveTail();

    for (; prop != NULL; prop = prop->prev)
    {
        if ((prop->flags & (PROPFLAG_ENABLED | PROPFLAG_ONSCREEN)) == (PROPFLAG_ENABLED | PROPFLAG_ONSCREEN))
        {
            g_OnScreenPropList[count] = prop;
            count++;
        }
    }

    g_OnScreenPropCount = count;
    g_OnScreenPropList[count] = NULL;

    if(1)
    {
        // removed
        #ifdef DEBUG
        assert(propznum<MAXPROPSVISIBLE); //800 props
        #endif
    }

    g_LastOnScreenProp = (PropRecord *)&g_OnScreenPropList[count];

    for (i=0; i<count; i++)
    {
        phi_a0 = -1;
        phi_f12 = -4.2949673e9f;

        for (j = i; j < count; j++)
        {
            f32 f = g_OnScreenPropList[j]->zDepth;

            if (phi_f12 < f)
            {
                phi_f12 = f;
                phi_a0 = j;
            }
        }

        if (phi_a0 >= 0)
        {
            prop = g_OnScreenPropList[i];
            g_OnScreenPropList[i] = g_OnScreenPropList[phi_a0];
            g_OnScreenPropList[phi_a0] = prop;
        }
    }
}


void chrpropEnable(PropRecord *prop)
{
    prop->flags |= PROPFLAG_ENABLED;
}



void chrpropDisable(PropRecord *prop)
{
    prop->flags &= ~PROPFLAG_ENABLED;
}


PropRecord *chrpropGetActiveTail(void)
{
    return g_ActivePropsTail;
}


PropRecord* chrpropAllocate(void)
{
    PropRecord* prop;

    if (g_FreeProps)
    {
        prop = g_FreeProps;
        g_FreeProps = prop->prev;

        prop->prev = NULL;
        prop->next = NULL;
        prop->parent = NULL;
        prop->child = NULL;

        prop->flags = 0;
        prop->stan = NULL;
        prop->timetoregen = 0;
        prop->rooms[0] = 0xFF;
        return prop;
    }

    return NULL;
}


void chrpropFree(PropRecord *prop)
{
    prop->prev = g_FreeProps;
    prop->next = 0x0;
    prop->stan = 0x0;
    g_FreeProps = prop;
}


void chrpropActivate(PropRecord* prop)
{
    PropRecord* cur;

    cur = g_ActivePropsTail;
    if (cur != NULL)
    {
        cur->next = prop;

        prop->prev = g_ActivePropsTail;
        prop->next = NULL;

        g_ActivePropsTail = prop;
        return;
    }

    prop->prev = NULL;
    prop->next = NULL;
    g_ActivePropsTail = g_ActivePropsHead = prop;
}


void chrpropActivateThisFrame(PropRecord* prop) {
    PropRecord* first;

    first = g_ActivePropsHead;
    if (first != NULL) {
        first->prev = prop;
        prop->next = g_ActivePropsHead;
        prop->prev = NULL;
        g_ActivePropsHead = prop;
        return;
    }
    prop->prev = NULL;
    prop->next = NULL;
    g_ActivePropsHead = prop;
    g_ActivePropsTail = prop;
}


void chrpropDelist(PropRecord *prop)
{
    PropRecord *temp_v0;
    PropRecord *temp_v0_2;

    if (prop == g_ActivePropsTail)
    {
        g_ActivePropsTail = prop->prev;
    }
    if (prop == g_ActivePropsHead)
    {
        g_ActivePropsHead = prop->next;
    }
    temp_v0 = prop->prev;
    if (temp_v0 != 0)
    {
        temp_v0->next = prop->next;
    }
    temp_v0_2 = prop->next;
    if (temp_v0_2 != 0)
    {
        temp_v0_2->prev = prop->prev;
    }
    prop->prev = NULL;
    prop->next = NULL;
}


void chrpropReparent(PropRecord *newChild, PropRecord *host)
{
    newChild->parent = host;

    // Link the newChild into its siblings
    if (host->child)
    {
        host->child->next = newChild;
    }
    newChild->prev = host->child;
    newChild->next = NULL;
    newChild->stan = NULL;
    host->child    = newChild;

}


void chrpropDetach(PropRecord* prop) {
    PropRecord* parent;
    PropRecord* prev;
    PropRecord* next;

    parent = prop->parent;
    if (parent) {
        if (prop == parent->child) {
            parent->child = prop->prev;
        }
        prev = prop->prev;
        if (prev) {
            prev->next = prop->next;
        }
        next = prop->next;
        if (next) {
            next->prev = prop->prev;
        }
        prop->parent = NULL;
        prop->prev = NULL;
        prop->next = NULL;
    }
}


#ifndef TARGET_N64
/* PORT: a model's translucent pass is authored for submission order (parts
 * back-to-front), but the renderer's deferred transparency pass re-sorts per
 * triangle and drew e.g. the Depot chair stem over its seat. So bracket every
 * model's alpha pass as one sort group (G_DXLU_GROUP_EXT, gfx_pc.cpp): order
 * inside is kept and the model sorts as a unit at its mean depth. */
#define PORT_DXLU_GROUP(gdl, mode) do { (gdl)->words.w0 = (u32) 0x49 << 24 /* G_DXLU_GROUP_EXT */; (gdl)->words.w1 = (mode); (gdl)++; } while (0)
#else
#define PORT_DXLU_GROUP(gdl, mode) do { } while (0)
#endif

/**
 * Address: 7F03A62C
*/
Gfx *chrpropRender(Gfx * gdl, PropRecord *prop, s32 withalpha)
{
    u8 type;

    type = prop->type;

    if (type == PROP_TYPE_CHR)
    {
        if (withalpha) PORT_DXLU_GROUP(gdl, 1);
        gdl = chrRenderProp(prop, gdl, withalpha);
        if (withalpha) PORT_DXLU_GROUP(gdl, 0);
    }
    else if ((type == PROP_TYPE_OBJ) || (type == PROP_TYPE_WEAPON) || (type == PROP_TYPE_DOOR))
    {
        if (withalpha) PORT_DXLU_GROUP(gdl, 1);
        gdl = chrobjRenderProp(prop, gdl, withalpha);
        if (withalpha) PORT_DXLU_GROUP(gdl, 0);
    }
    else if (type == PROP_TYPE_EXPLOSION)
    {
        gdl = explosionRenderPropExplosion(prop, gdl, withalpha);
    }
    else if (type == PROP_TYPE_SMOKE)
    {
        gdl = explosionRenderPropSmoke(prop, gdl, withalpha);
    }
    else if (type == PROP_TYPE_VIEWER)
    {
        if (withalpha) PORT_DXLU_GROUP(gdl, 1);
        gdl = bondviewRenderProp(prop, gdl, withalpha);
        if (withalpha) PORT_DXLU_GROUP(gdl, 0);
    }

    return gdl;
}


/**
 * Address: 7F03A6F4
*/
Gfx *chrpropsRenderPass(Gfx *gdl, s32 roomid, s32 renderpass)
{
    s32 flag;
    PropRecord **pp;
    PropRecord *prop;
    s32 i;
    s32* rp;
    s32 unused2;
    s32 sp48[PROPRECORD_STAN_ROOM_LEN];
    s32 unused3;
    s32 unused4;

    if (bossGetStageNum() == LEVELID_CUBA)
    {
        if (renderpass == 0)
        {
            return gdl;
        }
        else if (renderpass == 2)
        {
            renderpass = 0;
        }
    }

    if ((renderpass == 0) || (renderpass == 2))
    {
        for (pp = g_LastOnScreenProp; --pp >= g_OnScreenPropList; )
        {
            prop = *pp;

            if (prop != NULL)
            {
                flag = 0;

                if ((renderpass == 0) && ((prop->flags & (PROPFLAG_00000020 | PROPFLAG_RENDERPOSTBG)) == 0))
                {
                    flag = 1;
                }
                else if ((renderpass == 2) && ((prop->flags & (PROPFLAG_00000020 | PROPFLAG_RENDERPOSTBG)) == PROPFLAG_RENDERPOSTBG))
                {
                    flag = 1;
                }

                if (flag != 0)
                {
                    flag = 0;
                    chraiGetPropRoomIds(prop, sp48);

                    for (rp = sp48; *rp >= 0; rp++)
                    {
                        if (getROOMID_isRendered(*rp))
                        {
                            if (roomid == *rp)
                            {
                                flag = 1;
                            }

                            break;
                        }
                    }

                    if (flag)
                    {
                        gdl = chrpropRender(gdl, prop, 0);
                    }
                }
            }
        }
    }
    else
    {
        for (pp = g_OnScreenPropList; pp < g_LastOnScreenProp; pp++)
        {
            prop = *pp;

            if (prop != NULL)
            {
                flag = 0;
                chraiGetPropRoomIds(prop, sp48);

                for (rp = sp48; *rp >= 0; rp++)
                {
                    if (getROOMID_isRendered(*rp))
                    {
                        if (roomid == *rp)
                        {
                            flag = 1;
                        }

                        break;
                    }
                }

                if (flag)
                {
                    if (prop->flags & PROPFLAG_00000020)
                    {
                        gdl = chrpropRender(gdl, prop, 0);
                    }

                    gdl = chrpropRender(gdl, prop, 1);
                }
            }
        }
    }

    return bgScissorCurrentPlayerViewDefault(gdl);
}


/**
 * Address: 7F03A97C
 *
 * Tests if a ray intersects the bounding box of the given room.
 * @return TRUE if the ray intersects, otherwise FALSE.
*/
s32 chrpropRayIntersectsRoomBbox(s32 room, coord3d* start, coord3d* dir)
{
    s32 max[3];
    s32 min[3];
    s_room_info* roominfo;

    roominfo = &g_BgRoomInfo[room];

    // Skip check if room has no collision data
    if (roominfo->vtx_batch_bounds != NULL)
    {
        min[0] = roominfo->minbounds.f[0];
        min[1] = roominfo->minbounds.f[1];
        min[2] = roominfo->minbounds.f[2];
        max[0] = roominfo->maxbounds.f[0];
        max[1] = roominfo->maxbounds.f[1];
        max[2] = roominfo->maxbounds.f[2];
        if (bgTestRayIntersectsBbox(start, dir, min, max)) {
            return TRUE;
        }
    }
    return FALSE;
}


/**
 * Address: 7F03AA44
 *
 * Unreferenced
 *
 * This takes a list of rooms and flags the ones that do *not* intersect a ray.
 */
void chrpropFlagRoomsFromRayTest(s32 arg0, coord3d *from, coord3d *to, u8 *rooms)
{
    coord3d start;
    coord3d dir;
    f32 scale;
    s32 i;

    scale = get_room_data_float1() * bgGetLevelVisibilityScale();

    dir.x = to->x - from->x;
    dir.y = to->y - from->y;
    dir.z = to->z - from->z;

    start.x = from->x * scale;
    start.y = from->y * scale;
    start.z = from->z * scale;

    for (i = 1; i < getMaxNumRooms(); i++) {
        if (!rooms[i] && chrpropRayIntersectsRoomBbox(i, &start, &dir) == 0) {
            rooms[i] = 1;
        }
    }
}


/**
 * Address: 7F03AB58
 *
 * Refines an existing background bullet hit by checking currently visible rooms
 * that have not already been tested.
 *
 * The function scans the visible room list, marks each tested room in visited,
 * performs a room bbox test first, then tests the room geometry. If no previous hit exists,
 * the first visible room hit is accepted.
 * Otherwise, a hit is accepted only if it lies between from and the current
 * best hit on all three axes, making it closer along the shot ray.
 *
 * @return Returns the room number of the accepted hit, or the bestroom if no
 * closer visible room hit is found.
 */
s32 chrpropFindCloserBgHitInVisibleRooms(coord3d *from, coord3d *to, coord3d *dir, coord3d *scaledDir, u8 *visited, struct HitThing *besthit, s32 bestroom)
{
    s32 rooms[100];
    s32 *roomptr;
    s32 *end;
    s32 numrooms;
    struct HitThing hit;
    f32 scale;
    s32 room;

    scale = get_room_data_float2();

    // Get up to 100 currently visible rooms.
    numrooms = bgCopyVisibleRoomsToList(&rooms[0], 100);

    if (numrooms > 0)
    {
        roomptr = rooms;
        // The bitwise AND is just a matching trick and effectively does nothing.
        end = roomptr + (numrooms & 0xFFFFFFFF);

        do
        {
            // Only check rooms that have not been visited.
            if (visited[*roomptr] == 0)
            {
                visited[*roomptr] = 1;

                if (chrpropRayIntersectsRoomBbox(*roomptr, scaledDir, dir))
                {
                    if (bgTestBulletHitBackground(from, to, *roomptr, &hit))
                    {
                        room = *roomptr;
                        hit.hitpos.x *= scale;
                        hit.hitpos.y *= scale;
                        hit.hitpos.z *= scale;

                        /**
                         * The (numrooms * 0) is weird but harmless and needed for matching.
                         */
                        if ((bestroom <= (numrooms * 0))
                                || (((((from->x <= besthit->hitpos.x)
                                            && (from->x <= hit.hitpos.x))
                                            && (hit.hitpos.x < besthit->hitpos.x))
                                        || (((besthit->hitpos.x <= from->x)
                                            && (hit.hitpos.x <= from->x))
                                            && (besthit->hitpos.x < hit.hitpos.x)))
                                    && ((((from->y <= besthit->hitpos.y)
                                            && (from->y <= hit.hitpos.y))
                                            && (hit.hitpos.y < besthit->hitpos.y))
                                        || (((besthit->hitpos.y <= from->y)
                                            && (hit.hitpos.y <= from->y))
                                            && (besthit->hitpos.y < hit.hitpos.y)))
                                    && ((((from->z <= besthit->hitpos.z)
                                            && (from->z <= hit.hitpos.z))
                                            && (hit.hitpos.z < besthit->hitpos.z))
                                        || (((besthit->hitpos.z <= from->z)
                                            && (hit.hitpos.z <= from->z))
                                            && (besthit->hitpos.z < hit.hitpos.z)))))
                        {
                            bestroom = room;
                            *besthit = hit;
                        }
                    }
                }
            }

            roomptr++;
        }
        while (roomptr < end);

        if (rooms);
    }

    return bestroom;
}


/**
 * Address: 7F03ADF4
 *
 * Beginning at startroom, walk connected rooms looking for a background
 * bullet hit.
 *
 * Rooms are skipped if already marked in visited, and newly processed rooms are
 * marked visited.
 * @return Return 0 if no hit is found, otherwise the room number of the first room whose bbox and
 * background geometry intersect the bullet ray.
 */
s32 chrpropFindFirstBgHitInConnectedRooms(s32 startroom, coord3d *from, coord3d *to, coord3d *dir, coord3d *scaledDir, u8 *visited, struct HitThing *hit)
{
    u8 rooms[256];
    s32 pad;
    s32 neighbours[100];
    s32 numneighbours;
    s32 i;
    s32 j;
    s32 count;
    s32 curindex;
    s32 room;

    rooms[0] = startroom;
    count = 1;

    for (curindex = 0; curindex < count; curindex++)
    {
        room = rooms[curindex];

        if (visited[room] == 0)
        {
            visited[room] = 1;

            if (chrpropRayIntersectsRoomBbox(room, scaledDir, dir))
            {
                if (bgTestBulletHitBackground(from, to, room, hit))
                {
                    return room;
                }
            }
        }

        numneighbours = bgGetConnectedRooms(room, neighbours, 100);

        for (i = 0; i < numneighbours; i++)
        {
            for (j = 0; j < count; j++)
            {
                if (rooms[j] == neighbours[i])
                {
                    break;
                }
            }

            if (j == count)
            {
                rooms[count] = neighbours[i];
                count++;
            }
        }
    }

    return 0;
}


/**
 * Address: 7F03AF5C
 *
 * Finds the closest bg bullet collision among rooms not already visited by the shot traversal.
 * It first does a cheap bounding box test, then a precise test for rooms whose bounding boxes are intersected.
 * This seems to be a brute force/fallback version of the function above, chrpropFindFirstBgHitInConnectedRooms.
 * @return 0 if no bg hit in any unvisited room, otherwise the room number containing the closest bg hit.
 */
s32 chrpropFindClosestBgHitRoom(s32 unused, coord3d *from, coord3d *to, coord3d *dir, coord3d *scaledDir, u8 *visited, struct HitThing *besthit)
{
    f32 dx;
    f32 dy;
    struct HitThing hit;
    f32 scale;
    f32 dist;
    f32 adjusteddist;
    f32 bestdist;
    s32 bestroom;
    f32 tmp;
    s32 room;

    bestdist = M_U32_MAX_VALUE_F;
    bestroom = 0;

    scale = get_room_data_float2();

    room = 1;

    if (getMaxNumRooms() >= 2)
    {
        do
        {
            if (visited[room] == 0)
            {
                visited[room] = 1;

                if (chrpropRayIntersectsRoomBbox(room, scaledDir, dir))
                {
                    if (bgTestBulletHitBackground(from, to, room, &hit))
                    {
                        dx = (hit.hitpos.x * scale) - from->x;
                        dy = ((hit.hitpos.y * scale) - from->y) * 1.0f;
                        dist = (hit.hitpos.z * scale) - from->z;
                        dist = (tmp = ((dx * dx) + (dy * dy)) + (dist * dist));
                        adjusteddist = tmp;

                        if (check_if_imageID_is_light(hit.texturenum))
                        {
                            adjusteddist = tmp - 4.0f;
                        }

                        if (adjusteddist < bestdist)
                        {
                            besthit->hitpos.x = hit.hitpos.x;
                            besthit->hitpos.y = hit.hitpos.y;
                            besthit->hitpos.z = hit.hitpos.z;

                            bestdist = adjusteddist;
                            bestroom = room;

                            besthit->normal.x = hit.normal.x;
                            besthit->normal.y = hit.normal.y;
                            besthit->normal.z = hit.normal.z;

                            besthit->vtx0 = hit.vtx0;
                            besthit->vtx1 = hit.vtx1;
                            besthit->vtx2 = hit.vtx2;

                            besthit->texturenum = hit.texturenum;
                            besthit->tricmd = hit.tricmd;
                            besthit->unk28 = hit.unk28;
                        }
                    }
                }
            }

            room++;
        }
        while (room < getMaxNumRooms());
    }

    return bestroom;
}

/*
* Address: 0x7F03B15C
*
* This function has a bunch of issues. It allows the player to shoot through walls
* and even creates sparks in the distance when they shoot at the sky. Some of these
* issues arise because it traces over the stan tiles to find the first room to test.
* This leads to the player being able to shoot through floors if they have a portal
* in front of them (because it's a room boundary which leads to another room) as
* the code tracing over stan tiles only cares about the furthest stan's room. Luckily,
* none of those bugs affect props, so prop hits are always the best candidate.
*/
void chraiDefaultWeaponFireHandler(s32 hand)
{
    f32 new_var;
    coord3d *playerpos;
    s32 hitbgstan;
    coord3d stanhit;
    StandTile *hittile;
    s32 createSpark;
    s32 gotbghit;
    coord3d visiblehitpos;
    s32 bestroom;
    s32 besttexture;
    HitThing bghit;
    f32 negz;
    coord3d besthitpos;
    s32 pad;
    StandTile *fromtile;
    coord3d dest;
    ShotData shotdata;
    coord3d *finalpos;
    s32 numhits;
    u8 visited[256];
    struct image_sound *impact_sounds;
    coord3d scaleddir;
    coord3d hitdir;
    f32 distscale;
    PropRecord *playerprop;
    PropRecord *prop;
    PropRecord **pp;
    s32 startroom;
    s32 k;
    s32 i;
    u8 rooms[2];

    hitbgstan = 0;
    hittile = 0;
    gotbghit = 0;
    bestroom = 0;
    playerprop = getCurrentPlayerProp();
    fromtile = playerprop->stan;
    numhits = 0;
    bullet_path_from_screen_center(&shotdata.viewOrigin, &shotdata.viewDir, hand);
    shotdata.weapon = getCurrentPlayerWeaponId(hand);
    shotdata.maxdist = M_U32_MAX_VALUE_F;

    for (k = 0; k < 10; k++)
    {
        shotdata.hits[k].prop = 0;
        shotdata.hits[k].hitpart = 0;
        shotdata.hits[k].node = 0;
    }

    shotdata.gunpos.x = shotdata.viewOrigin.x;
    shotdata.gunpos.y = shotdata.viewOrigin.y;
    shotdata.gunpos.z = shotdata.viewOrigin.z;

    mtx4TransformVecInPlace(currentPlayerGetViewToWorldMtxf(), &shotdata.gunpos);

    shotdata.dir.x = shotdata.viewDir.x;
    shotdata.dir.y = shotdata.viewDir.y;
    shotdata.dir.z = shotdata.viewDir.z;

    mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), &shotdata.dir);

    dest.x = (shotdata.dir.x * M_U16_MAX_VALUE_F) + shotdata.gunpos.x;
    dest.y = (shotdata.dir.y * M_U16_MAX_VALUE_F) + shotdata.gunpos.y;
    dest.z = (shotdata.dir.z * M_U16_MAX_VALUE_F) + shotdata.gunpos.z;

    if (walkTilesBetweenPoints_NoCallback(&fromtile, playerprop->pos.x, playerprop->pos.z, shotdata.gunpos.x, shotdata.gunpos.z))
    {
        distscale = get_room_data_float1() * bgGetLevelVisibilityScale();
        playerpos = bondviewGetCurrentPlayersPosition();

        new_var++;
        new_var--;

        if (new_var == new_var);

        stanResetHits();

        if (!walkTilesBetweenPoints_NoCallback(&fromtile, shotdata.gunpos.x, shotdata.gunpos.z, dest.x, dest.z))
        {
            chrlvStanLineDirIntersection(&shotdata.gunpos, &shotdata.dir, &stanhit);
            hitbgstan = 1;
        }
        else
        {
            stanhit.x = dest.x;
            stanhit.y = dest.y;
            stanhit.z = dest.z;
        }

        hitdir.x = stanhit.x - playerpos->x;
        hitdir.y = stanhit.y - playerpos->y;
        hitdir.z = stanhit.z - playerpos->z;
        scaleddir.x = playerpos->x * distscale;
        scaleddir.y = playerpos->y * distscale;
        scaleddir.z = playerpos->z * distscale;
        hittile = fromtile;
        startroom = getTileRoom(fromtile);

        for (i = 0; i < 256; i++)
        {
            visited[i] = 0;
        }

        if (bgTestBulletHitBackground(playerpos, &stanhit, startroom, &bghit))
        {
            bestroom = startroom;
        }

        visited[startroom] = 1;

#ifndef TARGET_N64
        { /* VANILLA BUG FIX: the stan-tile walk can land in a room beyond a
           * portal, so test every room drawn this frame and keep the closest
           * hit, letting no near wall be skipped. Off during demo playback
           * (it can shift the impact-sound RNG) and with fix_shootthrough=0. */
            extern s32 portFixShootThrough(void);
            extern s32 get_is_ramrom_flag(void);

            /* Complex: the fix misbehaves outside a few verified rooms, so it
             * applies only to shots fired from room 16, 10, 11, 5 or 26;
             * elsewhere vanilla behaviour is used. The room is the shooter's,
             * since startroom is where the tile walk ended and can be past a
             * portal; the else branch below uses the same expression. */
            const s32 shooterroom = getTileRoom(getCurrentPlayerProp()->stan);

            if (portFixShootThrough() && get_is_ramrom_flag() == 0 &&
                (lvlGetCurrentStageToLoad() != LEVELID_COMPLEX ||
                 shooterroom == 16 || shooterroom == 10 || shooterroom == 11 ||
                 shooterroom == 5 || shooterroom == 26))
            {
                s32 drawnrooms[MAXROOMCOUNT];
                s32 numdrawn;
                s32 ri;
                f32 wscale = get_room_data_float2();

                numdrawn = bgCopyDrawnRoomsToList(drawnrooms, MAXROOMCOUNT);

                for (ri = 0; ri < numdrawn; ri++)
                {
                    s32 rm = drawnrooms[ri];
                    HitThing altbghit;

                    if (rm <= 0 || rm >= 256 || visited[rm])
                    {
                        continue;
                    }
                    visited[rm] = 1;

                    if (bgTestBulletHitBackground(playerpos, &stanhit, rm, &altbghit))
                    {
                        f32 dx, dy, dz, altdist;

                        dx = (altbghit.hitpos.x * wscale) - playerpos->x;
                        dy = (altbghit.hitpos.y * wscale) - playerpos->y;
                        dz = (altbghit.hitpos.z * wscale) - playerpos->z;
                        altdist = (dx * dx) + (dy * dy) + (dz * dz);

                        if (bestroom <= 0)
                        {
                            bestroom = rm;
                            bghit = altbghit;
                        }
                        else
                        {
                            f32 curdx, curdy, curdz, curdist;

                            curdx = (bghit.hitpos.x * wscale) - playerpos->x;
                            curdy = (bghit.hitpos.y * wscale) - playerpos->y;
                            curdz = (bghit.hitpos.z * wscale) - playerpos->z;
                            curdist = (curdx * curdx) + (curdy * curdy) + (curdz * curdz);

                            if (altdist < curdist)
                            {
                                bestroom = rm;
                                bghit = altbghit;
                            }
                        }
                    }
                }
            }
            else if (bestroom <= 0)
            {
                if (g_BgPortals[0].offset_portal != 0)
                {
                    bestroom = chrpropFindFirstBgHitInConnectedRooms(getTileRoom(getCurrentPlayerProp()->stan), playerpos, &stanhit, &hitdir, &scaleddir, visited, &bghit);
                }
                else
                {
                    bestroom = chrpropFindClosestBgHitRoom(getTileRoom(getCurrentPlayerProp()->stan), playerpos, &stanhit, &hitdir, &scaleddir, visited, &bghit);
                }
            }
        }
#else
        if (bestroom <= 0)
        {
            if (g_BgPortals[0].offset_portal != 0)
            {
                bestroom = chrpropFindFirstBgHitInConnectedRooms(getTileRoom(getCurrentPlayerProp()->stan), playerpos, &stanhit, &hitdir, &scaleddir, visited, &bghit);
            }
            else
            {
                bestroom = chrpropFindClosestBgHitRoom(getTileRoom(getCurrentPlayerProp()->stan), playerpos, &stanhit, &hitdir, &scaleddir, visited, &bghit);
            }
        }
#endif

        if (bestroom > 0)
        {
            distscale = get_room_data_float2();
            bghit.hitpos.x *= distscale;
            bghit.hitpos.y *= distscale;
            bghit.hitpos.z *= distscale;
        }

        bestroom = chrpropFindCloserBgHitInVisibleRooms(playerpos, &stanhit, &hitdir, &scaleddir, visited, &bghit, bestroom);

        if (bestroom > 0)
        {
            gotbghit = 1;
            besttexture = bghit.texturenum;
            besthitpos.f[0] = (visiblehitpos.f[0] = bghit.hitpos.f[0]);
            besthitpos.f[1] = (visiblehitpos.f[1] = bghit.hitpos.f[1]);
            besthitpos.f[2] = (visiblehitpos.f[2] = bghit.hitpos.f[2]);
        }
        else
        {
            bestroom = startroom;
            besttexture = -1;
            besthitpos.x = dest.x;
            besthitpos.y = dest.y;
            besthitpos.z = dest.z;
        }

        if (hitbgstan || gotbghit)
        {
            mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &besthitpos);
            negz = -besthitpos.f[2];
            shotdata.maxdist = negz;
        }
    }

    new_var = 300.0f;

    if ((shotdata.weapon == 23) && (shotdata.maxdist > new_var))
    {
        shotdata.maxdist = new_var;
    }

    for (pp = g_LastOnScreenProp; (--pp) >= g_OnScreenPropList;)
    {
        prop = *pp;

        if (prop != 0)
        {
            if ((prop->type == PROP_TYPE_CHR) || (((prop->type == PROP_TYPE_VIEWER) && (prop->chr != 0)) && (getPlayerPointerIndex(prop) != get_cur_playernum())))
            {
                chrTestHit(prop, &shotdata);
            }
            else if (((prop->type == PROP_TYPE_OBJ) || (prop->type == PROP_TYPE_WEAPON)) || (prop->type == PROP_TYPE_DOOR))
            {
                sub_GAME_7F04E9BC(prop, &shotdata);
            }
        }
    }

    for (k = 0; k < 10; k++)
    {
        if (shotdata.hits[k].prop != 0)
        {
#ifdef DEBUG
        assert(!IsBadVec3d((vec3d *)&shotdata.hits[k].hit.hitpos));
#endif
            if ((shotdata.hits[k].prop->type == PROP_TYPE_CHR) || (shotdata.hits[k].prop->type == PROP_TYPE_VIEWER))
            {
                chrHandleBulletHit(&shotdata, &shotdata.hits[k]);
            }
            else if (((shotdata.hits[k].prop->type == PROP_TYPE_OBJ) || (shotdata.hits[k].prop->type == PROP_TYPE_WEAPON)) || (shotdata.hits[k].prop->type == PROP_TYPE_DOOR))
            {
                objHit(&shotdata, &shotdata.hits[k]);
            }

            if (shotdata.hits[k].countsAsPenetration)
            {
                numhits++;

                if (numhits >= bondwalkItemGetObjectsShootThrough(shotdata.weapon))
                {
                    gotbghit = 0;
                    hitbgstan = 0;
                }
            }
        }
    }

    if (gotbghit || hitbgstan)
    {
        finalpos = 0;
        createSpark = 1;

        if ((shotdata.weapon == 23) && (negz > new_var))
        {
            createSpark = 0;
        }

        if (gotbghit)
        {
            if (bghit.texturenum < 0)
            {
                impact_sounds = g_HitTypeSounds[0];
            }
            else
            {
#ifdef TARGET_N64
                impact_sounds = g_HitTypeSounds[((u8 *) (&g_Textures[bghit.texturenum]))[0] & 0xf];
#else
                /* PORT: IDO packs the image_entry bitfields MSB-first, so byte
                 * 0's low nibble is hitTexture; GCC on little-endian packs
                 * LSB-first and this read the wrong nibble (hitSound), giving
                 * the wrong impact material. */
                impact_sounds = g_HitTypeSounds[g_Textures[bghit.texturenum].hitTexture];

                { /* diagnostic (GE_DUMP_BOND): name the BG texture a shot
                   * lands on - the F2.5 blue-floor hunt (shoot the floor,
                   * read its texnum). */
                    extern char *getenv(const char *);
                    extern int printf(const char *, ...);
                    static s32 dump = -1;
                    if (dump < 0) dump = getenv("GE_DUMP_BOND") != NULL;
                    if (dump)
                    {
                        printf("bghit: texnum=%d material=%d pos=(%.0f,%.0f,%.0f)\n",
                               bghit.texturenum,
                               (s32) g_Textures[bghit.texturenum].hitTexture,
                               besthitpos.x, besthitpos.y, besthitpos.z);
                    }
                }
#endif
            }

            if (createSpark)
            {
                if ((impact_sounds->thing2_len > 0) && (shotdata.weapon != 23))
                {
                    pad = randomGetNext() % impact_sounds->thing2_len;
                    explosionCreateBulletImpact(&visiblehitpos, &bghit.normal, impact_sounds->thing2[pad], bestroom, 0, -1, 0);
                }

                if (check_if_imageID_is_light(bghit.texturenum))
                {
                    lightFixtureBreak(bghit.tricmd, bghit.unk28, bestroom);
                }
            }

            finalpos = &visiblehitpos;
        }
        else if (hitbgstan)
        {
            stanhit.x = (shotdata.dir.x * M_U16_MAX_VALUE_F) + shotdata.gunpos.x;
            stanhit.y = (shotdata.dir.y * M_U16_MAX_VALUE_F) + shotdata.gunpos.y;
            stanhit.z = (shotdata.dir.z * M_U16_MAX_VALUE_F) + shotdata.gunpos.z;
            finalpos = &stanhit;
        }

        if (finalpos != 0)
        {
            if (createSpark)
            {
                recall_joy2_hits_edit_flag(shotdata.weapon, finalpos, besttexture);

                /* besttexture is -1 when there's no BG hit (e.g. shooting
                 * at the sky/ceiling with only hitbgstan set) -
                 * g_Textures[besttexture * 8] then reads 8 bytes before
                 * the array.
                 * gotbghit (the only path that sets besttexture >= 0,
                 * see the impact_sounds lookup above) gates this the
                 * same way. */
                if ((besttexture >= 0) &&
                    ((0xf & ((u8 *) g_Textures)[besttexture * 8]) != 5) && ((((u8 *) g_Textures)[besttexture * 8] & 0xf) != 6))
                {
                    rooms[0] = bestroom;
                    rooms[1] = 255;
                    explosionCreate(0, finalpos, hittile, 1, 0, get_cur_playernum(), rooms, 0);
                }
            }

            finalpos->x -= 26.0f * shotdata.dir.x;
            finalpos->y -= 26.0f * shotdata.dir.y;
            finalpos->z -= 26.0f * shotdata.dir.z;

            gunSetTracerTarget(finalpos);

            if (createSpark)
            {
                bullet_spark_create(finalpos, 1, 26.0f, bestroom);
            }
        }
    }
}


/**
 * Address: 7F03B9C0
 *
 * Hitscans gather candidate hits along the bullet path. This function records each candidate hit into shotdata
 * and enforces pentration limits and removes hits that should be blocked by closer objects.
 */
void chrpropAddBulletHit(struct ShotData *shotdata, PropRecord *prop, f32 dist, s32 hitpart, ModelNode *node, struct HitThing *hitthing, s32 room, ModelNode *unk44, Model *model, bool countsAsPenetration, s32 blocksFurtherHits)
{
    s32 pad;
    s32 i;
    s32 furthestindex;
    f32 prevfurthest;
    s32 numPenetratedObjects;
    f32 furthest;
    struct ShotData *localshot; // Assigned but never used, required for matching.

    /**
     * If countsAsPenetration is true, then this hit is on an object that bullets may pass through,
     * and it counts against the weapon's shoot-through object limit.
     */
    if (countsAsPenetration)
    {
        furthest = 0.0f;
        prevfurthest = furthest;
        furthestindex = 0;
        numPenetratedObjects = 0;
        localshot = shotdata;

        for (i = 0; i < ARRAYCOUNT(shotdata->hits); i++)
        {
            if (shotdata->hits[i].prop != NULL && shotdata->hits[i].countsAsPenetration)
            {
                numPenetratedObjects++;

                if (furthest < shotdata->hits[i].dist)
                {
                    prevfurthest = furthest;
                    furthest = shotdata->hits[i].dist;
                    furthestindex = i;
                }
            }
        }

        /**
         * The bullet has reached the max number of objects it can penetrate.
         */
        if (numPenetratedObjects >= bondwalkItemGetObjectsShootThrough(shotdata->weapon))
        {
            // Make room for this new hit.
            shotdata->hits[furthestindex].prop = NULL;
            shotdata->maxdist = prevfurthest;

            // Update the shot's useful distance.
            if (prevfurthest < dist)
            {
                shotdata->maxdist = dist;
            }

            // Remove hits that are beyond the penetration limit.
            for (i = 0; i < ARRAYCOUNT(shotdata->hits); i++)
            {
                if (shotdata->hits[i].prop != NULL && (!shotdata->hits[i].countsAsPenetration) && prevfurthest < shotdata->hits[i].dist)
                {
                    shotdata->hits[i].prop = NULL;
                }
            }
        }
        else
        {
            /**
             * This hit is the final allowed penetrable object.
             */
            if (numPenetratedObjects + 1 == bondwalkItemGetObjectsShootThrough(shotdata->weapon))
            {
                if (dist < shotdata->maxdist)
                {
                    shotdata->maxdist = dist;
                }
            }
        }
    }

    /**
     * If true, this stops the bullets for all weapons except the Cougar Magnum and Silver PP7.
     * Any already recorded hits farther than this one are removed,
     * and the shot's max distance is clamped to this hit distance.
     * Used by bulletproof glass.
    */
    if (blocksFurtherHits)
    {
        if (shotdata->weapon != ITEM_RUGER && shotdata->weapon != ITEM_SILVERWPPK)
        {
            for (i = 0; i < ARRAYCOUNT(shotdata->hits); i++)
            {
                if (shotdata->hits[i].prop != NULL && dist < shotdata->hits[i].dist)
                {
                    shotdata->hits[i].prop = NULL;
                }
            }

            shotdata->maxdist = dist;
        }
    }

    for (i = 0; i < ARRAYCOUNT(shotdata->hits); i++)
    {
        if (shotdata->hits[i].prop == NULL)
        {
            shotdata->hits[i].dist = dist;
            shotdata->hits[i].prop = prop;
            shotdata->hits[i].hitpart = hitpart;
            shotdata->hits[i].node = node;
            shotdata->hits[i].hit = *hitthing;
            shotdata->hits[i].room = room;
            shotdata->hits[i].unk44 = unk44;
            shotdata->hits[i].model = model;
            shotdata->hits[i].countsAsPenetration = countsAsPenetration;
            break;
        }
    }
}


void chraiFistAttackHandler(s32 hand, s32 item_id)
{
    PropRecord *playerprop;
    f32 ducking;
    s32 hit;
    PropRecord *prop;
    PropRecord **propptr;
    ChrRecord *chr;
    f32 max0;
    f32 min0;
    f32 max1;
    f32 min1;
    f32 max2;
    f32 min2;
    s32 hitpart;
    StandTile *tile;
    coord3d from;
    coord3d vector;
    f32 reach;

    hit = 0;
    playerprop = getCurrentPlayerProp();
    ducking = bondviewGetPlayerDuckingHeightRelated(g_CurrentPlayer);

    for (propptr = g_LastOnScreenProp - 1; propptr >= g_OnScreenPropList; propptr--)
    {
        prop = *propptr;

        if (prop == NULL)
        {
            continue;
        }

        if (!(prop->zDepth < 500.0f))
        {
            continue;
        }

        if (prop->type != PROP_TYPE_CHR)
        {
            if (prop->type != PROP_TYPE_VIEWER)
            {
                continue;
            }

            if (prop->chr == NULL)
            {
                continue;
            }

            if (getPlayerPointerIndex(prop) == get_cur_playernum())
            {
                continue;
            }
        }

        reach = 50.0f;
        chr = prop->chr;

        if (getCurrentWeaponOrItem() == ITEM_SNIPERRIFLE)
        {
            reach = 100.0f;
        }

        modelGetAxisExtents(chr->model, &max0, &min0, 0);

        if (!(0.0f <= max0))
        {
            continue;
        }

        if (!(min0 <= 0.0f))
        {
            continue;
        }

        modelGetAxisExtents(chr->model, &max1, &min1, 1);

        if (!(0.0f <= max1))
        {
            continue;
        }

        if (!(min1 <= 0.0f))
        {
            continue;
        }

        modelGetAxisExtents(chr->model, &max2, &min2, 2);

        if (!(min2 <= 0.0f))
        {
            continue;
        }

        if (!((-reach) <= max2))
        {
            continue;
        }

        tile = playerprop->stan;

        if (!stanTestLineUnobstructed(&tile, playerprop->pos.x, playerprop->pos.z, prop->pos.x, prop->pos.z, CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER, ducking, ducking, 0.0f, 1.0f))
        {
            continue;
        }

        if (tile != prop->stan) {
            continue;
        }

        hitpart = HIT_CHEST;

        if (currentPlayerGetCrouchPos() == CROUCH_HALF) {
            hitpart = HIT_GENERAL;
        }
        else if (currentPlayerGetCrouchPos() == CROUCH_SQUAT)
        {
            hitpart = HIT_GENERALHALF;
        }

        if (g_musicSfxBufferPtr && g_musicSfxBufferPtr);

        bullet_path_from_screen_center(&from, &vector, hand);
        mtx4RotateVecInPlace(currentPlayerGetViewToWorldMtxf(), &vector);

        if (handles_shot_actors(chr, hitpart, &vector, item_id, 1))
        {
            recall_joy2_hits_edit_detail_edit_flag(item_id, prop, -1);
            hit = 1;
        }
    }

    if ((!hit) && (item_id == ITEM_FIST))
    {
        sndPlaySfx(g_musicSfxBufferPtr, PUNCHING_AIR_SFX, 0);
    }
}


void chraiDefaultWeaponFireHandler(s32);

/**
 * Address 0x7F03C0F0.
*/
void chraiCheckUseHeldItem(s32 hand)
{
    s32 item_id;
    s32 i;

    if (get_hands_firing_status(hand) != 0)
    {
        item_id = getCurrentPlayerWeaponId(hand);

        if (item_id == ITEM_TRIGGER)
        {
            trigger_remote_mine_detonation();
        }
        else if (item_id == ITEM_GRENADELAUNCH
            || item_id == ITEM_ROCKETLAUNCH
            || item_id == ITEM_GRENADE
            || item_id == ITEM_THROWKNIFE
            || item_id == ITEM_REMOTEMINE
            || item_id == ITEM_PROXIMITYMINE
            || item_id == ITEM_TIMEDMINE
            || item_id == ITEM_FLAREPISTOL
            || item_id == ITEM_PITONGUN
            || item_id == ITEM_BOMBCASE
            || item_id == ITEM_BUG
            || item_id == ITEM_MICROCAMERA
            || item_id == ITEM_GOLDENEYEKEY
            || item_id == ITEM_TOKEN
            || item_id == ITEM_PLASTIQUE
        )
        {
            // nothing to do
        }
        else if (item_id == ITEM_TANKSHELLS)
        {
            gunFireTankShell(hand);
        }
        else if (item_id == ITEM_FIST || item_id == ITEM_KNIFE)
        {
            chraiFistAttackHandler(hand, item_id);
        }
        else if (item_id == ITEM_SHOTGUN || item_id == ITEM_AUTOSHOT)
        {
            inc_curplayer_hitcount_with_weapon(item_id, SHOT_REGISTER_TOTAL);

            for (i=0; i<NUMBER_SHOTGUN_BULLETS; i++)
            {
                chraiDefaultWeaponFireHandler(hand);
            }
        }
        else if (item_id == ITEM_CAMERA)
        {
            objectiveTakePictureHandler();
        }
        else if (item_id == ITEM_WATCHMAGNETATTRACT)
        {
            g_CurrentPlayer->magnetattracttime = 0;
        }
        else
        {
            inc_curplayer_hitcount_with_weapon(item_id, SHOT_REGISTER_TOTAL);
            chraiDefaultWeaponFireHandler(hand);
        }
    }
}


/**
 * Address 0x7F03C294.
*/
void chraiCheckUseHeldItems(void)
{
    chraiCheckUseHeldItem(GUNRIGHT);
    chraiCheckUseHeldItem(GUNLEFT);
}


void propExecuteTickOperation(PropRecord *prop, TICKOP op)
{
    ObjectRecord *propobj;

    if (op == TICKOP_FREE)
    {
        if ((prop->type == PROP_TYPE_WEAPON) || (prop->type == PROP_TYPE_OBJ))
        {
            propobj = prop->obj;
            if (prop->obj->state & PROPSTATE_RESPAWN) //matches only if called directly (not propobj)
            {
                #ifndef VERSION_EU
                prop->timetoregen = 0x4B0;
                #else
                prop->timetoregen = 0x3E8;
                #endif
                propobj->runtime_bitflags |= RUNTIMEBITFLAG_00000800;
                propobj->runtime_bitflags &= ~RUNTIMEBITFLAG_REMOVE;
                propobj->state &= ~0x80;
                propobj->maxdamage = 0.0f;
                chrpropDeregisterRooms(prop);
                chrpropDisable(prop);
                return;
            }
        }
        chrpropDeregisterRooms(prop);
        chrpropDelist(prop);
        chrpropDisable(prop);
        chrpropFree(prop);
    }
    else if (op == TICKOP_DISABLE)
    {
        chrpropDeregisterRooms(prop);
        chrpropDelist(prop);
        chrpropDisable(prop);
    }
    else if (op == TICKOP_GIVETOPLAYER)
    {
        chrpropDeregisterRooms(prop);
        chrpropDelist(prop);
        chrpropDisable(prop);
        objDetach(prop);
        objFreeEmbedmentOrProjectile(prop);
        chrpropReparent(prop, getCurrentPlayerProp());
    }
}


PropRecord *propFindForInteract(void)
{
    PropRecord **ptr;
    s32 i;
    bool checkmore = TRUE;

    g_InteractProp = NULL;

    // Iterate onscreen list near to far
    for (ptr = g_LastOnScreenProp - 1; ptr >= g_OnScreenPropList; ptr--)
    {
        PropRecord *prop = *ptr;

        if (prop)
        {
            if (prop->type == PROP_TYPE_CHR)
            {
                // empty
            }
            else if (prop->type == PROP_TYPE_OBJ || prop->type == PROP_TYPE_WEAPON)
            {
                checkmore = objTestForInteract(prop);
            }
            else if (prop->type == PROP_TYPE_DOOR)
            {
                checkmore = doorTestForInteract(prop);
            }
            else if (prop->type == PROP_TYPE_EXPLOSION)
            {
                // empty
            }
            else if (prop->type == PROP_TYPE_SMOKE)
            {
                // empty
            }

            if (!checkmore)
            {
                break;
            }
        }
    }

    return g_InteractProp;
}


bool bond_interact_object(void)
{
    PropRecord *prop;
    TICKOP tickop;

    prop = propFindForInteract();
    tickop = TICKOP_NONE;

    if (prop)
    {
        switch (prop->type)
        {
            case PROP_TYPE_OBJ:
            case PROP_TYPE_WEAPON:
                tickop = propobjInteract(prop);
                break;
            case PROP_TYPE_DOOR:
                tickop = propdoorInteract(prop);
                break;
            case PROP_TYPE_CHR:
            case PROP_TYPE_PLAYER:
            case PROP_TYPE_EXPLOSION:
            case PROP_TYPE_SMOKE:
                break;
        }

        propExecuteTickOperation(prop, tickop);

        return FALSE;
    }

    return TRUE;
}


/**
* Returns true when the given prop isn't within 400 units of any player prop.
*/
s32 chrpropIsFarFromPlayers(PropRecord* prop)
{
    PropRecord* player_prop;
    coord3d pos_diff;
    s32 uninitialized; // needed for match
    s32 rc;
    s32 i;
    s32 player_count;

    player_count = getPlayerCount();
    rc = 1;

    for (i = 0; i < player_count; i++)
    {
        player_prop = g_playerPointers[i]->prop;
        pos_diff.x = player_prop->pos.x - prop->pos.x;
        pos_diff.y = player_prop->pos.y - prop->pos.y;
        pos_diff.z = player_prop->pos.z - prop->pos.z;
        if (sqrtf((pos_diff.x * pos_diff.x) + (pos_diff.y * pos_diff.y) + (pos_diff.z * pos_diff.z)) < 400.0f)
        {
            rc = 0;
            break;
        }
    }

    return rc;
}


/**
 * Per-frame tick for chrprop-managed props.
 *
 * 1) Advance all AI act states.
 * 2) Update NPC bullet tracers.
 * 3) Handle MP pickup respawns.
 * 4) Update autogun bullet tracers.
 * 5) Tick explosions and smoke.
 * 6) Update MP character bullet tracers.
 * 7) Handle prop delisting or activation.
 */
void chrpropTick(void)
{
    PropRecord *prop;
    ObjectRecord *obj;
    PropRecord *prev;
    PropRecord *next;
    ChrRecord *chr;
    bool skip_regen_sfx;
    TICKOP tickop;
    bool is_under_60;
    struct ObjectRecord *autogun;
    s32 cmdindex;
    s32 pad;
    ObjectRecord *setupobj;

    // Advance AI states e.g. attacking, walking, dying, etc...
    chrlvAllChrTick();

    prop = chrpropGetActiveTail();

    while (prop != NULL)
    {
        prev = prop->prev;
        tickop = TICKOP_NONE;

        if (prop->type == PROP_TYPE_CHR)
        {
            chr = prop->chr;

            // Update NPC bullet tracers.
            gunAdvanceBeamTimer(&chr->beams[0]);
            gunAdvanceBeamTimer(&chr->beams[1]);
        }
        else if (((prop->type == PROP_TYPE_OBJ) || (prop->type == PROP_TYPE_WEAPON)) || (prop->type == PROP_TYPE_DOOR))
        {
            obj = prop->obj;
            skip_regen_sfx = FALSE;

            if (prop->timetoregen > 0)
            {
                is_under_60 = TRUE;

                if (prop->timetoregen >= CHROBJ_TIMETOREGEN)
                {
                    is_under_60 = FALSE;
                }

                prop->timetoregen -= g_ClockTimer;

                if (prop->timetoregen < CHROBJ_TIMETOREGEN)
                {
                    if (!is_under_60)
                    {
                        if (!chrpropIsFarFromPlayers(prop))
                        {
                            prop->timetoregen += CHROBJ_TIMETOREGEN;
                        }
                    }
                }

                if (prop->timetoregen <= 0)
                {
                    prop->timetoregen = 0;
                    if (obj->state & PROPSTATE_10)
                    {
                        obj->runtime_bitflags |= RUNTIMEBITFLAG_00001000;
                    }
                    else
                    {
                        obj->runtime_bitflags &= ~RUNTIMEBITFLAG_00001000;
                    }
                }
                else if ((prop->timetoregen < CHROBJ_TIMETOREGEN) && (!is_under_60))
                {
                    if ((obj->maxdamage == 0.0f) && (!(obj->state & PROPSTATE_DESTROYED)))
                    {
                        if (obj->flags & PROPFLAG_INSIDEANOTHEROBJ)
                        {
                            chrpropDeregisterRooms(prop);
                            chrpropDelist(prop);

                            obj->runtime_bitflags &= ~RUNTIMEBITFLAG_00000800;
                            cmdindex = setupGetCommandIndexByProp(prop);
                            pad = obj->pad;
                            setupobj = setupCommandGetObject(lvlGetCurrentStageToLoad(), cmdindex + pad);

                            if ((setupobj != NULL) && (setupobj->prop != NULL))
                            {
                                modelSetScale(obj->model, obj->model->scale);
                                chrpropReparent(obj->prop, setupobj->prop);
                                skip_regen_sfx = TRUE;
                            }
#ifdef DEBUG
                            else
                            {
                                osSyncPrintf("inobj link not found for object number %d\n", cmdindex + 1);
                            }
#endif
                        }
                        else
                        {
                            chrpropEnable(prop);
                            sub_GAME_7F03E134(prop);
                            obj->runtime_bitflags &= ~RUNTIMEBITFLAG_00000800;
                        }
                    }
                    else
                    {
                        if (obj->state & PROPSTATE_EXT_COLISION_BLOCK)
                        {
                            obj->flags |= PROPFLAG_00000100;
                        }
                        else
                        {
                            obj->flags &= ~PROPFLAG_00000100;
                        }

                        obj->maxdamage = 0.0f;
                        obj->state &= ~PROPSTATE_DESTROYED;
                        sub_GAME_7F050DE8(obj->model);
                    }

                    if (obj->type == PROPDEF_ARMOUR)
                    {
                        ((BodyArmourRecord *) obj)->amount = ((BodyArmourRecord *) obj)->initialamount;
                    }

                    if (!skip_regen_sfx)
                    {
                        chrobjSndCreatePostEventDefault(sndPlaySfx(g_musicSfxBufferPtr, OBJ_REGEN_SFX, NULL), &prop->pos);
                    }

                }
            }

            // Update autogun bullet tracers.
            if (obj->type == PROPDEF_AUTOGUN)
            {
                autogun = prop->obj;
                gunAdvanceBeamTimer((BeamRecord *) ((AutogunRecord *) autogun)->beam);
            }
        }
        else if (prop->type == PROP_TYPE_EXPLOSION)
        {
            tickop = explosionTick(prop);
        }
        else if (prop->type == PROP_TYPE_SMOKE)
        {
            tickop = explosionSmokeTick(prop);
        }
        else
        {
            if (prop->type == PROP_TYPE_VIEWER)
            {
                s32 playernum;
                playernum = getPlayerPointerIndex(prop);
                gunAdvanceBeamTimer(&g_playerPointers[playernum]->hands[0].weapon_beam);
                playernum = getPlayerPointerIndex(prop);
                gunAdvanceBeamTimer(&g_playerPointers[playernum]->hands[1].weapon_beam);

                // Update MP character bullet tracers.
                if (prop->chr != NULL)
                {
                    if (getPlayerCount() >= 2)
                    {
                        chr = prop->chr;
                        gunAdvanceBeamTimer(&chr->beams[0]);
                        gunAdvanceBeamTimer(&chr->beams[1]);
                    }
                }
            }
        }

        // The tick restructured the prop list which made prop->prev stale. Resume from the prev captured before the tick.
        if (tickop == TICKOP_CHANGEDLIST)
        {
          next = prev;
        }
        else
        {
            next = prop->prev;
            if (tickop == TICKOP_RETICK)
            {
                chrpropDelist(prop);
                chrpropActivateThisFrame(prop);

                if (next == NULL)
                {
                    next = prop;
                }
            }
            else
            {
                propExecuteTickOperation(prop, tickop);
            }
        }

        prop = next;
    }
}


/*
* Address: 0x7F03CA30
* PD: propsTick (src/game/proptick.c)
*/
void propsTick(void)
{
    TICKOP tickop;
    PropRecord *prop;
    PropRecord *prev;
    PropRecord *propprev;

    prop = chrpropGetActiveTail();

    while (prop != NULL)
    {
        tickop = TICKOP_NONE;
        prev = prop->prev;

        if (prop->type == PROP_TYPE_CHR)
        {
            tickop = chrTick(prop);
        }
        else if ((prop->type == PROP_TYPE_OBJ) || (prop->type == PROP_TYPE_WEAPON) || (prop->type == PROP_TYPE_DOOR))
        {
            tickop = objTick(prop);
        }
        else if (prop->type == PROP_TYPE_EXPLOSION)
        {
            tickop = explosionChrpropExplosionTick(prop);
        }
        else if (prop->type == PROP_TYPE_SMOKE)
        {
            tickop = explosionChrpropSmokeTick(prop);
        }
        else if (prop->type == PROP_TYPE_VIEWER)
        {
            tickop = playerTick(prop);
        }

		if (tickop == TICKOP_CHANGEDLIST)
        {
			propprev = prev;
		}
        else
        {
			propprev = prop->prev;

			if (tickop == TICKOP_RETICK)
            {
				chrpropDelist(prop);
				chrpropActivateThisFrame(prop);

				if (propprev == NULL)
                {
					propprev = prop;
				}
			}
            else
            {
				propExecuteTickOperation(prop, tickop);
			}
		}

		prop = propprev;
    }

    if (get_player_position_in_shuffled(get_cur_playernum()) == 0)
    {
        handle_alarm_gas_timer_calldamage();
        loop_set_sound_effect_all_slots();
        propsDefragRoomProps();
    }
}


/**
 * Copies stan roomids from prop to array. The list is terminated
 * with an entry of -1.
 *
 * @param self: prop
 * @param roomids: out parameter. Must contain enough space to store room ids.
 *
 * Address 0x7F03CB8C.
*/
void chraiGetPropRoomIds(PropRecord *self, s32 *roomids)
{
    StandTile *stan;
    s32 i;

    stan = self->stan;

    if (stan == NULL)
    {
        roomids[0] = -1;
    }
    else if ((self->type == PROP_TYPE_VIEWER) && (self->obj == NULL))
    {
        roomids[0] = stan->room;
        roomids[1] = -1;
    }
    else
    {
        for (i=0; self->rooms[i] != 0xff; i++)
        {
            roomids[i] = self->rooms[i];
        }

        roomids[i] = -1;
    }
}


/**
 * @param arg0:
 * @param arg1: out parameter. Bounding coords (x,z) by (x,z).
 * @param arg2: out parameter.
 * @param arg3: out parameter. Maybe ymin. (ground)
 * @param arg4: out parameter. Maybe ymax. (ground + chr/object height)
 *
 * Address 0x7F03CC20.
*/
void chraiGetCollisionBounds(PropRecord *prop, struct rect4f **polygon, s32 *edges, f32 *top, f32 *bottom)
{
    *polygon = NULL;
    *edges = 0;

    if (prop->type == PROP_TYPE_CHR)
    {
        chrUpdateCollisionBounds(prop, polygon, edges, top, bottom);
    }
    else if (prop->type == PROP_TYPE_VIEWER)
    {
        bondviewGetPropHeightRelatedValues(prop, polygon, edges, top, bottom);
    }
    else if (prop->type == PROP_TYPE_WEAPON)
    {
        // nothing to do
    }
    else if ((prop->type == PROP_TYPE_OBJ) || (prop->type == PROP_TYPE_DOOR))
    {
        sub_GAME_7F04F244(prop, polygon, edges, top, bottom);
    }
    else if (prop->type == PROP_TYPE_PLAYER)
    {
        // nothing to do
    }
    else if (prop->type == PROP_TYPE_NUL)
    {
        // nothing to do
    }

    return;
}





/**
 * Same as @see chraiGetCollisionBounds, but throws away arg3 and arg4.
 *
 * @param arg0:
 * @param arg1: out parameter. Bounding coords (x,z) by (x,z).
 * @param arg2: out parameter.
 *
 * Address 0x7F03CCB0.
*/
void chraiGetCollisionBoundsWithoutY(PropRecord *prop, struct rect4f **polygon, s32 *edges)
{
    f32 sp24;
    f32 sp20;

    chraiGetCollisionBounds(prop, polygon, edges, &sp24, &sp20);
}





/**
 * @param point: 3d point to test if inside polygon. Only uses (x,z).
 * @param polygon: Convex polygon. Iterates edges and checks that
 * point is oriented correctly inside all of them.
 * @param edges: Number of edges to iterate in polygon.
 * Address 0x7F03CCD8.
*/
s32 chrpropTestPointInPolygon(coord3d *point, struct rect4f *polygon, s32 edges)
{
    /**
     * Stack overflow:
     *
     * In any case, for any convex polygon (including rectangle) the test is
     * very simple: check each edge of the polygon, assuming each edge is
     * oriented in counterclockwise direction, and test whether the point lies
     * to the left of the edge (in the left-hand half-plane). If all edges pass
     * the test - the point is inside. If at least one fails - the point is outside.
     *
     * In order to test whether the point (xp, yp) lies on the left-hand
     * side of the edge (x1, y1) - (x2, y2), you just need to calculate
     *
     * D = (x2 - x1) * (yp - y1) - (xp - x1) * (y2 - y1)
     *
     * https://stackoverflow.com/a/2752753/1462295
    */

    /**
     * Assuming the above is correct, I think that means rectangles (polygons)
     * are clockwise oriented.
    */

    f32 diff;
    s32 i;
    s32 ret = -1;

    if (edges <= 0)
    {
        return 0;
    }

    for (i=0; i<edges; i++)
    {
        // curse you compiler loop unroller
        diff = (    (polygon->points[(i+1) % edges].f[1] - polygon->points[i].f[1]) * (point->f[0] - polygon->points[i].f[0]))
                 - ((polygon->points[(i+1) % edges].f[0] - polygon->points[i].f[0]) * (point->f[2] - polygon->points[i].f[1]));

        if (diff != 0.0f)
        {
            if (i == 0 || ret < 0)
            {
                ret = (diff > 0.0f);

                continue;
            }

            if ((ret != 0) && (diff < 0.0f))
            {
                return 0;
            }

            if ((ret == 0) && (diff > 0.0f))
            {
                return 0;
            }
        }
    }

    return 1;
}





/**
 * @param arg0: prop
 * @param collision_radius: out parameter, will be set to character width or player collision radius.
 * @param height: out parameter, will be set to height
 * @param always_20: out parameter, will be set to either 20 or 30.
 *
 * Address 0x7F03CF88.
*/
void chrpropGetCollisionBounds(PropRecord *arg0, f32 *collision_radius, f32 *height, f32 *arg3)
{
    if (arg0->type == PROP_TYPE_CHR)
    {
        chrGetChrWidthHeight(arg0, collision_radius, height, arg3);
        return;
    }

    if (arg0->type == PROP_TYPE_VIEWER)
    {
        bondviewGetCollisionRadius(arg0, collision_radius, height, arg3);
        return;
    }

    *collision_radius = 0.0f;
}




/**
 * Address 0x7F03CFE8.
*/
f32 sub_GAME_7F03CFE8(PropRecord *arg0)
{
    if (arg0->type == PROP_TYPE_CHR)
    {
        return chrGetChrGround(arg0);
    }

    if (arg0->type == PROP_TYPE_VIEWER)
    {
        return bondviewGetPlayerStanHeight(g_playerPointers[getPlayerPointerIndex(arg0)]);
    }

    return 0.0f;
}






void sub_GAME_7F03D058(PropRecord *prop, bool unset) //#MATCH
{
    if (prop->type == PROP_TYPE_CHR)
    {
        chrSetMoving(prop->chr, unset);
    }
    else if (prop->type == PROP_TYPE_VIEWER)
    {
        bondviewUpdateGuardTankFlagsRelated(prop, unset);
    }
    else if ((prop->type == PROP_TYPE_OBJ) || (prop->type == PROP_TYPE_DOOR) || (prop->type == PROP_TYPE_WEAPON))
    {
        sub_GAME_7F04F218(prop, unset);
    }
}



/**
 * NTSC address: 0x7F03D0D4.
*/
void propsTickPlayer(void)
{
    PropRecord *prop;
    PropRecord *propprev;
    bool isCollected = FALSE;

    if (!isBondInTank() && !g_PlayerInvincible)
    {
        //for each prop in setup
        for (prop = chrpropGetActiveTail(); prop != NULL; prop = propprev)
        {
            isCollected = FALSE;

            if (prop->timetoregen <= 0)
            {
                switch (prop->type)
                {
                    case PROP_TYPE_DOOR:
                    case PROP_TYPE_CHR:
                    case PROP_TYPE_PLAYER:
                    case PROP_TYPE_VIEWER:
                    case PROP_TYPE_EXPLOSION:
                    case PROP_TYPE_SMOKE:
                         break;

                    case PROP_TYPE_OBJ:
                        isCollected = objTickPlayer(prop);
                        break;

                    case PROP_TYPE_WEAPON:
                        isCollected = weaponTickPlayer(prop);
                        break;
                }
            }
            propprev = prop->prev; //not sure why rare put this here and not in the for statement

            propExecuteTickOperation(prop, isCollected);
        }
    }
}


/**
 * Address: 7F03D188
 *
 * Calculates an auto-aim score for a given prop based roughly on how close it is to the center of the screen.
 * Higher scores are better, with 1.0 being the best possible score.
 */
f32 chrpropScoreAutoAimTarget(PropRecord *targetprop, coord3d *aimpos, f32 *world_xbounds, f32 *world_ybounds, coord2d *out_screen)
{
    f32 aim_screen[2];
    coord3d testpos;
    f32 screen_left_edge[2];
    f32 screen_right_edge[2];
    f32 screen_top_edge[2];
    f32 screen_bottom_edge[2];
    f32 crosshair_x;
    f32 crosshair_y;
    f32 autoaim_top;
    f32 autoaim_bottom;
    f32 autoaim_left;
    f32 autoaim_right;
    f32 score;
    bool passes_horizontal_check;
    f32 horizontal_tolerance;
    PropRecord *playerprop;
    StandTile* line_stan;
    f32 player_los_height;

    /**
     * Define a central auto-aim acceptance region.
     * The sweet spot is 65% vertically in favor of the top of the screen and 50% horizontally.
     */
    autoaim_top = getPlayer_c_screentop() + getPlayer_c_screenheight() * 0.175f;
    autoaim_bottom = getPlayer_c_screentop() + getPlayer_c_screenheight() * 0.825f;
    autoaim_left = getPlayer_c_screenleft() + getPlayer_c_screenwidth() * 0.25f;
    autoaim_right = getPlayer_c_screenleft() + getPlayer_c_screenwidth() * 0.75f;

    score = -2.0f;

    transform3Dto2DCoords(aimpos, (coord3d*)aim_screen);
    testpos.x = world_xbounds[0];
    testpos.y = aimpos->y;
    testpos.z = aimpos->z;
    transform3Dto2DCoords(&testpos, (coord3d*)screen_left_edge);
    testpos.x = world_xbounds[1];
    testpos.y = aimpos->y;
    testpos.z = aimpos->z;
    transform3Dto2DCoords(&testpos, (coord3d*)screen_right_edge);
    testpos.x = aimpos->x;
    testpos.y = world_ybounds[1];
    testpos.z = aimpos->z;
    transform3Dto2DCoords(&testpos, (coord3d*)screen_top_edge);
    testpos.x = aimpos->x;
    testpos.y = world_ybounds[0];
    testpos.z = aimpos->z;
    transform3Dto2DCoords(&testpos, (coord3d*)screen_bottom_edge);

#ifndef TARGET_N64
    /* Widescreen: pin projected x back to the authored 4:3 space, since the
     * widened projection shrinks guard spans and would weaken autoaim and make
     * it depend on window aspect. Y is untouched; this is a no-op at 4:3. */
    aim_screen[0] = bondviewAimPinX(aim_screen[0]);
    screen_left_edge[0] = bondviewAimPinX(screen_left_edge[0]);
    screen_right_edge[0] = bondviewAimPinX(screen_right_edge[0]);
#endif

    if (screen_bottom_edge[1] >= autoaim_top && autoaim_bottom >= screen_top_edge[1])
    {
        passes_horizontal_check = FALSE;
        get_bullet_angle(&crosshair_x, &crosshair_y);
        screen_left_edge[0] = floorFloat(screen_left_edge[0]);
        screen_right_edge[0] = ceilFloat(screen_right_edge[0]);

        if (currentPlayerGetXAutoAimEnabledRedirect())
        {
            if (screen_left_edge[0] <= autoaim_right && autoaim_left <= screen_right_edge[0])
            {
                horizontal_tolerance = (screen_right_edge[0] - screen_left_edge[0]) * 1.5f;

                if (getPlayerCount() == 1)
                {
                    horizontal_tolerance = horizontal_tolerance * difficulty;
                }

                passes_horizontal_check = getPlayer_c_screenleft() + 0.5f * getPlayer_c_screenwidth() >= (screen_left_edge[0] + screen_right_edge[0]) * 0.5f - horizontal_tolerance
                    && getPlayer_c_screenleft() + 0.5f * getPlayer_c_screenwidth() <= (screen_left_edge[0] + screen_right_edge[0]) * 0.5f + horizontal_tolerance
                    && autoaim_left <= aim_screen[0]
                    && autoaim_right >= aim_screen[0];
            }
        }
        else
        {
            passes_horizontal_check = screen_left_edge[0] <= crosshair_x && crosshair_x <= screen_right_edge[0];
        }

        if (passes_horizontal_check)
        {
            playerprop = getCurrentPlayerProp();
            line_stan = playerprop->stan;
            player_los_height = bondviewGetPlayerDuckingHeightRelated(g_CurrentPlayer);
            bondviewUpdateGuardTankFlagsRelated(playerprop, FALSE);

            // Can auto-aim see the target?
            if ((stanTestLineUnobstructed(&line_stan, playerprop->pos.f[0], playerprop->pos.f[2], targetprop->pos.f[0], targetprop->pos.f[2], CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER, player_los_height, player_los_height, 0.0f, 1.0f) != 0))
            {
                if (line_stan == targetprop->stan)
                {
                    f32 clamped_screen_y = aim_screen[1];

                    if (clamped_screen_y < autoaim_top)
                    {
                        clamped_screen_y = autoaim_top;
                    }
                    else if (clamped_screen_y > autoaim_bottom)
                    {
                        clamped_screen_y = autoaim_bottom;
                    }

                    out_screen->y = clamped_screen_y;

                    if (currentPlayerGetXAutoAimEnabledRedirect())
                    {
                        f32 clamped_screen_x = aim_screen[0];

                        if (clamped_screen_x < autoaim_left)
                        {
                            clamped_screen_x = autoaim_left;
                        }
                        else if (clamped_screen_x > autoaim_right)
                        {
                            clamped_screen_x = autoaim_right;
                        }

                        out_screen->x = clamped_screen_x;
                    }

                    /** If the screen's center x-coord overlaps the target's horizontal span, give it the best possible score of 1.0.
                     *  If this happens, this function's caller, chrpropUpdateAutoaimTarget, treats this as the winning prop and stops searching.
                     */
                    if (getPlayer_c_screenleft() + 0.5f * getPlayer_c_screenwidth() >= screen_left_edge[0] && getPlayer_c_screenleft() + 0.5f * getPlayer_c_screenwidth() <= screen_right_edge[0])
                    {
                        score = 1.0f;
                    }
                    // If the target is towards the left side of the screen, penalize it based on how far towards the left.
                    else if (getPlayer_c_screenleft() + 0.5f * getPlayer_c_screenwidth() >= screen_left_edge[0])
                    {
                        score = 1.0f - ((getPlayer_c_screenleft() + 0.5f * getPlayer_c_screenwidth()) - screen_right_edge[0]) / horizontal_tolerance;
                    }
                    // If the target is towards the right side of the screen, penalize it based on how far towards the right.
                    else
                    {
                        score = 1.0f - (screen_left_edge[0] - (getPlayer_c_screenleft() + 0.5f * getPlayer_c_screenwidth())) / horizontal_tolerance;
                    }
                }
            }

            bondviewUpdateGuardTankFlagsRelated(playerprop, TRUE);
        }
    }

    return score;
}


/**
 * Iterates on screen props to find autoaim target.
 *
 * US address 7F03D78C.
*/
void chrpropUpdateAutoaimTarget(void)
{
    f32 best_score;
    struct coord2d best_screen_aim; // Winning target's screen space aim point.
    f32 candidate_score;
    struct PropRecord **onscreen_prop_iter;
    struct coord3d target_aimpos;
    struct coord2d target_world_xbounds;
    struct coord2d target_world_ybounds;
    struct PropRecord *candidate_prop;
    struct coord2d candidate_screen_aim;
    struct PropRecord *best_prop;
    struct ChrRecord *candidate_chr;

    best_prop = NULL;
    best_screen_aim = g_DefaultAutoAimCoord;

    if (currentPlayerGetYAutoAimEnabledRedirect() != FALSE)
    {
        best_score = -1.0f;

        // Search all on screen props and record the best target.
        for (onscreen_prop_iter = g_LastOnScreenProp - 1; onscreen_prop_iter >= &g_OnScreenPropList[0]; onscreen_prop_iter--)
        {
            candidate_prop = *onscreen_prop_iter;

            if (candidate_prop == NULL)
            {
                continue;
            }

            if ((candidate_prop->type != PROP_TYPE_CHR)
                    && ((candidate_prop->type != PROP_TYPE_VIEWER)
                        || (candidate_prop->obj == NULL)
                        || (getPlayerPointerIndex(candidate_prop) == get_cur_playernum())))
            {
                continue;
            }

            candidate_chr = candidate_prop->chr;

            // Characters not holding a weapon are exempt from being a target.
            if (((chrGetEquippedWeaponProp(candidate_chr, GUNRIGHT) == 0) && (chrGetEquippedWeaponProp(candidate_chr, GUNLEFT) == 0)))
            {
                continue;
            }

            if ((chrGetOnscreenRenderBounds(candidate_prop, &target_aimpos, &target_world_xbounds, &target_world_ybounds) == 0))
            {
                continue;
            }

            // Score the candidate based on how close it is to the center of the screen.
            candidate_score = chrpropScoreAutoAimTarget(candidate_prop, &target_aimpos, &target_world_xbounds.x, &target_world_ybounds.x, &candidate_screen_aim.x);

            if (best_score < candidate_score)
            {
                best_score = candidate_score;

                best_prop = candidate_prop;
                best_screen_aim.x = candidate_screen_aim.x;
                best_screen_aim.y = candidate_screen_aim.y;

                // If we find a score of 1.0, we can't do any better, so break out of this search.
                if (1.0f <= candidate_score)
                {
                    break;
                }
            }
        }
    }

    if (best_prop != NULL)
    {
        // Fake but needed for matching.
        if (best_screen_aim.x > 1.0f);

        bondviewUpdateYAutoAimTime(best_prop, ((best_screen_aim.y - getPlayer_c_screentop()) / (getPlayer_c_screenheight() * 0.5f)) - 1.0f);

        if (currentPlayerGetXAutoAimEnabledRedirect() != FALSE)
        {
            bondviewUpdateXAutoAimTime(best_prop, ((best_screen_aim.x - getPlayer_c_screenleft()) / (getPlayer_c_screenwidth() * 0.5f)) - 1.0f);
        }
    }
    else
    {
        bondviewUpdateYAutoAimTime(NULL, 0.0f);
        bondviewUpdateXAutoAimTime(NULL, 0.0f);
    }
}


/*
* Address: 7F03D9EC
*/
s32 propDoorGetCdTypes(PropRecord* prop)
{
    s32 var_v1;

    if (prop->door->openPosition <= 0.0f)
    {
        var_v1 = CDTYPE_CLOSEDDOORS;
    }
    else
    {
        var_v1 = (prop->door->maxFrac <= prop->door->openPosition)
            ? CDTYPE_OPENDOORS
            : CDTYPE_AJARDOORS;
    }

    if (((s32)prop->door->flags2 * 4) < 0)
    {
        var_v1 |= CDTYPE_DOORSLOCKEDTOAI;
    }

    return var_v1;
}


/*
* Address: 7F03DA50
* PD: prop_is_of_cd_type
*/
s32 propIsOfCdType(PropRecord* prop, s32 cdtypes) {
    s32 ret;
    ObjectRecord *obj;
    ret = 1;

    if (prop->type == PROP_TYPE_DOOR) {
        if ((cdtypes & CDTYPE_AIOPAQUE)) {
            obj = prop->obj;

            if (obj->flags & PROPFLAG_04000000) {
                ret = 0;
            }
        }

        if (!(cdtypes & CDTYPE_DOORS)) {
            if (!(propDoorGetCdTypes(prop) & cdtypes)) {
                ret = 0;
            }
        }
    } else if (prop->type == PROP_TYPE_VIEWER) {
        if (!(cdtypes & CDTYPE_PLAYERS)) {
            ret = 0;
        }
    } else if (prop->type == PROP_TYPE_CHR) {
        if (!(cdtypes & CDTYPE_CHRS)) {
            ret = 0;
        }
    } else {
        obj = prop->obj;

        if ((cdtypes & CDTYPE_AIOPAQUE) && (obj->flags & PROPFLAG_04000000)) {
            ret = 0;
        }

        if ((cdtypes & CDTYPE_OBJSIMMUNETOEXPLOSIONS) && !(obj->flags & PROPFLAG_INVINCIBLE)) {
            ret = 0;
        }

        if (obj->flags & PROPFLAG_00000800) {
            if (!(cdtypes & CDTYPE_PATHBLOCKER)) {
                ret = 0;
            }
        } else if (!(cdtypes & CDTYPE_OBJS)) {
            ret = 0;
        }
    }

    return ret;
}



/* I think the arguments are lists of roomids but I'm not certain. This function checks if any item in the two lists match */
s32 sub_GAME_7F03DB70(s32* roomids1, s32* roomids2)
{
    s32* itr1;
    s32* itr2;
    s32 itr1_val;
    s32 itr2_val;

    itr1 = roomids1;
    itr1_val = *itr1;
    while (itr1_val >= 0)
    {
        itr2 = roomids2;
        itr2_val = *itr2;
        while(itr2_val >= 0)
        {
            if (itr1_val == itr2_val) { return 1; }
            itr2++;
            itr2_val = *itr2;
        }
        itr1++;
        itr1_val = *itr1;
    }

    return 0;
}


#define MAXBLOCKS 256
#define ROOMLISTMAX 256
/*
* Address: 0x7F03DBCC
* PD: prop_try_add_to_chunk
* block: canonical name
*/
s32 chrpropInsertPropnum(s16 propnum, s32 block)
{
    s32 i;
    #ifdef DEBUG
    assert(block<MAXBLOCKS); //prop.c line 2136
    #endif
    // Note: The size of the propnums array is 16, but we're only iterating over the first 15 elements.
    //       Is this because the last element is always -1? Seems like a waste.
    for (i = 0; i < 15; i++)
    {
        if (RoomPropListBlocks[block].propnums[i] < 0)
        {
            RoomPropListBlocks[block].propnums[i] = propnum;
            return 1;
        }
    }

    return 0;
}



/*
* Address: 0x7F03DCB8
* canonical name newblockforroom
* Description: Find an emtpy chunk that can be assigned to a room
* PD: room_allocate_prop_list_chunk
* room: canonical name
* prevblock: canonical name
*/
s32 chrpropInitializeNewChunkForRoom(s32 room, s32 prevblock)
{
    s32 i;
#ifdef DEBUG
    assert(room < g_MaxNumRooms); // roomnumber
    assert(prevblock<MAXBLOCKS);
#endif
    for (i = 0; i < MAXBLOCKS; i++)
    {
        if (RoomPropListBlocks[i].propnums[0] == -2)
        {
            // This chunk is allowed to be erased
            s32 j;
            for (j = 0; j < 16; j++)
            {
                RoomPropListBlocks[i].propnums[j] = -1;
            }

            if (prevblock >= 0)
            {
                RoomPropListBlocks[prevblock].propnums[0xF] = i;
            }
            else
            {
                RoomPropListBlockIndices[room] = i;
            }

            return i;
        }
    }
#ifdef DEBUG
    osSyncPrintf("newblockforroom: no free blocks!\n");
#endif
    return -1;
}




/*
* Address: 0x7F03DD9C
* PD: prop_register_room
* PD adds an upper bound check to make sure room is not above the max number of rooms
*/
void chrpropRegisterRoom(PropRecord *prop, s16 room)
{
   	s32 prevchunk = -1;
#ifdef DEBUG
    assert(room < g_MaxNumRooms); // roomnumber
#endif
    if (room < 0)
    {
        return;
    }
    else
    {
        // Find which chunk to start at
        s32 block = RoomPropListBlockIndices[room];
        s16 propnum = (prop - g_Props);
#ifdef DEBUG
        assert(block<MAXBLOCKS);
#endif

        while (block >= 0)
        {
            if (chrpropInsertPropnum(propnum, block))
            {
                return;
            }

            prevchunk = block;
            block     = RoomPropListBlocks[block].propnums[0xF];
#ifdef DEBUG
            assert(block<MAXBLOCKS);
#endif
        }

        // Allocate a new chunk
        block = chrpropInitializeNewChunkForRoom(room, prevchunk);

        if (block >= 0)
        {
            chrpropInsertPropnum(propnum, block);
        }
    }
}




/*
* Address: 0x7F03DE94
* PD: prop_deregister_room
* PD adds an upper bound check to make sure room is not above the max number of rooms
*/
void chrpropDeregisterRoom(PropRecord* prop, s16 room) {
    bool removed = 0;
    s32 prev = -1;
#ifdef DEBUG
        assert(room < g_MaxNumRooms); // roomnumber
#endif

    if (room >= 0)
    {
        s16 block = RoomPropListBlockIndices[room];
        s16 propIndex = (prop - g_Props);
#ifdef DEBUG
        assert(block<MAXBLOCKS);
#endif

        while (block >= 0)
        {
            bool populated = 0;
            s32 var_s0_2;

            // Check each prop entry in the chunk
            for (var_s0_2 = 0; var_s0_2 < 15; var_s0_2++)
            {
                if (propIndex == RoomPropListBlocks[block].propnums[var_s0_2])
                {
                    RoomPropListBlocks[block].propnums[var_s0_2] = -1; // Mark entry as empty
                    removed = 1;
                }
                else if (!populated && RoomPropListBlocks[block].propnums[var_s0_2] >= 0)
                {
                    populated = 1;
                }
            }

            if (!populated) // not matching
            {
                // This chunk is empty, so it can be marked as available
                RoomPropListBlocks[block].propnums[0] = -2;

                if (prev >= 0)
                {
                    RoomPropListBlocks[prev].propnums[0xF] = RoomPropListBlocks[block].propnums[0xF];
                }
                else
                {
                    RoomPropListBlockIndices[room] = RoomPropListBlocks[block].propnums[0xF];
                }
            }
            else
            {
                prev = block; // not matching
            }

            if (removed)
            {
                return;
            }

            block = RoomPropListBlocks[block].propnums[0xF];
#ifdef DEBUG
            assert(block<MAXBLOCKS);
#endif

        }
    }
}



void sub_GAME_7F03E134(PropRecord* p)
{
    if (p->type == PROP_TYPE_CHR)
    {
        chrDetectRooms(p->chr);
    } else if ((p->type == PROP_TYPE_OBJ) || (p->type == PROP_TYPE_WEAPON))
    {
        setupUpdateObjectRoomPosition((ObjectRecord* ) p->obj);
    }
}



// Duplicate of the below function with a small extension.
void chrpropDeregisterRooms(PropRecord *prop)
{
    u8  room;
    u8 *roomIter;

    roomIter = prop->rooms;
    room     = roomIter[0];

    while (room != (u8)-1)
    {
        chrpropDeregisterRoom(prop, room);
        roomIter += 1;
        room = *roomIter;
    }
    if (!(prop->flags & PROPFLAG_00000010))
    {
        prop->rooms[0] = -1; //hide room
    }
}




void chrpropRegisterRooms(PropRecord *prop)
{
    u8  room;
    u8 *roomIter;

    roomIter = prop->rooms;
    room     = roomIter[0];

    while (room != (u8)-1)
    {
        chrpropRegisterRoom(prop, room);
        roomIter += 1;
        room = *roomIter;
    }
}


/*
* Address: 0x7F03E27C
*
* Recalculate the prop's room list with rooms it is found to be overlapping.
*/
void chrpropUpdateRoomList(PropRecord *prop, coord3d *bbmin, coord3d *bbmax, f32 radius)
{
    ObjectRecord *obj;
    s32 rooms[7]; // Room payload only, no terminator.
    StandTile *tile;
    s32 count;
    s32 i;
    u8 *src;

    count = 0;
    obj = NULL;

    if (prop->flags & PROPFLAG_00000008) {
        // Seed from the prop's existing room list.
        if (prop->type == PROP_TYPE_OBJ || prop->type == PROP_TYPE_WEAPON || prop->type == PROP_TYPE_DOOR) {
            obj = prop->obj;
        }

        if (obj != NULL
            && obj->runtime_bitflags & 0x80
            && obj->projectile->flags & PROJECTILEFLAG_00000008) {
            src = obj->projectile->unkCC;
        } else {
            src = prop->rooms;
        }

        for (i = 0; src[i] != 0xff; i++) {
            rooms[i] = src[i];
        }

        count = i;
    } else {
        // Seed from the stan tile locus around the prop's X/Z position.
        tile = prop->stan;
        count = 0;
        sub_GAME_7F0B21B0(&tile, prop->pos.x, prop->pos.z, radius, rooms, &count, 7);
    }

    // Update the room list with neighboring rooms reachable through portals and overlapped by the bounding box.
    sub_GAME_7F0BA2D4(bbmin, bbmax, rooms, &count, 7);

    for (i = 0; i < count; i++) {
        prop->rooms[i] = rooms[i];
    }

    // Commit the rebuilt room list to the prop, terminated by -1.
    prop->rooms[i] = -1;
}


/**
 * Given a list of rooms (terminated by -1), populate the propnums
 * list based on which props are in any of those rooms.
 * PD: roomGetProps
 */
void roomGetProps(s32 *rooms)
{
    s16 *writeptr = ptr_list_object_lookup_indices;
    s32 room;
    s32 i;
    s32 j;

    room = *rooms;

    // Iterate rooms
    while (room >= 0)
    {
        // Find the chunk to start at
        s32 chunkindex = RoomPropListBlockIndices[room];

        // Iterate the chunks
        while (chunkindex >= 0)
        {
            // Iterate the propnums within each chunk
            for (i = 0; i < 15; i++)
            {
                s32 propnum = RoomPropListBlocks[chunkindex].propnums[i];

                if (propnum >= 0)
                {
                    // Check if it's in the list already
                    s16 *ptr = ptr_list_object_lookup_indices;

                    while (ptr < writeptr)
                    {
                        if (*ptr == propnum) { break; }
                        ptr++;
                    }

                    if (ptr == writeptr)
                    {
                        // Prop is not in the list, so insert it
                        *writeptr = propnum;
                        writeptr++;
                    }
                }
            }

            chunkindex = RoomPropListBlocks[chunkindex].propnums[15];
        }

        rooms++;
        room = *rooms;
    }

    *writeptr = -1;
    writeptr++;
    num_obj_position_data_entries = writeptr - ptr_list_object_lookup_indices;
    #ifdef DEBUG
    assert(roomspropnum<ROOMLISTMAX-1); //num_obj_position_data_entries
    #endif
}


void propsDefragRoomProps(void)
{
	s32 i;
	s32 j;
	s32 k;

	// Iterate rooms
	for (i = 0; i < g_MaxNumRooms; i++)
    {
		s32 previndex = RoomPropListBlockIndices[i];

		if (previndex >= 0)
        {
			s32 nextindex = RoomPropListBlocks[previndex].propnums[0xF];

			// Iterate this room's chunks but skip the first
			while (nextindex >= 0)
            {
				// Iterate propnums within this chunk
				for (j = 0; j < 15; j++)
                {
					// If this propnum is unallocated
					if (RoomPropListBlocks[previndex].propnums[j] < 0)
                    {
						// Iterate forward through the chunk list and find a
						// propnum to move back to the prev chunk
						for (k = 0; k < 15; k++)
                        {
							if (RoomPropListBlocks[nextindex].propnums[k] >= 0)
                            {
								RoomPropListBlocks[previndex].propnums[j] = RoomPropListBlocks[nextindex].propnums[k];
								RoomPropListBlocks[nextindex].propnums[k] = -1;
								break;
							}
						}

						// Check if there are more propnums in the future chunk
						for (; k < 15; k++)
                        {
							if (RoomPropListBlocks[nextindex].propnums[k] >= 0)
                            {
								break;
							}
						}

						if (k == 15)
                        {
							// There's no more propnums, so this chunk can be removed
							RoomPropListBlocks[nextindex].propnums[0] = -2;
							RoomPropListBlocks[previndex].propnums[15] = RoomPropListBlocks[nextindex].propnums[15];

							nextindex = RoomPropListBlocks[previndex].propnums[15];

							if (nextindex < 0)
                            {
								break;
							}
						}
					}
				}

				if (nextindex >= 0)
                {
					previndex = nextindex;
					nextindex = RoomPropListBlocks[nextindex].propnums[15];
				}
			}
		}
	}
}


void removed_debug_roomblocks_feature(void)
{

}
//end of prop.c, now chrprop.c

/**
 * NTSC address 0x7F03E6A0.
*/
void sub_GAME_7F03E6A0(PropRecord *prop)
{
    struct LinkRecord *link;
    struct ObjectRecord *obj;

    obj = prop->obj;

    if (obj->runtime_bitflags & RUNTIMEBITFLAG_00000001)
    {
        for (link = g_LevelLoadPropSwitch; link != NULL; link = link->next)
        {
            if (prop == link->first)
            {
                if (link->second != NULL)
                {
                    doorActivateWrapper(link->second);
                }
            }
        }
    }
}



bool doorIsPadlockFree(DoorRecord* door)
{
    if (door->runtime_bitflags & RUNTIMEBITFLAG_PADLOCKEDDOOR)
    {
        LockDoorRecord *padlockeddoor = g_LevelLoadPropLockDoor;

        while (padlockeddoor)
        {
            if (door == padlockeddoor->door
                    && padlockeddoor->lock
                    && padlockeddoor->lock->prop
                    && objIsHealthy(padlockeddoor->lock)) {
                return FALSE;
            }

            padlockeddoor = padlockeddoor->next;
        }
    }

    return TRUE;
}


bool objCanPickupFromSafe(ObjectRecord *obj)
{
    if (obj->flags2 & PROPFLAG2_LINKEDTOSAFE)
    {
        SafeObjectRecord *link = g_LevelLoadPropSafeItem;

        while (link)
        {
            ObjectRecord *loopobj = link->item;

            if (obj == link->item && link->door && link->door->prop)
            {
                if (link->door->openPosition <= 0.5f)
                {
                    return FALSE;
                }
            }

            link = link->next;
        }
    }

    return TRUE;
}


void sub_GAME_7F03E830(ObjectRecord* arg0)
{
    PropRecord* prop = arg0->prop;
    stanGetPositionYValue(prop->stan, prop->pos.x, prop->pos.z);
}

f32 chrpropBBOXGetXmin(ModelRoData_BoundingBoxRecord *modelBoundingBox)
{
    return modelBoundingBox->Bounds.xmin;
}

f32 chrpropBBOXGetYmin(ModelRoData_BoundingBoxRecord *modelBoundingBox)
{
    return modelBoundingBox->Bounds.ymin;
}

f32 chrpropBBOXGetYmax(ModelRoData_BoundingBoxRecord *modelBoundingBox)
{
    return modelBoundingBox->Bounds.ymax;
}
f32 chrpropBBOXGetZmin(ModelRoData_BoundingBoxRecord *modelBoundingBox)
{
    return modelBoundingBox->Bounds.zmin;
}







/**
 * Address 0x7F03E87C.
*/
f32 chrpropSumMatrixPosX(struct ModelRoData_BoundingBoxRecord *bbox, Mtxf *arg1)
{
    f32 phi_f2;

    phi_f2 = 0.0f;

    if (arg1->m[0][0] >= 0.0f)
    {
        phi_f2 += (bbox->Bounds.xmin * arg1->m[0][0]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.xmax * arg1->m[0][0]);
    }

    if (arg1->m[1][0] >= 0.0f)
    {
        phi_f2 += (bbox->Bounds.ymin * arg1->m[1][0]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.ymax * arg1->m[1][0]);
    }

    if (arg1->m[2][0] >= 0.0f)
    {
        phi_f2 += (bbox->Bounds.zmin * arg1->m[2][0]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.zmax * arg1->m[2][0]);
    }

    return phi_f2;
}




/**
 * Address 0x7F03E91C.
*/
f32 chrpropSumMatrixNegX(struct ModelRoData_BoundingBoxRecord *bbox, Mtxf *arg1)
{
    f32 phi_f2;

    phi_f2 = 0.0f;

    if (arg1->m[0][0] <= 0.0f)
    {
        phi_f2 += (bbox->Bounds.xmin * arg1->m[0][0]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.xmax * arg1->m[0][0]);
    }

    if (arg1->m[1][0] <= 0.0f)
    {
        phi_f2 += (bbox->Bounds.ymin * arg1->m[1][0]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.ymax * arg1->m[1][0]);
    }

    if (arg1->m[2][0] <= 0.0f)
    {
        phi_f2 += (bbox->Bounds.zmin * arg1->m[2][0]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.zmax * arg1->m[2][0]);
    }

    return phi_f2;
}




/**
 * Address 0x7F03E9BC.
*/
f32 chrpropSumMatrixPosY(struct ModelRoData_BoundingBoxRecord *bbox, Mtxf *arg1)
{
    f32 phi_f2;

    phi_f2 = 0.0f;

    if (arg1->m[0][1] >= 0.0f)
    {
        phi_f2 += (bbox->Bounds.xmin * arg1->m[0][1]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.xmax * arg1->m[0][1]);
    }

    if (arg1->m[1][1] >= 0.0f)
    {
        phi_f2 += (bbox->Bounds.ymin * arg1->m[1][1]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.ymax * arg1->m[1][1]);
    }

    if (arg1->m[2][1] >= 0.0f)
    {
        phi_f2 += (bbox->Bounds.zmin * arg1->m[2][1]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.zmax * arg1->m[2][1]);
    }

    return phi_f2;
}



/**
 * Address 0x7F03EA5C.
*/
f32 chrpropSumMatrixNegY(struct ModelRoData_BoundingBoxRecord *bbox, Mtxf *arg1)
{
    f32 phi_f2;

    phi_f2 = 0.0f;

    if (arg1->m[0][1] <= 0.0f)
    {
        phi_f2 += (bbox->Bounds.xmin * arg1->m[0][1]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.xmax * arg1->m[0][1]);
    }

    if (arg1->m[1][1] <= 0.0f)
    {
        phi_f2 += (bbox->Bounds.ymin * arg1->m[1][1]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.ymax * arg1->m[1][1]);
    }

    if (arg1->m[2][1] <= 0.0f)
    {
        phi_f2 += (bbox->Bounds.zmin * arg1->m[2][1]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.zmax * arg1->m[2][1]);
    }

    return phi_f2;
}



/**
 * Address 0x7F03EAFC.
*/
f32 chrpropSumMatrixPosZ(struct ModelRoData_BoundingBoxRecord *bbox, Mtxf *arg1)
{
    f32 phi_f2;

    phi_f2 = 0.0f;

    if (arg1->m[0][2] >= 0.0f)
    {
        phi_f2 += (bbox->Bounds.xmin * arg1->m[0][2]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.xmax * arg1->m[0][2]);
    }

    if (arg1->m[1][2] >= 0.0f)
    {
        phi_f2 += (bbox->Bounds.ymin * arg1->m[1][2]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.ymax * arg1->m[1][2]);
    }

    if (arg1->m[2][2] >= 0.0f)
    {
        phi_f2 += (bbox->Bounds.zmin * arg1->m[2][2]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.zmax * arg1->m[2][2]);
    }

    return phi_f2;
}



/**
 * Address 0x7F03EB9C.
*/
f32 chrpropSumMatrixNegZ(struct ModelRoData_BoundingBoxRecord *bbox, Mtxf *arg1)
{
    f32 phi_f2;

    phi_f2 = 0.0f;

    if (arg1->m[0][2] <= 0.0f)
    {
        phi_f2 += (bbox->Bounds.xmin * arg1->m[0][2]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.xmax * arg1->m[0][2]);
    }

    if (arg1->m[1][2] <= 0.0f)
    {
        phi_f2 += (bbox->Bounds.ymin * arg1->m[1][2]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.ymax * arg1->m[1][2]);
    }

    if (arg1->m[2][2] <= 0.0f)
    {
        phi_f2 += (bbox->Bounds.zmin * arg1->m[2][2]);
    }
    else
    {
        phi_f2 += (bbox->Bounds.zmax * arg1->m[2][2]);
    }

    return phi_f2;
}




/**
 * Unreferenced.
 * 0x7F03EC3C.
*/
void sub_GAME_7F03EC3C(struct ModelRoData_BoundingBoxRecord *bbox, Mtxf *arg1, struct coord3d *arg2)
{
    if (arg1->m[0][2] <= 0.0f)
    {
        arg2->f[0] = bbox->Bounds.xmin;
    }
    else
    {
        arg2->f[0] = bbox->Bounds.xmax;
    }

    if (arg1->m[1][2] <= 0.0f)
    {
        arg2->f[1] = bbox->Bounds.ymin;
    }
    else
    {
        arg2->f[1] = bbox->Bounds.ymax;
    }

    if (arg1->m[2][2] <= 0.0f)
    {
        arg2->f[2] = bbox->Bounds.zmin;
    }
    else
    {
        arg2->f[2] = bbox->Bounds.zmax;
    }
}


void sub_GAME_7F03ECC0(f32 x1, f32 x2, f32 y1, f32 y2, f32 z1, f32 z2, Mtxf *m, struct rect4f *poly, struct collision_data *collision)
{
    f64 pts[8][2];
    f64 pad[1];
    s32 i;
    s32 lim;
    s32 minxi = 0;
    s32 maxxi = 0;
    s32 minzi;
    s32 maxzi = 0;
    s32 rem[4];
    s32 cnt;
    f64 x1d = x1;
    f64 x2d = x2;
    f64 y1d = y1;
    f64 y2d = y2;
    f64 z1d = z1;
    f64 z2d = z2;
    f64 m00 = m->m[0][0];
    f64 m02 = m->m[0][2];
    f64 m10 = m->m[1][0];
    f64 m12 = m->m[1][2];
    f64 m20 = m->m[2][0];
    f64 m22 = m->m[2][2];
    minzi = 0;
    pts[0][0] = ((m00 * x1d) + (m10 * y1d)) + (m20 * z1d);
    pts[0][1] = ((m02 * x1d) + (m12 * y1d)) + (m22 * z1d);
    pts[1][0] = ((m00 * x1d) + (m10 * y1d)) + (m20 * z2d);
    pts[1][1] = ((m02 * x1d) + (m12 * y1d)) + (m22 * z2d);
    pts[2][0] = ((m00 * x1d) + (m10 * y2d)) + (m20 * z1d);
    pts[2][1] = ((m02 * x1d) + (m12 * y2d)) + (m22 * z1d);
    pts[3][0] = ((m00 * x1d) + (m10 * y2d)) + (m20 * z2d);
    pts[3][1] = ((m02 * x1d) + (m12 * y2d)) + (m22 * z2d);
    pts[4][0] = ((m00 * x2d) + (m10 * y1d)) + (m20 * z1d);
    pts[4][1] = ((m02 * x2d) + (m12 * y1d)) + (m22 * z1d);
    pts[5][0] = ((m00 * x2d) + (m10 * y1d)) + (m20 * z2d);
    pts[5][1] = ((m02 * x2d) + (m12 * y1d)) + (m22 * z2d);
    pts[6][0] = ((m00 * x2d) + (m10 * y2d)) + (m20 * z1d);
    pts[6][1] = ((m02 * x2d) + (m12 * y2d)) + (m22 * z1d);
    pts[7][0] = ((m00 * x2d) + (m10 * y2d)) + (m20 * z2d);
    pts[7][1] = ((m02 * x2d) + (m12 * y2d)) + (m22 * z2d);

    for (i = 1; i < 8; i++)
    {
        if ((pts[i][0] < pts[minxi][0]) || ((pts[i][0] == pts[minxi][0]) && (pts[i][1] < pts[minxi][1])))
        {
            minxi = i;
        }
    }

    for (i = 1; i < 8; i++)
    {
        if ((pts[maxzi][1] < pts[i][1]) || ((pts[i][1] == pts[maxzi][1]) && (pts[i][0] < pts[maxzi][0])))
        {
            maxzi = i;
        }
    }

    for (i = 1; i < 8; i++)
    {
        if ((pts[maxxi][0] < pts[i][0]) || ((pts[i][0] == pts[maxxi][0]) && (pts[maxxi][1] < pts[i][1])))
        {
            maxxi = i;
        }
    }

    for (i = 1; i < 8; i++)
    {
        if ((pts[i][1] < pts[minzi][1]) || ((pts[i][1] == pts[minzi][1]) && (pts[minzi][0] < pts[i][0])))
        {
            minzi = i;
        }
    }

    lim = 8;
    cnt = 0;
    i = 0;

filterloop:
    if ((((i != minxi) && (i != maxxi)) && (i != maxzi)) && (i != minzi))
    {
#ifdef TARGET_N64
        rem[cnt] = i;
        cnt++;
#else
        /* when extreme indices coincide (axis-aligned boxes) more than 4
         * corners survive the filter and rem[4] overflows (sanitizer catch
         * #9). Downstream reads only rem[0..3], so keeping the first four
         * matches the N64-effective behavior without the stack spill. */
        if (cnt < 4)
        {
            rem[cnt] = i;
            cnt++;
        }
#endif
    }

    i++;

    if (i < lim)
    {
        goto filterloop;
    }

    cnt = 0;
    poly->points[cnt].x = pts[minxi][0];
    poly->points[cnt].y = pts[minxi][1];
    cnt++;

    for (i = 0; i < 4; i++)
    {
        s32 index = rem[i];

        if (((pts[index][0] - pts[minzi][0]) * (pts[minxi][1] - pts[minzi][1])) < ((pts[minxi][0] - pts[minzi][0]) * (pts[index][1] - pts[minzi][1])))
        {
            poly->points[cnt].x = pts[index][0];
            poly->points[cnt].y = pts[index][1];
            cnt++;
            break;
        }
    }

    poly->points[cnt].x = pts[minzi][0];
    poly->points[cnt].y = pts[minzi][1];
    cnt++;

    for (i = 0; i < 4; i++)
    {
        s32 index = rem[i];

        if (((pts[index][0] - pts[maxxi][0]) * (pts[minzi][1] - pts[maxxi][1])) < ((pts[minzi][0] - pts[maxxi][0]) * (pts[index][1] - pts[maxxi][1])))
        {
            poly->points[cnt].x = pts[index][0];
            poly->points[cnt].y = pts[index][1];
            cnt++;
            break;
        }
    }

    poly->points[cnt].x = pts[maxxi][0];
    poly->points[cnt].y = pts[maxxi][1];
    cnt++;

    for (i = 0; i < 4; i++)
    {
        s32 index = rem[i];

        if (((pts[index][0] - pts[maxzi][0]) * (pts[maxxi][1] - pts[maxzi][1])) < ((pts[maxxi][0] - pts[maxzi][0]) * (pts[index][1] - pts[maxzi][1])))
        {
            poly->points[cnt].x = pts[index][0];
            poly->points[cnt].y = pts[index][1];
            cnt++;
            break;
        }
    }

    poly->points[cnt].x = pts[maxzi][0];
    poly->points[cnt].y = pts[maxzi][1];
    cnt++;

    for (i = 0; i < 4; i++)
    {
        s32 index = rem[i];

        if (((pts[index][0] - pts[minxi][0]) * (pts[maxzi][1] - pts[minxi][1])) < ((pts[maxzi][0] - pts[minxi][0]) * (pts[index][1] - pts[minxi][1])))
        {
            poly->points[cnt].x = pts[index][0];
            poly->points[cnt].y = pts[index][1];
            cnt++;
            break;
        }
    }

    collision->edges = cnt;

    for (i = 0; i < cnt; i++)
    {
        poly->points[i].x += m->m[3][0];
        poly->points[i].y += m->m[3][2];
    }
}


void sub_GAME_7F03F540(struct ModelRoData_BoundingBoxRecord *bbox, Mtxf* arg1, struct rect4f* arg2, struct collision_data* arg3)
{
    sub_GAME_7F03ECC0(bbox->Bounds.xmin, bbox->Bounds.xmax, bbox->Bounds.ymin, bbox->Bounds.ymax, bbox->Bounds.zmin, bbox->Bounds.zmax, arg1, arg2, arg3);
}


/**
 * Address: 7F03F598
 *
 * Tests whether a world-space point is inside a bound pad's local bbox plus
 * a padding on all axes defined by the radius parameter.
 */
bool chrpropTestPointInPaddedBoundPad(coord3d *pos, f32 radius, BoundPadRecord *pad)
{
    f32 dx;
    f32 dy;
    f32 dz;
    f32 side[3];
    f32 d;

    dx = pos->x - pad->pos.x;
    dy = pos->y - pad->pos.y;
    dz = pos->z - pad->pos.z;

    side[0] = (pad->up.y * pad->look.z) - (pad->look.y * pad->up.z);
    side[1] = (pad->up.z * pad->look.x) - (pad->look.z * pad->up.x);
    side[2] = (pad->up.x * pad->look.y) - (pad->look.x * pad->up.y);

    d = (pad->look.z * dz) + ((dx * pad->look.x) + (dy * pad->look.y));
    if ((pad->bbox.zmax + radius < d) || (d < pad->bbox.zmin - radius))
    {
        return FALSE;
    }

    d = (pad->up.z * dz) + ((dx * pad->up.x) + (dy * pad->up.y));
    if ((pad->bbox.ymax + radius < d) || (d < pad->bbox.ymin - radius))
    {
        return FALSE;
    }

    d = (dx * side[0]) + (dy * side[1]) + (side[2] * dz);
    if ((pad->bbox.xmax + radius < d) || (d < pad->bbox.xmin - radius))
    {
        return FALSE;
    }

    return TRUE;
}


/*
* Address: 7F03F748
*/
void modelGetAxisExtents(Model* model, f32* max, f32* min, s32 axis)
{
    ModelNode *node = model->obj->RootNode;
    bool first = TRUE;

    while (node)
    {
        u32 type = node->Opcode & 0xFF;

        if (type == MODELNODE_OPCODE_BBOX)
        {
            struct ModelRoData_BoundingBoxRecord *bbox = &node->Data->BoundingBox;
            Mtxf *mtx = modelFindNodeMtx(model, node, 0);
            f32 dist1;
            f32 dist2;

            if (axis == 0)
            {
                dist1 = chrpropSumMatrixNegX(bbox, mtx) + mtx->m[3][0];
                dist2 = chrpropSumMatrixPosX(bbox, mtx) + mtx->m[3][0];
            }
            else if (axis == 1)
            {
                dist1 = chrpropSumMatrixNegY(bbox, mtx) + mtx->m[3][1];
                dist2 = chrpropSumMatrixPosY(bbox, mtx) + mtx->m[3][1];
            }
            else
            {
                dist1 = chrpropSumMatrixNegZ(bbox, mtx) + mtx->m[3][2];
                dist2 = chrpropSumMatrixPosZ(bbox, mtx) + mtx->m[3][2];
            }

            if (first || dist1 > *max)
            {
                *max = dist1;
            }

            if (first || dist2 < *min)
            {
                *min = dist2;
            }

            first = FALSE;
        }
        else
        {
            // empty
        }

        if (node->Child)
        {
            node = node->Child;
        }
        else
        {
            while (node)
            {
                if (node->Next)
                {
                    node = node->Next;
                    break;
                }

                node = node->Parent;
            }
        }
    }
}


void modelGetXYExtents(Model *model, f32 *arg1, f32 *arg2, f32 *arg3, f32 *arg4)
{
    modelGetAxisExtents(model, arg1, arg2, 0);
    modelGetAxisExtents(model, arg3, arg4, 1);
}


/**
 * NTSC address 0x7F03F948.
 * Project rectangle corners to screen
*/
void projectRectCornersTo2D(struct coord3d *center, struct coord2d *arg1, struct coord2d *arg2, struct coord2d *arg3, struct coord2d *arg4)
{
    struct coord3d sp24;
    struct coord2d tout;

    sp24.f[0] = arg1->f[0];
    sp24.f[1] = center->f[1];
    sp24.f[2] = center->f[2];
    transform3Dto2DCoords(&sp24, &tout);
    arg3->f[0] = tout.f[0];

    sp24.f[0] = arg1->f[1];
    sp24.f[1] = center->f[1];
    sp24.f[2] = center->f[2];
    transform3Dto2DCoords(&sp24, &tout);
    arg4->f[0] = tout.f[0];

    sp24.f[0] = center->f[0];
    sp24.f[1] = arg2->f[1];
    sp24.f[2] = center->f[2];
    transform3Dto2DCoords(&sp24, &tout);
    arg3->f[1] = tout.f[1];

    sp24.f[0] = center->f[0];
    sp24.f[1] = arg2->f[0];
    sp24.f[2] = center->f[2];
    transform3Dto2DCoords(&sp24, &tout);
    arg4->f[1] = tout.f[1];
}




/*
* Address: 0x7F03FA44
*/
ObjectRecord *scan_position_data_table_for_normal_object_at_preset(s32 PadId) {
    PropRecord *prop;
    s16 tempPadId = PadId;

    prop = chrpropGetActiveTail();
    while (prop != NULL)
    {
        if (prop->type == PROP_TYPE_OBJ)
        {
            if (tempPadId == prop->obj->pad)
            {
                return prop->obj;
            }
        }

        prop = prop->prev;
    }

    return NULL;
}




ObjectRecord * sub_GAME_7F03FAB0(struct coord3d *pos, s32 RoomID)
{
    s32 unused;
    rect4f * polygon;
    s32 edges;
    PropRecord * prop;

    prop = chrpropGetActiveTail();
    while (prop != NULL)
    {
        if ((prop->type == PROP_TYPE_OBJ) && (RoomID == prop->stan->room))
        {
            chraiGetCollisionBoundsWithoutY(prop, &polygon, &edges);
            if (chrpropTestPointInPolygon(pos, polygon, edges) != 0)
            {
                return (ObjectRecord *) prop->chr;
            }
        }
        prop = prop->prev;
    }

    return NULL;
}

