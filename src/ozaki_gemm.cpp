#include "ozaki/ozaki_gemm.hpp"

#include <stdexcept>

namespace ozaki {

void gemm_fp64_reference(
    size_t M, size_t N, size_t K,
    const double* A, size_t lda,
    const double* B, size_t ldb,
    double* C, size_t ldc)
{
    for (size_t i = 0; i < M; ++i)
        for (size_t j = 0; j < N; ++j) {
            double acc = 0.0;
            for (size_t k = 0; k < K; ++k)
                acc += A[i * lda + k] * B[k * ldb + j];
            C[i * ldc + j] = acc;
        }
}

void gemm_fp64_ozaki(
    size_t, size_t, size_t,
    const double*, size_t,
    const double*, size_t,
    double*, size_t,
    size_t)
{
    // TODO: slice (AVX-512) -> tile GEMM (AMX) -> reconstruct (AVX-512).
    throw std::logic_error("gemm_fp64_ozaki: not implemented yet");
}

} // namespace ozaki
