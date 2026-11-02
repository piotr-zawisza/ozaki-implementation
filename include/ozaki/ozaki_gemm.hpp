#pragma once
#include <cstddef>

namespace ozaki {

// Computes C = A * B using the Ozaki scheme on AMX-INT8 (row-major, FP64).
// TODO: add alpha/beta (C = alpha*A*B + beta*C) once the core works.
void gemm_fp64_ozaki(
    size_t M, size_t N, size_t K,
    const double* A, size_t lda,
    const double* B, size_t ldb,
    double* C, size_t ldc,
    size_t num_splits = 8);

// Plain triple-loop FP64 GEMM, used as the baseline in tests.
void gemm_fp64_reference(
    size_t M, size_t N, size_t K,
    const double* A, size_t lda,
    const double* B, size_t ldb,
    double* C, size_t ldc);

} // namespace ozaki
