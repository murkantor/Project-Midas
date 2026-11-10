#include <ultra64.h>
#include "game/loadobjectmodel.h"
#ifndef TARGET_N64
#include <stdio.h> /* MP startpad/spawn debug log */
#endif
#include <memp.h>
#include <bondconstants.h>
#include <boss.h>
#include "math_atan2f.h"
#include "bondview_r.h"
#include "bondview.h"
#include "random.h"
#include "game/bondinv.h"
#include "game/chrai.h"
#include "game/front.h"
#include "game/gun.h"
#include "game/language.h"
#include "game/player.h"
#include "game/ramromreplay.h"
#include "game/stan.h"


/**
 * Address 0x8002A780.
*/
struct coord3d default_start_position = { 0 };

u32 weaponLoadProjectileModels(ITEM_IDS modelid)
{
    s32 model;

    model = -1;
    switch(modelid)
    {
    case ITEM_THROWKNIFE:
        model = PROJECTILES_TYPE_KNIFE;
        break;
    case ITEM_GRENADELAUNCH:
        model = PROJECTILES_TYPE_GLAUNCH_ROUND;
        break;
    case ITEM_ROCKETLAUNCH:
        model = PROJECTILES_TYPE_ROCKET_ROUND;
        break;
    case ITEM_GRENADE:
        model = PROJECTILES_TYPE_GRENADE;
        break;
    case ITEM_TIMEDMINE:
        model = PROJECTILES_TYPE_TIMED_MINE;
        break;
    case ITEM_PROXIMITYMINE:
        model = PROJECTILES_TYPE_PROX_MINE;
        break;
    case ITEM_REMOTEMINE:
        model = PROJECTILES_TYPE_REMOTE_MINE;
        break;
    case ITEM_TANKSHELLS:
        model = PROJECTILES_TYPE_ROCKET_ROUND2;
        break;
    case ITEM_BOMBCASE:
        model = PROJECTILES_TYPE_BOMBCASE;
        break;
    case ITEM_PLASTIQUE:
        model = PROJECTILES_TYPE_PLASTIQUE;
        break;
    case ITEM_BUG:
        model = PROJECTILES_TYPE_BUG;
        break;
    case ITEM_MICROCAMERA:
        model = PROJECTILES_TYPE_MICROCAMERA;
    }

    if (-1 < model)
    {
        return modelLoad(model);
    }
    return 0;
}

void bondviewLoadSetupIntroSection(void)
{

#define FLOAT_INIT 0


#if defined(VERSION_EU)
#define FIELD_6C_FACTOR 0.20039999485f
#define FIELD_3B8_FACTOR 0.118799984455f
#else
#define FIELD_6C_FACTOR 0.170000016689f
#define FIELD_3B8_FACTOR 0.100000023842f
#endif

    // declarations

    struct coord3d start_pos;
    f32 start_look_angle;
    StandTile *start_stan;
    struct SetupIntroEmpty *intro_record;
    s32 set_starting_weapon;
    s32 rand_camera_index;
    CreditsEntry *credits;
    s32 rand_pad_index;
    f32 stan_height;
    s32 i;
    struct SetupIntroItem *intro_item;
    struct SetupIntroSwirl *intro_swirl;
    struct SetupIntroWatch *intro_watch;
    struct SetupIntroCredits *intro_credits;
    s32 padding[5];

    // done with declarations

    start_pos = default_start_position;

    intro_record = (struct SetupIntroEmpty *)g_CurrentSetup.intro;
    g_isBondKIA = 0;
    g_bondviewForceDisarm = 0;
    resolution = 0;
    cameraBufferToggle = 0;
    cameraFrameCounter1 = 0;
    set_starting_weapon = 0;
    cameraFrameCounter2 = 0;
    start_look_angle = FLOAT_INIT;

    if (bossGetStageNum() == LEVELID_CUBA)
    {
        resolution = (uintptr_t)mempAllocBytesInBank(0x46EA0, MEMPOOL_STAGE);
        resolution = (resolution + 0x3f) & ~0x3F;
        cameraFrameCounter1 = 1;
    }

    camera_80036438 = 0;
    credits_state = 0;
    credits_pointer = NULL;
    g_ForceBondMoveOffset.f[0] = FLOAT_INIT;
    g_ForceBondMoveOffset.f[1] = FLOAT_INIT;
    g_ForceBondMoveOffset.f[2] = FLOAT_INIT;
    g_SurroundBondWithExplosionsFlag = 0;
    startpadcount = 0;
    g_PlayerIsInTank = 0;
    g_WorldTankProp = 0;
    g_PlayerTankProp = NULL;
    g_PlayerTankYOffset = FLOAT_INIT;
    g_TankSfxState[0] = NULL;
    g_TankSfxState[1] = NULL;
    g_TankTurnSpeed = FLOAT_INIT;
    g_TankOrientationAngle = FLOAT_INIT;
    tank_turret_unused_angle = FLOAT_INIT;
    g_TankTurretVerticalAngle = FLOAT_INIT;
    g_TankTurretVerticalAngleRelated = FLOAT_INIT;
    g_TankTurretOrientationAngleRad = FLOAT_INIT;
    g_TankTurretOrientationAngleDeg = FLOAT_INIT;
    tank_turret_turn_speed = FLOAT_INIT;
    g_BondCanEnterTank = 0;
    g_TankTurretAngle = FLOAT_INIT;
    g_TankTurretTurn = FLOAT_INIT;
    g_ExplodeTankOnDeathFlag = 0;
    is_timer_active = 1;
    g_PlayerInvincible = FALSE;
    g_CameraMode = 0;
    g_CameraAfterCinema = 0;
    camera_fade_active = 0;
    stop_time_flag = 0;
    camera_transition_timer = FLOAT_INIT;
    intro_camera_index = CAMERAMODE_INTRO;
    g_IntroSwirl = NULL;
    ptr_random06cam_entry = NULL;
    g_CurrentSetupIntroCamera = NULL;
    g_SetupIntroCameraCount = 0;
    mission_timer = 0;
    watch_time_0 = 0;
    g_IntroAnimationIndex = 0;
    watch_transition_time = 0.9090909f;
    starting_weapon[GUNLEFT] = ITEM_UNARMED;
    starting_weapon[GUNRIGHT] = ITEM_UNARMED;

    if (intro_record != NULL)
    {
        while (INTRO_TYPE(intro_record) != INTROTYPE_END)
        {
            switch (INTRO_TYPE(intro_record))
            {
                case INTROTYPE_SPAWN:
                {
                    if (g_CurrentSetup.pads != NULL
                        && (check_ramrom_flags() == ((struct SetupIntroSpawn*)intro_record)->is_demo_playback))
                    {
                        g_Startpad[startpadcount] = &g_CurrentSetup.pads[((struct SetupIntroSpawn*)intro_record)->index];
#ifndef TARGET_N64
                        { /* MP: only pads with a real tile under them are spawnable,
                           * since some stages (e.g. Complex) ship broken start
                           * pads that would drop the player into the void. */
                            extern s32 getTileRoom(StandTile *tile);
                            extern s32 isPointInsideTriStandTileUnscaled_Maybe(StandTile *tile, f32 p_x, f32 p_z);
                            PadRecord *sp = g_Startpad[startpadcount];
                            s32 sound = (sp->stan != NULL) && (sp->stan->id != 0)
                                     && isPointInsideTriStandTileUnscaled_Maybe(sp->stan, sp->pos.f[0], sp->pos.f[2]);
                            fprintf(stderr,
                                    "ge: startpad %d = pad %d (%s) stanroom=%d pos=(%.0f %.0f %.0f)%s\n",
                                    startpadcount,
                                    ((struct SetupIntroSpawn*)intro_record)->index, sp->plink,
                                    (sp->stan != NULL) ? getTileRoom(sp->stan) : -1,
                                    sp->pos.f[0], sp->pos.f[1], sp->pos.f[2],
                                    sound ? "" : " - EXCLUDED (no sound tile under pad)");
                            fflush(stderr);
                            if (sound) {
                                startpadcount++;
                            }
                        }
#else
                        startpadcount++;
#endif
                    }

                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroSpawn));
                }
                break;

                case INTROTYPE_ITEM:
                {
                    intro_item = (struct SetupIntroItem*)intro_record;

                    if (check_ramrom_flags() == intro_item->is_demo_playback)
                    {
                        weaponLoadProjectileModels(intro_item->item_right);

                        if (intro_item->item_left >= 0)
                        {
                            weaponLoadProjectileModels(intro_item->item_left);
                            bondinvAddDoublesInvItem(intro_item->item_right, intro_item->item_left);
                        }
                        else
                        {
                            bondinvAddInvItem(intro_item->item_right);
                        }

                        if (set_starting_weapon == 0)
                        {
                            starting_weapon[GUNRIGHT] = intro_item->item_right;

                            if(intro_item->item_left);

                            set_starting_weapon = 1;

                            if (intro_item->item_left >= 0)
                            {
                                starting_weapon[GUNLEFT] = intro_item->item_left;
                            }
                        }
                    }

                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroItem));
                }
                break;

                case INTROTYPE_AMMO:
                {
                    if (check_ramrom_flags() == ((struct SetupIntroAmmo*)intro_record)->is_demo_playback)
                    {
                        give_cur_player_ammo(((struct SetupIntroAmmo*)intro_record)->ammo_type, ((struct SetupIntroAmmo*)intro_record)->ammo_amount);
                    }

                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroAmmo));
                }
                break;

                case INTROTYPE_SWIRL:
                {
                    intro_swirl = (struct SetupIntroSwirl*)intro_record;

                    if (g_IntroSwirl == NULL)
                    {
                        g_IntroSwirl = intro_swirl;
                    }

                    intro_swirl->unk08.fval = intro_swirl->unk08.ival / M_U16_MAX_VALUE_F;
                    intro_swirl->unk0C.fval = intro_swirl->unk0C.ival / M_U16_MAX_VALUE_F;
                    intro_swirl->unk10.fval = intro_swirl->unk10.ival / M_U16_MAX_VALUE_F;
                    intro_swirl->unk14.fval = intro_swirl->unk14.ival / M_U16_MAX_VALUE_F;
                    intro_swirl->unk18.fval = intro_swirl->unk18.ival / M_U16_MAX_VALUE_F;

                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroSwirl));
                }
                break;

                case INTROTYPE_ANIM:
                {
                    g_IntroAnimationIndex = ((struct SetupIntroAnim*)intro_record)->intro_anim;

                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroAnim));
                }
                break;

                case INTROTYPE_CUFF:
                {
                    g_CurrentPlayer->bondtype = ((struct SetupIntroCuff*)intro_record)->bondtype;

#ifndef TARGET_N64
                    /* midas.ini [display] adzy: force the snow cuff regardless of the
                     * level intro; the matching body override is in
                     * solo_char_load (bondview2.c). Cosmetic only. */
                    {
                        extern s32 portAdzyMode(void);

                        if (portAdzyMode())
                        {
                            g_CurrentPlayer->bondtype = CUFF_SNOW;
                        }
                    }
#endif

                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroCuff));
                }
                break;

                case INTROTYPE_CAMERA:
                {
                    if (get_cur_playernum() == 0)
                    {
                        ((struct SetupIntroCamera*)intro_record)->prev = g_CurrentSetupIntroCamera;
                        g_CurrentSetupIntroCamera = (struct SetupIntroCamera*)intro_record;
                        g_SetupIntroCameraCount = g_SetupIntroCameraCount + 1;

                        ((struct SetupIntroCamera*)intro_record)->unk04.fval = ((struct SetupIntroCamera*)intro_record)->unk04.ival / 100.0f;
                        ((struct SetupIntroCamera*)intro_record)->unk08.fval = ((struct SetupIntroCamera*)intro_record)->unk08.ival / 100.0f;
                        ((struct SetupIntroCamera*)intro_record)->unk0C.fval = ((struct SetupIntroCamera*)intro_record)->unk0C.ival / 100.0f;
                        ((struct SetupIntroCamera*)intro_record)->unk10.fval = ((struct SetupIntroCamera*)intro_record)->unk10.ival / M_U16_MAX_VALUE_F;
                        ((struct SetupIntroCamera*)intro_record)->unk14.fval = ((struct SetupIntroCamera*)intro_record)->unk14.ival / M_U16_MAX_VALUE_F;

#ifdef TARGET_N64
                        ((struct SetupIntroCamera*)intro_record)->lang1c.lang_ptr = langGet(((struct SetupIntroCamera*)intro_record)->lang1c.lang_index[1]);
#else
                        /* PORT: the text id sits in the low half of the raw word,
                         * which u16[1] only reaches on big-endian, so read it explicitly. */
                        ((struct SetupIntroCamera*)intro_record)->lang1c.lang_ptr = langGet(((struct SetupIntroCamera*)intro_record)->lang1c.lang_index[0]);
#endif

                        if (((struct SetupIntroCamera*)intro_record)->lang20.lang_index != 0)
                        {
                            ((struct SetupIntroCamera*)intro_record)->lang20.lang_ptr = langGet((u16)((struct SetupIntroCamera*)intro_record)->lang20.lang_index);
                        }
                    }

                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroCamera));
                }
                break;

                case INTROTYPE_WATCH:
                {
                    intro_watch = (struct SetupIntroWatch*)intro_record;

                    watch_time_0 = 0;

                    if (intro_watch->minutes > 0)
                    {
                        watch_time_0 += (intro_watch->minutes % 60) * (60*60);
                    }

                    if (intro_watch->hours > 0)
                    {
                        watch_time_0 += ((intro_watch->hours % 12) * (60*60*60));
                    }

                    if (watch_time_0);

                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroWatch));
                }
                break;

                case INTROTYPE_CREDITS:
                {
                    intro_credits = (struct SetupIntroCredits*)intro_record;

#ifdef TARGET_N64
                    // hack: bad address math
                    credits = (CreditsEntry*)((s32)g_ptrStageSetupFile + (s32)intro_credits->unk04);
#else
                    /* PORT: the compiled-in setup already stores a real pointer
                     * (&credits_data_0), so skip the N64 base+offset rebase
                     * that broke the Cuba credits. */
                    credits = (CreditsEntry*)intro_credits->unk04;
#endif
                    credits_pointer = credits;

                    // what is the point of this?
                    while (credits->TextId1 != 0 || credits->TextId2 != 0)
                    {
                        credits++;
                    }

                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroCredits));
                }
                break;

                default:
                {
                    #ifdef DEBUG
                        ossyncprintf("unknown bondstart type %d!\n",INTRO_TYPE(intro_record));
                    #endif
                    intro_record = (struct SetupIntroEmpty*)((intptr_t)intro_record + sizeof(struct SetupIntroEmpty));
                }
                break;

            }
        }
    }

    if (g_CurrentSetupIntroCamera != NULL)
    {
        ptr_random06cam_entry = g_CurrentSetupIntroCamera;
        rand_camera_index = (s32)(randomGetNext() % (u32) g_SetupIntroCameraCount);
        while (rand_camera_index > 0)
        {
            rand_camera_index--;
            ptr_random06cam_entry = ptr_random06cam_entry->prev;
        }
    }

    bondinvAddInvItem(ITEM_FIST);

    if (set_starting_weapon == 0)
    {
        starting_weapon[GUNRIGHT] = ITEM_FIST;
    }
#ifndef TARGET_N64
    { /* Spawn with Weapon (bondview2.c) applies to the first spawn too; every
       * player gets the same set's slot 1, which the FP camera then equips. */
        extern s32 portMpSpawnWithWeapon(void);
        const s32 item = portMpSpawnWithWeapon();

        if (item != ITEM_UNARMED)
        {
            starting_weapon[GUNRIGHT] = item;
        }
    }
#endif

    g_CurrentPlayer->field_78 = FLOAT_INIT;
    g_CurrentPlayer->field_7C = -0.0001f;
    g_CurrentPlayer->field_80 = FLOAT_INIT;

    if (startpadcount > 0)
    {
        if ((getPlayerCount() >= 2) && (startpadcount > 0))
        {
            rand_pad_index = bondviewGetRandomSpawnPadIndex();
        }
        else
        {
            rand_pad_index = 0;
        }

#ifdef DEBUG
        assert(g_Startpad[rand_pad_index]->stan); //              (".\\ported\\bondview_r.cpp",0x171,"Assertion failed: g_Startpad[sp]->stan");
#endif

        start_pos.f[0] = g_Startpad[rand_pad_index]->pos.f[0];
        start_pos.f[2] = g_Startpad[rand_pad_index]->pos.f[2];


        start_stan = g_Startpad[rand_pad_index]->stan;

#ifndef TARGET_N64
        { /* MP debug: initial spawn placement (mirror of the respawn log) */
            extern s32 getTileRoom(StandTile *tile);
            fprintf(stderr,
                    "ge: mp spawn p%d startpad=%d (%s) stanroom=%d pos=(%.0f ? %.0f)\n",
                    get_cur_playernum(), rand_pad_index, g_Startpad[rand_pad_index]->plink,
                    (start_stan != 0) ? getTileRoom((StandTile *) start_stan) : -1,
                    start_pos.f[0], start_pos.f[2]);
            fflush(stderr);
        }
#endif

        stan_height = bondviewYPositionRelated(start_stan, start_pos.f[0], start_pos.f[2]);
        start_pos.f[1] = g_CurrentPlayer->eyeheight + stan_height;
        g_CurrentPlayer->field_70 = stan_height;
        start_look_angle = M_TAU_F - atan2f(g_Startpad[rand_pad_index]->look.f[0], g_Startpad[rand_pad_index]->look.f[2]);

#ifndef TARGET_N64
        { /* GE_SPAWNXYZ="x,y,z[,heading_deg[,pitch_deg]]" test harness: spawn
           * Bond at arbitrary coords (with optional heading and pitch in
           * degrees), resolving the tile like the no-startpad branch does. */
            extern char *getenv(const char *);
            const char *sp_e = getenv("GE_SPAWNXYZ");
            if (sp_e != NULL && sp_e[0] != '\0')
            {
                f32 sx, sy, sz, sh, sv;
                s32 n = sscanf(sp_e, "%f,%f,%f,%f,%f", &sx, &sy, &sz, &sh, &sv);
                if (n >= 3)
                {
                    /* With stacked storeys the closest tile can be a floor too
                     * low, so probe a few heights around the requested Y and
                     * keep the candidate whose floor height is nearest it. */
                    static const f32 probeoffs[11] = { 0.0f, 40.0f, -40.0f, 120.0f, -120.0f,
                                                       260.0f, -260.0f, 460.0f, -460.0f,
                                                       800.0f, -800.0f };
                    void *ov_stan = NULL;
                    StandTile *best = NULL;
                    f32 bestdelta = 1.0e9f;
                    f32 besth = 0.0f;
                    f32 bestx = sx;
                    f32 bestz = sz;
                    s32 probe;

                    /* Hand-read coordinates often sit just off the walkable mesh,
                     * so try a small ring of nearby points and keep the best
                     * tile that contains the point at the requested height. */
                    static const f32 nudge[5] = { 0.0f, 50.0f, -50.0f, 110.0f, -110.0f };
                    s32 nx;
                    s32 nz;

                    for (probe = 0; probe < 11 * 25; probe++)
                    {
                        struct coord3d pp;
                        StandTile *walk;
                        f32 h;
                        f32 d;
                        f32 px;
                        f32 pz;

                        nx = (probe / 11) % 5;
                        nz = (probe / 11) / 5;
                        px = sx + nudge[nx];
                        pz = sz + nudge[nz];

                        pp.f[0] = px;
                        pp.f[1] = sy + probeoffs[probe % 11];
                        pp.f[2] = pz;

                        walk = (StandTile *) sub_GAME_7F0AFB78(&pp.f[0], &pp.f[1], &pp.f[2], 30.0f);
                        if (walk == NULL)
                        {
                            continue;
                        }
                        /* Try the resolver's own tile first, and only graph-walk if
                         * it doesn't contain the point, since the walk can
                         * wander off the storey being selected. */
                        if (!isPointInsideTriStandTileUnscaled_Maybe(walk, px, pz))
                        {
                            StandTile *walked = walk;

                            walkTilesBetweenPoints_NoCallback(&walked, pp.f[0], pp.f[2], px, pz);
                            if (walked != NULL)
                            {
                                walk = walked;
                            }
                        }
                        if (walk == NULL)
                        {
                            continue;
                        }
                        /* Only accept tiles that actually contain (sx,sz): an
                         * off-mesh spawn leaves Bond's tile stuck, which breaks
                         * movement tracking and every chr's chrCanSeeBond test. */
                        if (!isPointInsideTriStandTileUnscaled_Maybe(walk, px, pz))
                        {
                            continue;
                        }

                        h = bondviewYPositionRelated(walk, px, pz);
                        {   /* A Y read off the debug display may be eye height rather
                             * than floor height, so score each candidate
                             * against both and keep the better. */
                            f32 dfloor = (h > sy) ? (h - sy) : (sy - h);
                            f32 eye = h + g_CurrentPlayer->eyeheight;
                            f32 deye = (eye > sy) ? (eye - sy) : (sy - eye);

                            d = (deye < dfloor) ? deye : dfloor;
                        }

                        /* prefer the requested spot: a nudged candidate has to
                         * beat the height match by more than its own offset */
                        d += ((nudge[nx] < 0.0f) ? -nudge[nx] : nudge[nx]) * 0.25f;
                        d += ((nudge[nz] < 0.0f) ? -nudge[nz] : nudge[nz]) * 0.25f;

                        if (d < bestdelta)
                        {
                            bestdelta = d;
                            besth = h;
                            best = walk;
                            bestx = px;
                            bestz = pz;
                        }
                    }

                    if (best == NULL)
                    {
                        fprintf(stderr, "ge: spawnxyz: NO tile contains (%.0f,%.0f) - spawn is OFF-MESH, Bond's stan will not track him\n", sx, sz);
                    }
                    start_pos.f[0] = (best != NULL) ? bestx : sx;
                    start_pos.f[1] = sy;
                    start_pos.f[2] = (best != NULL) ? bestz : sz;
                    ov_stan = (best != NULL) ? (void *) best
                                             : sub_GAME_7F0AFB78(&start_pos.f[0], &start_pos.f[1], &start_pos.f[2], 30.0f);
                    if (ov_stan != NULL)
                    {
                        /* With stacked floors the resolver's closest tile can be
                         * the wrong storey, so walk the tile graph from its
                         * candidate to the requested x/z to find the tile
                         * actually under it. */
                        StandTile *walk = (StandTile *) ov_stan;
                        walkTilesBetweenPoints_NoCallback(&walk,
                            start_pos.f[0], start_pos.f[2], sx, sz);
                        start_stan = (walk != NULL) ? walk : ov_stan;
                        start_pos.f[0] = sx;
                        start_pos.f[2] = sz;
                    }
                    stan_height = bondviewYPositionRelated(start_stan, start_pos.f[0], start_pos.f[2]);
                    start_pos.f[1] = g_CurrentPlayer->eyeheight + stan_height;
                    g_CurrentPlayer->field_70 = stan_height;
                    if (n >= 4)
                    {
                        start_look_angle = (sh * M_TAU_F) / 360.0f;
                    }
                    if (n >= 5)
                    {
                        g_CurrentPlayer->vv_verta = sv;
                    }
                    fprintf(stderr, "ge: spawnxyz (%.0f %.0f %.0f) floor=%.0f askedY=%.0f dy=%.0f hdg=%s pitch=%s stan=%p\n",
                            start_pos.f[0], start_pos.f[1], start_pos.f[2],
                            stan_height, sy, stan_height - sy,
                            (n >= 4) ? "set" : "pad", (n >= 5) ? "set" : "default", ov_stan);
                }
            }
        }
#endif
    }
    else
    {
        start_stan = sub_GAME_7F0AFB78(&start_pos.f[0], &start_pos.f[1], &start_pos.f[2], 30.0f);
        stan_height = bondviewYPositionRelated(start_stan, start_pos.f[0], start_pos.f[2]);
        start_pos.f[1] = g_CurrentPlayer->eyeheight + stan_height;
        g_CurrentPlayer->field_70 = stan_height;
    }

#ifndef TARGET_N64
    { /* GE_SPAWNLOOK="heading_deg[,pitch_deg]": set only the look angles and
       * keep the vanilla pad spawn, since GE_SPAWNXYZ can land off-mesh and
       * draw the wrong rooms. */
        extern char *getenv(const char *);
        const char *lk_e = getenv("GE_SPAWNLOOK");
        if (lk_e != NULL && lk_e[0] != '\0')
        {
            f32 lh, lv;
            s32 ln = sscanf(lk_e, "%f,%f", &lh, &lv);
            if (ln >= 1)
            {
                start_look_angle = (lh * M_TAU_F) / 360.0f;
            }
            if (ln >= 2)
            {
                g_CurrentPlayer->vv_verta = lv;
            }
            fprintf(stderr, "ge: spawnlook hdg=%s pitch=%s (pad spawn kept)\n",
                    (ln >= 1) ? "set" : "pad", (ln >= 2) ? "set" : "default");
        }
    }
#endif
    g_CurrentPlayer->vv_theta = (start_look_angle * 360.0f) / M_TAU_F;
    g_CurrentPlayer->stanHeight = stan_height;
    g_CurrentPlayer->field_6C = stan_height / FIELD_6C_FACTOR;

#ifndef TARGET_N64
    { /* debug (GE_DUMP_BOND): solo spawn placement - a zero stan here
       * is the source of the NULL-tile crash in bondviewTryMoveToStan */
        extern void ge_dbg_vec(const char *tag, const f32 *v, s32 n);
        f32 v[6];
        v[0] = (f32) (u32) start_stan;
        v[1] = start_pos.f[0];
        v[2] = start_pos.f[1];
        v[3] = start_pos.f[2];
        v[4] = stan_height;
        v[5] = (f32) (u32) g_CurrentPlayer;
        ge_dbg_vec("bondspawn stan/x/y/z/h/plr", v, 6);
    }
#endif

    change_player_pos_to_target(&g_CurrentPlayer->field_488, &start_pos, start_stan);
    g_CurrentPlayer->field_488.theta_transform.f[0] = -sinf(start_look_angle);
    g_CurrentPlayer->field_488.theta_transform.f[1] = FLOAT_INIT;
    g_CurrentPlayer->field_488.theta_transform.f[2] = cosf(start_look_angle);
    sub_GAME_7F089718(D_800364D0);
    dword_CODE_bss_80079DA0 = 0;


    for (i=0; i<BSS_80079DA8_LENGTH; i++)
    {
        dword_CODE_bss_80079DA4 = 0;
        dword_CODE_bss_80079DA8[i] = 0;
    }

    bondviewResetIntroCameraMessageDialogs();
    bondviewResetUpperTextDisplay();
    g_CurrentPlayer->prop = chrpropAllocate();
    g_CurrentPlayer->prop->obj = NULL;
    g_CurrentPlayer->prop->type = PROP_TYPE_VIEWER;

    g_CurrentPlayer->prop->pos.f[0] =
        g_CurrentPlayer->bondprevpos.f[0] = start_pos.f[0];

    g_CurrentPlayer->prop->pos.f[1] =
        g_CurrentPlayer->bondprevpos.f[1] = start_pos.f[1];

    g_CurrentPlayer->prop->pos.f[2] =
        g_CurrentPlayer->bondprevpos.f[2] = start_pos.f[2];

    g_CurrentPlayer->prop->stan = start_stan;

    chrpropActivate(g_CurrentPlayer->prop);
    chrpropEnable(g_CurrentPlayer->prop);
    g_CurrentPlayer->field_3B8.f[0] = (g_CurrentPlayer->field_488.pos.f[0] / FIELD_3B8_FACTOR);
    g_CurrentPlayer->field_3B8.f[1] = (g_CurrentPlayer->field_488.pos.f[1] / FIELD_3B8_FACTOR);
    g_CurrentPlayer->field_3B8.f[2] = (g_CurrentPlayer->field_488.pos.f[2] / FIELD_3B8_FACTOR);

    if (getPlayerCount() == 1)
    {
        bondviewSetCameraMode(CAMERAMODE_INTRO);
    }
    else
    {
        bondviewSetCameraMode(CAMERAMODE_MP);
    }

    g_bondviewBondDeathAnimationsCount = 0;
    while (g_bondviewBondDeathAnimations[g_bondviewBondDeathAnimationsCount] != 0)
    {
        g_bondviewBondDeathAnimationsCount++;
    }

    g_CurrentPlayer->startnewbonddie = TRUE;
    g_CurrentPlayer->redbloodfinished = FALSE;
    g_CurrentPlayer->deathanimfinished = FALSE;
    camera_mode = CAMERAMODE_NONE;
}
