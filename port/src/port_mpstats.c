/**
 * port_mpstats.c - lifetime multiplayer stats per profile (see docs/port/mp-stats-tracker.md).
 *
 * Every MP match end (time limit, point limit, or the pause menu's Exit, which
 * counts as played and abandoned but never a win) runs mpCalculateAwards, which
 * hands one PortMpMatchLine per seat to portMpStatsRecord. Each seat with a
 * loaded profile gets the line added to Data/Profiles/<NAME>.gestats:
 *
 *     [mpstats]
 *     version=1
 *     matches=12
 *     ...          one key per PortMpStat, sums except the two bests
 *
 * This is a separate file because the profile itself is re-derived and
 * rewritten whole by its autosave (port_profiles.c). A missing key reads 0, an
 * unknown one is ignored, and the version bumps on any layout change.
 *
 * Not recorded: a seat with no profile, solo play, demo playback or recording,
 * a second end of the same match, and harness runs (GE_NOINISAVE / GE_WINDOW
 * log the line only). One profile cannot sit in two seats (port_profiles.c).
 * Read when touching mpmenu.c mpCalculateAwards (the gather), front.c's Player
 * Options Stats page, or port_profiles.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondconstants.h>   /* GAMEMODE - the real enum, not a copy */
#include "port_mpstats.h"
#include "port_paths.h"   /* portDataPath: stats follow the layout */

#define MPSTATS_VERSION 2   /* v2 added damage_dealt / damage_taken / award_NN */

extern void portConLogf(const char *fmt, ...);         /* port_console.c */
extern const char *portProfileActive(s32 player);      /* port_profiles.c */
extern s32 get_is_ramrom_flag(void);                   /* ramromreplay.c */
extern s32 get_recording_ramrom_flag(void);

static const char *const sKey[PORT_MPSTAT_COUNT] = {
    "matches", "wins", "abandoned", "seconds", "kills", "deaths", "suicides", "shots",
    "hits_head", "hits_body", "hits_limb", "hits_other", "best_streak", "longest_life",
    "damage_dealt", "damage_taken",
    "award_00", "award_01", "award_02", "award_03", "award_04", "award_05", "award_06", "award_07", "award_08",
    "award_09", "award_10", "award_11", "award_12", "award_13", "award_14", "award_15", "award_16",
};

/* this match's damage, per seat, in bars (portMpStatsDamage) */
static f32 sDealt[4];
static f32 sTaken[4];

void portMpStatsDamage(s32 victim, s32 attacker, f32 bars)
{
    if (bars <= 0.0f || victim < 0 || victim >= 4)
    {
        return;
    }
    sTaken[victim] += bars;
    if (attacker >= 0 && attacker < 4 && attacker != victim)
    {
        sDealt[attacker] += bars;
    }
}

static s32 sRecorded;    /* this stage's match is already in the files */
static s32 sAbandoned;   /* the pause menu's Exit ended it */

void portMpStatsStageStart(void)
{
    s32 k;

    sRecorded = 0;
    sAbandoned = 0;
    for (k = 0; k < 4; k++)
    {
        sDealt[k] = 0.0f;
        sTaken[k] = 0.0f;
    }
}

void portMpStatsAbandoned(void)
{
    sAbandoned = 1;
}

static const char *statsPath(const char *name)
{
    char rel[96];

    {
        extern void portProfileMigrate(void);   /* port_profiles.c: an old ./Profiles moves first */

        portProfileMigrate();
    }
    snprintf(rel, sizeof(rel), "Data/Profiles/%s.gestats", name);
    return portDataPath(rel);
}

void portMpStatsLoad(const char *name, s32 out[PORT_MPSTAT_COUNT])
{
    char line[128];
    FILE *f;

    memset(out, 0, sizeof(s32) * PORT_MPSTAT_COUNT);
    if (name == NULL || name[0] == '\0' || (f = fopen(statsPath(name), "r")) == NULL)
    {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *eq = strchr(line, '=');
        s32 k;

        if (eq == NULL || line[0] == ';' || line[0] == '[')
        {
            continue;
        }
        *eq = '\0';
        for (k = 0; k < PORT_MPSTAT_COUNT; k++)
        {
            if (strcmp(line, sKey[k]) == 0)
            {
                out[k] = (s32) strtol(eq + 1, NULL, 10);
                break;
            }
        }
    }
    fclose(f);
}

static void statsWrite(const char *name, const s32 v[PORT_MPSTAT_COUNT])
{
    FILE *f = fopen(statsPath(name), "w");
    s32 k;

    if (f == NULL)
    {
        portConLogf("ge: mpstats: cannot write %s\n", statsPath(name));
        return;
    }
    fprintf(f, "[mpstats]\n");
    fprintf(f, "version=%d\n", MPSTATS_VERSION);
    for (k = 0; k < PORT_MPSTAT_COUNT; k++)
    {
        fprintf(f, "%s=%d\n", sKey[k], (int) v[k]);
    }
    fclose(f);
}

void portMpStatsRecord(const struct PortMpMatchLine *lines, s32 count, s32 seconds)
{
    extern GAMEMODE gamemode;   /* front.c */
    static s32 s_Allowed = -1;
    s32 seat;

    if (s_Allowed < 0)
    {
        s_Allowed = (getenv("GE_NOINISAVE") == NULL && getenv("GE_WINDOW") == NULL) ? 1 : 0;
    }
    if (sRecorded || count < 2 || gamemode != GAMEMODE_MULTI || get_is_ramrom_flag() || get_recording_ramrom_flag())
    {
        return;
    }
    sRecorded = 1;
    for (seat = 0; seat < count && seat < 4; seat++)
    {
        const struct PortMpMatchLine *m = &lines[seat];
        const char *name = portProfileActive(seat);
        s32 v[PORT_MPSTAT_COUNT];

        if (name[0] == '\0')
        {
            continue;
        }
        portMpStatsLoad(name, v);
        v[PORT_MPSTAT_MATCHES] += 1;
        v[PORT_MPSTAT_WINS] += (m->won && !sAbandoned) ? 1 : 0;
        v[PORT_MPSTAT_ABANDONED] += sAbandoned ? 1 : 0;
        v[PORT_MPSTAT_SECONDS] += seconds;
        v[PORT_MPSTAT_KILLS] += m->kills;
        v[PORT_MPSTAT_DEATHS] += m->deaths;
        v[PORT_MPSTAT_SUICIDES] += m->suicides;
        v[PORT_MPSTAT_SHOTS] += m->shots;
        v[PORT_MPSTAT_HEAD] += m->head;
        v[PORT_MPSTAT_BODY] += m->body;
        v[PORT_MPSTAT_LIMB] += m->limb;
        v[PORT_MPSTAT_OTHER] += m->other;
        if (m->streak > v[PORT_MPSTAT_STREAK])
        {
            v[PORT_MPSTAT_STREAK] = m->streak;
        }
        if (m->longestlife60 / 60 > v[PORT_MPSTAT_LONGESTLIFE])
        {
            v[PORT_MPSTAT_LONGESTLIFE] = m->longestlife60 / 60;
        }
        v[PORT_MPSTAT_DMG_DEALT] += (s32) (sDealt[seat] * 100.0f + 0.5f);
        v[PORT_MPSTAT_DMG_TAKEN] += (s32) (sTaken[seat] * 100.0f + 0.5f);
        {
            s32 a;

            for (a = 0; a < PORT_MPSTAT_AWARDS; a++)
            {
                v[PORT_MPSTAT_AWARD0 + a] += (m->awards >> a) & 1u;
            }
        }
        portConLogf("ge: mpstats: %s seat %d: %s%s, %d kills, %d deaths, %d suicides, %d shots, %d/%d/%d/%d hits, streak %d, %d s, damage %d dealt / %d taken, awards 0x%05X%s\n",
                    name, (int) seat + 1, sAbandoned ? "abandoned" : "played", (m->won && !sAbandoned) ? " (won)" : "",
                    (int) m->kills, (int) m->deaths, (int) m->suicides, (int) m->shots,
                    (int) m->head, (int) m->body, (int) m->limb, (int) m->other, (int) m->streak, (int) seconds,
                    (int) (sDealt[seat] * 100.0f + 0.5f), (int) (sTaken[seat] * 100.0f + 0.5f), (unsigned) m->awards,
                    s_Allowed ? "" : " - harness run, not written");
        if (s_Allowed)
        {
            statsWrite(name, v);
        }
    }
}
