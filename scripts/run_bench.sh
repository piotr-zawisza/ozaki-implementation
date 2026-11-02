#!/usr/bin/env bash
set -euo pipefail

# Independent of the caller's cwd.
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# Host max (verified with numactl -H, 2026-11-02): 64 CPUs in 8 SNC4 nodes 0-7
# (8 CPUs each, DDR ~64 GB); HBM is nodes 8-15 (16 GB each, no CPUs); the
# nearest HBM node for CPU node N is N+8. Binding to all HBM nodes lets the
# kernel serve each first touch from the HBM node nearest the touching CPU.
# Override with OZAKI_HBM_NODES / OMP_NUM_THREADS.
OZAKI_HBM_NODES="${OZAKI_HBM_NODES:-8-15}"
export OMP_NUM_THREADS="${OMP_NUM_THREADS:-$(nproc)}"
export OMP_PROC_BIND=close
export OMP_PLACES=cores

echo "run_bench: membind=${OZAKI_HBM_NODES} OMP_NUM_THREADS=${OMP_NUM_THREADS}"

# --numa-report prints where the pages really landed (move_pages) and fails if
# any page is outside the bound nodes.
numactl --membind="${OZAKI_HBM_NODES}" ./build/ozaki_tests --numa-report
