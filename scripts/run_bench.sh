#!/usr/bin/env bash
set -euo pipefail

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=close
export OMP_PLACES=cores

# Bind memory strictly to HBM nodes (adjust node IDs to match lscpu on max).
numactl --membind=0,1,2,3 ./build/ozaki_tests
