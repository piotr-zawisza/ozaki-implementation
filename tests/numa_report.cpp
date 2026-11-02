#include <cstdio>

#if defined(__linux__)

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <linux/mempolicy.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

#ifndef MPOL_MODE_FLAGS
#define MPOL_MODE_FLAGS 0xE000 // mode flags live in the top bits of the mode word
#endif

namespace {

constexpr int kMaxNodes = 1024;
constexpr int kMaskLongs = kMaxNodes / (8 * static_cast<int>(sizeof(unsigned long)));
constexpr std::size_t kBytes = 64u << 20; // 64 MiB: tiny next to 128 GB of HBM, still many pages

bool mask_has(const unsigned long* mask, int node) {
    constexpr int bits = 8 * static_cast<int>(sizeof(unsigned long));
    return ((mask[node / bits] >> (node % bits)) & 1ul) != 0;
}

// "8-15" / "0,2-3" style list of the nodes for which has(node) is true.
template <class Pred>
void print_node_list(Pred has) {
    bool first = true;
    for (int n = 0; n < kMaxNodes;) {
        if (!has(n)) {
            ++n;
            continue;
        }
        int e = n;
        while (e + 1 < kMaxNodes && has(e + 1))
            ++e;
        std::printf(first ? "%d" : ",%d", n);
        if (e > n)
            std::printf("-%d", e);
        first = false;
        n = e + 1;
    }
    if (first)
        std::printf("none");
}

const char* mode_name(int mode) {
    switch (mode) {
    case MPOL_DEFAULT: return "default";
    case MPOL_PREFERRED: return "preferred";
    case MPOL_BIND: return "bind";
    case MPOL_INTERLEAVE: return "interleave";
    default: return "other";
    }
}

} // namespace

// Touches 64 MiB from all OpenMP threads (first touch), then asks the kernel
// via move_pages(2) (nodes == NULL: query only, nothing is moved) which NUMA
// node every page landed on. Under MPOL_BIND (numactl --membind) a page outside
// the bound nodes is a failure; under any other policy the report is informational.
int numa_report() {
    int mode = 0;
    unsigned long mask[kMaskLongs] = {};
    if (syscall(SYS_get_mempolicy, &mode, mask, static_cast<unsigned long>(kMaxNodes), nullptr, 0ul) != 0) {
        std::printf("FAIL numa_report: get_mempolicy: %s\n", std::strerror(errno));
        return 1;
    }
    const int policy = mode & ~MPOL_MODE_FLAGS;
    std::printf("numa_report: policy=%s nodes=", mode_name(policy));
    print_node_list([&](int n) { return mask_has(mask, n); });
    std::printf("\n");

    const long page = sysconf(_SC_PAGESIZE);
    void* mem = mmap(nullptr, kBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        std::printf("FAIL numa_report: mmap: %s\n", std::strerror(errno));
        return 1;
    }
    volatile unsigned char* bytes = static_cast<volatile unsigned char*>(mem);
    const long npages = static_cast<long>(kBytes) / page;

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (long i = 0; i < npages; ++i)
        bytes[i * page] = 1;

    std::vector<void*> pages(static_cast<std::size_t>(npages));
    std::vector<int> status(static_cast<std::size_t>(npages));
    for (long i = 0; i < npages; ++i)
        pages[static_cast<std::size_t>(i)] = static_cast<char*>(mem) + i * page;

    int rc = 0;
    if (syscall(SYS_move_pages, 0L, static_cast<unsigned long>(npages), pages.data(),
                static_cast<const int*>(nullptr), status.data(), 0L) != 0) {
        std::printf("FAIL numa_report: move_pages: %s\n", std::strerror(errno));
        rc = 1;
    } else {
        std::vector<long> per_node(kMaxNodes, 0);
        long bad_status = 0, outside = 0;
        int first_err = 0;
        for (long i = 0; i < npages; ++i) {
            const int s = status[static_cast<std::size_t>(i)];
            if (s < 0 || s >= kMaxNodes) {
                if (bad_status++ == 0)
                    first_err = -s;
                continue;
            }
            ++per_node[static_cast<std::size_t>(s)];
            if (policy == MPOL_BIND && !mask_has(mask, s))
                ++outside;
        }

        std::printf("numa_report: %ld pages:", npages);
        for (int n = 0; n < kMaxNodes; ++n)
            if (per_node[static_cast<std::size_t>(n)] != 0)
                std::printf(" node%d=%ld", n, per_node[static_cast<std::size_t>(n)]);
        std::printf("\n");

        if (bad_status != 0) {
            std::printf("FAIL numa_report: %ld pages without a node (first status: %s)\n", bad_status,
                        std::strerror(first_err));
            rc = 1;
        }
        if (outside != 0) {
            std::printf("FAIL numa_report: %ld pages outside the bound nodes\n", outside);
            rc = 1;
        }
        if (rc == 0)
            std::printf("PASS numa_report\n");
    }

    munmap(mem, kBytes);
    return rc;
}

#else // not Linux

int numa_report() {
    std::printf("SKIP numa_report: Linux only\n");
    return 0;
}

#endif
