#include "ozaki/ozaki_gemm.hpp"

#include <cstdio>
#include <cstring>

// Sanity check of the reference GEMM only. Ozaki tests (slicer, VNNI packing,
// accuracy vs __float128) get added together with the code they cover.
static int test_reference_gemm() {
    const double A[2 * 3] = {1, 2, 3, 4, 5, 6};
    const double B[3 * 2] = {7, 8, 9, 10, 11, 12};
    double C[2 * 2] = {};
    ozaki::gemm_fp64_reference(2, 2, 3, A, 3, B, 2, C, 2);
    const double expected[4] = {58, 64, 139, 154};
    for (int i = 0; i < 4; ++i)
        if (C[i] != expected[i]) {
            std::printf("FAIL reference_gemm: C[%d]=%g expected %g\n", i, C[i], expected[i]);
            return 1;
        }
    return 0;
}

int test_amx_smoke(bool require_amx); // tests/test_amx_smoke.cpp
int numa_report();                    // tests/numa_report.cpp

int main(int argc, char** argv) {
    bool require_amx = false;
    bool report_numa = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--require-amx") == 0) {
            require_amx = true;
        } else if (std::strcmp(argv[i], "--numa-report") == 0) {
            report_numa = true;
        } else {
            std::printf("usage: %s [--require-amx] [--numa-report]\n", argv[0]);
            return 2;
        }
    }

    int failures = 0;
    failures += test_reference_gemm();
    failures += test_amx_smoke(require_amx);
    if (report_numa)
        failures += numa_report();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
