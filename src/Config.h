#pragma once

#include <Arduino.h>

namespace config {

// --- ULN2003 driver inputs (ESP32-C3 SuperMini GPIO numbers) ---
// Avoid GPIO 2/8/9 (strapping) and 18/19 (USB D-/D+).
constexpr uint8_t kStepperIn1 = 4;
constexpr uint8_t kStepperIn2 = 5;
constexpr uint8_t kStepperIn3 = 6;
constexpr uint8_t kStepperIn4 = 7;

// --- Motor geometry ---
// 28BYJ-48: 32 steps/rev on the internal rotor x 63.68395 gear ratio.
// Driven in half-step mode that is ~4096 half-steps per output revolution.
constexpr uint16_t kStepsPerRevolution = 4096;

// --- Agitation motion ---
constexpr float kAgitationRpm = 12.0f;
constexpr float kAgitationSweepDegrees = 180.0f;
constexpr float kAutomaticAgitationSweepDegrees = 360.0f;
constexpr uint32_t kAgitationSweepPauseMs = 300;
constexpr float kAggressiveAgitationRpm = 15.0f;
constexpr float kAggressiveAgitationDegrees = 30.0f;
constexpr uint32_t kAggressiveAgitationPauseMs = 100;
constexpr uint8_t kAggressiveAgitationCycles = 20;
constexpr float kMaxDeveloperTimeMinutes = 24.0f * 60.0f;

// --- Access point ---
// SSID gets a per-device suffix so several stations can coexist.
constexpr char kApSsidPrefix[] = "agitate-esp-";
constexpr char kApPassword[] = "developfilm";
constexpr uint8_t kApChannel = 1;
constexpr uint8_t kApMaxClients = 4;
constexpr char kWifiHostname[] = "agitate-esp";
constexpr char kWifiPreferencesNamespace[] = "agitate";
constexpr char kWifiSsidPreferenceKey[] = "ssid";
constexpr char kWifiPasswordPreferenceKey[] = "password";
constexpr char kApPasswordPreferenceKey[] = "ap_password";
constexpr size_t kMaxWifiSsidLength = 32;
constexpr size_t kMaxWifiPasswordLength = 63;
// Home Wi-Fi retries back off so a missing router does not keep the shared radio scanning.
constexpr uint32_t kStationConnectTimeoutMs = 20000;
constexpr uint32_t kStationRetryInitialMs = 30000;
constexpr uint32_t kStationRetryMaxMs = 10UL * 60UL * 1000UL;
// Delay before applying Wi-Fi changes so the HTTP response is delivered first.
constexpr uint32_t kNetworkChangeDelayMs = 1000;
constexpr uint16_t kDiscoveryPort = 42131;
constexpr char kDiscoveryRequest[] = "AGITATEESP_DISCOVER_V1";
constexpr char kDiscoveryResponse[] = "AGITATEESP/1";

// --- Web server ---
constexpr uint16_t kDnsPort = 53;
constexpr size_t kMaxJsonBodyBytes = 1024;
constexpr size_t kMaxStartFieldLength = 64;
constexpr char kDefaultFixer[] = "Ilford Rapid Fixer (1+4)";
constexpr uint32_t kStatusBroadcastIntervalMs = 1000;

}  // namespace config
