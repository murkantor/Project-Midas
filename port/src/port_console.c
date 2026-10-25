/**
 * port_console.c - the tilde console (docs/port/menu-overhaul-spec.md 7).
 *
 * Modelled on the Perfect Dark port's console.c, with these differences:
 *   - there is no netplay, so no chat and no leading slash: every line typed
 *     here is a command.
 *   - commands set midas.ini keys through port_options.c, so a console change
 *     takes the same code path as a menu change and is saved the same way.
 *   - the port's log lines are copied into the console (portConLogf), so
 *     `~` then PageUp shows in-game what ge_run_*.log contains.
 *
 * This file uses no SDL. input_pc.c turns keystrokes into portConKey /
 * portConTextChar, and boss.c calls portConRender once per frame just before
 * gDPFullSync, so headless builds still collect the log but draw nothing.
 */
#ifndef TARGET_N64

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ultra64.h>

#include "port_console.h"
#include "port_console_env.h"
#include "game/textrelated.h"
#include "game/dyn.h"
#include "fr.h"

/* ---- ring buffer ---------------------------------------------------------- */

#define CON_ROWS      128          /* scrollback lines (a whole stage load fits) */
#define CON_COLS      120          /* fits a 4:3 window's 640-unit console space
                                   * (G_CONSOLE2D_EXT); long all-caps lines may clip */
#define CON_VISROWS   20           /* about the old box's share of the screen (10 at 240 lines) */
#define CON_MSGROWS   4
#define CON_MSGDWELL  (3000ull * 46875ull) /* transient messages show for 3 s of osGetTime() (46875
                                            * counts/ms); wall clock, since the tick
                                            * rate follows maxfps */
#define CON_HISTORY   16

#define CON_ROW_PITCH 12           /* units between console rows in its 480-line space */
#define CON_TEXT_X    10
#define CON_TOP_Y     4

static char conBuf[CON_ROWS][CON_COLS + 1];
static char conInput[CON_COLS + 1];
static char conHistory[CON_HISTORY][CON_COLS + 1];
static s32  conHistoryCount;
static s32  conHistoryPos = -1;    /* -1 = editing a fresh line */
static char *conVisRows[CON_VISROWS];
static char conMsgBuf[CON_MSGROWS][CON_COLS + 1]; /* flashed rows, [0] = newest; only showmsg prints write it */
static s32  conPrintRow;
static s32  conPrintCol;
static s32  conMsgRows;
static OSTime conMsgTimer;         /* osGetTime() deadline for the top row */
static s32  conInputCol;
static s32  conOpen;
static s32  conInited;

#define CON_SCROLLSTEP (CON_VISROWS / 2)
/* conPrintRow's own slot is the live tail, never history: 127 finished rows
 * at most, so the top visible row reaches the oldest one at 127 - 10 */
#define CON_SCROLLMAX  (CON_ROWS - 1 - CON_VISROWS)
static s32 conScrollOfs;
static s32 conRowsDone;            /* finished rows in the ring; 0 after init/clear,
                                    * saturates at CON_ROWS - 1 once it wraps */

/* how far back the view may go: only as far as rows actually written */
static s32 conScrollLimit(void)
{
    s32 lim = conRowsDone - CON_VISROWS;

    if (lim < 0)
    {
        lim = 0;
    }
    if (lim > CON_SCROLLMAX)
    {
        lim = CON_SCROLLMAX;
    }
    return lim;
}

/* Ring lock: several threads log through portConLogf, so every write to the ring takes this lock.
 * It is a bounded try-spin, never a blocking wait, because a holder may be a
 * suspended thread (a logger that gives up only loses the console copy, stderr
 * still has it); drawing reads unlocked, which is safe. */
static volatile s32 conLock;

static s32 conLockTry(void)
{
    s32 spin;

    for (spin = 0; spin < 0x100000; spin++)
    {
        if (conLock == 0 && __sync_lock_test_and_set(&conLock, 1) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static void conUnlock(void)
{
    __sync_lock_release(&conLock);
}

/* Text colour: GE's textRender takes 0xRRGGBBAA. Console green over a dark
 * box, matching the watch's own green rather than PD's. */
#define CON_COL_TEXT   0x60FF60FF
#define CON_COL_PROMPT 0xFFFFFFFF
#define CON_BOX_ALPHA  0xC8

/* Master display-list budget. portConRender appends to dyn.c's master DL after the frame is
 * built, and nothing bounds g_GfxBuffers, so an overrun would corrupt the other
 * buffer half or g_VtxBuffers. Every write is therefore gated on
 * dynGetFreeGfx2() (which counts Gfx commands, not bytes), using these
 * worst-case sizes:
 *   ROW   3 setup commands plus 12 per glyph (SetTextureLUT 1 + LoadTextureBlock <= 7 +
 *         TextureRectangle 3, rounded up), over the longest row text (CON_COLS + 24)
 *   SETUP microcode_constructor (11) + the backing box (<= 6), rounded up
 *   TAIL  combiner_bayer_lod_perspective (4) + boss.c's gDPFullSync and
 *         gSPEndDisplayList (2), rounded up */
#define CON_GFX_ROW    (3 + (CON_COLS + 24) * 12)
#define CON_GFX_ROWLEN(n) (3 + (s32) (n) * 12)   /* the gate for a row of n characters */
#define CON_GFX_SETUP  40                        /* + the space bracket and its scissor */
#define CON_GFX_TAIL   8

static void conRebuildVisRows(void)
{
    s32 i;

    for (i = 0; i < CON_VISROWS; i++)
    {
        s32 row = conPrintRow - i - 1 - conScrollOfs;
        while (row < 0)
        {
            row += CON_ROWS;
        }
        conVisRows[i] = &conBuf[row][0];
    }
}

void portConInit(void)
{
    memset(conBuf, 0, sizeof(conBuf));
    memset(conInput, 0, sizeof(conInput));
    memset(conHistory, 0, sizeof(conHistory));
    memset(conVisRows, 0, sizeof(conVisRows));
    conPrintRow = conPrintCol = 0;
    conHistoryCount = 0;
    conHistoryPos = -1;
    conOpen = 0;
    conMsgRows = 0;
    conMsgTimer = 0;
    conInputCol = 0;
    conScrollOfs = 0;
    conRowsDone = 0;
    conInited = 1;
    conRebuildVisRows();
}

void portConPrint(s32 showmsg, const char *str)
{
    const char *s;
    s32 oldRow;

    if (str == NULL || *str == '\0')
    {
        return;
    }
    if (!conLockTry())
    {
        return; /* another thread mid-print (or frozen holding it) */
    }
    if (!conInited)
    {
        portConInit();
    }

    oldRow = conPrintRow;

    for (s = str; *s != '\0'; s++)
    {
        char ch = *s;

        switch (ch)
        {
        case '\n':
            if (conPrintCol != 0)
            {
                conPrintCol = 0;
                conPrintRow = (conPrintRow + 1) % CON_ROWS;
                conBuf[conPrintRow][0] = '\0';
                if (conRowsDone < CON_ROWS - 1)
                {
                    conRowsDone++;
                }
            }
            break;
        case '\r':
            break;
        case '\t':
            ch = ' ';
            /* fallthrough */
        default:
            /* GE's font tables are ASCII; a high byte indexes off the end of
             * fontchar[] and draws garbage (or worse). Strip like PD does. */
            if ((u8) ch < 0x20 || (u8) ch >= 0x7f)
            {
                ch = '?';
            }
            conBuf[conPrintRow][conPrintCol++] = ch;
            if (conPrintCol == CON_COLS)
            {
                conPrintCol = 0;
                conPrintRow = (conPrintRow + 1) % CON_ROWS;
                conBuf[conPrintRow][0] = '\0';
                if (conRowsDone < CON_ROWS - 1)
                {
                    conRowsDone++;
                }
            }
            break;
        }
        conBuf[conPrintRow][conPrintCol] = '\0';
    }

    if (conPrintRow != oldRow)
    {
        s32 advanced = (conPrintRow > oldRow)
            ? (conPrintRow - oldRow)
            : (conPrintRow + (CON_ROWS - oldRow));

        /* keep a scrolled view anchored to the same absolute rows while new
         * lines pile on at the tail (PD's behaviour) */
        if (conScrollOfs > 0)
        {
            conScrollOfs += advanced;
            if (conScrollOfs > conScrollLimit())
            {
                conScrollOfs = conScrollLimit();
            }
        }

        conRebuildVisRows();

        if (showmsg)
        {
            /* the transient area keeps its OWN copy of the rows this print
             * completed. Drawing the ring's newest rows let any silent line
             * (portConLogf, the "> cmd" echo, help/list output) that landed
             * during the dwell replace the flashed one. */
            s32 n = (advanced < CON_MSGROWS) ? advanced : CON_MSGROWS;
            s32 j;

            for (j = CON_MSGROWS - 1; j >= n; j--)
            {
                memcpy(conMsgBuf[j], conMsgBuf[j - n], sizeof(conMsgBuf[j]));
            }
            for (j = 0; j < n; j++)
            {
                s32 row = conPrintRow - 1 - j;
                while (row < 0)
                {
                    row += CON_ROWS;
                }
                memcpy(conMsgBuf[j], conBuf[row], sizeof(conMsgBuf[j]));
                conMsgBuf[j][CON_COLS] = '\0';
            }

            if (conMsgRows < CON_MSGROWS)
            {
                conMsgRows += advanced;
                if (conMsgRows > CON_MSGROWS)
                {
                    conMsgRows = CON_MSGROWS;
                }
            }
            conMsgTimer = osGetTime() + CON_MSGDWELL;
        }
    }

    conUnlock();
}

static void conPrintLn(s32 showmsg, const char *s)
{
    char tmp[512];

    snprintf(tmp, sizeof(tmp), "%s\n", s);
    portConPrint(showmsg, tmp);
}

void portConPrintf(s32 showmsg, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    tmp[sizeof(tmp) - 1] = '\0';
    portConPrint(showmsg, tmp);
}

/**
 * The port's logging call: writes to stderr like the fprintf it replaced, plus
 * a silent copy in the console. It doesn't pop up on screen because a stage
 * load logs dozens of lines; open the console to see them.
 */
void portConLogf(const char *fmt, ...)
{
    char tmp[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    tmp[sizeof(tmp) - 1] = '\0';

    fputs(tmp, stderr);
    portConPrint(0, tmp);
}

/* ---- commands ------------------------------------------------------------- */

/* port_options.c - the same bridge the Extended Options rows and the watch
 * pages step through, so a console `set` persists exactly like a menu change. */
extern s32         portOptCount(void);
extern s32         portOptFind(const char *key);
extern const char *portOptKey(s32 i);
extern const char *portOptSection(s32 i);
extern const char *portOptLabel(s32 i);
extern s32         portOptValueIni(s32 i, char *buf, s32 n);
extern s32         portOptSetIni(s32 i, const char *text);
extern s32         portOptRangeText(s32 i, char *buf, s32 n);
extern s32         portOptCategoryCount(void);
extern const char *portOptCategoryLabel(s32 cat);
extern s32         portOptCategoryOf(s32 i);

/* video.c - the midas.ini dispatcher, for the keys that are not option rows
 * (the keyboard / Port binds). Weak-stubbed headless. */
extern s32 portVideoIniSetKey(const char *key, const char *value);
extern s32 portVideoIniGetKey(const char *key, char *buf, s32 n);

static s32 conStrEqI(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0')
    {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
        {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

/* substring match, case-insensitive; empty needle matches everything */
static s32 conStrHasI(const char *hay, const char *needle)
{
    s32 i;

    if (needle == NULL || needle[0] == '\0')
    {
        return 1;
    }
    for (i = 0; hay[i] != '\0'; i++)
    {
        s32 j = 0;
        while (needle[j] != '\0')
        {
            char ca = hay[i + j], cb = needle[j];
            if (ca == '\0') return 0;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) break;
            j++;
        }
        if (needle[j] == '\0')
        {
            return 1;
        }
    }
    return 0;
}

static void conCmdHelp(void)
{
    conPrintLn(0, "client commands (cl_ = changes the game; saved to midas.ini):");
    conPrintLn(0, "  cl_<setting> <value>  set any setting - cl_vsync 0, cl_maxfps 144");
    conPrintLn(0, "  cl_<setting>          show it");
    conPrintLn(0, "  cl_showfps 0|1        the FPS digits");
    conPrintLn(0, "  cl_audio_pump 0|1      live audio pump state, replacing the FPS digits");
    conPrintLn(0, "  cl_audio_log 0|1       the same, once a second, to Data/Logs/audio_pump.log");
    conPrintLn(0, "  cl_fov <deg>          field of view (16:9 horizontal); cl_fps = cl_maxfps");
    conPrintLn(0, "  cl_hitchms <ms>       log frames this slow as 'ge: hitch:' (0 = off, not saved)");
    conPrintLn(0, "  cl_fadedbg 0|1        log objects' fade / cull changes as 'ge: fade:'");
    conPrintLn(0, "console commands:");
    conPrintLn(0, "  list [text]           the cl_ settings matching key or label");
    conPrintLn(0, "  harness [text]        the GE_* diagnostic env knobs");
    conPrintLn(0, "  clear                 empty the scrollback");
    conPrintLn(0, "  help                  this");
    conPrintLn(0, "keys: PgUp/PgDn/Home/End scroll, Up/Down recall");
}

/* Client commands: cl_<key> reads or sets any setting by its midas.ini key (plus a few friendly names
 * and cl_hitchms); the older set / get still work, but help lists only the cl_ forms. */
static const char *const sConClAlias[][2] = {
    { "showfps", "show_fps" },
    { "fov",     "hfov_wide" },
    { "fps",     "maxfps" },
};

static void conCmdGet(const char *key);
static void conCmdSet(const char *key, const char *value);

/* hfov_wide reads 90 for the N64's view, whose true 16:9 width is 91.5
 * (video.c videoHfovWide): the console shows the truth beside it. Empty for
 * every other key and every value that is already true. */
static const char *conFovNote(const char *key, const char *shown, char *note, s32 n)
{
    extern s32 portVideoHfovWideTrueTenths(void);   /* video.c; weak 0 headless */
    const s32 t = portVideoHfovWideTrueTenths();

    note[0] = '\0';
    if (conStrEqI(key, "hfov_wide") && t > 0 && t != atoi(shown) * 10)
    {
        snprintf(note, n, " (True %d.%d)", (int) (t / 10), (int) (t % 10));
    }
    return note;
}

static void conCmdClient(const char *name, const char *value)
{
    extern s32 portVideoOptionGet(const char *key, s32 *out);   /* video.c */
    extern s32 portVideoOptionSet(const char *key, s32 value);
    extern s32 portIniWriteStrs(const char *section, const char *const *keys, const char *const *vals, s32 n);
    const char *key = name;
    char buf[128];
    s32 live;
    u32 a;

    if (name[0] == '\0')
    {
        conPrintLn(0, "usage: cl_<setting> [value] - try: list");
        return;
    }
    if (conStrEqI(name, "fadedbg"))
    {   /* propobj.c: log each object's visibility verdict as it changes */
        extern void portFadeDbgSet(s32 on);
        extern s32 g_PortFadeDbg;

        if (value != NULL)
        {
            portFadeDbgSet(atoi(value) != 0);
        }
        portConPrintf(value != NULL, "cl_fadedbg = %d (log: 'ge: fade:' lines)\n", (int) g_PortFadeDbg);
        return;
    }
    /* The overlay toggle lives in audio_pc.c beside the counters it shows. Not
     * a midas.ini key: it is a diagnostic, so it should not survive a restart
     * because someone typed it once while reading a crackle report. The hyphen
     * spelling is accepted too, since the console echoes the underscore form
     * but the name reads as two words. */
    if (conStrEqI(name, "audio_pump") || conStrEqI(name, "audio-pump"))
    {
        extern s32 portAudioPumpStatsShown(void);   /* audio_pc.c */
        extern void portAudioPumpStatsSet(s32 on);

        if (value != NULL)
        {
            portAudioPumpStatsSet(atoi(value) != 0);
        }
        portConPrintf(value != NULL, "cl_audio_pump = %d (%s, not saved)\n",
                      (int) portAudioPumpStatsShown(), portAudioPumpStatsShown() ? "on" : "off");
        return;
    }
    if (conStrEqI(name, "audio_log") || conStrEqI(name, "audio-log"))
    {
        extern s32 portAudioPumpLogShown(void);    /* audio_pc.c */
        extern void portAudioPumpLogSet(s32 on);

        if (value != NULL)
        {
            portAudioPumpLogSet(atoi(value) != 0);
        }
        if (portAudioPumpLogShown())
        {
            portConPrintf(value != NULL, "cl_audio_log = 1 (on, not saved) -> Data/Logs/audio_pump.log\n");
        }
        else
        {
            portConPrintf(value != NULL, "cl_audio_log = 0 (off, not saved)\n");
        }
        return;
    }
    if (conStrEqI(name, "hitchms"))
    {
        extern int portHitchMs(void);          /* hitchlog.c; weak 0 headless */
        extern void portHitchSetMs(int ms);

        if (value != NULL)
        {
            portHitchSetMs(atoi(value));
        }
        portConPrintf(value != NULL, "cl_hitchms = %d%s\n", portHitchMs(), portHitchMs() == 0 ? " (off)" : " ms");
        return;
    }
    for (a = 0; a < sizeof(sConClAlias) / sizeof(sConClAlias[0]); a++)
    {
        if (conStrEqI(name, sConClAlias[a][0]))
        {
            key = sConClAlias[a][1];
        }
    }

    /* a live [display] key with no option-table row: the video
     * bridge sets it, and the ini gets the line set would have written */
    if (portOptFind(key) < 0 && portVideoOptionGet(key, &live))
    {
        if (value == NULL)
        {
            portConPrintf(0, "cl_%s = %d\n", name, (int) live);
            return;
        }
        if (!portVideoOptionSet(key, atoi(value)))
        {
            portConPrintf(1, "bad value \"%s\" for cl_%s\n", value, name);
            return;
        }
        portVideoOptionGet(key, &live);
        snprintf(buf, sizeof(buf), "%d", (int) live);
        {
            const char *k[1] = { key };
            const char *v[1] = { buf };

            portIniWriteStrs("[display]", k, v, 1);
        }
        portConPrintf(1, "cl_%s = %s\n", name, buf);
        return;
    }
    if (value == NULL)
    {
        conCmdGet(key);
    }
    else
    {
        conCmdSet(key, value);
    }
}

static void conCmdGet(const char *key)
{
    char buf[128];
    s32 i;

    if (key == NULL || key[0] == '\0')
    {
        conPrintLn(0, "usage: get <key>");
        return;
    }

    i = portOptFind(key);
    if (i >= 0)
    {
        char range[96];

        if (!portOptValueIni(i, buf, sizeof(buf)))
        {
            portConPrintf(0, "%s = (not available in this build)\n", portOptKey(i));
            return;
        }
        char note[32];

        range[0] = '\0';
        portOptRangeText(i, range, sizeof(range));
        portConPrintf(0, "%s %s = %s%s%s%s\n", portOptSection(i), portOptKey(i), buf,
                      conFovNote(portOptKey(i), buf, note, sizeof(note)),
                      range[0] != '\0' ? "   " : "", range);
        return;
    }

    if (portVideoIniGetKey(key, buf, sizeof(buf)))
    {
        portConPrintf(0, "%s = %s\n", key, buf);
        return;
    }

    portConPrintf(0, "unknown key \"%s\" - try: list\n", key);
}

static void conCmdSet(const char *key, const char *value)
{
    s32 i;

    if (key == NULL || key[0] == '\0' || value == NULL || value[0] == '\0')
    {
        conPrintLn(0, "usage: set <key> <value>");
        return;
    }

    i = portOptFind(key);
    if (i >= 0)
    {
        if (portOptSetIni(i, value))
        {
            char buf[128];
            char note[32];
            buf[0] = '\0';
            portOptValueIni(i, buf, sizeof(buf));
            portConPrintf(1, "%s = %s%s\n", portOptKey(i), buf,
                          conFovNote(portOptKey(i), buf, note, sizeof(note)));
        }
        else
        {
            char range[96];
            range[0] = '\0';
            portOptRangeText(i, range, sizeof(range));
            portConPrintf(1, "bad value \"%s\" for %s%s%s\n", value, portOptKey(i),
                          range[0] != '\0' ? " - " : "", range);
        }
        return;
    }

    {
        const s32 r = portVideoIniSetKey(key, value);

        if (r < 0)
        {
            portConPrintf(1, "bad value \"%s\" for %s - use SDL key names, comma separated\n",
                          value, key);
            return;
        }
        if (r > 0)
        {
            char now[128];
            /* tickhz is derived: echo the rate that took, not the one typed */
            portConPrintf(1, "%s = %s\n", key,
                          (conStrEqI(key, "tickhz") && portVideoIniGetKey(key, now, sizeof(now))) ? now : value);
            return;
        }
    }

    portConPrintf(1, "unknown key \"%s\" - try: list\n", key);
}

/**
 * Lists the settings grouped by the option table's category field (its only
 * remaining use since the Extended Options menu was removed).
 */
static void conCmdList(const char *filter)
{
    const s32 cats = portOptCategoryCount();
    const s32 n = portOptCount();
    s32 cat;
    s32 i;
    s32 shown = 0;

    for (cat = 0; cat < cats; cat++)
    {
        s32 head = 0;

        for (i = 0; i < n; i++)
        {
            const char *key = portOptKey(i);
            const char *label = portOptLabel(i);
            char buf[128];

            if (key == NULL || portOptCategoryOf(i) != cat)
            {
                continue;
            }
            if (!conStrHasI(key, filter) && !(label != NULL && conStrHasI(label, filter)))
            {
                continue;
            }
            if (!head)
            {
                char name[32];
                s32 k;

                head = 1;
                /* the table's labels carry a trailing newline (the watch needs
                 * it); trim it or the bracket lands on the next line */
                for (k = 0; k < (s32) sizeof(name) - 1; k++)
                {
                    char c = portOptCategoryLabel(cat)[k];
                    if (c == '\0' || c == '\n')
                    {
                        break;
                    }
                    name[k] = c;
                }
                name[k] = '\0';
                portConPrintf(0, "[%s]\n", name);
            }
            if (!portOptValueIni(i, buf, sizeof(buf)))
            {
                snprintf(buf, sizeof(buf), "n/a");
            }
            portConPrintf(0, "  cl_%-19s %s\n", key, buf);
            shown++;
        }
    }
    portConPrintf(0, "%d setting(s)\n", shown);
}

static void conCmdHarness(const char *filter)
{
    s32 n = (s32) (sizeof(sPortConEnvNames) / sizeof(sPortConEnvNames[0]));
    s32 i;
    s32 shown = 0;
    s32 set = 0;

    for (i = 0; i < n; i++)
    {
        const char *name = sPortConEnvNames[i];
        const char *v = getenv(name);

        if (!conStrHasI(name, filter))
        {
            continue;
        }
        if (v != NULL)
        {
            portConPrintf(0, "%-26s = %s\n", name, v);
            set++;
        }
        else if (filter != NULL && filter[0] != '\0')
        {
            portConPrintf(0, "%-26s   (unset)\n", name);
        }
        shown++;
    }
    if (filter == NULL || filter[0] == '\0')
    {
        portConPrintf(0, "%d of %d GE_* knob(s) set; `harness <text>` lists matches\n", set, shown);
    }
}

static void conExecute(char *line)
{
    char *cmd;
    char *arg1 = NULL;
    char *arg2 = NULL;
    char *s = line;
    s32 n = (s32) strlen(line);

    /* trailing blanks off the whole line, so `set vsync on ` sets `on`;
     * the inner spaces of a key list in arg2 are kept */
    while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t'))
    {
        line[--n] = '\0';
    }

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0')
    {
        return;
    }

    cmd = s;
    while (*s != '\0' && *s != ' ' && *s != '\t') s++;
    if (*s != '\0')
    {
        *s++ = '\0';
        while (*s == ' ' || *s == '\t') s++;
        if (*s != '\0')
        {
            arg1 = s;
            while (*s != '\0' && *s != ' ' && *s != '\t') s++;
            if (*s != '\0')
            {
                *s++ = '\0';
                while (*s == ' ' || *s == '\t') s++;
                if (*s != '\0')
                {
                    arg2 = s; /* rest of the line, spaces kept (key lists) */
                }
            }
        }
    }

    if (conStrEqI(cmd, "help") || conStrEqI(cmd, "?"))
    {
        conCmdHelp();
    }
    else if (conStrEqI(cmd, "set"))
    {
        conCmdSet(arg1, arg2);
    }
    else if (conStrEqI(cmd, "get"))
    {
        conCmdGet(arg1);
    }
    else if (conStrEqI(cmd, "list"))
    {
        conCmdList(arg1);
    }
    else if (conStrEqI(cmd, "harness"))
    {
        conCmdHarness(arg1);
    }
    else if ((cmd[0] == 'c' || cmd[0] == 'C') && (cmd[1] == 'l' || cmd[1] == 'L') && cmd[2] == '_')
    {   /* cl_<setting> [value]: the value is the rest of the line (a key list keeps its spaces) */
        char value[160];

        if (arg1 != NULL && arg2 != NULL)
        {
            snprintf(value, sizeof(value), "%s %s", arg1, arg2);
        }
        else if (arg1 != NULL)
        {
            snprintf(value, sizeof(value), "%s", arg1);
        }
        conCmdClient(cmd + 3, arg1 != NULL ? value : NULL);
    }
    else if (conStrEqI(cmd, "clear"))
    {
        if (conLockTry())
        {
            memset(conBuf, 0, sizeof(conBuf));
            conPrintRow = conPrintCol = 0;
            conScrollOfs = 0;
            conRowsDone = 0;
            conMsgRows = 0;
            conRebuildVisRows();
            conUnlock();
        }
    }
    else
    {
        portConPrintf(0, "unknown command \"%s\" - try: help\n", cmd);
    }
}

/* ---- input ---------------------------------------------------------------- */

/* The scrollback view moves under conLock: portConPrint, on any thread,
 * re-anchors the same offset (conScrollOfs += advanced) and rebuilds
 * conVisRows. step is clamped, so +/-CON_ROWS mean Home / the tail. */
static void conScroll(s32 step)
{
    s32 newOfs;

    if (!conLockTry())
    {
        return;
    }
    newOfs = conScrollOfs + step;
    if (newOfs < 0) newOfs = 0;
    if (newOfs > conScrollLimit()) newOfs = conScrollLimit();
    if (newOfs != conScrollOfs)
    {
        conScrollOfs = newOfs;
        conRebuildVisRows();
    }
    conUnlock();
}

s32 portConIsOpen(void)
{
    return conOpen;
}

void portConToggle(void)
{
    if (!conInited)
    {
        portConInit();
    }
    conOpen = !conOpen;
    if (conOpen)
    {
        conInput[0] = '\0';
        conInputCol = 0;
        conHistoryPos = -1;
    }
    else
    {
        conScroll(-CON_ROWS); /* back to the tail */
    }
}

void portConClose(void)
{
    if (conOpen)
    {
        portConToggle();
    }
}

static void conRecallHistory(s32 dir)
{
    s32 pos;

    if (conHistoryCount == 0)
    {
        return;
    }
    pos = conHistoryPos + dir;
    if (pos < -1)
    {
        pos = -1;
    }
    if (pos >= conHistoryCount)
    {
        pos = conHistoryCount - 1;
    }
    conHistoryPos = pos;
    if (pos < 0)
    {
        conInput[0] = '\0';
    }
    else
    {
        snprintf(conInput, sizeof(conInput), "%s", conHistory[pos]);
    }
    conInputCol = (s32) strlen(conInput);
}

void portConKey(s32 key)
{
    if (!conOpen)
    {
        return;
    }

    switch (key)
    {
    case PORTCON_KEY_ESCAPE:
        portConToggle();
        return;
    case PORTCON_KEY_BACKSPACE:
        if (conInputCol > 0)
        {
            conInput[--conInputCol] = '\0';
        }
        return;
    case PORTCON_KEY_UP:
        conRecallHistory(+1);
        return;
    case PORTCON_KEY_DOWN:
        conRecallHistory(-1);
        return;
    case PORTCON_KEY_ENTER:
        if (conInput[0] != '\0')
        {
            char work[CON_COLS + 1];
            s32 h;

            portConPrintf(0, "> %s\n", conInput);
            /* history: newest first, no immediate duplicate */
            if (conHistoryCount == 0 || strcmp(conHistory[0], conInput) != 0)
            {
                for (h = CON_HISTORY - 1; h > 0; h--)
                {
                    memcpy(conHistory[h], conHistory[h - 1], sizeof(conHistory[0]));
                }
                snprintf(conHistory[0], sizeof(conHistory[0]), "%s", conInput);
                if (conHistoryCount < CON_HISTORY)
                {
                    conHistoryCount++;
                }
            }
            /* conExecute writes NULs into its copy, never into the history */
            snprintf(work, sizeof(work), "%s", conInput);
            conExecute(work);
        }
        conInput[0] = '\0';
        conInputCol = 0;
        conHistoryPos = -1;
        conScroll(-CON_ROWS);
        return;
    default:
        break;
    }

    switch (key)
    {
    case PORTCON_KEY_PAGEUP:   conScroll(+CON_SCROLLSTEP); break;
    case PORTCON_KEY_PAGEDOWN: conScroll(-CON_SCROLLSTEP); break;
    case PORTCON_KEY_HOME:     conScroll(+CON_ROWS);       break; /* clamps to conScrollLimit() */
    case PORTCON_KEY_END:      conScroll(-CON_ROWS);       break; /* clamps to 0 */
    default: break;
    }
}

void portConTextChar(s32 ch)
{
    if (!conOpen || ch < 0x20 || ch >= 0x7f)
    {
        return;
    }
    /* US backstop; input_pc.c swallows the console key's own text on every layout */
    if (ch == '`' || ch == '~')
    {
        return;
    }
    if (conInputCol >= CON_COLS)
    {
        return;
    }
    conInput[conInputCol++] = (char) ch;
    conInput[conInputCol] = '\0';
    conHistoryPos = -1;
}

/* ---- render --------------------------------------------------------------- */

/* The console draws in its own 2D space: 480 lines by the window's full width
 * (video.c portConsoleSpace), bracketed by G_CONSOLE2D_EXT so the renderer
 * skips the 4:3 pin; the world and HUD stay at 320x240. */
static s32 conSpaceW = 640, conSpaceH = 480;
static s16 conSavedViX, conSavedViY;   /* the frame's VI size, while the console borrows it */
#define CON_SPACE(gdl, w, h) do { (gdl)->words.w0 = (u32) 0x4c << 24 /* G_CONSOLE2D_EXT */; \
                                  (gdl)->words.w1 = ((u32) (w) << 16) | (u32) (h); (gdl)++; } while (0)

static Gfx *conSpaceBegin(Gfx *gdl)
{
    extern void portConsoleSpace(s32 *w, s32 *h);   /* video.c; weak 640x480 headless */

    portConsoleSpace(&conSpaceW, &conSpaceH);
    /* the text and rect code clip against the VI size (textRenderGlyph's
     * *x <= viGetX()): it is the console's space while the console draws,
     * and the frame's again at conSpaceEnd - the DL is built, nothing else
     * reads it in between */
    conSavedViX = viGetX();
    conSavedViY = viGetY();
    viSetXY((s16) conSpaceW, (s16) conSpaceH);
    CON_SPACE(gdl, conSpaceW, conSpaceH);
    gdl = microcode_constructor(gdl);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, conSpaceW, conSpaceH);   /* microcode_constructor's is the frame's 320x240 */
    return gdl;
}

static Gfx *conSpaceEnd(Gfx *gdl)
{
    gdl = combiner_bayer_lod_perspective(gdl);
    CON_SPACE(gdl, 0, 0);
    viSetXY(conSavedViX, conSavedViY);
    return gdl;
}

static s32 conTextWidth(const char *text)
{
    s32 h = 0, w = 0;

    textMeasure(&h, &w, (char *) text, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    return w;
}

/** How many characters of text fit in maxw: the longest prefix that does
 *  (binary search on its measured width), pulled back to just after a space
 *  when that leaves at least half the line - so words break whole. At least
 *  one character, so a line always advances. */
static s32 conFitLen(const char *text, s32 maxw)
{
    const s32 len = (s32) strlen(text);
    char buf[CON_COLS + 1];
    s32 lo = 1, hi = len, k;

    if (len <= 1 || conTextWidth(text) <= maxw)
    {
        return len;
    }
    while (lo < hi)
    {
        const s32 mid = (lo + hi + 1) / 2;

        memcpy(buf, text, (size_t) mid);
        buf[mid] = '\0';
        if (conTextWidth(buf) <= maxw)
        {
            lo = mid;
        }
        else
        {
            hi = mid - 1;
        }
    }
    for (k = lo; k > lo / 2; k--)
    {
        if (text[k] == ' ')
        {
            return k;
        }
    }
    return lo;
}

static Gfx *conDrawRow(Gfx *gdl, s32 x, s32 y, const char *text, u32 colour)
{
    s32 tx = x;
    s32 ty = y;

    if (text == NULL || text[0] == '\0')
    {
        return gdl;
    }
    /* no master-DL room for this row: drop it, never overrun. Gated on the
     * row's own length (the worst case for 120 columns would drop rows for
     * nothing) */
    if (dynGetFreeGfx2(gdl) < CON_GFX_ROWLEN(strlen(text)) + CON_GFX_TAIL)
    {
        return gdl;
    }
    return textRender(gdl, &tx, &ty, (char *) text, ptrFontBankGothicChars, ptrFontBankGothic,
                      colour, conSpaceW, conSpaceH, 0, 0);
}

/**
 * At the first rendered frame, GE_CONSOLE=1 opens the console and
 * GE_CONCMD="<line>[;<line>...]" runs those lines once. These are for automated
 * testing and screenshots without a keyboard, and are environment variables
 * only, never midas.ini keys.
 */
static void conEnvHarness(void)
{
    static s32 done;
    const char *cmds;
    const char *e;

    if (done)
    {
        return;
    }
    done = 1;

    e = getenv("GE_CONSOLE");
    if (e != NULL && e[0] != '0')
    {
        conOpen = 1;
    }

    cmds = getenv("GE_CONCMD");
    if (cmds != NULL && cmds[0] != '\0')
    {
        char work[512];
        char *p;
        char *start;

        snprintf(work, sizeof(work), "%s", cmds);
        start = work;
        for (p = work; ; p++)
        {
            if (*p == ';' || *p == '\0')
            {
                s32 last = (*p == '\0');
                *p = '\0';
                if (start[0] != '\0')
                {
                    portConPrintf(0, "> %s", start);
                    portConPrint(0, "\n");
                    conExecute(start);
                }
                if (last)
                {
                    break;
                }
                start = p + 1;
            }
        }
    }
}

Gfx *portConRender(Gfx *gdl)
{
    s32 i;

    conEnvHarness();

    /* the fonts are NULL before the frontend loads them (boot, early stage
     * load); drawing then walks a NULL fontchar table */
    if (ptrFontBankGothic == NULL || ptrFontBankGothicChars == NULL)
    {
        return gdl;
    }
    if (!conInited)
    {
        return gdl;
    }
    /* the frame already filled the master DL: add nothing to it */
    if (dynGetFreeGfx2(gdl) < CON_GFX_SETUP + CON_GFX_TAIL)
    {
        return gdl;
    }

    if (!conOpen)
    {
        if (conMsgRows > 0)
        {
            gdl = conSpaceBegin(gdl);
            for (i = 0; i < conMsgRows; i++)
            {
                gdl = conDrawRow(gdl, CON_TEXT_X, CON_TOP_Y + CON_ROW_PITCH * (conMsgRows - i - 1),
                                 conMsgBuf[i], CON_COL_TEXT);
            }
            gdl = conSpaceEnd(gdl);

            {
                const OSTime now = osGetTime();

                if (now >= conMsgTimer)
                {
                    conMsgTimer = now + CON_MSGDWELL;
                    conMsgRows--;
                }
            }
        }
        else
        {
            conMsgTimer = 0;
        }
        return gdl;
    }

    /* the transient rows only draw while closed, and every showmsg line (a
     * `set` answer) is printed while open - so the dwell starts at close */
    if (conMsgRows > 0)
    {
        conMsgTimer = osGetTime() + CON_MSGDWELL;
    }

    {
        const s32 boxBottom = CON_TOP_Y + CON_ROW_PITCH * (CON_VISROWS + 1) + 2;
        char prompt[CON_COLS + 24];

        gdl = conSpaceBegin(gdl);
        gdl = microcode_constructor_related_to_menus(gdl, 0, 0, conSpaceW, boxBottom, CON_BOX_ALPHA);

        {   /* Each row is word-wrapped to the console's width as it draws
             * (conFitLen) and the pieces stack up from the bottom until the box
             * is full; scrolling still moves by whole rows. */
            const s32 maxw = conSpaceW - CON_TEXT_X * 2;
            s32 line = 0;   /* display lines used, from the bottom */

            for (i = 0; i < CON_VISROWS && line < CON_VISROWS; i++)
            {
                /* Draw from a copy: textRender reads each byte twice, and another thread rewriting
                 * the row between reads could index the font table out of bounds. */
                char row[CON_COLS + 1];
                s32 start[CON_COLS + 1];
                s32 len[CON_COLS + 1];
                s32 pieces = 0;
                s32 at = 0;
                s32 k;

                memcpy(row, conVisRows[i], CON_COLS);
                row[CON_COLS] = '\0';
                while (row[at] != '\0' && pieces < CON_COLS)
                {
                    const s32 n = conFitLen(row + at, maxw);

                    start[pieces] = at;
                    len[pieces] = n;
                    pieces++;
                    at += n;
                    while (row[at] == ' ')
                    {
                        at++;   /* a wrapped line does not start with the space it broke at */
                    }
                }
                if (pieces == 0)
                {
                    line++;   /* an empty row keeps its line */
                    continue;
                }
                for (k = pieces - 1; k >= 0 && line < CON_VISROWS; k--, line++)
                {
                    char piece[CON_COLS + 1];

                    memcpy(piece, row + start[k], (size_t) len[k]);
                    piece[len[k]] = '\0';
                    gdl = conDrawRow(gdl, CON_TEXT_X, CON_TOP_Y + CON_ROW_PITCH * (CON_VISROWS - line - 1),
                                     piece, CON_COL_TEXT);
                }
            }
        }

        if (conScrollOfs > 0)
        {
            snprintf(prompt, sizeof(prompt), "[-%d] > %s_", conScrollOfs, conInput);
        }
        else
        {
            snprintf(prompt, sizeof(prompt), "> %s_", conInput);
        }
        {   /* a prompt wider than the box shows its newest end */
            const s32 maxw = conSpaceW - CON_TEXT_X * 2;
            const char *show = prompt;

            while (show[0] != '\0' && show[1] != '\0' && conTextWidth(show) > maxw)
            {
                show++;
            }
            gdl = conDrawRow(gdl, CON_TEXT_X, CON_TOP_Y + CON_ROW_PITCH * CON_VISROWS, show,
                             CON_COL_PROMPT);
        }

        gdl = conSpaceEnd(gdl);
    }

    return gdl;
}

#endif /* TARGET_N64 */
