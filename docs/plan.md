# Plan — Architecture of the Ozaki Scheme Implementation (Intel Xeon Max 9462)

## 1. Remote Execution (`ssh max`)

The target environment (`ssh max`) is a compute node with an **Intel Xeon Max 9462** processor. Code is edited locally; the repository on the node is updated with `git pull --ff-only`, then built and tested there.

---

## 2. Hardware Profile: Intel Xeon Max 9462 (Sapphire Rapids HBM)

The code architecture must directly exploit the specifics of the Xeon Max 9462:

| Parameter | Specification | Implication for the Ozaki Scheme |
| :--- | :--- | :--- |
| **Cores / Topology** | 32 cores (160 W), 4 NUMA domains (Sub-NUMA Clustering / 4 tiles) | Split the matrices along the $M$ and $N$ dimensions, pinned to local NUMA domains (`OMP_PROC_BIND=close`, `OMP_PLACES=cores`). |
| **HBM2e memory** | 64 GB HBM2e ($\sim 1\text{ TB/s}$ bandwidth) | The Ozaki scheme multiplies memory traffic by $d$ ($d$ – number of layers/splits). Layer buffers must reside in HBM (`numactl --membind=0-3` in Flat mode). |
| **Vector units** | AVX-512 (`F`, `DQ`, `BW`, `VL`, `VNNI`, `BF16`, `FP16`) | Feature extraction ($\text{getexp}$), scaling ($\text{scalef}$) and FP64 → INT8/BF16 quantization are done in `zmm0..zmm31` registers. |
| **Matrix units** | **Intel AMX** (`AMX-INT8`, `AMX-BF16`) | The main compute engine. Tile registers `tmm0..tmm7` ($16 \text{ rows} \times 64 \text{ bytes}$). The `tdpbssd` instruction multiplies $\text{INT8} \times \text{INT8}$ with exact accumulation into `INT32`. |

---

## 3. Algorithmic Architecture (Ozaki Scheme on AMX-INT8 / BF16)

The implementation is based on the integer variant (Ootomo/Ozaki INT8) backed by `AMX-INT8`, because $\text{INT8} \times \text{INT8} \to \text{INT32}$ accumulation is completely free of intermediate rounding errors (unlike `AMX-BF16`, which accumulates into FP32 with a 24-bit mantissa).

### Computational Pipeline (3 Phases)

1. **Phase I — Splitting (Bit-Slicing & VNNI Packing, AVX-512):**
   - For each row $i$ of matrix $A \in \mathbb{R}^{M \times K}$ and each column $j$ of matrix $B \in \mathbb{R}^{K \times N}$ the maximum exponent is determined:
     $$\mu_i^A = \max_k \lceil \log_2 |A_{ik}| \rceil, \quad \mu_j^B = \max_k \lceil \log_2 |B_{kj}| \rceil$$
   - The splitting step $\sigma$ is determined (it depends on $\lceil \log_2 K \rceil$, typically 6-7 bits per layer for an `INT32` accumulator, to avoid overflow when reducing over the $K_{tile}$ dimension).
   - Iterative extraction of $d$ layers: $A \approx \sum_{p=1}^{d} 2^{e_p^A} \hat{A}^{(p)}$ and $B \approx \sum_{q=1}^{d} 2^{e_q^B} \hat{B}^{(q)}$, where $\hat{A}^{(p)}, \hat{B}^{(q)} \in \text{INT8}$.
   - While a layer $\hat{B}^{(q)}$ is being written, the data is immediately packed into the **VNNI** layout (quadruples of elements from consecutive rows $k, k+1, k+2, k+3$ laid out linearly in memory), which is required by the `_tile_dpbssd` instruction.

2. **Phase II — Tile Products (Tile GEMM, Intel AMX):**
   - Only products satisfying the cutoff condition $p + q \le d + 1$ are computed. Pairs $(p, q)$ whose contribution lies below the target FP64 precision ($2^{-53}$) are skipped.
   - Compute $\hat{C}^{(p,q)} = \hat{A}^{(p)} \cdot \hat{B}^{(q)}$ in sub-blocks $K_{block}$ (matched to the 2 MB per-core L2 cache and chosen so that `INT32` does not overflow).
   - AMX registers: a $2 \times 2$ configuration of output tiles (`tmm0..tmm3` as $16 \times 16$ `INT32` accumulators), `tmm4..tmm5` for $\hat{A}^{(p)}$ ($16 \times 64$ `INT8`), `tmm6..tmm7` for $\hat{B}^{(q)}$ ($16 \times 64$ in VNNI `INT8x4` layout).

3. **Phase III — Reconstruction and Accumulation (AVX-512 FP64):**
   - Convert the `INT32` result from the tile to `FP64` (`_mm512_cvtepi32_pd`).
   - Restore the scale of the row/column and layer exponents using `_mm512_scalef_pd`.
   - Sum from the smallest to the largest powers ($p+q = d+1 \to 2$) or use compensated accumulation (Kahan-Babuška algorithm), which guarantees full FP64 accuracy (or extended).

---

## 4. Repository Structure

```text
ozaki-max/
├── CMakeLists.txt             # CMake configuration (flags: -march=sapphirerapids -fopenmp)
├── include/
│   └── ozaki/
│       ├── config.hpp         # Tiling constants (L1/L2/HBM blocking, max splits)
│       ├── hbm_allocator.hpp  # 64B-aligned allocator with memkind/libnuma support
│       ├── amx_context.hpp    # XSAVE/AMX initialization (ARCH_REQ_XCOMP_PERM syscall)
│       ├── slicer_avx512.hpp  # FP64 -> INT8 splitting + VNNI packing
│       ├── kernel_amx.hpp     # _tile_dpbssd micro-kernels
│       └── ozaki_gemm.hpp     # Main API
├── src/
│   ├── amx_context.cpp
│   ├── slicer_avx512.cpp
│   ├── kernel_amx.cpp
│   └── ozaki_gemm.cpp
├── tests/
│   ├── test_slicer.cpp        # Splitting losslessness test (reconstruction A == sum(A_p))
│   ├── test_vnni_pack.cpp     # Memory layout validation for tdpbssd
│   └── test_accuracy.cpp      # Comparison with an MPFR / FP128 (__float128) reference
└── scripts/
    └── run_bench.sh           # Launch script with numactl for ssh max
```

---

## 5. Key Interfaces and Code Signatures

### AMX Initialization on Linux (`include/ozaki/amx_context.hpp`)
Before using tile instructions, the process must ask the Linux kernel for permission to use the `XTILE_DATA` state (1024 bytes per tile):

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

### Layer Buffer Structure in HBM (`include/ozaki/ozaki_gemm.hpp`)

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

## 6. Testing and Validation Strategy

1. **Splitter unit test (`test_slicer.cpp`):**
   - For $d \ge 8$ and a random, well-conditioned matrix, the sum $\sum_{p=1}^d 2^{e_p^A} \hat{A}^{(p)}$ must be bitwise identical to the input FP64 matrix ($\|A - A_{\text{rec}}\|_{\max} = 0$).
2. **Hard-matrix test (`test_accuracy.cpp`):**
   - Generate matrices with a wide spread of magnitudes and cancellation of significant digits (Ozaki's test-matrix generation algorithm with a prescribed $\kappa(A)$).
   - Reference: multiplication in quadruple precision (`__float128` from `libquadmath`).
3. **Execution script on the remote machine (`scripts/run_bench.sh`):**
   - Forces allocation in HBM2e and separates OpenMP threads without migration between tiles:

```bash
#!/usr/bin/env bash
set -euo pipefail

export OMP_NUM_THREADS=32
export OMP_PROC_BIND=close
export OMP_PLACES=cores

# Bind memory strictly to HBM nodes (adjust node IDs to match lscpu on max)
numactl --membind=0,1,2,3 ./build/tests/ozaki_tests
```
