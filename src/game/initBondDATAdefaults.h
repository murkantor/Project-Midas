#ifndef _INITBONDDATADEFAULTS_H_
#define _INITBONDDATADEFAULTS_H_
#include <ultra64.h>

void sets_a_bunch_of_BONDdata_values_to_default(void);
#if defined(LEFTOVERDEBUG) && defined(TARGET_N64)
extern void exit(void);
#endif


void init_player_gait_object(void);

#endif
