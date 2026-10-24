#!/usr/bin/env bash
# Build future_logistics.grf with NML 0.9.0 (python3 -m pip install nml==0.9.0). Set NMLC to use a specific nmlc.
set -euo pipefail
cd "$(dirname "$0")"
"${NMLC:-nmlc}" -c -o future_logistics.grf future_logistics.nml
echo "Built $(pwd)/future_logistics.grf"
