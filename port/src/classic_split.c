/*
 * The one-time n64_aa -> classic_mode split. See classic_split.h.
 */

#include "classic_split.h"

void portClassicSplit(const PortClassicSplitIn *in, PortClassicSplitOut *out)
{
    if (in == NULL || out == NULL) {
        return;
    }
    /* The marker and an existing classic_mode are both "this file has been
     * through the split"; either one alone is enough to leave it alone. */
    out->migrate = (!in->had_classic_mode && !in->had_split_marker) ? 1 : 0;
    out->classic_mode = out->migrate ? (in->n64_aa != 0) : (in->classic_mode != 0);
}
