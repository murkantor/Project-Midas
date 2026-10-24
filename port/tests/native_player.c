#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "game/bondview.h"
#include "game/player.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/initanitable.h"
#include "game/model.h"
#include "game/unk_092E50.h"
#include "memp.h"

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); abort(); } } while (0)

extern void bondviewSelectCuff(Model *, ModelFileHeader *, s32);
extern s32 playerTick(PropRecord *);
extern Gfx MipMap2C_Something_Setup[], MipMap2C_Something2_Setup[];
struct player *g_CurrentPlayer, *g_playerPointers[4];
s32 g_ClockTimer, g_GlobalTickCount, g_PlayerTickCount;
s32 g_bondviewBondDeathAnimations[] = {PTR_ANIM_death_neck};
s32 g_bondviewBondDeathAnimationsCount = 1;
struct firing_anim_struct firing_animation_groups[4][6];
unsigned char _animation_entriesSegmentRomStart[1];
static ModelNode *cuffNodes;
static union ModelRwData cuffData[16];
static s32 folderBond;
static unsigned animationChanges;
static void *allocations[16];
static unsigned numAllocations;

void *mempAllocBytesInBank(u32 bytes, u8 bank)
{
    (void)bank;
    CHECK(numAllocations < 16);
    void *p = calloc(1, bytes);
    CHECK(p != NULL);
    allocations[numAllocations++] = p;
    return p;
}
void chrDetectRooms(ChrRecord *chr) { (void)chr; abort(); }
void chrpropDeregisterRoom(PropRecord *prop, s16 room) { (void)prop; (void)room; abort(); }
void chrpropRegisterRoom(PropRecord *prop, s16 room) { (void)prop; (void)room; abort(); }
Mtxf *currentPlayerGetViewToWorldMtxf(void) { abort(); }
void matrix_4x4_multiply_homogeneous(Mtxf *a, Mtxf *b, Mtxf *out) { (void)a; (void)b; (void)out; abort(); }
s32 fileGetBondForCurrentFolder(void) { return folderBond; }
union ModelRwData *modelGetNodeRwData(Model *model, ModelNode *node)
{
    (void)model;
    CHECK(node >= cuffNodes && node < cuffNodes + 16);
    return &cuffData[node - cuffNodes];
}
s32 getPlayerPointerIndex(PropRecord *prop) { (void)prop; return 1; }
s32 get_cur_playernum(void) { return 0; }
s32 get_player_position_in_shuffled(s32 player) { (void)player; return 0; }
s32 getPlayerCount(void) { return 2; }
s32 get_debug_render_raster(void) { return 1; }
s32 playerGetCrouchPos(s32 player) { (void)player; return 2; }
PropRecord *chrGetEquippedWeaponProp(ChrRecord *chr, GUNHAND hand) { (void)chr; (void)hand; return NULL; }
u32 bondwalkItemCheckBitflags(ITEM_IDS item, u32 mask) { (void)item; (void)mask; return 0; }
u32 randomGetNext(void) { return 0; }
void modelSetAnimation(Model *model, ModelAnimation *anim, s32 flip, f32 start, f32 speed, f32 merge)
{
    (void)flip; (void)start; (void)speed; (void)merge;
    animationChanges++;
    model->anim = portAnimationResolve(anim);
}
void modelSetAnimLooping(Model *model, f32 frame, f32 merge) { (void)merge; model->animlooping = 1; model->animloopframe = frame; }
void modelSetAnimEndFrame(Model *model, f32 frame) { (void)model; (void)frame; }
void modelSetAnimSpeed(Model *model, f32 speed, f32 merge) { (void)model; (void)speed; (void)merge; }
void chrlvUpdateAimendbackShoulders(ChrRecord *chr, struct weapon_firing_animation_table *table, s32 flip, s32 flag, f32 angle)
{ (void)chr; (void)table; (void)flip; (void)flag; (void)angle; }
void getsuboffset(Model *model, coord3d *offset) { (void)model; memset(offset, 0, sizeof(*offset)); }
void setsuboffset(Model *model, coord3d *offset) { (void)model; (void)offset; }
void setsubroty(Model *model, f32 angle) { (void)model; (void)angle; }
void chrSetFiring(ChrRecord *chr, s32 hand, s32 firing) { (void)chr; (void)hand; (void)firing; }
s32 chrTick(PropRecord *prop) { (void)prop; return 0; }
s32 sub_GAME_7F02D630(ChrRecord *chr, GUNHAND hand, coord3d *out) { (void)chr; (void)hand; (void)out; return 0; }

static void testCuffs(void)
{
    ModelNode nodes[16] = {0};
    ModelNode *switches[16];
    ModelFileHeader header = {0};
    Model model = {0};
    cuffNodes = nodes;
    for (unsigned i = 0; i < 16; i++) switches[i] = &nodes[i];
    header.Switches = switches;
    CUFF_TYPES types[] = {CUFF_BOILER, CUFF_BROSNAN, CUFF_CONNERY, CUFF_BLUE, CUFF_JUNGLE, CUFF_SNOW};
    /* Native pointer arrays must also work at nonzero switch indices. */
    for (unsigned start = 0; start <= 6; start += 6) {
        for (unsigned selected = 0; selected < 6; selected++) {
            memset(cuffData, 0x55, sizeof(cuffData));
            g_CurrentPlayer->bondtype = types[selected];
            bondviewSelectCuff(&model, &header, start);
            for (unsigned i = 0; i < 6; i++)
                CHECK(*(s32 *)&cuffData[start + i] == (i == selected));
        }
    }
    for (unsigned bond = 0; bond < 2; bond++) {
        folderBond = bond;
        g_CurrentPlayer->bondtype = CUFF_FOLDER;
        bondviewSelectCuff(&model, &header, 6);
        CHECK(*(s32 *)&cuffData[7] == !bond);
        CHECK(*(s32 *)&cuffData[8] == bond);
    }
}

static void testWater(void)
{
    Gfx first[13], second[13];
    memcpy(first, MipMap2C_Something_Setup, sizeof(first));
    memcpy(second, MipMap2C_Something2_Setup, sizeof(second));
    g_ClockTimer = 0;
    flt_CODE_bss_80079E80 = 20;
    flt_CODE_bss_80079E84 = 30;
    flt_CODE_bss_80079E88 = 0;
    sub_GAME_7F092E50();
    CHECK((MipMap2C_Something_Setup[2].words.w0 & 0xffffff) == (20 << 12 | 30));
    CHECK((MipMap2C_Something2_Setup[2].words.w0 & 0xffffff) == (20 << 12 | 30));
    CHECK((MipMap2C_Something_Setup[4].words.w0 & 0xff) == 128);
    CHECK((MipMap2C_Something2_Setup[4].words.w0 & 0xff) == 128);
    for (unsigned i = 0; i < 13; i++) {
        CHECK(first[i].words.w1 == MipMap2C_Something_Setup[i].words.w1);
        CHECK(second[i].words.w1 == MipMap2C_Something2_Setup[i].words.w1);
        if (i != 2 && i != 3 && i != 4) {
            CHECK(first[i].words.w0 == MipMap2C_Something_Setup[i].words.w0);
            CHECK(second[i].words.w0 == MipMap2C_Something2_Setup[i].words.w0);
        }
    }
}

static void testMultiplayerAnimation(void)
{
    Model model = {0};
    ChrRecord chr = {0};
    PropRecord prop = {0};
    struct weapon_firing_animation_table table = {0};
    struct player *remote = g_playerPointers[1];
    prop.chr = &chr; chr.prop = &prop; chr.model = &model;
    remote->prop = &prop; remote->bodyModel = &model;
    g_PlayerTickCount = 2;
    table.anim.anim = portAnimationFromOffset(PTR_ANIM_idle);
    CHECK((uintptr_t)table.anim.anim > UINT32_MAX);
    firing_animation_groups[2][0] = (struct firing_anim_struct){&table, 0, 1, -1, -1};
    animationChanges = 0;
    playerTick(&prop);
    CHECK(model.anim == table.anim.anim);
    CHECK((ModelAnimation *)remote->players_cur_animation == model.anim);
    CHECK(animationChanges == 1);
    playerTick(&prop);
    CHECK(animationChanges == 1);

    /* Offset-only entries and pointer-backed entries share one identity. */
    firing_animation_groups[2][0].anim = PTR_ANIM_running;
    playerTick(&prop);
    CHECK(model.anim == portAnimationFromOffset(PTR_ANIM_running));
    CHECK(animationChanges == 2);
    playerTick(&prop);
    CHECK(animationChanges == 2);
    remote->bonddead = 1;
    playerTick(&prop);
    CHECK(model.anim == portAnimationFromOffset(PTR_ANIM_death_neck));
    CHECK(animationChanges == 3);
    playerTick(&prop);
    CHECK(animationChanges == 3);
}

int main(void)
{
    struct player local = {0}, remote = {0};
    g_CurrentPlayer = &local;
    g_playerPointers[0] = &local; g_playerPointers[1] = &remote;
    ptr_animation_table = calloc(1, sizeof(*ptr_animation_table));
    CHECK(ptr_animation_table != NULL);
    testCuffs();
    testWater();
    testMultiplayerAnimation();
    for (unsigned i = 0; i < numAllocations; i++) free(allocations[i]);
    free(ptr_animation_table);
    puts("native player tests passed");
    return 0;
}
