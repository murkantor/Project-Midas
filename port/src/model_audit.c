/**
 * model_audit.c — MP model-data crash diagnostics (bug-triage F1).
 *
 * Two MP crashes walk model data into garbage pointers:
 *   F1.1  model->anim == 0x00000B84 (an index/raw word where a pointer
 *         belongs) -> modelConstrainOrWrapAnimFrame reads 0xB88.
 *   F1.2  hat attach node == 0x787f2a18 (outside every valid range) ->
 *         modelFindNodeMtxIndex faults walking node->Parent.
 *
 * This file provides:
 *   portPtrLooksBad()      - heuristic: is this value definitely not a
 *                            valid model-data pointer on this process?
 *   portModelAuditReport() - rate-limited console report for guard sites.
 *   portModelAuditWalk()   - GE_MODEL_AUDIT=1: walk a model's node tree at
 *                            modelInit time and report bad links up front
 *                            (name the culprit at load, not crash at use).
 *
 * Guard sites live in src/game/model.c and src/game/chr.c under
 * #ifndef TARGET_N64 — they print + skip instead of crashing. See
 * docs/port/bug-triage-2026-08.md F1 for the ledger.
 *
 * Honest limits: the check is deliberately conservative (near-NULL and
 * misalignment only) so it cannot false-positive on any host address map -
 * see portPtrLooksBad. It catches raw-value-as-pointer bugs, not wild
 * addresses.
 */
#include <ultra64.h>

extern int printf(const char *, ...);

int portPtrLooksBad(const void *p)
{
    uintptr_t v = (uintptr_t)p;

    /* Conservative on purpose: flag only a small integer where a pointer
     * belongs, or misalignment. Host address ranges differ per OS (Linux puts
     * the exe at 0x08xxxxxx), so stricter range checks rejected good pointers. */
    if (v < 0x10000)
    {
        return 1;
    }
    if (v & 3)
    {
        return 1;
    }
    return 0;
}

void portModelAuditReport(const char *site, const void *badptr,
                          const void *ctx1, const void *ctx2)
{
    static int budget = 40;     /* whole-session cap; enough to see a pattern */

    if (budget <= 0) {
        return;
    }
    budget--;
    printf("ge: MODEL AUDIT: %s bad ptr %p (ctx %p %p)%s\n",
           site, badptr, ctx1, ctx2,
           budget == 0 ? " [report cap reached]" : "");
}

/* Node layout offsets per bondtypes.h ModelNode (Opcode u32 @0x00, Data
 * @0x04, Parent @0x08, Next @0x0c, Prev @0x10, Child @0x14). Declared raw
 * here to keep this file free of bondtypes.h include-order baggage. */
struct auditnode {
    u32 opcode;
    void *data;
    struct auditnode *parent;
    struct auditnode *next;
    struct auditnode *prev;
    struct auditnode *child;
};

/* Walk the node tree depth-first (Child then Next), validating every link
 * and each node's Data pointer. header/root come from modelInit. Bounded so
 * a corrupt Next-cycle can't hang the load. */
void portModelAuditWalk(const void *header, const void *rootnode)
{
    static int armed = -1;
    struct auditnode *stack[64];
    int sp = 0;
    int seen = 0;
    struct auditnode *node;

    if (armed < 0) {
        extern char *getenv(const char *);
        armed = getenv("GE_MODEL_AUDIT") != NULL;
    }
    if (!armed) {
        return;
    }

    if (rootnode == NULL) {
        return;                 /* no tree is legal (anim-only models) */
    }
    if (portPtrLooksBad(rootnode)) {
        portModelAuditReport("modelInit:RootNode", rootnode, header, NULL);
        return;
    }

    node = (struct auditnode *) rootnode;
    for (;;) {
        if (seen++ > 4096) {
            portModelAuditReport("modelInit:node-cycle?", node, header, NULL);
            return;
        }
        if (node->data != NULL && portPtrLooksBad(node->data)) {
            portModelAuditReport("modelInit:node->Data", node->data, header, node);
        }
        if (node->child != NULL) {
            if (portPtrLooksBad(node->child)) {
                portModelAuditReport("modelInit:node->Child", node->child, header, node);
            } else {
                if (node->next != NULL && sp < 64) {
                    if (portPtrLooksBad(node->next)) {
                        portModelAuditReport("modelInit:node->Next", node->next, header, node);
                    } else {
                        stack[sp++] = node->next;
                    }
                }
                node = node->child;
                continue;
            }
        }
        if (node->next != NULL) {
            if (portPtrLooksBad(node->next)) {
                portModelAuditReport("modelInit:node->Next", node->next, header, node);
            } else {
                node = node->next;
                continue;
            }
        }
        if (sp > 0) {
            node = stack[--sp];
            continue;
        }
        return;
    }
}
