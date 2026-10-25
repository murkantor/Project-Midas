/*
 * The diagnostic boot flags, as a pure parse.
 *
 * Drives portBootFlagsParse against synthetic argument vectors, so the matching
 * rules are pinned without launching the game. That matters more here than the
 * line count suggests: a boot flag that is silently ignored leaves no trace
 * except what the game does not do, which is indistinguishable from a flag that
 * worked. The cases below are the spellings a person actually types.
 *
 * Build standalone:
 *   cc -std=c99 -Iport/include port/tests/boot_flags.c port/src/boot_flags.c -o t
 *   ./t
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_flags.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); abort(); } } while (0)

/* argv[0] is the program; pass it always, as a real launch does, so a bug that
 * reads from index 0 cannot hide. */
static void parse(PortBootFlags *f, int argc, char **argv)
{
    portBootFlagsParse(argc, argv, f);
}

static void no_flags(void)
{
    char *argv[] = { (char *) "ProjectMidas", NULL };
    PortBootFlags f;

    parse(&f, 1, argv);
    CHECK(f.classic == 0);
    CHECK(f.audio_pump == 0);
    CHECK(f.audio_log == 0);
}

/* Each flag on its own sets only itself. The negative halves are the point: a
 * matching bug that lit every flag would pass a "was it set" check. */
static void each_alone(void)
{
    PortBootFlags f;

    { char *argv[] = { (char *) "P", (char *) "-n64_aa", NULL };
      parse(&f, 2, argv);
      CHECK(f.classic == 1); CHECK(f.audio_pump == 0); CHECK(f.audio_log == 0); }

    { char *argv[] = { (char *) "P", (char *) "-audio_pump", NULL };
      parse(&f, 2, argv);
      CHECK(f.classic == 0); CHECK(f.audio_pump == 1); CHECK(f.audio_log == 0); }

    { char *argv[] = { (char *) "P", (char *) "-audio_log", NULL };
      parse(&f, 2, argv);
      CHECK(f.classic == 0); CHECK(f.audio_pump == 0); CHECK(f.audio_log == 1); }
}

/* The command the flags exist for. */
static void all_three(void)
{
    char *argv[] = { (char *) "P", (char *) "-n64_aa", (char *) "-audio_pump",
                     (char *) "-audio_log", NULL };
    PortBootFlags f;

    parse(&f, 4, argv);
    CHECK(f.classic == 1);
    CHECK(f.audio_pump == 1);
    CHECK(f.audio_log == 1);
}

/* Mix and match, and order must not matter. */
static void subset_and_order(void)
{
    PortBootFlags f;

    { char *argv[] = { (char *) "P", (char *) "-audio_log", (char *) "-n64_aa", NULL };
      parse(&f, 3, argv);
      CHECK(f.classic == 1); CHECK(f.audio_pump == 0); CHECK(f.audio_log == 1); }

    { char *argv[] = { (char *) "P", (char *) "-audio_pump", (char *) "-audio_log", NULL };
      parse(&f, 3, argv);
      CHECK(f.classic == 0); CHECK(f.audio_pump == 1); CHECK(f.audio_log == 1); }
}

/* One or two dashes, either name separator, either case. */
static void spelling_variants(void)
{
    PortBootFlags f;

    { char *argv[] = { (char *) "P", (char *) "--n64_aa", NULL };
      parse(&f, 2, argv); CHECK(f.classic == 1); }

    { char *argv[] = { (char *) "P", (char *) "-audio-pump", NULL };
      parse(&f, 2, argv); CHECK(f.audio_pump == 1); }

    { char *argv[] = { (char *) "P", (char *) "--audio-log", NULL };
      parse(&f, 2, argv); CHECK(f.audio_log == 1); }

    { char *argv[] = { (char *) "P", (char *) "-N64_AA", NULL };
      parse(&f, 2, argv); CHECK(f.classic == 1); }

    { char *argv[] = { (char *) "P", (char *) "-N64-AA", NULL };
      parse(&f, 2, argv); CHECK(f.classic == 1); }

    { char *argv[] = { (char *) "P", (char *) "--classic", NULL };
      parse(&f, 2, argv); CHECK(f.classic == 1); }

    { char *argv[] = { (char *) "P", (char *) "-CLASSIC", NULL };
      parse(&f, 2, argv); CHECK(f.classic == 1); }
}

/* -classic and -n64_aa are ONE flag: the Phase 3 key split gave the mode an
 * honest name, and the old spelling stays as a silent alias so no script or
 * capture run breaks. The recorded spelling exists only so the log line names
 * what the caller typed - a launch that printed "-n64_aa ->" for -classic
 * would be the same defect as a flag that silently does nothing. */
static void classic_has_two_spellings(void)
{
    PortBootFlags f;

    { char *argv[] = { (char *) "P", (char *) "-classic", NULL };
      parse(&f, 2, argv);
      CHECK(f.classic == 1);
      CHECK(f.classic_spelling != NULL && strcmp(f.classic_spelling, "-classic") == 0); }

    { char *argv[] = { (char *) "P", (char *) "-n64_aa", NULL };
      parse(&f, 2, argv);
      CHECK(f.classic == 1);
      CHECK(f.classic_spelling != NULL && strcmp(f.classic_spelling, "-n64_aa") == 0); }

    /* given both: still one flag, not a count */
    { char *argv[] = { (char *) "P", (char *) "-classic", (char *) "-n64_aa", NULL };
      parse(&f, 3, argv); CHECK(f.classic == 1); }

    /* absent: NULL, so nothing can report a spelling for a flag nobody gave */
    { char *argv[] = { (char *) "P", NULL };
      parse(&f, 1, argv);
      CHECK(f.classic == 0); CHECK(f.classic_spelling == NULL); }

    /* -classic_mode is the INI KEY's name, not a flag. The end-anchored match
     * must not read it as -classic plus a suffix. */
    { char *argv[] = { (char *) "P", (char *) "-classic_mode", NULL };
      parse(&f, 2, argv); CHECK(f.classic == 0); }
}

/* ---- -skip and -stage -------------------------------------------------- */

/* A bare -skip means the whole chain, which is the request a person makes:
 * land on the file select instead of watching five logos. */
static void skip_bare_means_all(void)
{
    PortBootFlags f;

    { char *argv[] = { (char *) "P", (char *) "-skip", NULL };
      parse(&f, 2, argv); CHECK(f.skip_screens == -1); }

    /* absent: 0, not -1. A parser that defaulted to "all" would skip the boot
     * sequence of every launch. */
    { char *argv[] = { (char *) "P", NULL };
      parse(&f, 1, argv); CHECK(f.skip_screens == 0); }
}

/* -skip N counts screens, and the count is consumed rather than left to be read
 * as anything else. */
static void skip_takes_a_count(void)
{
    PortBootFlags f;

    { char *argv[] = { (char *) "P", (char *) "-skip", (char *) "2", NULL };
      parse(&f, 3, argv); CHECK(f.skip_screens == 2); }

    { char *argv[] = { (char *) "P", (char *) "-skip", (char *) "5", NULL };
      parse(&f, 3, argv); CHECK(f.skip_screens == 5); }

    /* 0 is off, not "skip none of them but say you did" */
    { char *argv[] = { (char *) "P", (char *) "-skip", (char *) "0", NULL };
      parse(&f, 3, argv); CHECK(f.skip_screens == 0); }

    /* a bare -skip outranks a count: the more aggressive request */
    { char *argv[] = { (char *) "P", (char *) "-skip", (char *) "2",
                       (char *) "-skip", NULL };
      parse(&f, 4, argv); CHECK(f.skip_screens == -1); }
}

/* A non-number after -skip is NOT eaten: the flag keeps its bare meaning and
 * the argument stays available. This is the rule that keeps a ROM path safe. */
static void skip_does_not_eat_a_non_number(void)
{
    PortBootFlags f;

    { char *argv[] = { (char *) "P", (char *) "-skip", (char *) "/tmp/x.n64", NULL };
      parse(&f, 3, argv); CHECK(f.skip_screens == -1); }

    { char *argv[] = { (char *) "P", (char *) "-skip", (char *) "banana", NULL };
      parse(&f, 3, argv); CHECK(f.skip_screens == -1); }

    /* the count is digits only: -skip 2x is not 2, and not a -skip at all */
    { char *argv[] = { (char *) "P", (char *) "-skip", (char *) "2x", NULL };
      parse(&f, 3, argv); CHECK(f.skip_screens == -1); }
}

/* -stage names a mission; the next argument is its value, like --rom-file. */
static void stage_takes_a_name(void)
{
    PortBootFlags f;

    { char *argv[] = { (char *) "P", (char *) "-stage", (char *) "dam", NULL };
      parse(&f, 3, argv);
      CHECK(f.stage != NULL && strcmp(f.stage, "dam") == 0); }

    /* absent: NULL */
    { char *argv[] = { (char *) "P", NULL };
      parse(&f, 1, argv); CHECK(f.stage == NULL); }

    /* -stage with nothing after it asks for nothing: it must not swallow a
     * neighbouring flag and try to load a mission called "-audio_pump". */
    { char *argv[] = { (char *) "P", (char *) "-stage", (char *) "-audio_pump", NULL };
      parse(&f, 3, argv);
      CHECK(f.stage == NULL);
      CHECK(f.audio_pump == 1); }

    { char *argv[] = { (char *) "P", (char *) "-stage", NULL };
      parse(&f, 2, argv); CHECK(f.stage == NULL); }
}

/* The two compose, which is the whole point: skip the menus AND name a stage. */
static void skip_and_stage_compose(void)
{
    char *argv[] = { (char *) "P", (char *) "-skip", (char *) "-stage",
                     (char *) "frigate", (char *) "-audio_log", NULL };
    PortBootFlags f;

    parse(&f, 5, argv);
    CHECK(f.skip_screens == -1);
    CHECK(f.stage != NULL && strcmp(f.stage, "frigate") == 0);
    CHECK(f.audio_log == 1);
}

/* A bare word is NOT a flag: the first bare argument is the drag-and-drop ROM
 * path (romdataInit), and nothing may consume it. */
static void bare_word_is_not_a_flag(void)
{
    PortBootFlags f;

    { char *argv[] = { (char *) "P", (char *) "audio_pump", NULL };
      parse(&f, 2, argv); CHECK(f.audio_pump == 0); }

    { char *argv[] = { (char *) "P", (char *) "n64_aa", NULL };
      parse(&f, 2, argv); CHECK(f.classic == 0); }
}

/* Unknown flags are ignored, not errors - and must not partially match. */
static void unknown_and_prefix(void)
{
    PortBootFlags f;

    { char *argv[] = { (char *) "P", (char *) "-audio", NULL };
      parse(&f, 2, argv);
      CHECK(f.audio_pump == 0); CHECK(f.audio_log == 0); }

    { char *argv[] = { (char *) "P", (char *) "-audio_pumped", NULL };
      parse(&f, 2, argv); CHECK(f.audio_pump == 0); }

    { char *argv[] = { (char *) "P", (char *) "-n64", NULL };
      parse(&f, 2, argv); CHECK(f.classic == 0); }

    { char *argv[] = { (char *) "P", (char *) "-n64_aaa", NULL };
      parse(&f, 2, argv); CHECK(f.classic == 0); }

    /* The one spelling deliberately not accepted: no `=value` form. Ignored,
     * like any other unknown flag. */
    { char *argv[] = { (char *) "P", (char *) "-n64_aa=1", NULL };
      parse(&f, 2, argv); CHECK(f.classic == 0); }

    { char *argv[] = { (char *) "P", (char *) "-", NULL };
      parse(&f, 2, argv); CHECK(f.classic == 0); }

    { char *argv[] = { (char *) "P", (char *) "--", NULL };
      parse(&f, 2, argv); CHECK(f.audio_pump == 0); }
}

/* The real command line has a ROM path in it. Flags must survive beside it,
 * before or after, and the path itself must not be mistaken for one. */
static void beside_a_rom_path(void)
{
    PortBootFlags f;

    { char *argv[] = { (char *) "P", (char *) "--rom-file", (char *) "/tmp/x.n64",
                       (char *) "-audio_pump", NULL };
      parse(&f, 4, argv); CHECK(f.audio_pump == 1); CHECK(f.classic == 0); }

    { char *argv[] = { (char *) "P", (char *) "-n64_aa", (char *) "/tmp/x.n64", NULL };
      parse(&f, 3, argv); CHECK(f.classic == 1); CHECK(f.audio_pump == 0); }

    /* A path that happens to contain a flag name is not a flag. */
    { char *argv[] = { (char *) "P", (char *) "/carts/audio_pump.n64", NULL };
      parse(&f, 2, argv); CHECK(f.audio_pump == 0); }
}

/* Parse writes every field, so a caller may reuse the struct without clearing. */
static void parse_clears_previous_state(void)
{
    char *yes[] = { (char *) "P", (char *) "-classic", (char *) "-audio_log", NULL };
    char *no[]  = { (char *) "P", NULL };
    PortBootFlags f;

    parse(&f, 3, yes);
    CHECK(f.classic == 1 && f.audio_log == 1);
    parse(&f, 1, no);
    CHECK(f.classic == 0 && f.audio_pump == 0 && f.audio_log == 0);
    /* the spelling too, or a reused struct would name the previous launch's
     * flag in the log line */
    CHECK(f.classic_spelling == NULL);
}

/* The known-flag test the logging uses. Its whole purpose is that it cannot
 * drift from the documented spellings, so the cases are the spellings that
 * appear in CLI.md. */
static void known_flags(void)
{
    /* the three this file parses */
    CHECK(portBootFlagKnown("-classic"));
    CHECK(portBootFlagKnown("--classic"));
    CHECK(portBootFlagKnown("-n64_aa"));
    CHECK(portBootFlagKnown("--n64_aa"));
    CHECK(portBootFlagKnown("-audio_pump"));
    CHECK(portBootFlagKnown("-audio_pump"));
    CHECK(portBootFlagKnown("-audio_log"));
    CHECK(portBootFlagKnown("-skip"));
    CHECK(portBootFlagKnown("--skip"));
    CHECK(portBootFlagKnown("-stage"));

    /* the two romdata.c parses, both spellings each. `=value` is the form the
     * first cut of the log reported as UNKNOWN for two working flags. */
    CHECK(portBootFlagKnown("--rom-file"));
    CHECK(portBootFlagKnown("--rom-file=/tmp/x.n64"));
    CHECK(portBootFlagKnown("-rom-file=/tmp/x.n64"));
    CHECK(portBootFlagKnown("--lang"));
    CHECK(portBootFlagKnown("--lang=jp"));
    CHECK(portBootFlagKnown("-lang=j"));

    /* unknown, and must stay unknown so the report is worth reading */
    CHECK(!portBootFlagKnown("-audio"));
    CHECK(!portBootFlagKnown("-audio_pumped"));
    CHECK(!portBootFlagKnown("--rom"));
    CHECK(!portBootFlagKnown("--langs=jp"));
    CHECK(!portBootFlagKnown("-n64_aa=1"));   /* not acted on, and not known */
    CHECK(!portBootFlagKnown("-classic_mode")); /* the ini key is not a flag */
    CHECK(!portBootFlagKnown("audio_pump"));   /* bare word: the ROM path slot */
    CHECK(!portBootFlagKnown("-"));
    CHECK(!portBootFlagKnown("--"));
    CHECK(!portBootFlagKnown(""));
    CHECK(!portBootFlagKnown(NULL));
}

int main(void)
{
    no_flags();
    each_alone();
    all_three();
    subset_and_order();
    spelling_variants();
    classic_has_two_spellings();
    skip_bare_means_all();
    skip_takes_a_count();
    skip_does_not_eat_a_non_number();
    stage_takes_a_name();
    skip_and_stage_compose();
    bare_word_is_not_a_flag();
    unknown_and_prefix();
    beside_a_rom_path();
    parse_clears_previous_state();
    known_flags();

    printf("boot_flags: all checks passed\n");
    return 0;
}
