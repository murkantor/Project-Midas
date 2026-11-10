#ifndef _LOADOBJECTMODEL_H_
#define _LOADOBJECTMODEL_H_
#include <ultra64.h>
#include <bondtypes.h>

s32                    getposstan(struct coord3d *arg0, StandTile *arg1, f32 arg2, struct coord3d *arg3, StandTile **arg4);
s32                    modelLoad(s32 modelid);
s32                    sizepropdef(struct PropDefHeaderRecord *pdef);
void                   setupUpdateObjectRoomPosition(struct ObjectRecord *);
struct ObjectRecord    *setupGetPtrToCommandByIndex(s32 index);
struct ObjectRecord    *setupCommandGetObject(s32 stageID, s32 index);
s32                    setupGetCommandIndexByProp(struct PropRecord *prop);
s32                    tagGetCommandIndex(struct ObjectRecord *arg0);

ObjectRecord *setupFindObjForReuse(s32 wanttype, ObjectRecord **offscreenobjptr, ObjectRecord **anyobjptr, bool musthaveprop, bool musthavemodel, ModelFileHeader *modeldef);

#endif
