#include <ultra64.h>
#include "game/bondview.h"
#include "game/lv.h"
#include "game/player.h"
#include "file.h"
#include "file2.h"
#include "front.h"
#ifndef TARGET_N64
#include <stdio.h> /* stderr - mission-end diagnostics */
#endif

f32 get_007_reaction_speed(void)
{
    if (lvlGetSelectedDifficulty() == DIFFICULTY_007) {
        return slider_007_mode_reaction;
    }
    return 0.f;
}

f32 get_007_health_mod(void)
{
    if (lvlGetSelectedDifficulty() == DIFFICULTY_007) {
        return slider_007_mode_health;
    }
    return 1.f;
}

f32 get_007_damage_mod(void)
{
    if (lvlGetSelectedDifficulty() == DIFFICULTY_007) {
        return slider_007_mode_damage;
    }
    return 1.f;
}

f32 get_007_accuracy_mod(void)
{
    if (lvlGetSelectedDifficulty() == DIFFICULTY_007) {
        return slider_007_mode_accuracy;
    }
    return 1.f;
}

#ifndef TARGET_N64
/**
 * Mission-end diagnostics, for tracking down a completed mission that fails to
 * unlock the next one (e.g. Aztec). Each mission end writes two lines to the
 * launcher's log:
 *   ge: mission end: ...  from boss.c bossReturnTitleStage: whether all
 *                         objectives were complete (no briefing write if not)
 *   ge: debrief: ...      the end_of_mission_briefing gates (briefingpage,
 *                         difficulty, cheats active), then the stage's status
 *                         and Aztec's status per difficulty after the write
 * Status values: 0 locked, 1 unlocked, 3 completed. file2.c adds a "ge: save:"
 * line when the eeprom write is dropped (no free slot or no pak).
 */
void portMissionEndLog(s32 stage, s32 allcomplete)
{
    fprintf(stderr, "ge: mission end: levelid=%d all_objectives_complete=%d\n", stage, allcomplete);
}
#endif

void end_of_mission_briefing(void)
{
    s16 var1;

#ifndef TARGET_N64
    fprintf(stderr, "ge: debrief: briefingpage=%d stage=%d difficulty=%d cheats_active=%d folder=%d time=%ds\n",
            briefingpage,
            (briefingpage >= 0) ? (s32) mission_folder_setup_entries[briefingpage].mission_num : -1,
            selected_difficulty, g_AppendCheatSinglePlayer, selected_folder_num, getMissiontimer() / 0x3c);
#endif
    if ((-1 < briefingpage) && selected_difficulty != DIFFICULTY_007 && g_AppendCheatSinglePlayer == FALSE)
    {
        var1 = solo_target_time_array[mission_folder_setup_entries[briefingpage].mission_num][selected_difficulty];
        fileUnlockStageInFolderAtDifficulty(selected_folder_num, mission_folder_setup_entries[briefingpage].mission_num, selected_difficulty, getMissiontimer() / 0x3c);
#ifndef TARGET_N64
        fprintf(stderr, "ge: debrief: stage %d @ difficulty %d status=%d; aztec agent=%d secret=%d 00=%d (0 locked 1 unlocked 3 completed)\n",
                mission_folder_setup_entries[briefingpage].mission_num, selected_difficulty,
                fileIsStageUnlockedAtDifficulty(selected_folder_num, mission_folder_setup_entries[briefingpage].mission_num, selected_difficulty),
                fileIsStageUnlockedAtDifficulty(selected_folder_num, SP_LEVEL_AZTEC, DIFFICULTY_AGENT),
                fileIsStageUnlockedAtDifficulty(selected_folder_num, SP_LEVEL_AZTEC, DIFFICULTY_SECRET),
                fileIsStageUnlockedAtDifficulty(selected_folder_num, SP_LEVEL_AZTEC, DIFFICULTY_00));
#endif
        if ((getMissiontimer() / GAME_TICKRATE) <= var1)
        {
#ifndef TARGET_N64
            /* the save's own bit: under Unlock All fileGetIsCheatUnlocked reports
             * every cheat owned, so a target time beaten would never be saved */
            extern bool portFileGetIsCheatUnlockedRaw(save_data *save, s32 cheat);

            if (!portFileGetIsCheatUnlockedRaw(fileGetSaveForFoldernum(selected_folder_num), mission_folder_setup_entries[briefingpage].mission_num))
#else
            if (!fileGetIsCheatUnlocked(fileGetSaveForFoldernum(selected_folder_num), mission_folder_setup_entries[briefingpage].mission_num))
#endif
            {
                fileSaveFolderUnlockCheat(selected_folder_num, mission_folder_setup_entries[briefingpage].mission_num);
                g_NewCheatUnlocked = TRUE;
                return;
            }
        }
#ifdef VERSION_US
        g_NewCheatUnlocked = FALSE;
#endif
    }
}

void fileLoadSaveSettingsForSelectedFolder(void)
{
    fileLoadSettingsForFolder(selected_folder_num);
}

void deleteCurrentSelectedFolder(void)
{
  fileClearSavefileForFolder(selected_folder_num);
}

void fileUpdateBondInCurrentFolder(void)
{
  fileUpdateSelectedBondInSave(selected_folder_num);
}

s32 fileGetBondForCurrentFolder(void)
{
  return fileGetBondForFolder(selected_folder_num);
}

void fileSetCurrentFolder(u32 foldernum)
{
  selected_folder_num = foldernum;
}

void set_selected_difficulty(DIFFICULTY difficulty)
{
    switch(difficulty)
    {
    case DIFFICULTY_AGENT:
    default:
        selected_difficulty = DIFFICULTY_AGENT;
        return;
    case DIFFICULTY_SECRET:
        selected_difficulty = DIFFICULTY_SECRET;
        return;
    case DIFFICULTY_00:
        selected_difficulty = DIFFICULTY_00;
        return;
    case DIFFICULTY_007:
        selected_difficulty = DIFFICULTY_007;
        return;
    }
}

void set_solo_and_ptr_briefing(LEVELID stage)
{
    gamemode = GAMEMODE_SOLO;
    selected_stage = stage;
#ifdef TARGET_N64
    briefingpage = pull_and_display_text_for_folder_a0(stage);
#else
    /* PORT: the original lookup matched .mission_num against a LEVELID, so it
     * never matched and later crashed loading a NULL briefing filename; match
     * by stage_id instead. */
    {
        s32 i;

        briefingpage = BRIEFING_INVALID;

        for (i = 0; mission_folder_setup_entries[i].folder_text_preset != 0; i++)
        {
            if (mission_folder_setup_entries[i].stage_id == stage)
            {
                briefingpage = i;
                break;
            }
        }
    }
#endif
}

void sub_GAME_7F01D61C(struct save_data *savefile)
{
    fileCopySave(selected_folder_num, savefile);
}



//this feels fake, generated with a permuter session i forgot was running
void set_selected_foldernum_and_copy_demo_eeprom(struct save_data *eeprom)
{
  int new_var;
  long long new_var2;
  new_var2 = (long long) 0x64;
  new_var = new_var2;
  selected_folder_num_copy = (s32) selected_folder_num;
  selected_folder_num = new_var;
  fileCopyDemoSaveToRamRomSave(new_var, eeprom);
}


void store_favorite_weapon_current_player(u32 right,u32 left)
{
  u32 playerNum;

  playerNum = get_cur_playernum();
  array_favweapon[playerNum][GUNRIGHT] = right;
  array_favweapon[playerNum][GUNLEFT] = left;
}

