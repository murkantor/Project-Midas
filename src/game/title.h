#ifndef _INTRO_LOGOS_H_
#define _INTRO_LOGOS_H_
#include <ultra64.h>

extern uintptr_t barrelDisplayListPtr;
extern Gfx *gunbarrelgfxListPointer;
extern Mtx *matrixBufferRareLogo0;
extern Mtx *matrixBufferGunbarrel0;
extern Mtx *matrixBufferRareLogo1;
extern Mtx *matrixBufferRareLogo2;
extern Mtx *matrixBufferGunbarrel1;
extern Mtx *matrixBufferIntroBackdrop;
extern Mtx *matrixBufferIntroBond;
extern f32 x;
extern f32 y;
extern f32 titleTransitionX;
extern f32 titleTransitionY;
extern s16 word_CODE_bss_80069584;
extern uintptr_t dword_CODE_bss_80069588;
extern uintptr_t dword_CODE_bss_8006958C;
extern uintptr_t virtualaddress;
void setupRarewareLogoData(uintptr_t address, s32 size);
extern s32 gunbarrelTimer;

extern u32 D_8002A7D0;

Gfx *titleRenderFolderMenuBackground(Gfx *gdl, s32 xOffset, struct FolderSelectColour *topColour, struct FolderSelectColour *bottomColour);
Gfx *renderGunbarrelEyeIntroSequence (Gfx *gdl);

Gfx *retrieve_display_rareware_logo(Gfx *gdl);


s32 isGunBarrelInMode2(void);
void initializeGunBarrelIntro(u8 *gfxBuffer, s32 bufferSize);
void clearChrGunModelInstances(void);
s32 isGunBarrelInMode9(void);
void sub_GAME_7F008DE4(u8 **addr, s32 *size);

#endif
