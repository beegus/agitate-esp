#include "WifiController.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "Config.h"

namespace {
bool timeReached(uint32_t nowMs, uint32_t targetMs) {
  return static_cast<int32_t>(nowMs - targetMs) >= 0;
}

bool validApPassword(const String &password) {
  return password.length() >= 8 && password.length() <= config::kMaxWifiPasswordLength;
}
}  // namespace

void WifiController::begin(uint32_t nowMs) {
  WiFi.persistent(false);
  // Reconnects are scheduled in loop(). The driver's own auto-reconnect keeps
  // scanning every channel while the router is missing, which makes the AP
  // (sharing the single radio) drop clients.
  WiFi.setAutoReconnect(false);
  WiFi.setHostname(config::kWifiHostname);

  char ssid[33] = {};
  const uint64_t chipId = ESP.getEfuseMac();
  snprintf(ssid, sizeof(ssid), "%s%06llX", config::kApSsidPrefix,
           static_cast<unsigned long long>(chipId & 0xFFFFFFULL));
  apSsid_ = ssid;

  String apPassword = config::kApPassword;
  Preferences preferences;
  if (preferences.begin(config::kWifiPreferencesNamespace, false)) {
    apPassword = preferences.getString(config::kApPasswordPreferenceKey, config::kApPassword);
    stationSsid_ = preferences.getString(config::kWifiSsidPreferenceKey, "");
    stationPassword_ = preferences.getString(config::kWifiPasswordPreferenceKey, "");
    preferences.end();
  } else {
    Serial.println("Could not open saved Wi-Fi settings; using defaults");
  }
  if (!validApPassword(apPassword)) {
    apPassword = config::kApPassword;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  startAp(apPassword);

  retryDelayMs_ = config::kStationRetryInitialMs;
  stationConfigured_ = !stationSsid_.isEmpty();
  if (stationConfigured_) {
    Serial.printf("Connecting to home Wi-Fi SSID: %s\n", stationSsid_.c_str());
    connectStation(nowMs);
  } else {
    Serial.println("No home Wi-Fi saved; direct AP remains available");
  }
}

void WifiController::startAp(const String &password) {
  apStarted_ = WiFi.softAP(apSsid_.c_str(), password.c_str(), config::kApChannel, 0, config::kApMaxClients);
  if (apStarted_) {
    const esp_err_t protocolResult =
        esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    Serial.printf("AP 802.11b/g/n protocol %s (0x%x)\n", protocolResult == ESP_OK ? "enabled" : "FAILED",
                  static_cast<unsigned int>(protocolResult));
  }
  Serial.printf("AP %s SSID: %s\n", apStarted_ ? "up" : "FAILED", apSsid_.c_str());
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void WifiController::connectStation(uint32_t nowMs) {
  WiFi.begin(stationSsid_.c_str(), stationPassword_.c_str());
  connecting_ = true;
  attemptStartMs_ = nowMs;
}

void WifiController::loop(uint32_t nowMs) {
  applyPendingChanges(nowMs);
  if (stationSsid_.isEmpty()) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!wasConnected_) {
      Serial.printf("Home Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
    }
    wasConnected_ = true;
    connecting_ = false;
    retryDelayMs_ = config::kStationRetryInitialMs;
    return;
  }

  if (wasConnected_) {
    wasConnected_ = false;
    Serial.println("Home Wi-Fi lost; reconnecting");
    connectStation(nowMs);
    return;
  }

  if (connecting_) {
    if (nowMs - attemptStartMs_ >= config::kStationConnectTimeoutMs) {
      WiFi.disconnect();  // stop the station scan so the AP stays stable
      connecting_ = false;
      retryAtMs_ = nowMs + retryDelayMs_;
      Serial.printf("Home Wi-Fi unavailable; retrying in %lu s\n",
                    static_cast<unsigned long>(retryDelayMs_ / 1000));
      retryDelayMs_ = min(retryDelayMs_ * 2, config::kStationRetryMaxMs);
    }
    return;
  }

  if (timeReached(nowMs, retryAtMs_)) {
    connectStation(nowMs);
  }
}

void WifiController::applyPendingChanges(uint32_t nowMs) {
  bool stationChanged = false;
  bool apPasswordChanged = false;
  String ssid;
  String password;
  String apPassword;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!changePending_ || !timeReached(nowMs, changeAtMs_)) {
      return;
    }
    changePending_ = false;
    stationChanged = stationChanged_;
    apPasswordChanged = apPasswordChanged_;
    stationChanged_ = false;
    apPasswordChanged_ = false;
    ssid = pendingSsid_;
    password = pendingPassword_;
    apPassword = pendingApPassword_;
  }

  if (apPasswordChanged) {
    Serial.println("Restarting AP with the new password");
    startAp(apPassword);
  }

  if (stationChanged) {
    stationSsid_ = ssid;
    stationPassword_ = password;
    wasConnected_ = false;
    retryDelayMs_ = config::kStationRetryInitialMs;
    if (stationSsid_.isEmpty()) {
      Serial.println("Home Wi-Fi forgotten");
      WiFi.disconnect();
      connecting_ = false;
    } else {
      Serial.printf("Connecting to home Wi-Fi SSID: %s\n", stationSsid_.c_str());
      connectStation(nowMs);
    }
  }
}

// Gives the HTTP response time to reach the phone before the radio is reconfigured.
// Caller holds mutex_.
void WifiController::queueChange() {
  changePending_ = true;
  changeAtMs_ = millis() + config::kNetworkChangeDelayMs;
}

bool WifiController::saveStation(const String &ssid, const String &password) {
  Preferences preferences;
  if (!preferences.begin(config::kWifiPreferencesNamespace, false)) {
    return false;
  }
  preferences.putString(config::kWifiSsidPreferenceKey, ssid);
  preferences.putString(config::kWifiPasswordPreferenceKey, password);
  const bool saved = preferences.getString(config::kWifiSsidPreferenceKey, "") == ssid &&
                     preferences.getString(config::kWifiPasswordPreferenceKey, "\x01") == password;
  preferences.end();
  if (!saved) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  stationChanged_ = true;
  pendingSsid_ = ssid;
  pendingPassword_ = password;
  stationConfigured_ = true;
  queueChange();
  return true;
}

bool WifiController::forgetStation() {
  Preferences preferences;
  if (!preferences.begin(config::kWifiPreferencesNamespace, false)) {
    return false;
  }
  preferences.remove(config::kWifiSsidPreferenceKey);
  preferences.remove(config::kWifiPasswordPreferenceKey);
  const bool cleared = !preferences.isKey(config::kWifiSsidPreferenceKey);
  preferences.end();
  if (!cleared) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  stationChanged_ = true;
  pendingSsid_ = "";
  pendingPassword_ = "";
  stationConfigured_ = false;
  queueChange();
  return true;
}

bool WifiController::saveApPassword(const String &password) {
  if (!validApPassword(password)) {
    return false;
  }
  Preferences preferences;
  if (!preferences.begin(config::kWifiPreferencesNamespace, false)) {
    return false;
  }
  preferences.putString(config::kApPasswordPreferenceKey, password);
  const bool saved = preferences.getString(config::kApPasswordPreferenceKey, "") == password;
  preferences.end();
  if (!saved) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  apPasswordChanged_ = true;
  pendingApPassword_ = password;
  queueChange();
  return true;
}
