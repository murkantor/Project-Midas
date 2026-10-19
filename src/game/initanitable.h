#ifndef _INITANITABLE_H_
#define _INITANITABLE_H_
#include <ultra64.h>
#include <assets/animationtable_data.h>



/**
 * Struct to hold animation data. This is never instantiated.
 * Instead, only a pointer to this will exist.
 */
struct animation_table_data {
    /**
     * Array length is arbitrary and shouldn't matter. The largest offset
     * into this is for the last animation pointer 0xE7C0, so just choosing
     * a value bigger than that, like u16_max_value.
    */
    u8 data[0xffff];
};

/**
 * Data holder for animations.
 */
extern struct animation_table_data* ptr_animation_table;
/* Native animation headers live outside the serialized segment. Use this
 * accessor for pointer comparisons as well as field access. */
#ifdef GE_NATIVE_64
struct ModelAnimation *portAnimationFromOffset(uintptr_t offset);
struct ModelAnimation *portAnimationResolve(struct ModelAnimation *anim);
#else
#define portAnimationFromOffset(offset) ((struct ModelAnimation *)(ptr_animation_table->data + (offset)))
#define portAnimationResolve(anim) (anim)
#endif

/**
 * Contains offsets into ptr_animation_table for player and guard animations.
 * The index of each value corresponds to `enum ANIMATION`.
 * The value corresponds to (e.g. index=0) PTR_ANIM_idle (same as ANIM_DATA_idle)
*/
extern intptr_t animation_table_ptrs1[];

/**
 * Contains offsets into ptr_animation_table for object/vehicle animations.
 * The index of each value corresponds to `enum AIRCRAFT_ANIMATION`.
 * The value corresponds to (e.g. index=0) PTR_ANIM_helicopter_cradle (same as ANIM_DATA_helicopter_cradle)
*/
extern struct ModelAnimation * animation_table_ptrs2[];


void alloc_load_expand_ani_table(void);
void initAnimationsBuffer(struct bondstruct_unk_animation_related* animBuffer, OSMesgQueue* mq, s32 unused);

#endif
