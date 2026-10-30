#pragma once

#include <cstddef>

// Values shared with the ESP32 firmware (src/Config.h, src/WebUI.cpp). Keep
// them in sync when the firmware API changes.
namespace agitate3ds {

constexpr char kFallbackServerUrl[] = "http://192.168.4.1";
constexpr char kDefaultFixer[] = "Ilford Rapid Fixer (1+4)";
constexpr size_t kMaximumSetupFieldBytes = 64;
constexpr size_t kMaximumRequestBytes = 1024;

}  // namespace agitate3ds
