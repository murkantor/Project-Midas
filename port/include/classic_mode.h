#ifndef PORT_CLASSIC_MODE_H
#define PORT_CLASSIC_MODE_H

/*
 * Classic Mode's forced values, as pure arithmetic.
 *
 * Entering the mode remembers the seven settings it owns and forces its own
 * values on them; leaving it puts the remembered ones back. No SDL, no globals,
 * no key names - the caller passes values positionally and gets back the writes
 * to perform. port/tests/classic_mode.c drives it.
 *
 * Why it exists: that rule was written three times in three surfaces and skipped
 * by four entry points. See docs/port/settings-and-save-audit.md.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed array size. The real count is portClassicCount() (7, append-only); this
 * is headroom, and callers pass their own n. */
#define CLASSIC_MODE_MAX 16

typedef enum {
    CLASSIC_OP_TOGGLE_ON = 0,   /* entering: remember `current`, then force   */
    CLASSIC_OP_TOGGLE_OFF,      /* leaving: restore what `saved` holds        */
    CLASSIC_OP_FIX              /* repair in place, never remember            */
} ClassicOp;

typedef struct {
    ClassicOp op;
    int mode_on;          /* the state being moved to; for FIX, the state now  */
    const int *parts_on;  /* n entries: 1 = the CLASSIC sheet forces this part  */
    const int *classic;   /* n entries: the value that part forces              */
    const int *current;   /* n entries: the live value now                      */
    const int *saved;     /* saved_n entries: what classic_saved holds          */
    int saved_n;          /* 0 = nothing remembered (old or hand-edited ini)    */
    int n;                /* number of parts; portClassicCount()                */
} ClassicInput;

typedef struct {
    int snapshot;                     /* 1 = store `saved` BEFORE the writes  */
    int saved[CLASSIC_MODE_MAX];
    int saved_n;
    int write_idx[CLASSIC_MODE_MAX];  /* part index to write */
    int write_val[CLASSIC_MODE_MAX];
    int write_n;
} ClassicPlan;

/*
 * Fill `out` from `in`. Every field is written, so `out` need not be cleared.
 *
 * Only values that differ are emitted - a consistent file plans no writes; but
 * the snapshot is decided by the OP, not by whether a write differs: entering
 * while already at the classic values still remembers them, or leaving would
 * restore nothing. The snapshot must land before the writes.
 */
void portClassicPlan(const ClassicInput *in, ClassicPlan *out);

/*
 * How many forced parts do not match the live values. `idx_out` (optional) gets
 * the PART indices, for a log line that names them. 0 when consistent, or when
 * every part is switched off - nothing is forced then, so nothing is wrong.
 */
int portClassicMismatch(const int *parts_on, const int *classic, const int *current,
                        int n, int *idx_out);

#ifdef __cplusplus
}
#endif

#endif /* PORT_CLASSIC_MODE_H */
