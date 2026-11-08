#ifndef _BONDINV_H_
#define _BONDINV_H_
#include <ultra64.h>
#include <bondconstants.h>
#include "bondview.h"
#include "bondtypes.h"

void bondinvReinitInv(void);
s32 bondinvIsAliveWithFlag(void);
s32 bondinvCountTotalItemsInInv(void);

InvItem *bondinvGetItemByIndex(s32 index);
textoverride *bondinvGetTextbyObj(ObjectRecord *obj);
textoverride *bondinvGetTextbyWeaponID(ITEM_IDS weaponnum);

void bondinvCycleBackward(s32 *nextright, s32 *nextleft, s32 requireammo);
void bondinvCycleForward(s32 *nextright, s32 *nextleft, s32 requireammo);
bool bondinvHasGoldenGun(void);
int bondinvAddInvItem(ITEM_IDS item);
int bondinvAddDoublesInvItem(ITEM_IDS right, ITEM_IDS left);
s32 bondinvGetAllGunsFlag(void);
void bondinvSetAllGunsFlag(s32 all_guns);
bool          bondinvHasPropInInv(PropRecord *prop);
WeaponObjRecord *bondinvRemovePropWeaponByID(ITEM_IDS weaponnum);
void bondinvRemoveItemByID(ITEM_IDS weaponnum);
s32 bondinvGetWeaponOfChoice(s32 *weapon1, s32 *weapon2);
s32 bondinvItemAvailableForHand(ITEM_IDS right, ITEM_IDS left);
s32 bondinvItemAvailable(ITEM_IDS weaponid);
void bondinvAddTextOverride(textoverride *override);

u16 *bondinvGetFirstTitlebyIndex(s32 index);

u16 *bondinvGetLongNameByIndex(s32 index);

u16 *bondinvGetSecondTitlebyIndex(s32 index);

u8 *bondinvGetActivatedTextObject(ObjectRecord *obj);

u8 *bondinvGetActivatedTextWeapon(ITEM_IDS weaponnum);


void bondinvDetermineEquippedItem(void);
bool bondinvHasGEKey(void);
void bondinvIncrementHeldTime(s32 weapon1, s32 weapon2);
s32 bondinvGetTextbyInvIndex(s32 index);
void bondinvSetCurEquippedItem(int current_item);
int bondinvGetCurEquippedItem(void);
int bondinvAddWeaponByProp(PropRecord *prop);
int bondinvAddPropToInv(PropRecord *prop);
bool bondinvCheckHasKeyFlags(u32 wantkeyflags);

#endif
