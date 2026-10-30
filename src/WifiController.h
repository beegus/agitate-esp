#pragma once

#include <Arduino.h>

#include <atomic>
#include <mutex>

// Owns the always-on access point and the optional home Wi-Fi (station) link.
// Web handlers run in the AsyncTCP task, so they only persist settings and
// queue the change; every radio call happens in loop() on the Arduino task.
class WifiController {
 public:
  void begin(uint32_t nowMs);
  void loop(uint32_t nowMs);

  // Safe to call from web handlers. Each returns false if NVS could not be written.
  bool saveStation(const String &ssid, const String &password);
  bool forgetStation();
  bool saveApPassword(const String &password);

  bool apStarted() const { return apStarted_; }
  const String &apSsid() const { return apSsid_; }
  bool stationConfigured() const { return stationConfigured_; }

 private:
  void startAp(const String &password);
  void connectStation(uint32_t nowMs);
  void applyPendingChanges(uint32_t nowMs);
  void queueChange();

  String apSsid_;
  bool apStarted_ = false;
  std::atomic<bool> stationConfigured_{false};

  // Owned by loop().
  String stationSsid_;
  String stationPassword_;
  bool connecting_ = false;
  bool wasConnected_ = false;
  uint32_t attemptStartMs_ = 0;
  uint32_t retryAtMs_ = 0;
  uint32_t retryDelayMs_ = 0;

  // Written by web handlers, consumed by loop() under mutex_.
  std::mutex mutex_;
  bool changePending_ = false;
  uint32_t changeAtMs_ = 0;
  bool stationChanged_ = false;
  String pendingSsid_;
  String pendingPassword_;
  bool apPasswordChanged_ = false;
  String pendingApPassword_;
};
