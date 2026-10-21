#!/bin/sh
# Runs the PC build from build_pc with its bundled libs (libSDL2 lives there).
# Env knobs pass through, e.g.  GE_STAGE=dam ./run.sh
cd "$(dirname "$0")/build_pc" || exit 1
LD_LIBRARY_PATH=".${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" exec ./ProjectMidas "$@"
