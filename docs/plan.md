# Plan — Architektura Implementacji Schematu Ozaki (Intel Xeon Max 9462)

## 1. Zdalne Wykonanie (`ssh max`)

Środowisko docelowe (`ssh max`) to węzeł obliczeniowy z procesorem **Intel Xeon Max 9462**. Kod edytuje się lokalnie; repozytorium na węźle jest aktualizowane przez `git pull --ff-only`, a następnie budowane i testowane na miejscu.

---

## 2. Profil Sprzętowy: Intel Xeon Max 9462 (Sapphire Rapids HBM)

Architektura kodu musi bezpośrednio wykorzystywać specyfikę układu Xeon Max 9462:

| Parametr | Specyfikacja | Implikacja dla Schematu Ozaki |
| :--- | :--- | :--- |
| **Rdzenie / Topologia** | 32 rdzenie (160W), 4 domeny NUMA (Sub-NUMA Clustering / 4 kafelki) | Podział macierzy wzdłuż wymiaru $M$ i $N$ przypięty do lokalnych domen NUMA (`OMP_PROC_BIND=close`, `OMP_PLACES=cores`). |
| **Pamięć HBM2e** | 64 GB HBM2e ($\sim 1\text{ TB/s}$ przepustowości) | Schemat Ozaki zwiększa ruch pamięciowy $d$-krotnie ($d$ – liczba warstw/splits). Bufory warstw muszą rezydować w HBM (`numactl --membind=0-3` w trybie Flat). |
| **Jednostki Wektorowe** | AVX-512 (`F`, `DQ`, `BW`, `VL`, `VNNI`, `BF16`, `FP16`) | Ekstrakcja cech ($\text{getexp}$), skalowanie ($\text{scalef}$) oraz kwantyzacja FP64 do INT8/BF16 realizowane na rejestrach `zmm0..zmm31`. |
| **Jednostki Macierzowe** | **Intel AMX** (`AMX-INT8`, `AMX-BF16`) | Główny silnik obliczeniowy. Rejestry kafelkowe `tmm0..tmm7` ($16 \text{ wierszy} \times 64 \text{ bajty}$). Instrukcja `tdpbssd` mnoży $\text{INT8} \times \text{INT8}$ z dokładną akumulacją do `INT32`. |

---

## 3. Architektura Algorytmiczna (Ozaki Scheme na AMX-INT8 / BF16)

Implementacja opiera się na wariancie całkowitoliczbowym (Ootomo/Ozaki INT8) wspieranym przez `AMX-INT8`, ponieważ akumulacja $\text{INT8} \times \text{INT8} \to \text{INT32}$ jest całkowicie wolna od błędów zaokrągleń pośrednich (w przeciwieństwie do `AMX-BF16`, który akumuluje do FP32 z 24-bitową mantysą).

### Potok Obliczeniowy (3 Fazy)

1. **Faza I — Podział (Bit-Slicing & VNNI Packing, AVX-512):**
   - Dla każdego wiersza $i$ macierzy $A \in \mathbb{R}^{M \times K}$ oraz kolumny $j$ macierzy $B \in \mathbb{R}^{K \times N}$ wyznaczane jest maksimum wykładnika:
     $$\mu_i^A = \max_k \lceil \log_2 |A_{ik}| \rceil, \quad \mu_j^B = \max_k \lceil \log_2 |B_{kj}| \rceil$$
   - Wyznaczany jest krok podziału $\sigma$ (zależny od $\lceil \log_2 K \rceil$, typowo 6-7 bitów na warstwę dla akumulatora `INT32`, aby uniknąć przepełnienia przy redukcji po wymiarze $K_{tile}$).
   - Iteracyjna ekstrakcja $d$ warstw: $A \approx \sum_{p=1}^{d} 2^{e_p^A} \hat{A}^{(p)}$ oraz $B \approx \sum_{q=1}^{d} 2^{e_q^B} \hat{B}^{(q)}$, gdzie $\hat{A}^{(p)}, \hat{B}^{(q)} \in \text{INT8}$.
   - W trakcie zapisu warstwy $\hat{B}^{(q)}$ dane są natychmiast pakowane w układ **VNNI** (czwórki elementów z kolejnych wierszy $k, k+1, k+2, k+3$ ułożone liniowo w pamięci), co jest wymagane przez instrukcję `_tile_dpbssd`.

2. **Faza II — Iloczyny Kafelkowe (Tile GEMM, Intel AMX):**
   - Wykonywane są tylko iloczyny spełniające warunek odcięcia (cutoff): $p + q \le d + 1$. Pomijane są pary $(p, q)$, których wkład leży poniżej precyzji docelowej FP64 ($2^{-53}$).
   - Obliczenie $\hat{C}^{(p,q)} = \hat{A}^{(p)} \cdot \hat{B}^{(q)}$ w podblokach $K_{block}$ (dopasowanych do pamięci podręcznej L2 2MB na rdzeń i bez przepełnienia `INT32`).
   - Rejestry AMX: konfiguracja $2 \times 2$ kafelków wyjściowych (`tmm0..tmm3` jako akumulatory `INT32` o wymiarach $16 \times 16$), `tmm4..tmm5` dla $\hat{A}^{(p)}$ ($16 \times 64$ `INT8`), `tmm6..tmm7` dla $\hat{B}^{(q)}$ ($16 \times 64$ w układzie VNNI `INT8x4`).

3. **Faza III — Rekonstrukcja i Akumulacja (AVX-512 FP64):**
   - Konwersja wyniku `INT32` z kafelka do `FP64` (`_mm512_cvtepi32_pd`).
   - Przywrócenie skali wykładników wierszy/kolumn oraz warstw za pomocą `_mm512_scalef_pd`.
   - Sumowanie od najmniejszych do największych potęg ($p+q = d+1 \to 2$) lub akumulacja kompensowana (algorytm Kahana-Babuški), gwarantująca zachowanie pełnej dokładności FP64 (lub rozszerzonej).

---

## 4. Struktura Repozytorium

```text
ozaki-max/
├── CMakeLists.txt             # Konfiguracja CMake (flagi: -march=sapphirerapids -fopenmp)
├── include/
│   └── ozaki/
│       ├── config.hpp         # Stałe kafelkowania (L1/L2/HBM blocking, max splits)
│       ├── hbm_allocator.hpp  # Alokator wyrównany do 64B z obsługą memkind/libnuma
│       ├── amx_context.hpp    # Inicjalizacja XSAVE/AMX (syscall ARCH_REQ_XCOMP_PERM)
│       ├── slicer_avx512.hpp  # Podział FP64 -> INT8 + pakowanie VNNI
│       ├── kernel_amx.hpp     # Mikro-jądra _tile_dpbssd
│       └── ozaki_gemm.hpp     # Główny interfejs API
├── src/
│   ├── amx_context.cpp
│   ├── slicer_avx512.cpp
│   ├── kernel_amx.cpp
│   └── ozaki_gemm.cpp
├── tests/
│   ├── test_slicer.cpp        # Test bezstratności podziału (rekonstrukcja A == sum(A_p))
│   ├── test_vnni_pack.cpp     # Walidacja układu pamięci dla tdpbssd
│   └── test_accuracy.cpp      # Porównanie z referencją MPFR / FP128 (__float128)
└── scripts/
    └── run_bench.sh           # Skrypt uruchomieniowy z numactl dla ssh max
```

---

## 5. Kluczowe Interfejsy i Sygnatury Kodu

### Inicjalizacja AMX w Linuksie (`include/ozaki/amx_context.hpp`)
Przed użyciem instrukcji kafelkowych proces musi poprosić jądro Linux o uprawnienia do obsługi stanu `XTILE_DATA` (1024 bajty na kafelek):

```cpp
#pragma once
#include <sys/syscall.h>
#include <unistd.h>
#include <immintrin.h>
#include <cstdint>

namespace ozaki {

struct alignas(64) TileConfig {
    uint8_t palette_id = 1;
    uint8_t start_row = 0;
    uint8_t reserved_0[14] = {0};
    uint16_t colsb[16] = {0}; // Bytes per row (64 for 16x16 INT32 or 16x64 INT8)
    uint8_t rows[16] = {0};   // Number of rows (max 16)
};

inline bool init_amx_thread_state() {
    constexpr int ARCH_REQ_XCOMP_PERM = 0x1023;
    constexpr int XFEATURE_XTILEDATA = 18;
    return syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA) == 0;
}

} // namespace ozaki
```

### Struktura Buforów Warstw w HBM (`include/ozaki/ozaki_gemm.hpp`)

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

namespace ozaki {

struct SlicedMatrix {
    int8_t* data;          // Contiguous HBM buffer: [num_splits][M_padded][K_padded]
    double* scale_factors; // Row/Col base exponents: [M_padded]
    size_t num_splits;
    size_t ld;
};

// Computes C = alpha * A * B + beta * C using Ozaki scheme on AMX-INT8
void gemm_fp64_ozaki(
    size_t M, size_t N, size_t K,
    const double* A, size_t lda,
    const double* B, size_t ldb,
    double* C, size_t ldc,
    size_t num_splits = 8
);

} // namespace ozaki
```

---

## 6. Strategia Testowania i Walidacji

1. **Test Jednostkowy Slicera (`test_slicer.cpp`):**
   - Dla $d \ge 8$ i losowej macierzy o niskim uwarunkowaniu suma $\sum_{p=1}^d 2^{e_p^A} \hat{A}^{(p)}$ musi być bitowo identyczna z wejściową macierzą FP64 ($\|A - A_{\text{rec}}\|_{\max} = 0$).
2. **Test Trudnych Macierzy (`test_accuracy.cpp`):**
   - Generowanie macierzy o dużym rozrzucie rzędów wielkości oraz kasowaniu cyfr znaczących (algorytm generowania macierzy testowych Ozakiego z zadanym $\kappa(A)$).
   - Punkt odniesienia: mnożenie w arytmetyce poczwórnej precyzji (`__float128` z biblioteki `libquadmath`).
3. **Skrypt Wykonawczy na Maszynie Zdalnej (`scripts/run_bench.sh`):**
   - Wymusza alokację w HBM2e i separację wątków OpenMP bez migracji między kafelkami:

```bash
#!/usr/bin/env bash
set -euo pipefail

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=close
export OMP_PLACES=cores

# Bind memory strictly to HBM nodes (adjust node IDs to match lscpu on max)
numactl --membind=0,1,2,3 ./build/tests/ozaki_tests
```
