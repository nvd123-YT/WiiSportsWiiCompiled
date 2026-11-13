#include <cstdio>
#include <cstdlib>

#include "wp/memory.h"
#include "wp/pi.h"

int main() {
    wp::g_memory = static_cast<uint8_t*>(std::calloc(wp::kMemorySize, 1));
    if (!wp::g_memory) {
        return 2;
    }

    int failures = 0;
    auto check = [&failures](bool condition, const char* expression) {
        if (!condition) {
            std::fprintf(stderr, "failed: %s\n", expression);
            failures++;
        }
    };

    wp::pi::reset();
    check(wp::mmio::read32(wp::pi::kInterruptCause) == 0x00010100, "initial PI cause");
    check(wp::mmio::read32(wp::pi::kInterruptMask) == 0, "initial PI mask");
    check(wp::mmio::read16(wp::pi::kInterruptCause) == 0x0001, "PI cause high-half read");
    check(wp::mmio::read16(wp::pi::kInterruptCause + 2) == 0x0100, "PI cause low-half read");

    wp::mmio::write32(wp::pi::kInterruptMask, 0x00010408);
    check(wp::mmio::read32(wp::pi::kInterruptMask) == 0x00010408, "PI interrupt mask readback");

    wp::mmio::write32(wp::pi::kInterruptCause, 0x00000100);
    check(wp::mmio::read32(wp::pi::kInterruptCause) == 0x00010000, "PI cause write-one-to-clear");

    wp::mmio::write32(wp::pi::kInterruptCause, 0x00010000);
    check(wp::mmio::read32(wp::pi::kInterruptCause) == 0, "PI cause clear remaining bit");

    wp::mmio::write32(wp::pi::kInterruptCause, 0xFFFFFFFF);
    check(wp::mmio::read32(wp::pi::kInterruptCause) == 0, "PI cause remains clear");

    wp::mmio::write16(wp::pi::kInterruptMask, 0xFFFF);
    check(wp::mmio::read32(wp::pi::kInterruptMask) == 0x00010408, "PI halfword writes are ignored");

    std::free(wp::g_memory);
    wp::g_memory = nullptr;
    if (failures == 0) {
        std::puts("PI register tests passed");
    }
    return failures == 0 ? 0 : 1;
}
