#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "memp.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/gun.h"
#include "game/initanitable.h"
#include "game/model.h"
#include "game/propobj.h"
#include "game/stan.h"

extern void play_hit_soundeffect_and_proper_volume(ChrRecord *);
extern void triggered_on_shot_hit(ChrRecord *, coord3d *, f32, s32, ITEM_IDS);
extern f32 chrlvModelScaleAnimationRelated(ChrRecord *);
extern void set_actor_on_path(ChrRecord *, struct patrol_path *);
struct stagesetup g_CurrentSetup;

static unsigned failures;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)

ChrModelFileRecord c_item_entries[1];
unsigned char _animation_entriesSegmentRomStart[1];
f32 D_80030984 = 1, D_80030988 = 2, D_8003098C = 3;
f32 D_80030990 = 4, D_80030994 = 5, D_80030998 = 6;
f32 D_8003099C = 7, D_800309A0 = 8, D_800309A4 = 9;
static PropRecord *weapons[2];
static u32 randomValues[8], randomIndex;
s32 g_GlobalTimer;
struct ChrHitReaction g_HitReactionTable[15] = {{.hitpart = -1}};
struct StruckAnim death_stagger[2];
static f32 lastEndFrame;

void ge_dbg_vec(const char *tag, const f32 *v, s32 n) { (void)tag; (void)v; (void)n; abort(); }
u8 getROOMID_isRendered(s32 room) { (void)room; abort(); }
void setsubroty(Model *model, f32 angle) { (void)model; (void)angle; abort(); }
s32 sub_GAME_7F0B0D0C(StandTile *tile, f32 x, f32 z, StandTile **dest, f32 dx, f32 dz, s32 *rooms, s32 count)
{ (void)tile; (void)x; (void)z; (void)dest; (void)dx; (void)dz; (void)rooms; (void)count; abort(); }

/* Death/pathing branches are outside this hit-reaction fixture. */
void chrGetChrWidthHeight(PropRecord *p, f32 *w, f32 *h, f32 *b) { (void)p; (void)w; (void)h; (void)b; abort(); }
void chrSetMoving(ChrRecord *chr, s32 moving) { (void)chr; (void)moving; abort(); }
void stanResetHits(void) { abort(); }
s32 getCollisionEdge_maybe(coord3d *a, coord3d *b) { (void)a; (void)b; abort(); }
s32 stanTestLineUnobstructed(StandTile **tile, f32 x, f32 z, f32 dx, f32 dz,
    int cdtypes, f32 height, f32 a, f32 b, f32 c)
{
    (void)tile; (void)x; (void)z; (void)dx; (void)dz;
    (void)cdtypes; (void)height; (void)a; (void)b; (void)c; abort();
}
void weaponSetGunfireVisible(PropRecord *prop, s32 firing) { (void)prop; (void)firing; }

s32 getPlayerCount(void) { return 1; }
f32 get_007_reaction_speed(void) { return 0; }
f32 getsubroty(Model *model) { (void)model; abort(); }
f32 bondwalkItemGetForceOfImpact(ITEM_IDS item) { (void)item; abort(); }
void chrDropItems(ChrRecord *chr) { (void)chr; abort(); }
void increment_num_kills_display_text_in_MP(void) { abort(); }
void inc_cur_civilian_casualties(void) { abort(); }
void propobjSetDropped(PropRecord *prop, DROPTYPE type) { (void)prop; (void)type; abort(); }
void modelSetAnimEndFrame(Model *model, f32 frame) { (void)model; lastEndFrame = frame; }
void modelSetAnimationWithMerge(Model *model, ModelAnimation *anim, s32 flip,
    f32 frame, f32 speed, f32 merge, s32 domerge)
{
    (void)domerge;
    modelSetAnimation(model, anim, flip, frame, speed, merge);
}
static void *allocations[32];
static unsigned numAllocations;

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    void *p = calloc(1, bytes);
    (void)bank;
    if (!p || numAllocations == 32) abort();
    allocations[numAllocations++] = p;
    return p;
}

u32 randomGetNext(void) { return randomValues[randomIndex++ % 8]; }
PropRecord *chrGetEquippedWeaponProp(ChrRecord *chr, GUNHAND hand)
{
    (void)chr;
    return weapons[hand];
}
u32 bondwalkItemCheckBitflags(ITEM_IDS item, u32 mask)
{
    return item == ITEM_WPPK && mask == WEAPONSTATBITFLAG_ONLY_1_HANDED;
}
ModelAnimation *objecthandlerGetModelAnim(Model *model) { return model->anim; }
void modelSetAnimation(Model *model, ModelAnimation *anim, s32 flip,
    f32 frame, f32 speed, f32 merge)
{
    (void)flip; (void)frame; (void)speed; (void)merge;
    model->anim = portAnimationResolve(anim);
}

static void testMovement(s32 male, SPEED speed, uintptr_t expected)
{
    ChrRecord chr = {0};
    Model model = {0};
    /* Poison the old N64 byte offset, which now lies inside a waypoint pointer. */
    memset(&chr.act_gopos, 0xa5, sizeof(chr.act_gopos));
    chr.model = &model;
    chr.act_gopos.unk59 = speed;
    c_item_entries[0].isMale = male;
    play_hit_soundeffect_and_proper_volume(&chr);
    CHECK(model.anim == portAnimationFromOffset(expected));
}

static void testTravelScale(uintptr_t offset, f32 factor)
{
    Model model = {0};
    ChrRecord chr = {0};
    model.anim = portAnimationFromOffset(offset);
    model.scale = 0.1f;
    chr.model = &model;
    f32 difference = chrlvModelScaleAnimationRelated(&chr) - factor;
    CHECK(difference > -0.0001f && difference < 0.0001f);
    CHECK(portAnimationResolve((ModelAnimation *)(ptr_animation_table->data + offset)) == model.anim);
}

static void testPatrol(void)
{
    ChrRecord chr = {0};
    Model model = {0};
    PropRecord prop = {0};
    PadRecord pad = {0};
    waypoint waypoint = {0};
    s32 steps[] = {0, -1};
    struct patrol_path path = {.data = steps};
    chr.prop = &prop; chr.model = &model;
    prop.flags = PROPFLAG_ONSCREEN;
    /* Avoid offscreen travel, which has a separate fixture. */
    prop.stan = pad.stan = (StandTile *)(uintptr_t)1;
    chr.act_patrol.waydata.segdisttotal = 123;
    chr.act_patrol.lastvisible60 = 999;
    g_CurrentSetup.pads = &pad; g_CurrentSetup.pathwaypoints = &waypoint;
    weapons[0] = weapons[1] = NULL;
    set_actor_on_path(&chr, &path);
    CHECK(chr.actiontype == ACT_PATROL);
    CHECK(chr.act_patrol.lastvisible60 == -1);
    CHECK(chr.act_patrol.waydata.segdisttotal == 123);
    CHECK(chr.act_patrol.nextstep == 0);
    CHECK(chr.act_patrol.path == &path);
}

static void testHitReaction(uintptr_t offset, s32 special, s32 longReaction, f32 explicitEnd)
{
    Model model = {0};
    ChrRecord chr = {0};
    PropRecord prop = {0};
    coord3d direction = {{0, 0, 1}};
    ModelAnimation *anim = portAnimationFromOffset(offset);
    struct StruckAnim flinch = {.struck_anim = anim, .endframe = explicitEnd, .speed = 0.5f};
    /* Deliberately disagree with the frame count at the old N64 offset. */
    anim->address = UINT64_C(0x123400000000);
    anim->unk04 = 123;
    memset(g_HitReactionTable, 0, sizeof(g_HitReactionTable));
    g_HitReactionTable[0].hitpart = special ? 7 : 1;
    g_HitReactionTable[0].flinchAnims = &flinch;
    g_HitReactionTable[0].flinchAnimCount = 1;
    g_HitReactionTable[1].hitpart = -1;
    prop.type = PROP_TYPE_CHR;
    chr.prop = &prop; chr.model = &model; chr.maxdamage = 100;
    weapons[0] = weapons[1] = NULL;
    memset(randomValues, 0, sizeof(randomValues));
    randomIndex = 0;
    randomValues[1] = 4; /* Choose the full-length special reaction. */
    randomValues[2] = longReaction;
    lastEndFrame = -1;
    triggered_on_shot_hit(&chr, &direction, special ? 3.0f : 0.0f, special ? 7 : 1, ITEM_WPPK);
    CHECK(chr.actiontype == ACT_ARGH);
    CHECK(model.anim == anim);
    CHECK(lastEndFrame == (explicitEnd >= 0 ? explicitEnd : 122));
}

int main(void)
{
    WeaponObjRecord weapon = {0};
    PropRecord prop = {0};
    ptr_animation_table = calloc(1, sizeof(*ptr_animation_table));
    if (!ptr_animation_table) abort();

    testMovement(0, SPEED_WALK, PTR_ANIM_walking_female);
    testMovement(0, SPEED_RUN, PTR_ANIM_running_female);
    testMovement(0, SPEED_SPRINT, PTR_ANIM_sprinting_one_handed_weapon);
    testMovement(1, SPEED_WALK, PTR_ANIM_walking_unarmed);
    testMovement(1, SPEED_RUN, PTR_ANIM_running_one_handed_weapon);
    testMovement(1, SPEED_SPRINT, PTR_ANIM_sprinting_one_handed_weapon);

    prop.weapon = &weapon;
    weapons[GUNRIGHT] = &prop;
    weapon.weaponnum = ITEM_AK47;
    testMovement(0, SPEED_WALK, PTR_ANIM_walking);
    testMovement(0, SPEED_RUN, PTR_ANIM_running);
    testMovement(0, SPEED_SPRINT, PTR_ANIM_sprinting);
    weapon.weaponnum = ITEM_WPPK;
    testMovement(0, SPEED_RUN, PTR_ANIM_running_female);
    weapons[GUNLEFT] = &prop;
    testMovement(0, SPEED_SPRINT, PTR_ANIM_sprinting_one_handed_weapon);

    testTravelScale(PTR_ANIM_walking, D_80030984);
    testTravelScale(PTR_ANIM_running, D_80030988);
    testTravelScale(PTR_ANIM_sprinting, D_8003098C);
    testTravelScale(PTR_ANIM_walking_unarmed, D_80030990);
    testTravelScale(PTR_ANIM_running_one_handed_weapon, D_80030994);
    testTravelScale(PTR_ANIM_sprinting_one_handed_weapon, D_80030998);
    testTravelScale(PTR_ANIM_walking_female, D_8003099C);
    testTravelScale(PTR_ANIM_running_female, D_800309A0);

    testPatrol();
    testHitReaction(PTR_ANIM_hit_butt_long, 1, 1, -1);
    testHitReaction(PTR_ANIM_hit_butt_short, 1, 0, -1);
    testHitReaction(PTR_ANIM_hit_left_leg, 0, 0, -1);
    testHitReaction(PTR_ANIM_hit_left_leg, 0, 0, 42);

    for (unsigned i = 0; i < numAllocations; i++) free(allocations[i]);
    free(ptr_animation_table);
    if (failures) return EXIT_FAILURE;
    puts("native movement tests passed");
    return 0;
}
