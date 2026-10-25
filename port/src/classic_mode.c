/*
 * Classic Mode's forced values - implementation.
 *
 * Pure arithmetic over values the caller hands in: no SDL, no engine headers,
 * no globals, no file. port/tests/classic_mode.c drives these functions; see
 * classic_mode.h for why the rule lives here rather than in any one surface.
 *
 * The one rule that is easy to get wrong, and is therefore stated once here:
 * the SNAPSHOT is decided by the op, never by whether a write differs. Turning
 * the mode on while already at the classic values still remembers them, because
 * turning it off must then restore those same values rather than nothing.
 */

#include <string.h>

#include "classic_mode.h"

static int clampN(int n)
{
    if (n < 0) {
        return 0;
    }
    return (n > CLASSIC_MODE_MAX) ? CLASSIC_MODE_MAX : n;
}

void portClassicPlan(const ClassicInput *in, ClassicPlan *out)
{
    int n;
    int saved_n;
    int i;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (in == NULL) {
        return;
    }

    n = clampN(in->n);
    if (n == 0) {
        return; /* nothing is forced by this build: no snapshot, no writes */
    }
    if (in->parts_on == NULL || in->classic == NULL || in->current == NULL) {
        return;
    }

    /* Remembering happens only when entering the mode. FIX never snapshots, and
     * leaving the mode has nothing to remember - its job is to give back what a
     * previous entry stored. */
    if (in->op == CLASSIC_OP_TOGGLE_ON && in->mode_on) {
        out->snapshot = 1;
        for (i = 0; i < n; i++) {
            out->saved[i] = in->current[i];
        }
        out->saved_n = n;
    }

    if (in->mode_on) {
        /* Forcing: every enabled part goes to its classic value. */
        for (i = 0; i < n; i++) {
            if (in->parts_on[i] && in->current[i] != in->classic[i]) {
                out->write_idx[out->write_n] = i;
                out->write_val[out->write_n] = in->classic[i];
                out->write_n++;
            }
        }
    } else {
        /* Restoring: only what was actually saved, and only for parts still
         * enabled. saved_n may be shorter than n - an older ini stored four
         * fields - and those four are the only ones that go back. */
        saved_n = in->saved_n;
        if (saved_n < 0) {
            saved_n = 0;
        }
        if (saved_n > n) {
            saved_n = n;
        }
        if (in->saved == NULL) {
            saved_n = 0;
        }
        for (i = 0; i < saved_n; i++) {
            if (in->parts_on[i] && in->current[i] != in->saved[i]) {
                out->write_idx[out->write_n] = i;
                out->write_val[out->write_n] = in->saved[i];
                out->write_n++;
            }
        }
    }
}

int portClassicMismatch(const int *parts_on, const int *classic, const int *current,
                        int n, int *idx_out)
{
    int count = 0;
    int i;

    n = clampN(n);
    if (parts_on == NULL || classic == NULL || current == NULL) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (parts_on[i] && current[i] != classic[i]) {
            if (idx_out != NULL) {
                idx_out[count] = i;
            }
            count++;
        }
    }
    return count;
}
