#!/usr/bin/env bash
# Build CorporateInfrastructure.grf with NML 0.9.0 (python3 -m pip install nml==0.9.0). Set NMLC to use a specific nmlc.
set -euo pipefail
cd "$(dirname "$0")"
"${NMLC:-nmlc}" --grf CorporateInfrastructure.grf CorporateInfrastructure.nml
echo "Built $(pwd)/CorporateInfrastructure.grf"
