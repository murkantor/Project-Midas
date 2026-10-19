#include <ultra64.h>
#include "ramrom.h"
#include <memp.h>
#include "initanitable.h"
#include "objecthandler.h"
#include "bondgame.h"
#ifdef GE_NATIVE_64
#include <string.h>
#endif

//bss

// Where animation frames are saved. Can possibly hold as much as nine, but the game will ever store four at maximum.
char animations_frame_buffer[0x2D0];

// Msg Queue stuff (unused)
OSMesgQueue animMsgQ;
char dword_CODE_bss_80069458[0xC0]; // Unused. Possibly meant for unused message queue.
OSMesg animMesg[8];

// Animation table ptr
struct animation_table_data * ptr_animation_table;

//data
struct bondstruct_unk_animation_related D_80029D60 = {
    NULL,
    &animations_frame_buffer, // Two pointers. One always points to the start of the buffer, the other can be modified.
    &animations_frame_buffer
};

intptr_t animation_table_ptrs1[] = {
    PTR_ANIM_idle,
    PTR_ANIM_fire_standing,
    PTR_ANIM_fire_standing_fast,
    PTR_ANIM_fire_hip,
    PTR_ANIM_fire_shoulder_left,
    PTR_ANIM_fire_turn_right1,
    PTR_ANIM_fire_turn_right2,
    PTR_ANIM_fire_kneel_right_leg,
    PTR_ANIM_fire_kneel_left_leg,
    PTR_ANIM_fire_kneel_left,
    PTR_ANIM_fire_kneel_right,
    PTR_ANIM_fire_roll_left,
    PTR_ANIM_fire_roll_right1,
    PTR_ANIM_fire_roll_left_fast,
    PTR_ANIM_hit_left_shoulder,
    PTR_ANIM_hit_right_shoulder,
    PTR_ANIM_hit_left_arm,
    PTR_ANIM_hit_right_arm,
    PTR_ANIM_hit_left_hand,
    PTR_ANIM_hit_right_hand,
    PTR_ANIM_hit_left_leg,
    PTR_ANIM_hit_right_leg,
    PTR_ANIM_death_genitalia,
    PTR_ANIM_hit_neck,
    PTR_ANIM_death_neck,
    PTR_ANIM_death_stagger_back_to_wall,
    PTR_ANIM_death_forward_face_down,
    PTR_ANIM_death_forward_spin_face_up,
    PTR_ANIM_death_backward_fall_face_up1,
    PTR_ANIM_death_backward_spin_face_down_right,
    PTR_ANIM_death_backward_spin_face_up_right,
    PTR_ANIM_death_backward_spin_face_down_left,
    PTR_ANIM_death_backward_spin_face_up_left,
    PTR_ANIM_death_forward_face_down_hard,
    PTR_ANIM_death_forward_face_down_soft,
    PTR_ANIM_death_fetal_position_right,
    PTR_ANIM_death_fetal_position_left,
    PTR_ANIM_death_backward_fall_face_up2,
    PTR_ANIM_side_step_left,
    PTR_ANIM_fire_roll_right2,
    PTR_ANIM_walking,
    PTR_ANIM_sprinting,
    PTR_ANIM_running,
    PTR_ANIM_bond_eye_walk,
    PTR_ANIM_bond_eye_fire,
    PTR_ANIM_bond_watch,
    PTR_ANIM_surrendering_armed,
    PTR_ANIM_surrendering_armed_drop_weapon,
    PTR_ANIM_fire_walking,
    PTR_ANIM_fire_running,
    PTR_ANIM_null50,
    PTR_ANIM_null51,
    PTR_ANIM_fire_jump_to_side_left,
    PTR_ANIM_fire_jump_to_side_right,
    PTR_ANIM_hit_butt_long,
    PTR_ANIM_hit_butt_short,
    PTR_ANIM_death_head,
    PTR_ANIM_death_left_leg,
    PTR_ANIM_slide_right,
    PTR_ANIM_slide_left,
    PTR_ANIM_jump_backwards,
    PTR_ANIM_extending_left_hand,
    PTR_ANIM_fire_throw_grenade,
    PTR_ANIM_spotting_bond,
    PTR_ANIM_look_around,
    PTR_ANIM_fire_standing_one_handed_weapon,
    PTR_ANIM_fire_standing_draw_one_handed_weapon_fast,
    PTR_ANIM_fire_standing_draw_one_handed_weapon_slow,
    PTR_ANIM_fire_hip_one_handed_weapon_fast,
    PTR_ANIM_fire_hip_one_handed_weapon_slow,
    PTR_ANIM_fire_hip_forward_one_handed_weapon,
    PTR_ANIM_fire_standing_right_one_handed_weapon,
    PTR_ANIM_fire_step_right_one_handed_weapon,
    PTR_ANIM_fire_standing_left_one_handed_weapon_slow,
    PTR_ANIM_fire_standing_left_one_handed_weapon_fast,
    PTR_ANIM_fire_kneel_forward_one_handed_weapon_slow,
    PTR_ANIM_fire_kneel_forward_one_handed_weapon_fast,
    PTR_ANIM_fire_kneel_right_one_handed_weapon_slow,
    PTR_ANIM_fire_kneel_right_one_handed_weapon_fast,
    PTR_ANIM_fire_kneel_left_one_handed_weapon_slow,
    PTR_ANIM_fire_kneel_left_one_handed_weapon_fast,
    PTR_ANIM_fire_kneel_left_one_handed_weapon,
    PTR_ANIM_aim_walking_one_handed_weapon,
    PTR_ANIM_aim_walking_left_one_handed_weapon,
    PTR_ANIM_aim_walking_right_one_handed_weapon,
    PTR_ANIM_aim_running_one_handed_weapon,
    PTR_ANIM_aim_running_right_one_handed_weapon,
    PTR_ANIM_aim_running_left_one_handed_weapon,
    PTR_ANIM_aim_sprinting_one_handed_weapon,
    PTR_ANIM_running_one_handed_weapon,
    PTR_ANIM_sprinting_one_handed_weapon,
    PTR_ANIM_null91,
    PTR_ANIM_null92,
    PTR_ANIM_null93,
    PTR_ANIM_null94,
    PTR_ANIM_null95,
    PTR_ANIM_null96,
    PTR_ANIM_draw_one_handed_weapon_and_look_around,
    PTR_ANIM_draw_one_handed_weapon_and_stand_up,
    PTR_ANIM_aim_one_handed_weapon_left_right,
    PTR_ANIM_cock_one_handed_weapon_and_turn_around,
    PTR_ANIM_holster_one_handed_weapon_and_cross_arms,
    PTR_ANIM_cock_one_handed_weapon_turn_around_and_stand_up,
    PTR_ANIM_draw_one_handed_weapon_and_turn_around,
    PTR_ANIM_step_forward_and_hold_one_handed_weapon,
    PTR_ANIM_holster_one_handed_weapon_and_adjust_suit,
    PTR_ANIM_idle_unarmed,
    PTR_ANIM_walking_unarmed,
    PTR_ANIM_fire_walking_dual_wield,
    PTR_ANIM_fire_walking_dual_wield_hands_crossed,
    PTR_ANIM_fire_running_dual_wield,
    PTR_ANIM_fire_running_dual_wield_hands_crossed,
    PTR_ANIM_fire_sprinting_dual_wield,
    PTR_ANIM_fire_sprinting_dual_wield_hands_crossed,
    PTR_ANIM_walking_female,
    PTR_ANIM_running_female,
    PTR_ANIM_fire_kneel_dual_wield,
    PTR_ANIM_fire_kneel_dual_wield_left,
    PTR_ANIM_fire_kneel_dual_wield_right,
    PTR_ANIM_fire_kneel_dual_wield_hands_crossed,
    PTR_ANIM_fire_kneel_dual_wield_hands_crossed_left,
    PTR_ANIM_fire_kneel_dual_wield_hands_crossed_right,
    PTR_ANIM_fire_standing_dual_wield,
    PTR_ANIM_fire_standing_dual_wield_left,
    PTR_ANIM_fire_standing_dual_wield_right,
    PTR_ANIM_fire_standing_dual_wield_hands_crossed_left,
    PTR_ANIM_fire_standing_dual_wield_hands_crossed_right,
    PTR_ANIM_fire_standing_aiming_down_sights,
    PTR_ANIM_fire_kneel_aiming_down_sights,
    PTR_ANIM_hit_taser,
    PTR_ANIM_death_explosion_forward,
    PTR_ANIM_death_explosion_left1,
    PTR_ANIM_death_explosion_back_left,
    PTR_ANIM_death_explosion_back1,
    PTR_ANIM_death_explosion_right,
    PTR_ANIM_death_explosion_forward_right1,
    PTR_ANIM_death_explosion_back2,
    PTR_ANIM_death_explosion_forward_roll,
    PTR_ANIM_death_explosion_forward_face_down,
    PTR_ANIM_death_explosion_left2,
    PTR_ANIM_death_explosion_forward_right2,
    PTR_ANIM_death_explosion_forward_right2_alt,
    PTR_ANIM_death_explosion_forward_right3,
    PTR_ANIM_null143,
    PTR_ANIM_null144,
    PTR_ANIM_null145,
    PTR_ANIM_null146,
    PTR_ANIM_running_hands_up,
    PTR_ANIM_sprinting_hands_up,
    PTR_ANIM_aim_and_blow_one_handed_weapon,
    PTR_ANIM_aim_one_handed_weapon_left,
    PTR_ANIM_aim_one_handed_weapon_right,
    PTR_ANIM_conversation,
    PTR_ANIM_drop_weapon_and_show_fight_stance,
    PTR_ANIM_yawning,
    PTR_ANIM_swatting_flies,
    PTR_ANIM_scratching_leg,
    PTR_ANIM_scratching_butt,
    PTR_ANIM_adjusting_crotch,
    PTR_ANIM_sneeze,
    PTR_ANIM_conversation_cleaned,
    PTR_ANIM_conversation_listener,
    PTR_ANIM_startled_and_looking_around,
    PTR_ANIM_laughing_in_disbelief,
    PTR_ANIM_surrendering_unarmed,
    PTR_ANIM_coughing_standing,
    PTR_ANIM_coughing_kneel1,
    PTR_ANIM_coughing_kneel2,
    PTR_ANIM_standing_up,
    PTR_ANIM_null169,
    PTR_ANIM_dancing,
    PTR_ANIM_dancing_one_handed_weapon,
    PTR_ANIM_keyboard_right_hand1,
    PTR_ANIM_keyboard_right_hand2,
    PTR_ANIM_keyboard_left_hand,
    PTR_ANIM_keyboard_right_hand_tapping,
    PTR_ANIM_bond_eye_fire_alt,
    PTR_ANIM_dam_jump,
    PTR_ANIM_surface_vent_jump,
    PTR_ANIM_cradle_jump,
    PTR_ANIM_cradle_fall,
    PTR_ANIM_credits_bond_kissing,
    PTR_ANIM_credits_natalya_kissing,
    0
};

struct ModelAnimation *animation_table_ptrs2[] = {
    PTR_ANIM_helicopter_cradle,
    PTR_ANIM_plane_runway,
    PTR_ANIM_helicopter_takeoff,
    0
};


extern u32 _animation_entriesSegmentRomStart[];

struct anim_entry
{
    s32 unk00;
    s32 unk04;
    s32 unk08;
    s32 unk0C;
    s32 unk10;
};

#ifdef GE_NATIVE_64
ModelAnimation *portAnimationFromOffset(uintptr_t offset)
{
    static ModelAnimation *cache[0xE7E0 / 4];
    u8 *base = ptr_animation_table->data;
    if (offset >= 0xE7E0 || (offset & 3) != 0) return NULL;
    if (cache[offset / 4] == NULL) {
        const u8 *raw = ptr_animation_table->data + offset;
        ModelAnimation *anim = mempAllocBytesInBank(sizeof(*anim), MEMPOOL_PERMANENT);
        u32 address, descriptors, stream;
        memset(anim, 0, sizeof(*anim));
        memcpy(&address, raw, 4);
        memcpy(&descriptors, raw + 8, 4);
        memcpy(&stream, raw + 16, 4);
        anim->address = (uintptr_t)_animation_entriesSegmentRomStart + address;
        memcpy(&anim->unk04, raw + 4, 2);
        anim->unk06 = raw[6];
        anim->unk07 = raw[7];
        anim->bitDescriptors = (ModelAnimBitField *)(base + descriptors);
        memcpy(&anim->unk0C, raw + 12, 2);
        memcpy(&anim->unk0E, raw + 14, 2);
        anim->bitStream = (u8 *)base + stream;
        cache[offset / 4] = anim;
    }
    return cache[offset / 4];
}
ModelAnimation *portAnimationResolve(ModelAnimation *anim)
{
    uintptr_t offset = (uintptr_t)anim - (uintptr_t)ptr_animation_table;
    return offset < 0xE7E0 ? portAnimationFromOffset(offset) : anim;
}
#endif

void expand_ani_table_entries(s32** arg0)
{
#ifdef GE_NATIVE_64
    intptr_t *entry = (intptr_t *)arg0;
    for (; *entry != 0; entry++) {
        if (*entry != 1) *entry = (intptr_t)portAnimationFromOffset(*entry);
    }
#else
    s32** var_v0;

    var_v0 = arg0;
    while (*var_v0 != 0) {
        if (*var_v0 != (s32*)1) {
            *var_v0 = (s32)((s32)*var_v0 + (s32)(&ptr_animation_table->data));
            ((struct anim_entry *)*var_v0)->unk08 += (s32)&ptr_animation_table->data;
            ((struct anim_entry *)*var_v0)->unk10 += (s32)&ptr_animation_table->data;
        }
        var_v0++;
    }

    for (var_v0 = arg0; *var_v0 != 0; var_v0++) {
        if (*var_v0 != (s32*)1) {
            **var_v0 += (s32)&_animation_entriesSegmentRomStart;
        }
    }
#endif
}

void alloc_load_expand_ani_table(void)
{
    s32 animsDataSegmentSize;
    
    osCreateMesgQueue(&animMsgQ, animMesg, 8);
    initAnimationsBuffer(&D_80029D60, &animMsgQ, &dword_CODE_bss_80069458);
    
    animsDataSegmentSize = (u8 *)&_animation_dataSegmentEnd - (u8 *)&_animation_dataSegmentStart;
    
    ptr_animation_table = mempAllocBytesInBank(animsDataSegmentSize, MEMPOOL_PERMANENT);

    romCopy(ptr_animation_table, &_animation_dataSegmentRomStart, animsDataSegmentSize);
    expand_ani_table_entries((s32*)&animation_table_ptrs1);
    expand_ani_table_entries((s32*)&animation_table_ptrs2);
}
