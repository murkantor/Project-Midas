/*
 * Boot flags - implementation. See boot_flags.h for the matching rules and why
 * they are tested rather than assumed.
 */

#include <stddef.h>

#include "boot_flags.h"

/* Case-insensitive, with '-' and '_' treated as the same character. Written
 * out rather than calling strcasecmp so this file needs neither <string.h>'s
 * locale-sensitive behaviour nor SDL. */
static char lowerDash(char c)
{
    if (c == '-') {
        return '_';
    }
    if (c >= 'A' && c <= 'Z') {
        return (char) (c - 'A' + 'a');
    }
    return c;
}

int portBootFlagIs(const char *arg, const char *name)
{
    if (arg == NULL || name == NULL) {
        return 0;
    }
    /* At least one dash, or this is not a flag. A bare first argument is the
     * drag-and-drop ROM path; nothing may consume it. */
    if (arg[0] != '-') {
        return 0;
    }
    while (*arg == '-') {
        arg++;
    }
    while (*arg != '\0' && *name != '\0') {
        if (lowerDash(*arg) != lowerDash(*name)) {
            return 0;
        }
        arg++;
        name++;
    }
    /* Both must end together: "-audio" must not match "audio_pump". */
    return (*arg == '\0' && *name == '\0') ? 1 : 0;
}

/* A whole decimal argument, or -1. Used for -skip's count: "3" is a count,
 * "dam" and "-audio_pump" are not, and neither is anything too long to be
 * one. Zero counts: -skip 0 is the same as not passing -skip. */
static int argIsCount(const char *a)
{
    int n = 0;
    int i;

    if (a == NULL || a[0] == '\0') {
        return -1;
    }
    for (i = 0; a[i] != '\0'; i++) {
        if (a[i] < '0' || a[i] > '9' || i >= 3) {
            return -1;   /* not a count (or absurd: the chain is 5 long) */
        }
        n = n * 10 + (a[i] - '0');
    }
    return n;
}

void portBootFlagsParse(int argc, char **argv, PortBootFlags *out)
{
    int i;
    int skip_all = 0;   /* a bare -skip: the whole chain */
    int skip_n = 0;     /* -skip N: that many */

    if (out == NULL) {
        return;
    }
    out->classic = 0;
    out->audio_pump = 0;
    out->audio_log = 0;
    out->skip_screens = 0;
    out->stage = NULL;
    out->classic_spelling = NULL;

    if (argv == NULL) {
        return;
    }
    for (i = 1; i < argc; i++) {
        if (portBootFlagIs(argv[i], "classic") || portBootFlagIs(argv[i], "n64_aa")) {
            out->classic = 1;
            out->classic_spelling = argv[i];
        } else if (portBootFlagIs(argv[i], "audio_pump")) {
            out->audio_pump = 1;
        } else if (portBootFlagIs(argv[i], "audio_log")) {
            out->audio_log = 1;
        } else if (portBootFlagIs(argv[i], "skip")) {
            /* The count is the NEXT argument when it is a number, which is how
             * --rom-file and --lang already take their values. A non-number is
             * left alone for whatever else wanted it, and the flag keeps its
             * bare meaning: skip the whole boot chain. */
            const int n = (i + 1 < argc) ? argIsCount(argv[i + 1]) : -1;

            if (n >= 0) {
                skip_n = n;
                i++;             /* consumed */
            } else {
                skip_all = 1;
            }
        } else if (portBootFlagIs(argv[i], "stage")) {
            /* The next argument, unless it is another flag: -stage with nothing
             * after it is a mistake, not a request to load a stage called
             * "-audio_pump". */
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                out->stage = argv[++i];
            }
        }
    }
    /* A bare -skip outranks a count: it is the more aggressive request, and
     * "-skip 2 -skip" should not quietly skip two. */
    out->skip_screens = skip_all ? -1 : skip_n;
}

/* 1 when `arg` matches `name` followed by '=' and anything: the `--flag=value`
 * form. Same dash and separator rules as portBootFlagIs, then a strict '='. */
static int flagIsAssignment(const char *arg, const char *name)
{
    if (arg == NULL || name == NULL || arg[0] != '-') {
        return 0;
    }
    while (*arg == '-') {
        arg++;
    }
    while (*arg != '\0' && *name != '\0') {
        if (lowerDash(*arg) != lowerDash(*name)) {
            return 0;
        }
        arg++;
        name++;
    }
    return (*name == '\0' && *arg == '=') ? 1 : 0;
}

int portBootFlagKnown(const char *arg)
{
    if (arg == NULL) {
        return 0;
    }

    /* The three this file parses. -classic and -n64_aa are one flag, so both
     * spellings must be known: reporting the documented one as ignored is the
     * defect portBootFlagKnown exists to prevent. -skip and -stage take their
     * value from the NEXT argument, so only the bare flag is a flag here. */
    if (portBootFlagIs(arg, "classic") || portBootFlagIs(arg, "n64_aa")
        || portBootFlagIs(arg, "audio_pump") || portBootFlagIs(arg, "audio_log")
        || portBootFlagIs(arg, "skip") || portBootFlagIs(arg, "stage")) {
        return 1;
    }

    /* The two romdata.c parses: the bare flag (its value is the NEXT argument)
     * and the `=value` form. Both are documented, and both must not be reported
     * as unknown. */
    if (portBootFlagIs(arg, "rom-file") || portBootFlagIs(arg, "lang")
        || flagIsAssignment(arg, "rom-file") || flagIsAssignment(arg, "lang")) {
        return 1;
    }

    return 0;
}
