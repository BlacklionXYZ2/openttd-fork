#!/usr/bin/env bash
# Build starship.grf with NML 0.9.0 (python3 -m pip install nml==0.9.0). Set NMLC to use a specific nmlc.
# The sprites in gfx/ are made from the handoff art by tools/make_sprites.py (needs Pillow).
set -euo pipefail
cd "$(dirname "$0")"
"${NMLC:-nmlc}" -c -o starship.grf starship.nml
echo "Built $(pwd)/starship.grf"
