#ifndef PORT_BOOT_FLAGS_H
#define PORT_BOOT_FLAGS_H

/*
 * The three diagnostic boot flags, as a pure parse.
 *
 *   ProjectMidas -classic -audio_pump -audio_log
 *
 * Each is independent and mix-and-match. Nothing here touches the engine, SDL,
 * a clock or a file: the caller passes the argument vector and gets back three
 * booleans (plus the spelling of the one that has two, for the log line).
 * video.c acts on them; port/tests/boot_flags.c drives them.
 *
 * -classic and -n64_aa are THE SAME FLAG. -n64_aa was its only spelling until
 * the Phase 3 key split, when 'n64_aa' stopped meaning Classic Mode and became
 * the AA bit; the old spelling is kept as a silent alias so no script, capture
 * run or muscle memory breaks, and -classic is what new writing should use.
 *
 * WHY THIS IS ITS OWN FILE AND NOT SIX LINES IN video.c
 *
 * A boot flag that is silently ignored is indistinguishable from one that
 * worked, because the only evidence is what the game does afterwards - and by
 * then the reader is looking at the game, not the command line. That is the same
 * defect class as GE_TICKHZ=hw (strtol reads it as 0, so "hw" silently means
 * uncapped), which cost a session. So the matching rules are pinned by a test:
 *
 *   - at least one leading '-' is REQUIRED. `audio_pump` as a bare word is not a
 *     flag, because a bare first argument is the drag-and-drop ROM path and
 *     nothing may quietly eat it.
 *   - more than one dash is accepted: `-x` and `--x` are the same flag. The
 *     ROM/lang flags already in use are `--rom-file`/`--lang`, so both spellings
 *     have to work.
 *   - '-' and '_' are interchangeable in the name: `-audio-pump` and
 *     `-audio_pump` both work. The console verb is `cl_audio_pump`, so the
 *     underscore is the canonical spelling; a hyphen is what most people type.
 *     Accepting both costs a comparison and removes a silent no-op.
 *   - case does not matter.
 *   - an unrecognised flag is ignored, like the rest of this command line, and
 *     is deliberately NOT an error. `-n64_aa=1` is therefore not a flag; it is
 *     ignored, and that is the one spelling this deliberately does not accept.
 */

typedef struct {
    int classic;    /* -classic (alias -n64_aa): start in Classic Mode, the
                     * preset - what the classic_toggle key toggles, not the
                     * AA bit, which the preset sets through its own part */
    int audio_pump; /* -audio_pump: draw the cl_audio_pump block                */
    int audio_log;  /* -audio_log: write Data/Logs/audio_pump.log               */
    /* -skip [N]: boot screens to skip. 0 = skip none; -1 = skip them all, i.e.
     * land on the file select screen. A bare -skip means all, because that is
     * the request a person makes; -skip N is for sitting on a specific screen
     * to capture it. The chain is legal -> Nintendo -> Rare -> eye ->
     * GoldenEye logo -> file select, so N=5 and "all" are the same landing. */
    int skip_screens;
    /* -stage <name>: start a mission directly, the same thing GE_STAGE does.
     * Not owned - points into argv. NULL = no stage requested. */
    const char *stage;
    /* The token that set `classic`, or NULL. For the log line only, and only so
     * it can name the spelling the caller actually typed: a launch that says
     * "-n64_aa -> Classic Mode on" when the command line said -classic would be
     * the same defect as a flag that silently does nothing. Not owned - it
     * points into argv, which outlives this. */
    const char *classic_spelling;
} PortBootFlags;

/*
 * Fill `out` from the argument vector. argv[0] (the program) is skipped.
 * Every field is written on every call, so `out` does not need clearing first.
 *
 * -skip and -stage take the NEXT argument as their value, like the existing
 * --rom-file and --lang. A value that is not a number (-skip banana) is not a
 * count: it is left alone, so the flag keeps its bare meaning and the value is
 * free to be the ROM path. Nothing is silently consumed.
 */
void portBootFlagsParse(int argc, char **argv, PortBootFlags *out);

/*
 * 1 when `arg` names `name`. `name` is the canonical spelling with underscores.
 * Exposed for the test; the caller wants Parse.
 */
int portBootFlagIs(const char *arg, const char *name);

/*
 * 1 when `arg` is a flag this program KNOWS, whether or not portBootFlagsParse
 * acts on it: the three above, plus `rom-file` and `lang` with their `=value`
 * forms, which romdata.c parses.
 *
 * Exists so the "ignoring argument" report cannot drift from the parse. The
 * first cut of that report matched the names itself and quietly reported
 * `--rom-file=/x.n64` and `--lang=jp` as ignored - the documented spelling of
 * two flags that work - because a hand-written name list forgets the `=value`
 * form the moment it is written. One function, used by both.
 */
int portBootFlagKnown(const char *arg);

#endif /* PORT_BOOT_FLAGS_H */
