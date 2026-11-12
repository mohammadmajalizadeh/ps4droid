#include "va_probe.h"
#include <sys/mman.h>
#include <cstdio>
#include <cstring>
// Occupy part of the low range the way ART would, then check the probe reports it.
int main() {
    void* p = mmap((void*)0x12c00000, 512u << 20, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); return 2; }
    std::string r = ps4droid::RunVaProbe();
    fputs(r.c_str(), stdout);
    bool ok = r.find("system_managed: 1 existing mappings, 524288 KiB occupied") != std::string::npos
           && r.find("system_managed whole: FAILED") != std::string::npos
           && r.find("system_managed largest free run: ok") != std::string::npos
           && r.find("free space starting at 0x400000: 296 MiB") != std::string::npos;
    printf("\nSELFTEST %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
