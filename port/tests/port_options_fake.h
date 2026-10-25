#ifndef PORT_OPTIONS_FAKE_H
#define PORT_OPTIONS_FAKE_H

/*
 * A fake world for driving the shipped port_options.c without a game.
 *
 * Fakes the six bridges port_options.c declares extern, so tests link the real
 * option table, portOptFind, the optSet choke point and the classic adapters.
 *
 * It records WORK as well as values, because two implementations can leave
 * identical values while doing different amounts of it: applying a transition
 * twice ends at the same numbers but overwrites the player's snapshot with the
 * forced ones. Hence the ordered write log, and the save/snapshot counters.
 */

/* Marker used in the write log for an ini save. Never a real key. */
#define FAKE_SAVE_MARK "__save__"

/* "key does not exist" / "no value". */
#define FAKE_MISSING (-99999)

/* Player's own values, mode off, every part enabled, nothing saved. */
void fakeReset(void);

/* Mode ON, its values in force, the player's own values in classic_saved. */
void fakeClassicOn(void);

int  fakeGet(const char *key);
void fakeSet(const char *key, int v);      /* also logs a write */

int  fakeSavedN(void);
int  fakeSaved(int i);

int  fakeIniCalls(void);          /* portOptSave calls this once per section */
void fakeIniCallsReset(void);     /* also clears the write log */
int  fakeSnapshotCalls(void);

int  fakeWriteCount(void);
int  fakeWriteAt(int n, const char **out_key, int *out_val);   /* 0 past the end */

void fakeRefuse(const char *key);      /* make the setter refuse this key */
void fakeAcceptInput(const char *key); /* make this [input] row settable */

/* The preset's classic_n64aa part: 1 (the default) = turning Classic Mode on
 * sets n64_aa, 0 = the preset leaves the bit alone. */
void fakeClassicAaPart(int on);

#endif /* PORT_OPTIONS_FAKE_H */
