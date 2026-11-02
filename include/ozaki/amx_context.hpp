#pragma once
#include <cstdint>

#ifdef __linux__
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace ozaki {

struct alignas(64) TileConfig {
    uint8_t palette_id = 1;
    uint8_t start_row = 0;
    uint8_t reserved_0[14] = {0};
    uint16_t colsb[16] = {0}; // bytes per row (64 for 16x16 INT32 or 16x64 INT8)
    uint8_t rows[16] = {0};   // number of rows (max 16)
};

// Ask the Linux kernel for permission to use AMX tile data (once per thread).
// Always false on non-Linux hosts.
inline bool init_amx_thread_state() {
#ifdef __linux__
    constexpr int ARCH_REQ_XCOMP_PERM = 0x1023;
    constexpr int XFEATURE_XTILEDATA = 18;
    return syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA) == 0;
#else
    return false;
#endif
}

} // namespace ozaki
