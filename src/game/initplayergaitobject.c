#include <ultra64.h>
#include "chrobjdata.h"

void init_player_gait_object(void) {
  player_gait_object_header.RootNode = (ModelNode *)&player_gait_hdr;
  return;
}

