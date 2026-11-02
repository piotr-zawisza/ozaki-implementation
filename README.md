# ozaki-implementation

FP64 GEMM emulation with the Ozaki scheme on Intel Xeon Max 9462 (AMX-INT8).

The pipeline has three phases:

1. **Splitting (AVX-512)** — FP64 → INT8 layers + row/column exponents, VNNI packing.
2. **Tile products (AMX)** — `tdpbssd` (INT8×INT8→INT32) for layer pairs `(p,q)` with `p+q ≤ d+1`.
3. **Reconstruction (AVX-512)** — conversion to FP64, exponent scaling, summation.

Status: skeleton. `gemm_fp64_reference` (the baseline) works; `gemm_fp64_ozaki` is a stub for now.

## Build

Requirements: CMake ≥ 3.21, a C++17 compiler (GCC ≥ 11 or Clang for `-march=sapphirerapids`), optionally OpenMP.

```bash
cmake --preset local            # generic build (no AMX), directory build_local
cmake --build build_local
ctest --test-dir build_local --output-on-failure

cmake --preset max              # Release + -march=sapphirerapids, directory build (AMX host)
cmake --build build --target ozaki_tests
ctest --test-dir build --output-on-failure
```

Options: `OZAKI_NATIVE_ARCH` (default ON), `OZAKI_WERROR` (default OFF; ON in CI).

The AMX code can only run on hardware with AMX — CI compiles the native variant but does not run it.

## Benchmark

`./scripts/run_bench.sh` — OpenMP pinning + `numactl --membind`; requires a built `build/ozaki_tests`.

## Documentation

- `docs/plan.md` — specification of the algorithm, hardware, interfaces and tests.
- `docs/setup-max.md` — one-time setup of the repository on the `max` server.
