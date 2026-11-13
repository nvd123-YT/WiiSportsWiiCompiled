#include "wp/pi.h"

namespace wp::pi {

namespace {

constexpr uint32_t kInitialCause = 0x00010100;
uint32_t g_interrupt_cause = kInitialCause;
uint32_t g_interrupt_mask = 0;

}

bool owns(uint32_t address) {
    uint32_t aligned = address & ~3u;
    return aligned == kInterruptCause || aligned == kInterruptMask;
}

uint16_t read16(uint32_t address) {
    uint32_t value = read32(address & ~3u);
    return (address & 2u) ? static_cast<uint16_t>(value) : static_cast<uint16_t>(value >> 16);
}

uint32_t read32(uint32_t address) {
    switch (address) {
    case kInterruptCause:
        return g_interrupt_cause;
    case kInterruptMask:
        return g_interrupt_mask;
    default:
        return 0;
    }
}

void write8(uint32_t, uint8_t) {}

void write16(uint32_t, uint16_t) {}

void write32(uint32_t address, uint32_t value) {
    switch (address) {
    case kInterruptCause:
        g_interrupt_cause &= ~value;
        break;
    case kInterruptMask:
        g_interrupt_mask = value;
        break;
    default:
        break;
    }
}

void reset() {
    g_interrupt_cause = kInitialCause;
    g_interrupt_mask = 0;
}

}
