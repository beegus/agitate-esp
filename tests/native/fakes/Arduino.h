#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

using String = std::string;

constexpr uint8_t OUTPUT = 1;
constexpr uint8_t HIGH = 1;
constexpr uint8_t LOW = 0;

namespace native_test {
extern uint32_t nowMicros;
extern uint32_t digitalWriteCount;
}

template <typename T>
T constrain(T value, T lower, T upper) {
  return std::max(lower, std::min(value, upper));
}

inline void pinMode(uint8_t, uint8_t) {}

inline void digitalWrite(uint8_t, uint8_t) {
  ++native_test::digitalWriteCount;
}

inline uint32_t micros() {
  return native_test::nowMicros;
}
