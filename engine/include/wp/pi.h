#pragma once

#include <cstdint>

namespace wp::pi {

constexpr uint32_t kInterruptCause = 0xCC003000;
constexpr uint32_t kInterruptMask = 0xCC003004;

bool owns(uint32_t address);
uint16_t read16(uint32_t address);
uint32_t read32(uint32_t address);
void write8(uint32_t address, uint8_t value);
void write16(uint32_t address, uint16_t value);
void write32(uint32_t address, uint32_t value);
void reset();

}
