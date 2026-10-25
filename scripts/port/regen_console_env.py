#!/usr/bin/env python3
"""Regenerate port/src/port_console_env.h - the list the console's `harness`
command prints.

That table is a snapshot of every `"GE_..."` string literal in the port, written
into a C array. Keeping it current used to mean running the grep from the header
comment and pasting the output between the braces by hand, which is why it had
drifted to 290 of 310 entries by 2026-10-25 - twenty knobs, including GE_HWLOAD
and GE_HWLOAD_SCALE, were invisible to the in-game listing.

Run from the repository root:

    python3 scripts/port/regen_console_env.py

It rewrites the array in place and prints the count. Sorting is LC_ALL=C so the
order is bytewise and the diff shows only real additions; a locale sort puts
"GE_GUNSND_OVERLAP" before "GE_GUNSNDRATE" and produces spurious churn.

The list is what `harness` PRINTS, not a list of knobs that can be toggled: most
are read once at init. It is also a convenience, not the authority - a fresh grep
is, and this script is that grep.
"""

import pathlib
import subprocess
import sys

ROOT_MARKERS = ("port/src/port_console_env.h", "port/src", "src")

# Keep the environment explicit: LC_ALL=C makes `sort -u` bytewise, and a fixed
# PATH keeps the grep on the system one rather than anything the caller's shell
# had set.
ENV = {"LC_ALL": "C", "PATH": "/usr/bin:/bin:/usr/sbin:/sbin"}
GREP = (
    "grep -rhoa '\"GE_[A-Z0-9_]*\"' port/src src port/fast3d "
    "| tr -d '\"' | LC_ALL=C sort -u"
)
OPEN = "static const char *const sPortConEnvNames[] = {"


def main():
    if not all(pathlib.Path(m).exists() for m in ROOT_MARKERS):
        print("run me from the repository root", file=sys.stderr)
        return 2

    header = pathlib.Path("port/src/port_console_env.h")
    text = header.read_text()

    found = subprocess.run(
        GREP, shell=True, capture_output=True, text=True, env=ENV, check=True
    ).stdout.split()
    if not found:
        print("no GE_* names found - refusing to write an empty table", file=sys.stderr)
        return 1

    head, rest = text.split(OPEN, 1)
    tail = rest.split("};", 1)[1]  # keeps the #endif and anything after it
    body = "\n".join('    "%s",' % name for name in found)

    header.write_text(head + OPEN + "\n" + body + "\n};" + tail)
    print("%d GE_* names written to %s" % (len(found), header))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
