#ifndef _DEB_LOADALLMODELS_H_
#define _DEB_LOADALLMODELS_H_
#include <ultra64.h>

typedef struct WeaponProjectileModels {
    union {
        struct {
            s32 knife;
            s32 grenade;
            s32 remote;
            s32 prox;
            s32 timed;
            s32 rocket;
            s32 glaunch;
            s32 none;
        };
        s32 array[8];
    };
} WeaponProjectileModels;

void init_obj_register_difficulty_vals(void);
void alloc_false_GUARDdata_to_exec_global_action(void);


void debug_object_load_all_models(void);
void debug_weapon_load_table(void);

#endif
