#include <ultra64.h>
#include "game/initactorpropstuff.h"
#include "model.h"

#include "chr.h"

void init_weapon_animation_groups_maybe(void) {
    set_vtxallocator(&get_ptr_allocated_block_for_vertices);
    initWeaponAnimGroups();
}
