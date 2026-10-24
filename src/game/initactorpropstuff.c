#include <ultra64.h>
#include "game/model.h"
#include "initactorpropstuff.h"
#include "initanitable.h"
#include "chr.h"
#include "gun.h"
#include "math.h"
#include "math_floor.h"


/**
 * Gets the number of currently allocated heads and bodies
 * Note: Compile-time static? why bother with a function?
 */
void reset_counter_rand_body_head(void)
{
    num_bodies = 0;
    while (list_of_bodies[num_bodies] >= 0)
    {
        num_bodies++;
    }
#ifdef ISGOLDFINGER
    return; //return early as we have a new function for heads
#endif
    num_male_heads = 0;
    while (random_male_heads[num_male_heads] >= 0)
    {
        num_male_heads++;
    }

    num_female_heads = 0;
    while (random_female_heads[num_female_heads] >= 0)
    {
        num_female_heads++;
    }
}


u32 sub_GAME_7F0001F0(void *ani, int aniid, int param_3) {
    u16 asStack8[4];
    u16 result = 0;

    while (aniid < param_3) {
        result += sub_GAME_7F06D2E4(0, 0, &skeleton_guard, ani, aniid, asStack8);
        aniid++;
    }
    return result;
}


s32 sub_GAME_7F000290(ModelAnimation *anim, s32 startframe, s32 endframe)
{
    s32 sum;
    s16 out[3];

    sum = 0;

    if (startframe < endframe)
    {
        do
        {
            sub_GAME_7F06D2E4(0, 0, &skeleton_guard, anim, startframe, out);

            startframe++;
            sum += out[2];
        }
        while (startframe < endframe);
    }

    return sum;
}


/**
 * Address: 7F00032C
 * 
 * pd is raceInitAnimGroup
 * 
 * Initializes a null-terminated table of weapon firing animation configs.
 * 
 * @returns the number of table entires.
 */
s32 initResolveAnimGroupTable(struct weapon_firing_animation_table *animconfig)
{
    s32 animoffset;
    s32 numconfigs;
    struct weapon_firing_animation_table *config;

    union
    {
        u32 offset;
        struct ModelAnimation *anim;
    } *initialanim;

    s32 endframe;
    u32 angle16;
    f32 duration;

    numconfigs = 0;
    config = animconfig;

    if (animconfig->anim.offset != 0)
    {
        f32 fullturn = M_TAU_F;
        f32 angleconv = 0.0000958738f;

        initialanim = (void *)&animconfig->anim;
        animoffset = (*initialanim).offset;

        do
        {
            #ifdef GE_NATIVE_64
            config->anim.anim = portAnimationFromOffset(animoffset);
#else
            config->anim.anim = (struct ModelAnimation *)(((0, animoffset)) + ((s32)ptr_animation_table));
#endif
            endframe = floorFloatToInt(config->unk04);
            angle16 = sub_GAME_7F0001F0(config->anim.anim, 0, endframe) & 0xffff;
            duration = config->unk04;

            if (duration > 0.0f)
            {
                if (((s32)angle16) < 0x8000)
                {
                    config->turn_angle_per_frame = (angle16 * angleconv) / duration;
                }
                else
                {
                    config->turn_angle_per_frame = ((angle16 * angleconv) - fullturn) / duration;
                }
            }
            else
            {
                config->turn_angle_per_frame = 0.0f;
            }

            animoffset = config[1].anim.offset;
            config++;
            numconfigs++;
        }
        while (animoffset != 0);
    }

    return numconfigs;
}


//pd is raceInitAnimGroups
/**
 * Address: 7F00046C
 */
void initResolveAnimGroups(struct anim_group_info **groups)
{
    s32 i;

    for (i = 0; i < 32; i++)
    {
        if (groups[i]->len < 0)
        {
            groups[i]->len = initResolveAnimGroupTable(groups[i]->table);
        }
    }
}


/**
 * Address: 7F0004D0
 * 
 * Resolves each entry's anim offset into an absolute ModelAnimation*.
 * @returns the entry count.
 */
s32 initResolveAnimTable(struct StruckAnim *entries)
{
#ifdef GE_NATIVE_64
    s32 count = 0;
    while (entries[count].struck_anim != NULL) {
        entries[count].struck_anim = portAnimationFromOffset((uintptr_t)entries[count].struck_anim);
        count++;
    }
    return count;
#else
    s32 count;
    struct StruckAnim *entry;
    s32 address;
    struct StruckAnim *ptr_animation_table_addr;

    count = 0;
    entry = entries;
    ptr_animation_table_addr = (struct StruckAnim *)(&ptr_animation_table);

    if (1);

    if (entry->struck_anim != 0)
    {
        do
        {
            address = (*entry).struck_anim;
            entries = ptr_animation_table_addr;
            count++;
            entry++;
            ptr_animation_table_addr = (struct StruckAnim *)(&ptr_animation_table);
            entry[-1].struck_anim = (ModelAnimation *)((*((s32 *)entries)) + (0, address));
        }
        while (entry->struck_anim != 0);
    }

    return count;
#endif
}


#ifdef GE_NATIVE_64
#define ANIM_PTR(anim) portAnimationFromOffset((uintptr_t)&anim)
#else
#define ANIM_PTR(anim) \
    ((ModelAnimation *)((s32)&anim + ((s32)ptr_animation_table)))
#endif

#define ANIM_FRAC(anim) \
    ((((f32)sub_GAME_7F000290(ANIM_PTR(anim), 0, ANIM_PTR(anim)->unk04 - 1)) * 0.10000001f) / \
        ((f32)((u32)ANIM_PTR(anim)->unk04)))

#define ANIM_FRAC_MUL_FIRST(anim) \
    ((0.10000001f * ((f32)sub_GAME_7F000290(ANIM_PTR(anim), 0, ANIM_PTR(anim)->unk04 - 1))) / \
        ((f32)((u32)ANIM_PTR(anim)->unk04)))

void initWeaponAnimGroups(void)
{
    s32 i;

    if (g_HitReactionTable[0].hitpart != (-1))
    {
        i = 0;

        do
        {
            if (g_HitReactionTable[i].deathAnims != NULL)
            {
                g_HitReactionTable[i].deathAnimCount = initResolveAnimTable(g_HitReactionTable[i].deathAnims);
            }

            if (g_HitReactionTable[i].flinchAnims != NULL)
            {
                g_HitReactionTable[i].flinchAnimCount = initResolveAnimTable(g_HitReactionTable[i].flinchAnims);
            }

            i++;
        }
        while (g_HitReactionTable[i].hitpart != (-1));
    }


    initResolveAnimTable(death_stagger);

    initResolveAnimGroups(ptr_rifle_firing_animation_groups);
    initResolveAnimGroups(ptr_pistol_firing_animation_groups);
    initResolveAnimGroups(ptr_doubles_firing_animation_groups);
    initResolveAnimGroups(ptr_crouched_rifle_firing_animation_groups);
    initResolveAnimGroups(ptr_crouched_pistol_firing_animation_groups);
    initResolveAnimGroups(ptr_crouched_doubles_firing_animation_groups);

    initResolveAnimGroupTable(D_80030078);
    initResolveAnimGroupTable(D_80030660);

#ifndef TARGET_N64
    /* PORT: on N64 groupA only got resolved by accident when group1 (which had
     * no terminator) overran into it; with the terminator added, resolve groupA
     * explicitly or MP crouch-firing a rifle crashes on a raw offset. PC only:
     * on N64 the overrun already based it, so a second call would double-add. */
    initResolveAnimGroupTable(crouched_rifle_firing_animation_groupA);
#endif

    /* PORT: was stubbed to 0.01f on PC; now that animation data is addressed by
     * segment offset again (port/src/animdata.c), the retail computation works on both targets. */
    D_80030984 = ANIM_FRAC(ANIM_DATA_walking);
    D_80030988 = ANIM_FRAC(ANIM_DATA_running);
    D_8003098C = ANIM_FRAC(ANIM_DATA_sprinting);
    D_80030990 = ANIM_FRAC(ANIM_DATA_walking_unarmed);
    D_80030994 = ANIM_FRAC(ANIM_DATA_running_one_handed_weapon);
    D_80030998 = ANIM_FRAC(ANIM_DATA_sprinting_one_handed_weapon);
    D_8003099C = ANIM_FRAC(ANIM_DATA_walking_female);
    D_800309A0 = ANIM_FRAC_MUL_FIRST(ANIM_DATA_running_female);
    D_800309A4 = ANIM_FRAC(ANIM_DATA_sprinting_one_handed_weapon);
}

#undef ANIM_PTR
#undef ANIM_FRAC
#undef ANIM_FRAC_MUL_FIRST


/**
 * Address: 7F000980
 */
void casingsInit(void) 
{
    initCasingPool();
}


/**
 * Address: 7F0009A0
 * 
 * Sets the header field of every g_Casings entry to NULL.
 */
void initCasingPool(void) 
{
    CasingRecord *end = &g_Casings[20];
    CasingRecord *ptr = &g_Casings[0];
    
    while (end > ptr) 
    {
        ptr->header = NULL;
        ptr++;
    }
}
