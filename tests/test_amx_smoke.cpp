#include "ozaki/amx_context.hpp"

#include <cstdio>

#if defined(__linux__) && defined(__AMX_TILE__) && defined(__AMX_INT8__)

#include <cerrno>
#include <cpuid.h>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <random>

static_assert(sizeof(ozaki::TileConfig) == 64, "TileConfig must match the 64-byte LDTILECFG layout");

namespace {

// CPUID.(EAX=7,ECX=0):EDX bit 24 = AMX-TILE, bit 25 = AMX-INT8.
bool cpu_has_amx() {
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
        return false;
    return (edx & (1u << 24)) != 0 && (edx & (1u << 25)) != 0;
}

constexpr int M = 16; // rows of A and C
constexpr int N = 16; // columns of B and C (INT32)
constexpr int K = 64; // INT8 elements per A row; B has K/4 VNNI rows

alignas(64) int8_t g_a[M * K];       // row-major 16 x 64
alignas(64) int8_t g_b[K * N];       // logical B, row-major 64 x 16
alignas(64) int8_t g_b_vnni[K * N];  // 16 rows x 64 bytes: [k/4][j*4 + k%4]
alignas(64) int32_t g_c[M * N];      // 16 x 16 INT32

} // namespace

int test_amx_smoke(bool require_amx) {
    if (!cpu_has_amx()) {
        if (require_amx) {
            std::printf("FAIL amx_smoke: CPUID reports no AMX-TILE/AMX-INT8 but --require-amx was given\n");
            return 1;
        }
        std::printf("SKIP amx: CPU has no AMX-TILE/AMX-INT8\n");
        return 0;
    }

    if (!ozaki::init_amx_thread_state()) {
        const int err = errno;
        std::printf("FAIL amx_smoke: AMX in CPUID but arch_prctl(ARCH_REQ_XCOMP_PERM, XTILEDATA) denied: %s\n",
                    std::strerror(err));
        return 1;
    }

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(-128, 127);
    for (int i = 0; i < M * K; ++i)
        g_a[i] = static_cast<int8_t>(dist(rng));
    for (int i = 0; i < K * N; ++i)
        g_b[i] = static_cast<int8_t>(dist(rng));
    for (int k = 0; k < K; ++k)
        for (int j = 0; j < N; ++j)
            g_b_vnni[(k / 4) * 64 + j * 4 + (k % 4)] = g_b[k * N + j];

    // Register convention of docs/plan.md: C = tmm0, A = tmm4, B = tmm6.
    ozaki::TileConfig cfg;
    cfg.palette_id = 1;
    cfg.colsb[0] = 64;
    cfg.rows[0] = 16;
    cfg.colsb[4] = 64;
    cfg.rows[4] = 16;
    cfg.colsb[6] = 64;
    cfg.rows[6] = 16;

    _tile_loadconfig(&cfg);
    _tile_zero(0);
    _tile_loadd(4, g_a, 64);
    _tile_loadd(6, g_b_vnni, 64);
    _tile_dpbssd(0, 4, 6);
    _tile_stored(0, g_c, 64);
    _tile_release();

    for (int i = 0; i < M; ++i)
        for (int j = 0; j < N; ++j) {
            int32_t ref = 0;
            for (int k = 0; k < K; ++k)
                ref += static_cast<int32_t>(g_a[i * K + k]) * static_cast<int32_t>(g_b[k * N + j]);
            if (g_c[i * N + j] != ref) {
                std::printf("FAIL amx_smoke: C[%d][%d]=%d expected %d\n", i, j, static_cast<int>(g_c[i * N + j]),
                            static_cast<int>(ref));
                return 1;
            }
        }

    std::printf("PASS amx_smoke (tdpbssd 16x16x64)\n");
    return 0;
}

#else // built without AMX (non-Linux or no -march=sapphirerapids)

int test_amx_smoke(bool require_amx) {
    if (require_amx) {
        std::printf("FAIL amx_smoke: built without AMX-TILE/AMX-INT8 but --require-amx was given\n");
        return 1;
    }
    std::printf("SKIP amx: built without AMX-TILE/AMX-INT8 (non-Linux or no -march=sapphirerapids)\n");
    return 0;
}

#endif
