# ozaki-implementation

Emulacja GEMM FP64 schematem Ozaki na Intel Xeon Max 9462 (AMX-INT8).

Potok w trzech fazach:

1. **Podział (AVX-512)** — FP64 → warstwy INT8 + wykładniki wierszy/kolumn, pakowanie VNNI.
2. **Iloczyny kafelkowe (AMX)** — `tdpbssd` (INT8×INT8→INT32) dla par warstw `(p,q)` z `p+q ≤ d+1`.
3. **Rekonstrukcja (AVX-512)** — konwersja do FP64, skalowanie wykładników, sumowanie.

Stan: szkielet. Działa `gemm_fp64_reference` (baseline); `gemm_fp64_ozaki` to na razie stub.

## Build

Wymagania: CMake ≥ 3.21, kompilator C++17 (GCC ≥ 11 lub Clang dla `-march=sapphirerapids`), opcjonalnie OpenMP.

```bash
cmake --preset local            # generyczny build (bez AMX), katalog build_local
cmake --build build_local
ctest --test-dir build_local --output-on-failure

cmake --preset max              # Release + -march=sapphirerapids, katalog build (host z AMX)
cmake --build build --target ozaki_tests
ctest --test-dir build --output-on-failure
```

Opcje: `OZAKI_NATIVE_ARCH` (domyślnie ON), `OZAKI_WERROR` (domyślnie OFF; ON w CI).

Kod AMX da się uruchomić tylko na sprzęcie z AMX — CI kompiluje wariant natywny, ale go nie uruchamia.

## Benchmark

`./scripts/run_bench.sh` — piny OpenMP + `numactl --membind`; wymaga zbudowanego `build/ozaki_tests`.

## Dokumentacja

- `docs/plan.md` — specyfikacja algorytmu, sprzętu, interfejsów i testów.
- `docs/setup-max.md` — jednorazowa konfiguracja repo na serwerze `max`.
