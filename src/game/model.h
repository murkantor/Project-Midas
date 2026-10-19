#ifndef _MODEL_H_
#define _MODEL_H_

#include <ultra64.h>
#include <bondtypes.h>
#include "bondconstants.h"


extern s32 g_MaxModelSlots;
extern s32 g_MaxAnimModelSlots;
extern s32 g_ModelIsLvResetting;

bool modelmgrCanSlotFitRwdata(Model *modelslot, ModelFileHeader *modeldef);
Model* modelmgrInstantiateModel(struct ModelFileHeader* arg0);
void clear_model_obj(Model* model);
Model *modelmgrInstantiateModelWithAnim(ModelFileHeader *);
void modelAttachHead(Model *, ModelNode*,  ModelFileHeader *);
void clear_aircraft_model_obj(Model *objinstance);
void modelSetDistanceDisabled(s32 param_1);
void modelSetDistanceScale(f32 param_1);
void set_vtxallocator(Vertex *(*param_1)(s32));
void modelCalculateScaledRootToOriginDir(Model* model, coord3d* coord);
void modelGetScaledRootToOriginDir(Model* model, coord3d* coord);
s32 modelFindNodeMtxIndex(ModelNode *node, s32 arg1);
Mtxf *modelFindNodeMtx(struct Model *model, struct ModelNode *node, s32 arg2);
Mtxf *getsubmatrix(Model *objinst);
f32 sub_GAME_7F06C768(Model *objinst);
union ModelRwData* modelGetNodeRwData(Model *Objinst, ModelNode *root);
void getpartoffset(Model *objinst, ModelNode *part, coord3d *offset);
void setpartoffset(Model *model, ModelNode *node, coord3d *pos);
void getsuboffset(Model *objinst, coord3d *offset);
void setsuboffset(Model *objinst, coord3d *offset);
f32 getsubroty(Model *objinst);
void setsubroty(Model *model, f32 angle);
void modelSetScale(Model *objinst, f32 scale);
void modelSetAnimTranslationScale(Model* model, f32 scale);
f32 getjointsize(Model *model, ModelNode *node);
f32 getinstsize(Model *arg0);
void interpolate3dVectors(vec3d *v, vec3d *w, float frac);
f32 sub_GAME_7F06D0CC(f32 arg0, f32 angle, f32 mult);
void sub_GAME_7F06D160(coord3d *arg0, coord3d *arg1, f32 mult);
u16 sub_GAME_7F06D2E4(s32 jointnum, s32 flip, ModelSkeleton *skeleton, ModelAnimation *anim, s32 frame, coord16 *out);
void sub_GAME_7F06D490(struct Model *arg0, struct ModelNode *arg1);
void subcalcpos(Model *arg0);
void process_01_group_heading(ModelRenderData* renderdata, Model* model, ModelNode* node);
void process_02_position(ModelRenderData *arg0, Model *model, ModelNode *node);
void process_03_unknown(ModelRenderData *arg0, Model *model, ModelNode *node);
void process_15_subposition(ModelRenderData* arg0, Model *model, ModelNode *node);
void modelUpdateDistanceRelations(Model* model, ModelNode* node);
void modelApplyDistanceRelations(Model* model, ModelNode* node);
void modelApplyToggleRelations(Model* model, ModelNode* node);
void modelApplyHeadRelations(Model* model, ModelNode* bodynode);
void modelApplyReorderRelationsByArg(ModelNode *basenode, bool visible);
void modelApplyReorderRelations(Model* model, ModelNode* node);
void modelUpdateReorderRelations(Model *model, ModelNode *node);
void process_07_unknown(Model *model, ModelNode *node);
void modelUpdateRelationsQuick(Model *model, ModelNode *parent);
void modelUpdateNodeRelations(Model *model);
void modelUpdateMatrices(ModelRenderData *arg0, Model *model);
void instcalcmatrices(ModelRenderData* arg0, Model* arg1);
void subcalcmatrices(ModelRenderData *arg0, struct Model *arg1);
struct ModelAnimation * objecthandlerGetModelAnim(struct Model* model);
s8 objecthandlerGetModelGunhand(Model *model);
f32 modelGetAnimFrame(Model *model);
f32 modelGetAnimEndFrame(Model *model);
f32 modelGetAnimSpeed(Model *model);
f32 modelGetAbsAnimSpeed(Model *model);
s32 modelConstrainOrWrapAnimFrame(s32 frame, ModelAnimation *anim, f32 endframe);
void modelCopyAnimForMerge(Model *model, f32 timemerge);
void modelSetAnimation2(Model *, ModelAnimation *, s32, f32, f32, f32);
void modelSetAnimationWithMerge(Model *model, ModelAnimation *modelAnimation, s32 flip, f32 startframe, f32 speed, f32 timemerge, s32 domerge);
void modelSetAnimation(Model *model, ModelAnimation *modelAnimation, s32 flip, f32 startframe, f32 speed, f32 merge);
void modelSetAnimLooping(Model *model, f32 loopframe, f32 loopmerge);
void modelSetAnimEndFrame(Model *model, f32 endframe);
void modelSetAnimFlipFunction(Model *model, void *callback);
void modelSetAnimSpeed(Model *model, f32 anim_speed, f32 startframe);
void sub_GAME_7F06FE90(Model *model, f32 arg1, f32 arg2);
void modelSetAnimPlaySpeed(Model *model, f32 animation_rate, f32 startframe);
void sub_GAME_7F06FF5C(Model *model, intptr_t arg1);
void modelSetAnimFrame(Model* model, f32 frame);
void modelSetAnimFrame2(Model* model, f32 frame1, f32 frame2);
void modelSetAnimMergingEnabled(s32 arg0);
u32 modelIsAnimMergingEnabled(void);
void modelTickAnim(struct Model *model, s32 numticks, s32 update_chrstuff);
#ifndef TARGET_N64
void modelTickAnimF(struct Model *model, f32 ticks, s32 update_chrstuff); /* fractional ticks, see frametiming.c */
#endif
void modelApplyRenderModeType1(ModelRenderData *renderdata);
void modelApplyRenderModeType3(ModelRenderData *renderdata, bool isPrimary);
void modelApplyRenderModeType4(ModelRenderData *renderdata, bool isPrimary);
void modelApplyRenderModeType2(ModelRenderData *renderdata);
void modelApplyCullMode(ModelRenderData *renderdata);
void modelRenderNodeGundl(ModelRenderData* renderdata, ModelNode* arg1);
void modelRenderNodeDl(ModelRenderData *renderdata, Model *model, ModelNode *node);
void dorottex(ModelRenderData *renderdata, ModelNode *node);
void sub_GAME_7F073038(ModelRenderData *renderdata, struct sImageTableEntry *tconfig, s32 arg2);
void sub_GAME_7F07306C(s32 param_1,struct Model *param_2,struct ModelNode *param_3);
void dotube(ModelRenderData* renderdata, Model* model, ModelNode* node);
void sub_GAME_7F0737EC(s32 param_1,struct Model *param_2, struct ModelNode *param_3);
void sub_GAME_7F0737FC(s32 param_1,struct Model *param_2,struct ModelNode *param_3);
void dogfnegx(ModelRenderData *renderdata, Model *model, ModelNode *node);
void sub_GAME_7F073FC8(s32 arg0);
void doshadow(ModelRenderData *renderdata, Model *model, ModelNode *node);
void sub_GAME_7F074514(s32 param_1,struct Model *param_2,struct ModelNode *param_3);
void sub_GAME_7F074524(Gfx *param_1,struct Model *param_2, struct ModelNode *param_3);
void sub_GAME_7F074534(ModelRenderData* data, Model* model, ModelNode* node);
void subdraw(ModelRenderData *arg0, struct Model *);
uintptr_t loadAnimationFrame(ModelAnimation* anim, s32 frame, ModelSkeleton* unused);
void modelResetAnimationsScratchBuffer(void);
void modelPromoteNodeOffsetsToPointers(ModelNode *node, u32 vma, u32 fileramaddr);
void sub_GAME_7F075A90(ModelFileHeader *header, s32 vma, u32 addr);
#ifndef TARGET_N64
void portModelCollisionAudit(ModelNode *node, const char *name);
#endif
s32 modelCalculateRwDataIndexes(ModelNode *basenode);
void modelCalculateRwDataLen(struct ModelFileHeader *objheader);
void modelInitRwData(Model *model, ModelNode *startnode);
void modelInit(struct Model *objinst, struct ModelFileHeader *header, u32 *data);
void animInit(struct Model *objinst, struct ModelFileHeader *header, u32 *data);
void modelAttachPart(Model *pmodel, ModelFileHeader *pmodeldef, ModelNode *pnode, ModelFileHeader *cmodeldef);
void modelIterateDisplayLists(ModelFileHeader *fileheader, ModelNode **nodeptr, Gfx **gdlptr);
void modelNodeReplaceGdl(u32 arg0, ModelNode *node, Gfx *find, Gfx *replacement);
bool modelTestRayIntersectsTransformedBBox(ModelRoData_BoundingBoxRecord *bbox, Mtxf *mtx, coord3d *pos, coord3d *dir);

#ifndef VERSION_EU
void return_null(void);
#endif


bool modelTestRayIntersectsNodeBBox(Model *model, ModelNode *node, coord3d *pos, coord3d *dir);
s32 sub_GAME_7F074CAC(Model *model, ModelNode *node, coord3d *raypos, coord3d *raydir);
u32 modelFindNextProjectileHitCandidate(Model *model, coord3d *arg1, coord3d *arg2, ModelNode **nodeptr);
bool intersectRayTriangle(Vertex *vertex0, Vertex *vertex1, Vertex *vertex2, coord3d *vertexOffset, coord3d *rayStart, coord3d *linePoint, coord3d *rayDirection, HitThing *hit);

#endif

