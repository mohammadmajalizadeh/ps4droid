// SPDX-License-Identifier: GPL-2.0-or-later
//
// Experiment E1 (docs/ARCHITECTURE.md 3.1): can the PS4 guest address ranges be
// reserved inside this process, and what already lives there?
//
// Built two ways:
//   - as a standalone executable (VA_PROBE_STANDALONE) for `adb shell` or a Linux host;
//   - as part of the JNI library, called from the app process, which is the case that
//     matters because the app process is forked from zygote and carries ART mappings.
//
// The probe only creates PROT_NONE reservations with MAP_FIXED_NOREPLACE and releases
// them again. It never overwrites an existing mapping.

#include "va_probe.h"

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <sys/mman.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

namespace ps4droid {
namespace {

// Ranges taken from shadPS4 src/core/address_space.cpp at 09f84a0e.
constexpr uint64_t kSystemManagedMin = 0x400000ULL;
constexpr uint64_t kSystemManagedMax = 0x7FFFFBFFFULL;
constexpr uint64_t kSystemReservedMin = 0x7FFFFC000ULL;
constexpr uint64_t kSystemReservedMax = 0xFFFFFFFFFULL;
constexpr uint64_t kUserMin = 0x1000000000ULL;
// Candidate upper bounds for the user area, largest first.
constexpr uint64_t kUserMaxCandidates[] = {
    0x54FFFFFFFFFFULL, // upstream Linux
    0xFFFFFFFFFFULL,   // upstream Windows fallback (1 TiB)
    0x3FFFFFFFFFULL,   // 256 GiB, fits a 39-bit address space
    0x1FFFFFFFFFULL,   // 128 GiB
};

struct Mapping {
    uint64_t begin;
    uint64_t end; // exclusive
    std::string perms;
    std::string name;
};

void Appendf(std::string& out, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

void Appendf(std::string& out, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out += buf;
}

std::vector<Mapping> ReadMaps() {
    std::vector<Mapping> maps;
    FILE* f = fopen("/proc/self/maps", "re");
    if (!f) {
        return maps;
    }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        uint64_t begin = 0, end = 0;
        char perms[8] = {};
        int name_off = 0;
        if (sscanf(line, "%" SCNx64 "-%" SCNx64 " %7s %*s %*s %*s %n", &begin, &end, perms,
                   &name_off) < 3) {
            continue;
        }
        std::string name = name_off > 0 ? std::string(line + name_off) : std::string();
        while (!name.empty() && (name.back() == '\n' || name.back() == ' ')) {
            name.pop_back();
        }
        maps.push_back({begin, end, perms, name});
    }
    fclose(f);
    return maps;
}

// Tries a PROT_NONE reservation that must not replace anything. Returns true on success
// and releases the reservation again.
bool TryReserve(uint64_t begin, uint64_t end_inclusive, int* err) {
    const size_t size = end_inclusive - begin + 1;
    void* p = mmap(reinterpret_cast<void*>(begin), size, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) {
        *err = errno;
        return false;
    }
    if (reinterpret_cast<uint64_t>(p) != begin) {
        // Kernels older than 4.17 ignore MAP_FIXED_NOREPLACE and treat the address as a hint.
        munmap(p, size);
        *err = EEXIST;
        return false;
    }
    munmap(p, size);
    *err = 0;
    return true;
}

// Highest address at which a single page can be mapped, found by bisection. This is the
// practical user address-space limit (39, 42, 47 or 48 bits on arm64).
uint64_t HighestMappablePage(size_t page) {
    uint64_t lo = 1ULL << 30;
    uint64_t hi = 1ULL << 56;
    while (hi - lo > page) {
        const uint64_t mid = (lo + (hi - lo) / 2) & ~(static_cast<uint64_t>(page) - 1);
        void* p = mmap(reinterpret_cast<void*>(mid), page, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
        const bool in_range = p != MAP_FAILED || errno == EEXIST;
        if (p != MAP_FAILED) {
            munmap(p, page);
        }
        if (in_range) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

// First line of a small /proc or /sys text file, or "unreadable".
std::string ReadFirstLine(const char* path) {
    FILE* f = fopen(path, "re");
    if (!f) {
        return "unreadable";
    }
    char line[256] = {};
    if (!fgets(line, sizeof(line), f)) {
        line[0] = 0;
    }
    fclose(f);
    std::string s = line;
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) {
        s.pop_back();
    }
    return s;
}

int BitsFor(uint64_t highest) {
    int bits = 0;
    while (bits < 64 && (highest >> bits) != 0) {
        ++bits;
    }
    return bits;
}

void ReportOccupants(std::string& out, const std::vector<Mapping>& maps, uint64_t begin,
                     uint64_t end_inclusive, const char* label) {
    uint64_t occupied = 0;
    int count = 0;
    for (const auto& m : maps) {
        const uint64_t b = std::max(m.begin, begin);
        const uint64_t e = std::min(m.end, end_inclusive + 1);
        if (b >= e) {
            continue;
        }
        occupied += e - b;
        if (count < 40) {
            Appendf(out, "    %012" PRIx64 "-%012" PRIx64 " %s %s\n", m.begin, m.end,
                    m.perms.c_str(), m.name.empty() ? "[anon]" : m.name.c_str());
        }
        ++count;
    }
    Appendf(out, "  %s: %d existing mappings, %" PRIu64 " KiB occupied\n", label, count,
            occupied / 1024);
}

// First address >= begin from which `size` bytes are free of existing mappings.
uint64_t FirstFreeRun(const std::vector<Mapping>& maps, uint64_t begin, uint64_t end_inclusive,
                      uint64_t* run_size) {
    uint64_t cursor = begin;
    uint64_t best_begin = begin, best = 0;
    for (const auto& m : maps) {
        if (m.end <= cursor) {
            continue;
        }
        if (m.begin > end_inclusive) {
            break;
        }
        if (m.begin > cursor && m.begin - cursor > best) {
            best = m.begin - cursor;
            best_begin = cursor;
        }
        cursor = std::max(cursor, m.end);
    }
    if (cursor <= end_inclusive && end_inclusive + 1 - cursor > best) {
        best = end_inclusive + 1 - cursor;
        best_begin = cursor;
    }
    *run_size = best;
    return best_begin;
}

} // namespace

std::string RunVaProbe() {
    std::string out;
    const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    Appendf(out, "va_probe v2\n");
    Appendf(out, "page_size: %zu\n", page);
    struct utsname uts {};
    if (uname(&uts) == 0) {
        Appendf(out, "kernel: %s %s\n", uts.release, uts.machine);
    }
    Appendf(out, "meminfo: %s\n", ReadFirstLine("/proc/meminfo").c_str());
    Appendf(out, "overcommit_memory: %s\n",
            ReadFirstLine("/proc/sys/vm/overcommit_memory").c_str());

    const uint64_t highest = HighestMappablePage(page);
    Appendf(out, "highest_mappable: 0x%" PRIx64 " (%d-bit user address space)\n", highest,
            BitsFor(highest));

    auto maps = ReadMaps();
    std::sort(maps.begin(), maps.end(),
              [](const Mapping& a, const Mapping& b) { return a.begin < b.begin; });
    Appendf(out, "total_mappings: %zu\n", maps.size());
    if (!maps.empty()) {
        Appendf(out, "lowest_mapping: 0x%" PRIx64 " %s\n", maps.front().begin,
                maps.front().name.c_str());
    }

    Appendf(out, "\n[occupants of guest ranges]\n");
    ReportOccupants(out, maps, kSystemManagedMin, kSystemManagedMax, "system_managed");
    ReportOccupants(out, maps, kSystemReservedMin, kSystemReservedMax, "system_reserved");
    ReportOccupants(out, maps, kUserMin, kUserMaxCandidates[2], "user (to 256 GiB)");

    uint64_t run = 0;
    const uint64_t run_begin = FirstFreeRun(maps, kSystemManagedMin, kSystemManagedMax, &run);
    Appendf(out, "  largest free run in system_managed: 0x%" PRIx64 " + %" PRIu64 " MiB\n",
            run_begin, run >> 20);
    uint64_t low_free_end = kSystemManagedMax + 1;
    for (const auto& m : maps) {
        if (m.end > kSystemManagedMin) {
            low_free_end = std::min(low_free_end, std::max(m.begin, kSystemManagedMin));
            break;
        }
    }
    Appendf(out, "  free space starting at 0x400000: %" PRIu64 " MiB\n",
            (low_free_end - kSystemManagedMin) >> 20);

    Appendf(out, "\n[reservation attempts, PROT_NONE + MAP_FIXED_NOREPLACE]\n");
    int err = 0;
    bool ok = TryReserve(kSystemManagedMin, kSystemManagedMax, &err);
    Appendf(out, "  system_managed whole: %s%s%s\n", ok ? "ok" : "FAILED", ok ? "" : " errno=",
            ok ? "" : strerror(err));
    if (!ok && run >= 2 * page) {
        const uint64_t mask = ~(static_cast<uint64_t>(page) - 1);
        const uint64_t aligned_begin = (run_begin + page - 1) & mask;
        const uint64_t aligned_end = (run_begin + run) & mask; // exclusive
        ok = TryReserve(aligned_begin, aligned_end - 1, &err);
        Appendf(out, "  system_managed largest free run: %s%s%s\n", ok ? "ok" : "FAILED",
                ok ? "" : " errno=", ok ? "" : strerror(err));
    }
    ok = TryReserve(kSystemReservedMin, kSystemReservedMax, &err);
    Appendf(out, "  system_reserved whole: %s%s%s\n", ok ? "ok" : "FAILED", ok ? "" : " errno=",
            ok ? "" : strerror(err));
    uint64_t best_user_max = 0;
    for (const uint64_t user_max : kUserMaxCandidates) {
        ok = TryReserve(kUserMin, user_max, &err);
        Appendf(out, "  user 0x%" PRIx64 "-0x%" PRIx64 " (%" PRIu64 " GiB): %s%s%s\n", kUserMin,
                user_max, (user_max - kUserMin + 1) >> 30, ok ? "ok" : "FAILED",
                ok ? "" : " errno=", ok ? "" : strerror(err));
        if (ok && best_user_max == 0) {
            best_user_max = user_max;
        }
    }
    Appendf(out, "\nrecommended_user_max: 0x%" PRIx64 "\n", best_user_max);
    return out;
}

} // namespace ps4droid

#ifdef VA_PROBE_STANDALONE
int main() {
    fputs(ps4droid::RunVaProbe().c_str(), stdout);
    return 0;
}
#endif
