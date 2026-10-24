#include <ultra64.h>
#include "fr.h"
#include "game/bg.h"
#ifndef TARGET_N64
#include <stdio.h> /* snprintf for the profile-name rank lines */
#endif
#include <music.h>
#include <bondgame.h>
#include <bondinv.h>
#include <bondconstants.h>
#include <boss.h>
#include <joy.h>
#include <random.h>
#include "textrelated.h"
#include "frametiming.h"
#include "player.h"
#include "mpmenu.h"
#include "gun.h"
#include "front.h"
#include "lv.h"
#include "language.h"
#include "mp_music.h"
#include "file.h"
#include "assets/obseg/text/LmpmenuE.h"

#ifdef REFRESH_PAL
#define MPMENU_YOFF 8   /* PAL: every text row sits 8 pixels lower */
#else
#define MPMENU_YOFF 0
#endif

// bss
s32 g_stopPlayFlag;

/**
 * g_gameOverFlag has two meanings. One is the standard true/false for whether the game is over.
 * The other is as a timer. Values >= 2 mean a countdown is still running.
 */
s32 g_gameOverFlag;

s32 chevron_glow;
s32 chevron_glow_timer;
s32 alt_gameover_msg;
s32 alt_gameover_msg_timer;
s32 g_pausedFlag;
s32 who_paused;

// data
u16 g_AwardNames[] = {
    getStringID(LMPMENU, MPMENU_STR_00_LEMMINGAWARD),getStringID(LMPMENU, MPMENU_STR_01_WHERESTHEAMMO),getStringID(LMPMENU, MPMENU_STR_02_WHERESTHEARMOR),getStringID(LMPMENU, MPMENU_STR_03_AC10AWARD),getStringID(LMPMENU, MPMENU_STR_04_MARKSMANSHIPAWARD),getStringID(LMPMENU, MPMENU_STR_05_MOSTPROFESSIONAL),
    getStringID(LMPMENU, MPMENU_STR_06_MOSTDEADLY),getStringID(LMPMENU, MPMENU_STR_07_MOSTLYHARMLESS),getStringID(LMPMENU, MPMENU_STR_08_MOSTCOWARD),getStringID(LMPMENU, MPMENU_STR_09_MOSTFRANTIC),getStringID(LMPMENU, MPMENU_STR_0A_MOSTHONORABLE),getStringID(LMPMENU, MPMENU_STR_0B_MOSTDISHONORABLE),
    getStringID(LMPMENU, MPMENU_STR_0C_SHORTESTINNINGS),getStringID(LMPMENU, MPMENU_STR_0D_LONGESTINNINGS),getStringID(LMPMENU, MPMENU_STR_0E_DOUBLEKILL),getStringID(LMPMENU, MPMENU_STR_0F_TRIPLEKILL),getStringID(LMPMENU, MPMENU_STR_10_QUADKILL)
};


s32 mpwatchMenuCanGoRight(void) 
{
    switch(g_CurrentPlayer->mpmenumode)
    {
        case MENU_GOWOC:
        case MENU_LOSSES:
        case MENU_KILLS:
        case MENU_PAUSE:
            return 1;
        case MENU_EXIT:
        case MENU_EXIT_CONFIRM:
        case MENU_FINISHED:
            return 0;
        case MENU_SCORES:
            return g_gameOverFlag ? 0 : 1;
        default:
            return 0;
    }
}


s32 mpwatchMenuCanGoLeft(void) 
{
    switch(g_CurrentPlayer->mpmenumode)
    {
        case MENU_KILLS:
        case MENU_SCORES:
        case MENU_PAUSE:
        case MENU_EXIT:
            return 1;
        case MENU_GOWOC:
        case MENU_EXIT_CONFIRM:
        case MENU_FINISHED:
            return 0;
        case MENU_LOSSES:
            return g_gameOverFlag ? 1 : 0;
        default:
#ifdef DEBUG
            // kill the process
            assert(1 == 0); // mpmenu.c, line87
#endif
        return 0;
    }
}


s32 mpwatchIsPlayerPressingRight(s32 player)
{
    s32 iVar3 = joyGetStickXInRange(player, -2, 1);

    if ((joyGetButtonsPressedThisFrame(player, R_JPAD|R_CBUTTONS)) || ((iVar3 >= 1  && (g_CurrentPlayer->mpjoywascentre)))) 
    {
        return 1;
    }

    return 0;
}

s32 mpwatchIsPlayerPressingLeft(s32 player)
{
    s32 iVar3 = joyGetStickXInRange(player, -2, 1);

    if ((joyGetButtonsPressedThisFrame(player, L_JPAD|L_CBUTTONS)) || ((iVar3 < -1 && (g_CurrentPlayer->mpjoywascentre)))) 
    {
        return 1;
    }

    return 0;
}


void mpwatchPlayBeep(void)
{
    sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, 0);
}


void mpwatchUnpauseGame(void)
{
    g_stopPlayFlag = 0;
    g_gameOverFlag = 0;
    g_pausedFlag = 0;
}


/**
 * Returns the index (0-3) of the player with the highest value.
 */
s32 mpFindMaxInt(s32 numplayers, s32 value0, s32 value1, s32 value2, s32 value3)
{
    s32 aux;
    s32 result;
 
    if ((value0 < value1) || ((value1 == value0 && ((randomGetNext() & 1))))) 
    {
        result = 1;
        aux = value1;
    }
    else 
    {
        result = 0;
        aux = value0;
    }
 
    if (numplayers >= 3) 
    {
 
        if ((aux < value2) || ((value2 == aux && ((randomGetNext() & 1))))) 
        {
            result = 2;
            aux = value2;
        }
 
        if (numplayers >= 4) 
        {
            if ((aux < value3) || ((value3 == aux && ((randomGetNext() & 1))))) {
                result = 3;
            }
        }
    }
 
    return result;
}


/**
 * Returns the index (0-3) of the player with the lowest value.
 */
s32 mpFindMinInt(s32 numplayers, s32 value0, s32 value1, s32 value2, s32 value3)
{
    s32 aux;
    s32 result;
 
    if ((value1 < value0) || ((value1 == value0 && ((randomGetNext() & 1))))) 
    {
        result = 1;
        aux = value1;
    }
    else 
    {
        result = 0;
        aux = value0;
    }
 
    if (numplayers >= 3) 
    {
        if ((value2 < aux) || ((value2 == aux && ((randomGetNext() & 1)))))
        {
            result = 2;
            aux = value2;
        }
 
        if (numplayers >= 4) 
        {
            if ((value3 < aux) || ((value3 == aux && ((randomGetNext() & 1))))) 
            {
                result = 3;
            }
        }
    }
 
    return result;
}


/**
 * Returns the index of the player with the highest value.
 * 
 * @bug: aux is s32 so each time it's set to one of the values, the decimal portion gets truncated.
 * The first comparison is safe from this since it doesn't use aux, but the rest are impacted.
 */
s32 mpFindMaxFloat(s32 numplayers, f32 value0, f32 value1, f32 value2, f32 value3)
{
    s32 aux;
    s32 result;
 
    if ((value0 < value1) || ((value1 == value0 && ((randomGetNext() & 1))))) 
    {
        aux = (s32) value1;
        result = 1;
    }
    else 
    {
        aux = (s32) value0;
        result = 0;
    }
 
    if (numplayers >= 3)
    {
        if ((aux < value2) || ((value2 == aux && ((randomGetNext() & 1))))) 
        {
            aux = (s32) value2;
            result = 2;
        }
 
        if (numplayers >= 4)
        {
            if ((aux < value3) || ((value3 == aux && ((randomGetNext() & 1))))) 
            {
                result = 3;
            }
        }
    }
 
    return result;
}


/**
 * Returns the index of the player with the lowest value.
 * 
 * Also suffers from the same bug as the above function.
 */
s32 mpFindMinFloat(s32 numplayers, f32 value0, f32 value1, f32 value2, f32 value3)
{
    s32 aux;
    s32 result;
 
    if ((value1 < value0) || ((value1 == value0 && ((randomGetNext() & 1))))) 
    {
        aux = (s32) value1;
        result = 1;
    }
    else 
    {
        aux = (s32) value0;
        result = 0;
    }
 
    if (numplayers >= 3)
    {
        if ((value2 < aux) || ((value2 == aux && ((randomGetNext() & 1))))) 
        {
            aux = (s32) value2;
            result = 2;
        }
 
        if (numplayers >= 4)
        {
            if ((value3 < aux) || ((value3 == aux && ((randomGetNext() & 1))))) 
            {
                result = 3;
            }
        }
    }
 
    return result;
}


void pauseAndLockControls(void) 
{
    lvlSetControlsLockedFlag(1);
    g_pausedFlag = TRUE;
}


bool disablePlayerActionsWhenPausedOrInMpMenu(void)
{
    if (getPlayerCount() == 1)
    {
        return TRUE;
    }

    if (g_stopPlayFlag)
    {
        return FALSE;
    }

    if (g_CurrentPlayer->mpmenuon)
    {
        return FALSE;
    }

    return TRUE;
}


void mpwatchSetStopPlayFlag(void)
{
    g_stopPlayFlag = TRUE;
}



#ifndef TARGET_N64
#include <string.h> /* memset */
#include "port_mpstats.h"

/* MP STATS (port_mpstats.c): each seat's share of the match that just ended,
 * read straight from g_playerPlayerData before anything resets it. A win is
 * the endscreen's "Rank: 1st" - the top score, shared on a tie, and every
 * member of a winning team (their score is the team's). */
s32 get_points_for_mp_player(s32 playernum);

/* Who won, when the Limits screen's Lives are on outside YOLT: the match is
 * decided by who is left standing, so survivors rank first, then the later
 * eliminations (order_out_in_yolt, as YOLT ranks), and points only break ties.
 * In a team scenario a team with anyone standing ranks first. The points
 * themselves stay the kill score, because the kill limit (lv.c) and the score
 * screen read get_points_for_mp_player directly. */
/* the scoreboard's sides: teams in any scenario, else Rare's holder flag */
#ifndef TARGET_N64
#define MPMENU_SIDE(p) portMpColourSide(p)
#else
#define MPMENU_SIDE(p) g_playerPlayerData[p].have_token_or_goldengun
#endif

static s32 portMpRankScore(s32 playernum)
{
    extern s32 portMpLives(void);
    const s32 points = get_points_for_mp_player(playernum);
    const s32 sc = get_scenario();
    s32 j;

    if (portMpLives() <= 0 || sc == SCENARIO_YOLT || sc == SCENARIO_TLD)
    {
        return points;
    }
    if (portMpTeamsOn())
    {
        for (j = 0; j < getPlayerCount(); j++)
        {
            if (portMpTeamOf(j) == portMpTeamOf(playernum)
                && g_playerPlayerData[j].order_out_in_yolt == 0)
            {
                return 100000 + points;   /* a side still standing */
            }
        }
        return points;
    }
    return (MAX_PLAYER_COUNT + 1 - g_playerPlayerData[playernum].order_out_in_yolt) * 100000 + points;
}

static void portMpStatsGather(s32 player_count, s32 duration, const struct AwardMetrics *metrics)
{
    struct PortMpMatchLine lines[4];
    s32 scores[4];
    s32 best = 0;
    s32 i;
    s32 j;

    for (i = 0; i < player_count && i < 4; i++)
    {
        scores[i] = portMpRankScore(i);
        if (i == 0 || scores[i] > best)
        {
            best = scores[i];
        }
    }
    for (i = 0; i < player_count && i < 4; i++)
    {
        struct PortMpMatchLine *m = &lines[i];
        const struct player_data *pd = &g_playerPlayerData[i];

        memset(m, 0, sizeof(*m));
        m->won = (scores[i] == best);
        for (j = 0; j < get_selected_num_players(); j++)
        {
            m->deaths += g_playerPlayerData[j].kill_counts[i];   /* suicides included, as metrics.num_deaths */
            if (i == j)
            {
                m->suicides += pd->kill_counts[j];
            }
            else
            {
                m->kills += pd->kill_counts[j];
            }
        }
        m->shots = pd->shot_count[SHOT_REGISTER_TOTAL];
        m->head = pd->shot_count[SHOT_REGISTER_HEAD];
        m->body = pd->shot_count[SHOT_REGISTER_BODY];
        m->limb = pd->shot_count[SHOT_REGISTER_LIMB];
        m->other = pd->shot_count[SHOT_REGISTER_GUN] + pd->shot_count[SHOT_REGISTER_HAT];
        m->streak = pd->most_killed_one_life;
        m->longestlife60 = pd->longest_inning;
        m->awards = (u32) metrics[i].awards;   /* every award earned, before the random pick clears them */
    }
    portMpStatsRecord(lines, player_count, duration / 60);
}
#endif

void mpCalculateAwards(bool gameoverdelay)
{
    s32 player_count;
    s32 i;
    s32 j;
    s32 weapon_choice_2;
    s32 weapon_choice_1;
    s32 prev_player_num;
    s32 duration;

    struct AwardMetrics metrics[4];

    player_count = getPlayerCount();
    duration = getMissiontimer();

    sndDeactivateAllSfxByFlag_1();
    set_missionstate(MISSION_STATE_0);

    musicTrack1ApplySeqpVol(sub_GAME_7F0C0BF0());
    g_musicXTrack1Fade = 0;
    musicTrack1Play(M_INTROSWOOSH);

    pauseAndLockControls();

    if (gameoverdelay != 0)
    {
        g_gameOverFlag = (PAL ? 250 : 300);
    }
    else
    {
        g_gameOverFlag = 1;
    }

    alt_gameover_msg = 1;

    // Possible copy/paste error: these are the timer values for the chevron glow
    alt_gameover_msg_timer =  (PAL ? 16 : 20);

    chevron_glow = 0;
    chevron_glow_timer = 0;

    prev_player_num = get_cur_playernum();

    for (i = 0; i < player_count; i++)
    {

        set_cur_player(i);

        g_CurrentPlayer->mpmenuon = TRUE;
        g_CurrentPlayer->mpmenumode = MENU_SCORES;
        g_CurrentPlayer->ptr_text_first_mp_award = 0;
        g_CurrentPlayer->ptr_text_second_mp_award = 0;

        bondinvGetWeaponOfChoice(&weapon_choice_1, &weapon_choice_2);
        store_favorite_weapon_current_player((u32) weapon_choice_1, (u32) weapon_choice_2);

        metrics[i].num_shots = get_curplayer_shot_register(SHOT_REGISTER_TOTAL);
        metrics[i].num_headshots = get_curplayer_shot_register(SHOT_REGISTER_HEAD);
        metrics[i].num_kills = 0;
        metrics[i].num_deaths = 0;
        metrics[i].num_suicides = 0;

        for (j = 0; j < get_selected_num_players(); j++)
        {
            metrics[i].num_deaths += g_playerPlayerData[j].kill_counts[i];
            if (i == j)
            {
                metrics[i].num_suicides += g_playerPlayerData[i].kill_counts[j];
            }
            else
            {
                metrics[i].num_kills += g_playerPlayerData[i].kill_counts[j];
            }
        }

        metrics[i].ks_ratio = metrics[i].num_kills * 100.0f / (metrics[i].num_shots + 1.0f);
        metrics[i].kd_ratio = metrics[i].num_kills * 100.0f / (metrics[i].num_deaths + 1.0f);
        metrics[i].damage_to_backside = g_playerPlayerData[i].damage_to_backside;
        metrics[i].time_other_players_on_screen = g_playerPlayerData[i].time_other_players_on_screen;
        metrics[i].avg_km_per_hour = g_playerPlayerData[i].distance_traveled / 100000.0f / ((duration + 1) / (3600.0f * 60.0f));
        metrics[i].body_armor_pickups = g_playerPlayerData[i].body_armor_pickups;
        metrics[i].awards = 0;
        metrics[i].longest_inning = g_playerPlayerData[i].longest_inning;
        metrics[i].shortest_inning = g_playerPlayerData[i].shortest_inning;
    }

    set_cur_player(prev_player_num);

    // Choose which players are eligible for which awards
    i = mpFindMaxInt(player_count, metrics[0].num_suicides, metrics[1].num_suicides, metrics[2].num_suicides, metrics[3].num_suicides);

    if (metrics[i].num_suicides > 0)
    {
        metrics[i].awards |= AWARD_MOSTSUICIDAL;
    }

    i = mpFindMinInt(player_count, metrics[0].num_shots, metrics[1].num_shots, metrics[2].num_shots, metrics[3].num_shots);

    if (metrics[i].num_shots < 100)
    {
        metrics[i].awards |= AWARD_WHONEEDSAMMO;
    }

    i = mpFindMinFloat(player_count, metrics[0].body_armor_pickups, metrics[1].body_armor_pickups, metrics[2].body_armor_pickups, metrics[3].body_armor_pickups);

    if (metrics[i].body_armor_pickups <= 2.0f)
    {
        metrics[i].awards |= AWARD_WHERESTHEARMOUR;
    }

    i = mpFindMaxFloat(player_count, metrics[0].body_armor_pickups, metrics[1].body_armor_pickups, metrics[2].body_armor_pickups, metrics[3].body_armor_pickups);

    if (metrics[i].body_armor_pickups > 6.0f)
    {
        metrics[i].awards |= AWARD_ACNEGATIVE10;
    }

    i = mpFindMaxInt(player_count, metrics[0].num_headshots, metrics[1].num_headshots, metrics[2].num_headshots, metrics[3].num_headshots);

    if (metrics[i].num_headshots > 0)
    {
        metrics[i].awards |= AWARD_MARKSMANSHIP;
    }

    i = mpFindMaxFloat(player_count, metrics[0].ks_ratio, metrics[1].ks_ratio, metrics[2].ks_ratio, metrics[3].ks_ratio);

    if (metrics[i].ks_ratio > 0.0f)
    {
        metrics[i].awards |= AWARD_MOSTPROFESSIONAL;
    }

    i = mpFindMaxFloat(player_count, metrics[0].kd_ratio, metrics[1].kd_ratio, metrics[2].kd_ratio, metrics[3].kd_ratio);

    if (metrics[i].kd_ratio > 0.0f)
    {
        metrics[i].awards |= AWARD_MOSTDEADLY;
    }

    i = mpFindMinFloat(player_count, metrics[0].kd_ratio, metrics[1].kd_ratio, metrics[2].kd_ratio, metrics[3].kd_ratio);
    metrics[i].awards |= AWARD_MOSTHARMLESS;

    i = mpFindMinInt(player_count, metrics[0].time_other_players_on_screen, metrics[1].time_other_players_on_screen, metrics[2].time_other_players_on_screen, metrics[3].time_other_players_on_screen);
    metrics[i].awards |= AWARD_MOSTCOWARDLY;

    i = mpFindMaxFloat(player_count, metrics[0].avg_km_per_hour, metrics[1].avg_km_per_hour, metrics[2].avg_km_per_hour, metrics[3].avg_km_per_hour);

    if (metrics[i].avg_km_per_hour > 10.0f)
    {
        metrics[i].awards |= AWARD_MOSTFRANTIC;
    }

    i = mpFindMinInt(player_count, metrics[0].damage_to_backside, metrics[1].damage_to_backside, metrics[2].damage_to_backside, metrics[3].damage_to_backside);
    metrics[i].awards |= AWARD_MOSTHONORABLE;

    i = mpFindMaxInt(player_count, metrics[0].damage_to_backside, metrics[1].damage_to_backside, metrics[2].damage_to_backside, metrics[3].damage_to_backside);

    if (metrics[i].damage_to_backside > 0 && (metrics[i].awards & AWARD_MOSTHONORABLE) == 0)
    {
        metrics[i].awards |= AWARD_MOSTDISHONORABLE;
    }

    i = mpFindMaxInt(player_count, metrics[0].longest_inning, metrics[1].longest_inning, metrics[2].longest_inning, metrics[3].longest_inning);

    if (metrics[i].longest_inning > 0)
    {
        metrics[i].awards |= AWARD_LONGESTINNINGS;
    }

    i = mpFindMinInt(player_count, metrics[0].shortest_inning, metrics[1].shortest_inning, metrics[2].shortest_inning, metrics[3].shortest_inning);

    if (metrics[i].shortest_inning > 0)
    {
        metrics[i].awards |= AWARD_SHORTESTINNINGS;
    }

    for (i = 0; i < player_count; i++)
    {
        if (g_playerPlayerData[i].most_killed_one_time == 4)
        {
            metrics[i].awards |= AWARD_QUADKILL;
        }
        else if (g_playerPlayerData[i].most_killed_one_time == 3)
        {
            metrics[i].awards |= AWARD_TRIPLEKILL;
        }
        else if (g_playerPlayerData[i].most_killed_one_time == 2)
        {
            metrics[i].awards |= AWARD_DOUBLEKILL;
        }
    }

#ifndef TARGET_N64
    portMpStatsGather(player_count, duration, metrics);
#endif

    // For each player, choose which two awards to actually give them.
    // Note that the first award checked is quad kill, but after that the awards
    // are checked randomly. So if a player has quad kill they'll definitely see
    // it on the endscreen, but this is not the case for triple kill or any
    // other awards.
    for (i = 0; i < player_count; i++)
    {
        s32 numdone = 0;
        s32 awardindex = 16;

        while (numdone == 0)
        {
            if (metrics[i].awards & (1 << awardindex))
            {
                metrics[i].awards &= ~(1 << awardindex);
                g_playerPointers[i]->ptr_text_first_mp_award = langGet(g_AwardNames[awardindex]);
                numdone = 1;
            }

            if (metrics[i].awards == 0)
            {
                numdone = 1;
            }

            awardindex = randomGetNext() % 17;
        }

        while (numdone < 2)
        {
            awardindex = randomGetNext() % 17;

            if (metrics[i].awards & (1 << awardindex))
            {
                metrics[i].awards &= ~(1 << awardindex);
                g_playerPointers[i]->ptr_text_second_mp_award = langGet(g_AwardNames[awardindex]);
                numdone = 2;
            }

            if (metrics[i].awards == 0)
            {
                numdone = 2;
            }
        }
    }
}


#ifndef TARGET_N64
/* Player 1 ends the match (midas.ini [display] p1_ends_match, default on): player
 * 1's START (only START; A and B still confirm for the presser alone) marks
 * every player finished, so one idle player can't pin the others on the
 * endscreen. Off restores the vanilla all-must-press rule. */
static s32 mpwatchP1ForcesExit(s32 player_num)
{
    extern s32 portMpP1EndsMatch(void); /* video.c; weak 1 headless */

    return (player_num == 0) && g_gameOverFlag && portMpP1EndsMatch()
        && joyGetButtonsPressedThisFrame(player_num, START_BUTTON);
}

static void mpwatchFinishAllPlayers(s32 player_count)
{
    s32 i;

    for (i = 0; i < player_count; i++)
    {
        g_playerPointers[i]->mpmenumode = MENU_FINISHED;
    }
}
#endif

void mpwatchMenuTick(void)
{
    s32 player_num;
    s32 player_count;
    s32 x_centered;
    s32 menu_count;
    s32 i;

    player_num = get_cur_playernum();
    player_count = getPlayerCount();
    x_centered = joyGetStickXInRange(player_num, -2, 1);

#ifndef TARGET_N64
    { /* Port binds in the MP pause menu: like the frontend menus and solo
       * watch, the Port `use` bind counts as A and either inventory bind as B,
       * so Port-bind players can confirm and back out.
     * Only while this player's menu is up, so an in-play use tap still reaches the door. */
        extern s32 portJoyPortUseMenuPressed(s32 pad);
        extern s32 portJoyPortBackMenuPressed(s32 pad);

        if (g_CurrentPlayer->mpmenuon)
        {
            if (portJoyPortUseMenuPressed(player_num))
            {
                portJoyInjectPressed((s8) player_num, A_BUTTON);
            }
            if (portJoyPortBackMenuPressed(player_num))
            {
                portJoyInjectPressed((s8) player_num, B_BUTTON);
            }
        }
    }
#endif

    // The player in shuffled position 0 drives down g_gameOverFlag which is both a flag and a timer.
    if (!get_player_position_in_shuffled(player_num) && (g_gameOverFlag >= 2))
    {
        g_gameOverFlag -= speedgraphframes;

        if (g_gameOverFlag <= 0) 
        { 
            g_gameOverFlag = 1;
        }
    }

    if (player_count != 1)
    {
        // If a player has their pause menu up when they die and the game isn't over, turn their menu off. 
        if ((g_CurrentPlayer->bonddead) && (!g_gameOverFlag))
        {
            g_CurrentPlayer->mpmenuon = FALSE;
            g_CurrentPlayer->healthdisplaytime = 0;
            return;
        }

        if (g_gameOverFlag < 2)
        {
            if (get_player_position_in_shuffled(player_num) == 0)
            {
                chevron_glow_timer += speedgraphframes;
                alt_gameover_msg_timer += speedgraphframes;

                // Toggle chevron glow
                if (chevron_glow_timer >= (PAL ? 16 : 20))
                {
                    chevron_glow_timer -= (PAL ? 16 : 20);
                    chevron_glow = !chevron_glow;
                }

                // Flip between "GAME OVER" and "START TO EXIT" text.
                if (alt_gameover_msg_timer >= (PAL ? 100 : 120))
                {
                    alt_gameover_msg_timer -= (PAL ? 100: 120);
                    alt_gameover_msg = !alt_gameover_msg;
                }
            }

            if (g_playerPerm->most_killed_one_life < g_CurrentPlayer->kills_this_life)
            {
                g_playerPerm->most_killed_one_life = g_CurrentPlayer->kills_this_life;
            }

            if (g_playerPerm->longest_inning < (getMissiontimer() - g_CurrentPlayer->lifestarttime60))
            {
                g_playerPerm->longest_inning = getMissiontimer() - g_CurrentPlayer->lifestarttime60;
            }

            if (g_CurrentPlayer->mpmenuon != FALSE)
            {
                if (mpwatchIsPlayerPressingRight(player_num) && mpwatchMenuCanGoRight())
                {
                    mpwatchPlayBeep();
                    g_CurrentPlayer->mpmenumode++;
                }
                else if (mpwatchIsPlayerPressingLeft(player_num) && mpwatchMenuCanGoLeft())
                {
                    mpwatchPlayBeep();
                    g_CurrentPlayer->mpmenumode--;
                }
                else if (mpwatchIsPlayerPressingRight(player_num) && (g_CurrentPlayer->mpmenumode == MENU_EXIT_CONFIRM))
                {
                    mpwatchPlayBeep();
                    g_CurrentPlayer->mpquitconfirm = 1;
                }
                else if (mpwatchIsPlayerPressingLeft(player_num) && (g_CurrentPlayer->mpmenumode == MENU_EXIT_CONFIRM))
                {
                    mpwatchPlayBeep();
                    g_CurrentPlayer->mpquitconfirm = 0;
                }
                else if (joyGetButtonsPressedThisFrame(player_num, A_BUTTON) && (g_CurrentPlayer->mpmenumode == MENU_PAUSE))
                {
                    mpwatchPlayBeep();
                    if (!g_pausedFlag)
                    {
                        g_pausedFlag = 1;
                        who_paused = get_cur_playernum();
                        lvlSetControlsLockedFlag(1);
                    }
                    else if (get_cur_playernum() == who_paused)
                    {
                        g_pausedFlag = 0;
                        lvlSetControlsLockedFlag(0);
                    }
                }
                else if (g_CurrentPlayer->mpmenumode == MENU_FINISHED)
                {
                    if (joyGetButtonsPressedThisFrame(player_num, B_BUTTON))
                    {
                        mpwatchPlayBeep();
                        g_CurrentPlayer->mpmenuon = TRUE;
                        g_CurrentPlayer->mpmenumode = MENU_SCORES;
                    }
#ifndef TARGET_N64
                    /* P1 confirmed first and is now waiting on the others -
                     * a second START ends it for everyone. */
                    else if (mpwatchP1ForcesExit(player_num))
                    {
                        mpwatchPlayBeep();
                        mpwatchFinishAllPlayers(player_count);
                        bossSetLoadedStage(LEVELID_TITLE);
                    }
#endif
                }
                else if (((joyGetButtonsPressedThisFrame(player_num, A_BUTTON | START_BUTTON)) && ((((g_CurrentPlayer->mpmenumode != MENU_EXIT)) && (g_CurrentPlayer->mpmenumode != MENU_EXIT_CONFIRM)) || ((g_CurrentPlayer->mpmenumode == MENU_EXIT_CONFIRM) && (g_CurrentPlayer->mpquitconfirm != 1)))) || (joyGetButtonsPressedThisFrame(player_num, B_BUTTON)))
                {
                    mpwatchPlayBeep();

                    if (g_gameOverFlag)
                    {
                        menu_count = 0;
                        g_CurrentPlayer->mpmenumode = MENU_FINISHED;

#ifndef TARGET_N64
                        if (mpwatchP1ForcesExit(player_num))
                        {
                            mpwatchFinishAllPlayers(player_count);
                        }
#endif

                        for (i = 0; i < player_count; i++)
                        {
                            if (g_playerPointers[i]->mpmenumode == MENU_FINISHED)
                            {
                                menu_count++;
                            }
                        }

                        if (menu_count == player_count)
                        {
                            bossSetLoadedStage(LEVELID_TITLE);
                        }
                    }
                    else
                    {
                        g_CurrentPlayer->mpmenuon = FALSE;
                        g_CurrentPlayer->healthdisplaytime = (PAL ? 50 : 60);

                        if (get_cur_playernum() == who_paused)
                        {
                            g_pausedFlag = 0;
                            lvlSetControlsLockedFlag(0);
                        }
                    }
                }
                else if ((joyGetButtonsPressedThisFrame(player_num, A_BUTTON | START_BUTTON)) && (g_CurrentPlayer->mpmenumode == MENU_EXIT))
                {
                    mpwatchPlayBeep();
                    g_CurrentPlayer->mpmenumode = MENU_EXIT_CONFIRM;
                    g_CurrentPlayer->mpquitconfirm = 0;
                }
                else if (joyGetButtonsPressedThisFrame(player_num, A_BUTTON | START_BUTTON))
                {
                    if ((g_CurrentPlayer->mpmenumode == MENU_EXIT_CONFIRM) && (g_CurrentPlayer->mpquitconfirm == 1))
                    {
                        mpwatchPlayBeep();
                        g_CurrentPlayer->mpmenuon = FALSE;
                        g_CurrentPlayer->healthdisplaytime = 0;
#ifndef TARGET_N64
                        portMpStatsAbandoned();   /* the stats count it as played and abandoned, never won */
#endif
                        mpCalculateAwards(FALSE);
                    }
                }

                if ((x_centered == 0) || (x_centered == -1))
                {
                    g_CurrentPlayer->mpjoywascentre = 1;
                    return;
                }

                g_CurrentPlayer->mpjoywascentre = 0;
                return;
            }

            if (joyGetButtonsPressedThisFrame(player_num, START_BUTTON))
            {
                mpwatchPlayBeep();
                g_CurrentPlayer->mpmenuon = TRUE;
                g_CurrentPlayer->mpmenumode = MENU_SCORES;
                g_CurrentPlayer->mpjoywascentre = 1;
                g_CurrentPlayer->apparenthealth = g_CurrentPlayer->bondhealth;
                g_CurrentPlayer->apparentarmour = g_CurrentPlayer->bondarmour;
            }
        }
    }
}


Gfx *display_text_for_playerdata_on_MP_menu(Gfx *gdl, s32 x, s32 y, s32 points, TEXTCOLORS text_color) {

    s32 textX;
    s32 textY;
    s32 textwidth;
    s32 textheight;
    s32 unused;
    u16 *text;
    s16 viX;
    s32 viY;

    sprintf(&text, "%d", points);

    textMeasure(&textheight, &textwidth, &text, ptrFontBankGothicChars, ptrFontBankGothic, 0);

    textX = x - (textwidth >> 1);
    textY = y;

    switch (text_color) 
    {
        case GREEN_NORMAL:
            viX = viGetX();
            viY = viGetY();
            gdl = textRender(gdl, &textX, &textY, &text, ptrFontBankGothicChars, ptrFontBankGothic, 0xFF00B0, viX, viY, 0, 0);
            break;

        case GREEN_HIGHLIGHT:
            viX = viGetX();
            viY = viGetY();
            gdl = textRenderOutlined(gdl, &textX, &textY, &text, ptrFontBankGothicChars, ptrFontBankGothic, 0xA0FFA0F0, 0x7000A0, viX, viY, 0, 0);
            break;

        case RED_NORMAL:
            viX = viGetX();
            viY = viGetY();
            gdl = textRender(gdl, &textX, &textY, &text, ptrFontBankGothicChars, ptrFontBankGothic, 0xFF4040B0, viX, viY, 0, 0);
            break;

        case RED_HIGHLIGHT:
            viX = viGetX();
            viY = viGetY();
            gdl = textRenderOutlined(gdl, &textX, &textY, &text, ptrFontBankGothicChars, ptrFontBankGothic, 0xFFA0A0F0, 0x700000A0, viX, viY, 0, 0);
            break;

        case BLUE_NORMAL:
            viX = viGetX();
            viY = viGetY();
            gdl = textRender(gdl, &textX, &textY, &text, ptrFontBankGothicChars, ptrFontBankGothic, 0x4040FFB0, viX, viY, 0, 0);
            break;

        case BLUE_HIGHLIGHT:
            viX = viGetX();
            viY = viGetY();
            gdl = textRenderOutlined(gdl, &textX, &textY, &text, ptrFontBankGothicChars, ptrFontBankGothic, 0xA0A0FFF0, 0x70A0, viX, viY, 0, 0);
            break;
    }

    return gdl;
}


//rodata
/*8005BC20*/
const char ascii_MP_watch_menu_BLANK[] = "";
const char ascii_MP_watch_menu_left_chevron[] = "<";
const char ascii_MP_watch_menu_right_chevron[] = ">";
const char ascii_pnum_KILLS[] = "%s%d %s";
const char ascii_pnum_LOSSES[] = "%s%d %s";


#ifndef TARGET_N64
/* TEAMS IN ANY SCENARIO (front.c portMpTeamsOn): every player scores their
 * team's total. Kills score Rare's team way (an enemy +1, a team-mate or
 * yourself -1); Flag Tag adds the team's flag time; You Only Live Twice takes
 * the team's best survivor. Rare's own team scenarios take the kills path, so
 * they score as before. */
static s32 portMpTeamPoints(s32 playernum)
{
    const s32 team = portMpTeamOf(playernum);
    const s32 player_count = getPlayerCount();
    s32 points = 0;
    s32 i, j;

    for (i = 0; i < player_count; i++)
    {
        if (portMpTeamOf(i) != team)
        {
            continue;
        }
        switch (get_scenario())
        {
            case SCENARIO_YOLT:
                if (MAX_PLAYER_COUNT - g_playerPlayerData[i].order_out_in_yolt > points)
                {
                    points = MAX_PLAYER_COUNT - g_playerPlayerData[i].order_out_in_yolt;
                }
                break;

            case SCENARIO_TLD:
                points += g_playerPlayerData[i].flag_counter;
                break;

            default:
                for (j = 0; j < player_count; j++)
                {
                    if (portMpTeamOf(j) != team)
                    {
                        points += g_playerPlayerData[i].kill_counts[j];
                    }
                    else
                    {
                        points -= g_playerPlayerData[i].kill_counts[j];
                    }
                }
                if (get_scenario() == SCENARIO_MWTGG)
                {
                    points += g_playerPlayerData[i].killed_gg_owner_count * (player_count - 2);
                }
                break;
        }
    }
    return points;
}
#endif

s32 get_points_for_mp_player(s32 playernum)
{
    s32 team_or_token;
    s32 player_count;
    s32 i;
    s32 j;
    s32 points;

    // The have_token_or_goldengun field is also treated as a team identifier.
    team_or_token = g_playerPlayerData[playernum].have_token_or_goldengun;

    player_count = getPlayerCount();

    points = 0;

#ifndef TARGET_N64
    if (portMpTeamsOn())
    {
        return portMpTeamPoints(playernum);
    }
#endif

    switch (get_scenario())
    {
        case SCENARIO_NORMAL:
        case SCENARIO_MWTGG:
        case SCENARIO_LTK:
            for (i = 0; i < player_count; i++)
            {
                if (i != playernum)
                {
                    points += g_playerPlayerData[playernum].kill_counts[i];
                }
                else
                {
                    points -= g_playerPlayerData[i].kill_counts[playernum];
                }
            }

            points += g_playerPlayerData[playernum].killed_gg_owner_count * (player_count - 2);
            break;

        case SCENARIO_YOLT:
            points = MAX_PLAYER_COUNT - g_playerPlayerData[playernum].order_out_in_yolt;
            break;

        case SCENARIO_TLD:
            points = g_playerPlayerData[playernum].flag_counter;
            break;
            
        case SCENARIO_2v2:
        case SCENARIO_3v1:
        case SCENARIO_2v1:
            for (i = 0; i < player_count; i++)
            {
                if (g_playerPlayerData[i].have_token_or_goldengun == team_or_token)
                {
                    for (j = 0; j < player_count; j++)
                    {
                        if (g_playerPlayerData[j].have_token_or_goldengun != team_or_token)
                        {
                            points += g_playerPlayerData[i].kill_counts[j];
                        }
                        else
                        {
                            points -= g_playerPlayerData[i].kill_counts[j];
                        }
                    }
                }
            }
            break;

        default:
            break;
    }

    return points;
}


void write_playerrank_to_buffer(char *buffer, s32 playernum)
{
    s32 scenario;
    s32 count;
    s32 scores[4];
    s32 players[4];
    s32 tmp;
    s32 i;
    s32 j;

    scenario = get_scenario();
    count = getPlayerCount();

    for (i = 0; i < count; i++)
    {
#ifndef TARGET_N64
        scores[i] = portMpRankScore(i);   /* Lives: the last standing ranks 1st */
#else
        scores[i] = get_points_for_mp_player(i);
#endif
        players[i] = i;
    }

    for (j = 0; j < count; j++)
    {
        for (i = 0; i < (count - 1); i++)
        {
            if (scores[i] < scores[i + 1])
            {
                tmp = scores[i + 1];
                scores[i + 1] = scores[i];
                scores[i] = tmp;
                tmp = players[i + 1];
                players[i + 1] = players[i];
                players[i] = tmp;
            }
        }

    }

    for (i = 0; i < count; i++)
    {
        if (playernum == players[i])
        {
            break;
        }
    }

    for (j = 0; j <= i; j++)
    {
        if (scores[j] == scores[i])
        {
            break;
        }
    }
#ifndef TARGET_N64
    if (portMpTeamsOn() && j > 1)
    {
        j = 1;   /* two sides: the losing side is 2nd, in any scenario */
    }
#endif

    switch (j)
    {
        case 0:
            sprintf(buffer, langGet(getStringID(LMPMENU, MPMENU_STR_11_RANK1ST))); /* Rank: 1st */
            break;
        case 1:
            sprintf(buffer, langGet(getStringID(LMPMENU, MPMENU_STR_12_RANK2ND))); /* Rank: 2nd */
            break;
        case 2:
            if ((scenario != SCENARIO_2v2) && (scenario != SCENARIO_2v1))
            {
                sprintf(buffer, langGet(getStringID(LMPMENU, MPMENU_STR_13_RANK3RD))); /* Rank: 3rd */
            }
            else
            {
                sprintf(buffer, langGet(getStringID(LMPMENU, MPMENU_STR_12_RANK2ND))); /* Rank: 2nd */
            }
            break;
        case 3:
            if (scenario != SCENARIO_3v1)
            {
                sprintf(buffer, langGet(getStringID(LMPMENU, MPMENU_STR_14_RANK4TH))); /* Rank: 4th */
            }
            else
            {
                sprintf(buffer, langGet(getStringID(LMPMENU, MPMENU_STR_12_RANK2ND))); /* Rank: 2nd */
            }
            break;
    }

}


s32 mpwatchShouldDisplayRank(s32 param_1)
{
    switch(get_scenario())
    {
        case SCENARIO_NORMAL:
        case SCENARIO_TLD:
        case SCENARIO_MWTGG:
        case SCENARIO_LTK:
        case SCENARIO_2v2:
        case SCENARIO_3v1:
        case SCENARIO_2v1:
            return 1;
        case SCENARIO_YOLT:
            return param_1 ? 0 : 1;
        default:
#ifdef DEBUG
            osSyncPrintf("Invalid scenario %d!", get_scenario());
#endif
        do {} while (1);
    }
}

s32 mpwatchShouldDisplayScore(s32 param_1)
{
    switch(get_scenario())
    {
        case SCENARIO_NORMAL:
        case SCENARIO_MWTGG:
        case SCENARIO_LTK:
        case SCENARIO_2v2:
        case SCENARIO_3v1:
        case SCENARIO_2v1:
            return 1;
        break;
        case SCENARIO_YOLT:
        case SCENARIO_TLD:
            return 0;
        break;
        default:
#ifdef DEBUG
            osSyncPrintf("Invalid scenario %d!", get_scenario());
#endif
            do {} while (1);
    }
}


/**
 * Draws the text for the multiplayer pause menu, post-game screens, and the "press start" prompt shown after death.
 * 
 * When the match is running it works through a horizontal carousel of pages starting at the "Score" page.
 * Losses << Kills << Score >> Pause >> Exit
 * 
 * When the game is over it displays these screens:
 * WOC/Awards << Losses << Kills << Score
 *
 * With the menu closed, nothing is drawn unless the player is dead and their
 * death animation has finished, in which case the continue prompt is centred
 * in the viewport. That's suppressed in YOLT once two of the player's lives are
 * gone since there is nothing to continue to.
 *
 * @param gdl display list to append to
 * @return the advanced display list pointer
 */
Gfx *mp_watch_menu_display(Gfx *gdl)
{
    s32 curplayernum;
    s32 player_count;
    s32 x;
    s32 y;
    s32 k;
    s32 textwidth;
    s32 textheight;
    s32 m;
    s32 h1;
    intptr_t h2;
    /* [16], not [4]: write_playerrank_to_buffer sprintf's strings like
     * "Rank: 1st" (9 chars + null = 10) straight in - the 4-byte buffer
     * overflowed on every call (multiplayer watch-menu kills/losses/ points
     * screens). */
#ifdef TARGET_N64
    char rankbuffer[16];
#else
    char rankbuffer[48];   /* a profile's name stands where "P1" stood (portProfilePlayerLabel below) */
#endif
    s32 two_player_x_offset;
    char *text;
    s32 scores[4];
    s32 i;
    TEXTCOLORS current_colour;
    TEXTCOLORS same_team_colour;
    TEXTCOLORS other_team_colour;
    MPSCENARIOS scenario;
    s32 fav_textheight;
    s32 fav_textwidth;
    s32 fav_x_offset;
    s32 x3;
    s32 y3;
    s32 textwidth3;
    s32 textheight3;
    char *text3;
    s32 self_paused;
    s32 total_kills_against_current;
    s16 x2;
    s16 viewleft;
    s32 colour;
    intptr_t q;
 
    curplayernum = get_cur_playernum();
    player_count = getPlayerCount();
    self_paused = 0;
 
    if (player_count == 1)
    {
        return gdl;
    }
 
    if (g_CurrentPlayer->mpmenuon)
    {
        gdl = microcode_constructor(gdl);
    
        if (player_count == 2)
        {
            two_player_x_offset = 80;
        }
        else
        {
            two_player_x_offset = 0;
        }

        switch (g_CurrentPlayer->mpmenumode)
        {
            case MENU_GOWOC:
            case MENU_LOSSES:
            case MENU_KILLS:
            case MENU_SCORES:
                if (!g_gameOverFlag)
                {
                    text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_15_PLAY)); /* PLAY */
                }
                else
                {
                    if (alt_gameover_msg)
                    {
                        text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_16_GAMEOVER)); /* GAME OVER */
                    }
                    else
                    {
                        text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_17_STARTTTOEXIT)); /* START TO EXIT */
                    }
                }
                break;
            case MENU_FINISHED:
                text = (char *) ascii_MP_watch_menu_BLANK;
                break;
            case MENU_PAUSE:
                if (g_pausedFlag)
                {
                    text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_18_PAUSED)); /* PAUSED */
                    if (get_cur_playernum() == who_paused)
                    {
                        self_paused = 1;
                    }
                }
                else
                {
                    text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_19_PAUSE)); /* PAUSE */
                }
                break;
            case MENU_EXIT:
            case MENU_EXIT_CONFIRM:
                text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_1A_EXIT)); /* EXIT */
                x = (viGetViewLeft() + two_player_x_offset) + 65;
                break;
        }
 
        textMeasure(&textheight, &textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
        x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 80;
        y = (viGetViewTop() - (textheight >> 1)) + (22 + MPMENU_YOFF);
 
        if (self_paused)
        {
            viewleft = viGetX(); 
            h1 = viGetY();
            gdl = textRenderOutlined(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0xa0ffa0f0, 0x007000a0, viewleft, h1, 0, 0);
        }
        else
        {
            viewleft = viGetX(); 
            h1 = viGetY();
            gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
        }
 
        if (mpwatchMenuCanGoLeft())
        {
            viewleft = viGetViewLeft();
    
            /**
             *  Colour is reused to store a pixel offset here. Efforts to introduce a new variable to store the offset resulted in stack problem I could not solve,
             *  so it's possible Rare really did reuse one variable for this. Perhaps it was not named "colour" but something more generic like "tmp."
             */ 
            colour = g_gameOverFlag ? 10 : 0, x = ((viewleft + two_player_x_offset) - colour) + 40;
    
            if (g_gameOverFlag)
            {
                x -= 8;
            }
 
            y = viGetViewTop() + (22 + MPMENU_YOFF);
 
            if (!chevron_glow)
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, (char *) ascii_MP_watch_menu_left_chevron, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }
            else
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRenderOutlined(gdl, &x, &y, (char *) ascii_MP_watch_menu_left_chevron, ptrFontBankGothicChars, ptrFontBankGothic, 0xa0ffa0f0, 0x007000a0, viewleft, h1, 0, 0);
            }
        }
 
        if (mpwatchMenuCanGoRight())
        {
            viewleft = viGetViewLeft();

            // Colour is again used to store a pixel offset.
            colour = g_gameOverFlag ? 10 : 0, x = ((colour + 112) + viewleft) + two_player_x_offset;
 
            if (g_gameOverFlag)
            {
                x += 8;
            }
 
            y = viGetViewTop() + (22 + MPMENU_YOFF);
 
            if (!chevron_glow)
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, (char *) ascii_MP_watch_menu_right_chevron, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }
            else
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRenderOutlined(gdl, &x, &y, (char *) ascii_MP_watch_menu_right_chevron, ptrFontBankGothicChars, ptrFontBankGothic, 0xa0ffa0f0, 0x007000a0, viewleft, h1, 0, 0);
            }
        }
 
        if ((g_CurrentPlayer->mpmenumode == MENU_SCORES) || (g_CurrentPlayer->mpmenumode == MENU_PAUSE))
        {
            if (player_count > 0)
            {
                i = 0;
 
                do
                {
                    scores[i] = get_points_for_mp_player(i);
                    i++;
                }
                while (i != player_count);
            }
 
            fav_x_offset = (g_gameOverFlag == 0); 
 
            if (fav_x_offset) 
            { 
                fav_x_offset = (g_stopPlayFlag == 0); 
            }
 
            if (mpwatchShouldDisplayRank(fav_x_offset))
            {
                write_playerrank_to_buffer(rankbuffer, curplayernum);
                textMeasure(&textheight, &textwidth, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0);
 
                x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 80;
                y = (viGetViewTop() - (textheight >> 1)) + (37 + MPMENU_YOFF);
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }

            fav_x_offset = (g_gameOverFlag == 0); 
 
            if (fav_x_offset) 
            { 
                fav_x_offset = (g_stopPlayFlag == 0); 
            }
 
            if (mpwatchShouldDisplayScore(fav_x_offset))
            {
                scenario = get_scenario();
                text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_1B_SCORES)); /* SCORES */
                textMeasure(&textheight, &textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
                x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 80;
                y = (viGetViewTop() - (textheight >> 1)) + (53 + MPMENU_YOFF);
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
 
#ifndef TARGET_N64
                if (portMpColourSide(curplayernum) >= 0)
#else
                if (((((scenario == SCENARIO_2v2) || (scenario == SCENARIO_3v1)) || (scenario == SCENARIO_2v1)) || (scenario == SCENARIO_TLD)) || (scenario == SCENARIO_MWTGG))
#endif
                {
                    if (MPMENU_SIDE(curplayernum) == 0)
                    {
                        current_colour = RED_HIGHLIGHT;
                        same_team_colour = RED_NORMAL;
                        other_team_colour = BLUE_NORMAL;
                    }
                    else
                    {
                        current_colour = BLUE_HIGHLIGHT;
                        same_team_colour = BLUE_NORMAL;
                        other_team_colour = RED_NORMAL;
                    }
                }
                else
                {
                    current_colour = GREEN_HIGHLIGHT;
                    same_team_colour = GREEN_NORMAL;
                    other_team_colour = GREEN_NORMAL;
                }
 
                if (player_count == 2)
                {
                    viewleft = viGetViewLeft();
                    x2 = viGetViewTop();

                    if (curplayernum == 0)
                    {
                        colour = current_colour;
                    }
                    else
                    {
                        q = MPMENU_SIDE(0) == MPMENU_SIDE(curplayernum) ? same_team_colour : other_team_colour;
                        colour = q;
                    }
 
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, x2 + (70 + MPMENU_YOFF), scores[0], colour);
                    viewleft = viGetViewLeft();
                    x2 = viGetViewTop();

                    curplayernum == 1 ? (colour = current_colour) : (q = MPMENU_SIDE(1) == MPMENU_SIDE(curplayernum) ? same_team_colour : other_team_colour, colour = q);

                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, x2 + (86 + MPMENU_YOFF), scores[1], colour);
                }
                else
                {
                    viewleft = viGetViewLeft();
                    x2 = viGetViewTop();
 
                    if (curplayernum == 0)
                    {
                        colour = current_colour;
                    }
                    else
                    {
                        q = MPMENU_SIDE(0) == MPMENU_SIDE(curplayernum) ? same_team_colour : other_team_colour;
                        colour = q;
                    }
 
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, x2 + (70 + MPMENU_YOFF), scores[0], colour);
                    viewleft = viGetViewLeft();
                    x2 = viGetViewTop();
 
                    if (curplayernum == 1)
                    {
                        colour = current_colour;
                    }
                    else
                    {
                        q = MPMENU_SIDE(1) == MPMENU_SIDE(curplayernum) ? same_team_colour : other_team_colour;
                        colour = q;
                    }
 
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, x2 + (70 + MPMENU_YOFF), scores[1], colour);
                    viewleft = viGetViewLeft();
                    x2 = viGetViewTop();
 
                    if (curplayernum == 2)
                    {
                        colour = current_colour;
                    }
                    else
                    {
                        q = MPMENU_SIDE(2) == MPMENU_SIDE(curplayernum) ? same_team_colour : other_team_colour;
                        colour = q;
                    }
 
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, x2 + (86 + MPMENU_YOFF), scores[2], colour);
 
                    if (player_count == 4)
                    {
                        viewleft = viGetViewLeft();
                        x2 = viGetViewTop();

                        curplayernum == 3 ? (colour = current_colour) : (q = MPMENU_SIDE(3) == MPMENU_SIDE(curplayernum) ? same_team_colour : other_team_colour, colour = q);
                        gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, x2 + (86 + MPMENU_YOFF), scores[3], colour);
                    }
                }
            }

            // Keep colour live through this branch to reproduce the original register allocation.
            if (colour);
        }
        else if (g_CurrentPlayer->mpmenumode == MENU_KILLS)
        {
            fav_x_offset = (g_gameOverFlag == 0); 

            if (fav_x_offset) 
            { 
                fav_x_offset = (g_stopPlayFlag == 0); 
            } 

            if (mpwatchShouldDisplayRank(fav_x_offset))
            {
                write_playerrank_to_buffer(rankbuffer, curplayernum);
                textMeasure(&textheight, &textwidth, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0);
                x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 80;
                y = (viGetViewTop() - (textheight >> 1)) + (37 + MPMENU_YOFF);
                viewleft = viGetX(); h1 = viGetY();
                gdl = textRender(gdl, &x, &y, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }
 
            q = (intptr_t) langGet(getStringID(LMPMENU, MPMENU_STR_1C_P)); /* P */
 
            // Must remain a comma expression for matching
            h2 = (intptr_t) langGet(getStringID(LMPMENU, MPMENU_STR_1D_KILLS)), /* KILLS */
                sprintf(rankbuffer, ascii_pnum_KILLS, (char *) q, curplayernum + 1, (char *) h2); /* -> "P<n> KILLS" */
#ifndef TARGET_N64
            {   /* PORT: the profile's name is shown instead of P1 to P4 in game
                 * - port_profiles.c; NULL = none */
                extern const char *portProfilePlayerLabel(s32 player);
                const char *pname = portProfilePlayerLabel(curplayernum);

                if (pname != NULL)
                {
                    snprintf(rankbuffer, sizeof(rankbuffer), "%s %s", pname, (char *) h2);
                }
            }
#endif
 
            textMeasure(&textheight, &textwidth, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0);
            x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 80;
            y = (viGetViewTop() - (textheight >> 1)) + (53 + MPMENU_YOFF);
            viewleft = viGetX(); 
            h1 = viGetY();
            gdl = textRender(gdl, &x, &y, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
 
            if (player_count == 2)
            {
                if (curplayernum != 0)
                {
                    viewleft = viGetViewLeft(); h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[curplayernum].kill_counts[0], GREEN_NORMAL);
                }
                if (curplayernum != 1)
                {
                    viewleft = viGetViewLeft(); h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[curplayernum].kill_counts[1], GREEN_NORMAL);
                }
            }
            else
            {
                if (curplayernum != 0)
                {
                    viewleft = viGetViewLeft(); h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[curplayernum].kill_counts[0], GREEN_NORMAL);
                }
                if (curplayernum != 1)
                {
                    viewleft = viGetViewLeft(); h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[curplayernum].kill_counts[1], GREEN_NORMAL);
                }
                if (curplayernum != 2)
                {
                    viewleft = viGetViewLeft(); h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[curplayernum].kill_counts[2], GREEN_NORMAL);
                }
                if ((player_count == 4) && (curplayernum != 3))
                {
                    viewleft = viGetViewLeft(); h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[curplayernum].kill_counts[3], GREEN_NORMAL);
                }
            }
        }
        else if (g_CurrentPlayer->mpmenumode == MENU_LOSSES)
        {
            fav_x_offset = (g_gameOverFlag == 0); 

            if (fav_x_offset) 
            { 
                fav_x_offset = (g_stopPlayFlag == 0); 
            } 

            if (mpwatchShouldDisplayRank(fav_x_offset))
            {
                write_playerrank_to_buffer(rankbuffer, curplayernum);
                textMeasure(&textheight, &textwidth, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0);
                x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 80;
                y = (viGetViewTop() - (textheight >> 1)) + (37 + MPMENU_YOFF);
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }
 
            q = (intptr_t) langGet(getStringID(LMPMENU, MPMENU_STR_1C_P)); /* P */
 
            // Must remain a comma expression for matching.
            h2 = (intptr_t) langGet(getStringID(LMPMENU, MPMENU_STR_1E_LOSSES)), /* LOSSES */
                sprintf(rankbuffer, ascii_pnum_LOSSES, (char *) q, curplayernum + 1, (char *) h2); /* -> "P<n> LOSSES" */
#ifndef TARGET_N64
            {   /* PORT: the profile's name is shown instead of P1 to P4 in game
                 * - port_profiles.c; NULL = none */
                extern const char *portProfilePlayerLabel(s32 player);
                const char *pname = portProfilePlayerLabel(curplayernum);

                if (pname != NULL)
                {
                    snprintf(rankbuffer, sizeof(rankbuffer), "%s %s", pname, (char *) h2);
                }
            }
#endif
 
            textMeasure(&textheight, &textwidth, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0);
            x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 80;
            y = (viGetViewTop() - (textheight >> 1)) + (53 + MPMENU_YOFF);
            viewleft = viGetX(); 
            h1 = viGetY();
            gdl = textRender(gdl, &x, &y, rankbuffer, ptrFontBankGothicChars, ptrFontBankGothic, 0xff4040b0, viewleft, h1, 0, 0);
 
            if (player_count == 2)
            {
                if (curplayernum != 0)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[0].kill_counts[curplayernum], GREEN_NORMAL);
                }
                else if (g_playerPlayerData[0].kill_counts[0] > 0)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[0].kill_counts[curplayernum], RED_HIGHLIGHT);
                }
                if (curplayernum != 1)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[1].kill_counts[curplayernum], GREEN_NORMAL);
                }
                else if (g_playerPlayerData[1].kill_counts[1] > 0)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 80, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[1].kill_counts[curplayernum], RED_HIGHLIGHT);
                }
            }
            else
            {
                if (curplayernum != 0)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[0].kill_counts[curplayernum], GREEN_NORMAL);
                }
                else if (g_playerPlayerData[0].kill_counts[0] > 0)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[0].kill_counts[curplayernum], RED_HIGHLIGHT);
                }

                if (curplayernum != 1)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[1].kill_counts[curplayernum], GREEN_NORMAL);
                }
                else if (g_playerPlayerData[1].kill_counts[1] > 0)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, h1 + (70 + MPMENU_YOFF), g_playerPlayerData[1].kill_counts[curplayernum], RED_HIGHLIGHT);
                }

                if (curplayernum != 2)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[2].kill_counts[curplayernum], GREEN_NORMAL);
                }
                else if (g_playerPlayerData[2].kill_counts[2] > 0)
                {
                    viewleft = viGetViewLeft(); 
                    h1 = viGetViewTop();
                    gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 64, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[2].kill_counts[curplayernum], RED_HIGHLIGHT);
                }

                if (player_count == 4)
                {
                    if (curplayernum != 3)
                    {
                        viewleft = viGetViewLeft(); 
                        h1 = viGetViewTop();
                        gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[3].kill_counts[curplayernum], GREEN_NORMAL);
                    }
                    else if (g_playerPlayerData[3].kill_counts[3] > 0)
                    {
                        viewleft = viGetViewLeft(); 
                        h1 = viGetViewTop();
                        gdl = display_text_for_playerdata_on_MP_menu(gdl, (viewleft + two_player_x_offset) + 96, h1 + (86 + MPMENU_YOFF), g_playerPlayerData[3].kill_counts[curplayernum], RED_HIGHLIGHT);
                    }
                }
            }
        }
        else if (g_CurrentPlayer->mpmenumode == MENU_GOWOC)
        {
            fav_x_offset = two_player_x_offset;

#ifdef TARGET_N64
            if (player_count >= 3)
            {
                if (curplayernum & 1)
                {
                    fav_x_offset = two_player_x_offset - 7;
                }
                else
                {
                    fav_x_offset = two_player_x_offset + 7;
                }
            }
#endif
            /* PORT: no per-column +/-7 - Weapon of Choice sits centred in every
             * quadrant alike (gunfire.c, "same layout in every quadrant") */
 
            text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_1F_WEAPONOFCHOICE)); /* Weapon of choice: */
            textMeasure(&fav_textheight, &fav_textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
            x = ((viGetViewLeft() + fav_x_offset) - (fav_textwidth >> 1)) + 80;
            y = (viGetViewTop() - (fav_textheight >> 1)) + (37 + MPMENU_YOFF);
            viewleft = viGetX(); h1 = viGetY();
            gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            text = frontGetPlayersFavoriteWeaponInHand(curplayernum, 0);
            textMeasure(&fav_textheight, &fav_textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
            x = ((viGetViewLeft() + fav_x_offset) - (fav_textwidth >> 1)) + 80;
            x2 = viGetViewTop();
 
            if (j_text_trigger) 
            { 
                i = 4; 
            } 
            else 
            { 
                i = 0; 
            } 

            y = ((i + (u32) x2) - (fav_textheight >> 1)) + (53 + MPMENU_YOFF);
 
            viewleft = viGetX(); 
            h1 = viGetY();
            gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
 
            if (g_CurrentPlayer->ptr_text_first_mp_award)
            {
                text = (char *) g_CurrentPlayer->ptr_text_first_mp_award;
                textMeasure(&fav_textheight, &fav_textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
                x = ((viGetViewLeft() + fav_x_offset) - (fav_textwidth >> 1)) + 80;
                y = (viGetViewTop() - (fav_textheight >> 1)) + (75 + MPMENU_YOFF);
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }

            if (g_CurrentPlayer->ptr_text_second_mp_award)
            {
                text = (char *) g_CurrentPlayer->ptr_text_second_mp_award;
                textMeasure(&fav_textheight, &fav_textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
                x = ((viGetViewLeft() + fav_x_offset) - (fav_textwidth >> 1)) + 80;
                y = (viGetViewTop() - (fav_textheight >> 1)) + (88 + MPMENU_YOFF);
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }
        }

        if (g_CurrentPlayer->mpmenumode == MENU_EXIT_CONFIRM)
        {
            text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_20_CANCEL)); /* cancel */
            textMeasure(&textheight, &textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
            x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 54;
            y = (viGetViewTop() - (textheight >> 1)) + (54 + MPMENU_YOFF);
 
            if (g_CurrentPlayer->mpquitconfirm == 0)
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRenderOutlined(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0xa0ffa0f0, 0x007000a0, viewleft, h1, 0, 0);
            }
            else
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }
 
            text = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_21_CONFIRM)); /* confirm */
            textMeasure(&textheight, &textwidth, text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
            x = ((viGetViewLeft() + two_player_x_offset) - (textwidth >> 1)) + 104;
            y = (viGetViewTop() - (textheight >> 1)) + (54 + MPMENU_YOFF);
 
            if (g_CurrentPlayer->mpquitconfirm == 1)
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRenderOutlined(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0xa0ffa0f0, 0x007000a0, viewleft, h1, 0, 0);
            }
            else
            {
                viewleft = viGetX(); 
                h1 = viGetY();
                gdl = textRender(gdl, &x, &y, text, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            }
        }
        gdl = combiner_bayer_lod_perspective(gdl);
    }
    else if (((((g_CurrentPlayer->bonddead) && (g_CurrentPlayer->deathanimfinished)) && (g_CurrentPlayer->redbloodfinished)) && (!g_stopPlayFlag)) && (!g_gameOverFlag))
    {
        total_kills_against_current = 0;
 
        for (m = 0; m < player_count; m++)
        {
            total_kills_against_current += g_playerPlayerData[m].kill_counts[curplayernum];
        }
 
#ifndef TARGET_N64
        if ((portMpLives() == 0) || (total_kills_against_current < portMpLives()))
#else
        if ((get_scenario() != SCENARIO_YOLT) || (total_kills_against_current < 2))
#endif
        {
            gdl = bgScissorCurrentPlayerViewDefault(gdl);
            gdl = microcode_constructor(gdl);
            text3 = (char *) langGet(getStringID(LMPMENU, MPMENU_STR_22_PRESSSTART_LF)); /* press start */
            textMeasure(&textheight3, &textwidth3, text3, ptrFontBankGothicChars, ptrFontBankGothic, 0);
            x2 = viGetViewLeft();
            x3 = (x2 + (viGetViewWidth() >> 1)) - (textwidth3 >> 1);
            x2 = viGetViewTop();
            y3 = (x2 + (viGetViewHeight() >> 1)) - (textheight3 >> 1);
#ifndef VERSION_US
            gdl = microcode_constructor_related_to_menus(gdl, x3 - 1, y3 - 1, x3 + textwidth3 + 1, y3 + textheight3 + 1, 0);
#endif
            viewleft = viGetX(); 
            h1 = viGetY();
            gdl = textRender(gdl, &x3, &y3, text3, ptrFontBankGothicChars, ptrFontBankGothic, 0x00ff00b0, viewleft, h1, 0, 0);
            gdl = combiner_bayer_lod_perspective(gdl);
        }
    }
 
    return gdl;
}


s32 mpwatchShouldDisplayGauges(void)
{
    return g_gameOverFlag ? FALSE : (g_CurrentPlayer->mpmenuon | (g_CurrentPlayer->healthdisplaytime > 0));
}


s32 checkGamePaused(void) 
{
    return g_pausedFlag;
}
