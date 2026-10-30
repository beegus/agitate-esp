#include "WebUI.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <math.h>
#include <WiFi.h>

#include "Config.h"
#include "StateLock.h"

namespace {
bool isApHost(AsyncWebServerRequest *request) {
  const String apAddress = WiFi.softAPIP().toString();
  const String requestHost = request->host();
  return requestHost == apAddress || requestHost == apAddress + ":80";
}

String stationAddress() {
  return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
}

bool isStationHost(AsyncWebServerRequest *request) {
  const String address = stationAddress();
  const String requestHost = request->host();
  return !address.isEmpty() &&
         (requestHost == address || requestHost == address + ":80");
}

bool isHostnameHost(AsyncWebServerRequest *request) {
  const String hostname = String(config::kWifiHostname) + ".local";
  const String requestHost = request->host();
  return requestHost == hostname || requestHost == hostname + ":80";
}

bool isControlHost(AsyncWebServerRequest *request) {
  return isApHost(request) || isStationHost(request) ||
         isHostnameHost(request);
}

// Browsers send Origin on cross-site POSTs and WebSocket handshakes; curl and scripts usually omit it.
bool isSameOriginRequest(AsyncWebServerRequest *request) {
  if (!isControlHost(request)) {
    return false;
  }

  const AsyncWebHeader *origin = request->getHeader("Origin");
  if (origin == nullptr) {
    return true;
  }

  const String value = origin->value();
  const String apOrigin = String("http://") + WiFi.softAPIP().toString();
  const String staOrigin = String("http://") + stationAddress();
  const String hostnameOrigin =
      String("http://") + config::kWifiHostname + ".local";
  return value == apOrigin || value == apOrigin + ":80" ||
         (!stationAddress().isEmpty() &&
          (value == staOrigin || value == staOrigin + ":80")) ||
         value == hostnameOrigin || value == hostnameOrigin + ":80";
}

bool isApOriginRequest(AsyncWebServerRequest *request) {
  if (!isApHost(request)) {
    return false;
  }

  const AsyncWebHeader *origin = request->getHeader("Origin");
  if (origin == nullptr) {
    return true;
  }

  const String apOrigin = String("http://") + WiFi.softAPIP().toString();
  return origin->value() == apOrigin || origin->value() == apOrigin + ":80";
}

ArRequestHandlerFunction controlHostOnly(ArRequestHandlerFunction handler) {
  return [handler](AsyncWebServerRequest *request) {
    if (!isControlHost(request)) {
      request->send(403, "application/json", "{\"error\":\"unknown host\"}");
      return;
    }
    handler(request);
  };
}

ArRequestHandlerFunction apOriginOnly(ArRequestHandlerFunction handler) {
  return [handler](AsyncWebServerRequest *request) {
    if (!isApOriginRequest(request)) {
      request->send(403, "application/json",
                    "{\"error\":\"Wi-Fi settings can only be changed from the ESP32 access point\"}");
      return;
    }
    handler(request);
  };
}

ArRequestHandlerFunction sameOriginOnly(ArRequestHandlerFunction handler) {
  return [handler](AsyncWebServerRequest *request) {
    if (!isSameOriginRequest(request)) {
      request->send(403, "application/json", "{\"error\":\"cross-origin request rejected\"}");
      return;
    }
    handler(request);
  };
}
}  // namespace

WebUI::WebUI(AsyncWebServer &server, Workflow &workflow, StepperAgitator &agitator, WifiController &network)
    : server_(server),
      workflow_(workflow),
      agitator_(agitator),
      network_(network),
      ws_("/ws"),
      startHandler_("/api/start",
                    [this](AsyncWebServerRequest *request, JsonVariant &json) { handleStart(request, json); }),
      wifiHandler_("/api/wifi",
                   [this](AsyncWebServerRequest *request, JsonVariant &json) { handleWifiConfig(request, json); }),
      apPasswordHandler_("/api/ap-password",
                         [this](AsyncWebServerRequest *request, JsonVariant &json) {
                           handleApPassword(request, json);
                         }),
      lastBroadcastMs_(0) {}

void WebUI::begin() {
  registerRoutes();
  ws_.handleHandshake(isSameOriginRequest);
  server_.addHandler(&ws_);

  // Registered last so API requests never pay for LittleFS lookups first.
  // no-cache forces revalidation on every load so updated profile JSON files
  // (e.g. re-uploaded filesystem images) are never served stale from the
  // browser's HTTP cache.
  server_.serveStatic("/", LittleFS, "/")
      .setDefaultFile("index.html")
      .setCacheControl("no-cache")
      .setFilter(isControlHost);
}

void WebUI::loop(uint32_t nowMs) {
  ws_.cleanupClients();
  if (nowMs - lastBroadcastMs_ >= config::kStatusBroadcastIntervalMs) {
    lastBroadcastMs_ = nowMs;
    if (ws_.count() > 0) {
      broadcastStatus();
    }
  }
}

void WebUI::registerRoutes() {
  server_.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isControlHost(request)) {
      request->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
      return;
    }
    request->send(LittleFS, "/index.html", "text/html");
  });

  server_.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isApOriginRequest(request)) {
      request->send(403, "text/plain", "Wi-Fi setup is available from the ESP32 access point.");
      return;
    }
    request->send(LittleFS, "/wifi.html", "text/html");
  });

  // Lists the JSON preset files under /profiles so the WebUI can fetch and
  // parse each one directly; no profile data is hardcoded in firmware.
  server_.on("/api/profiles", HTTP_GET, controlHostOnly([](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonArray profiles = doc["profiles"].to<JsonArray>();

    File dir = LittleFS.open("/profiles");
    if (dir && dir.isDirectory()) {
      File entry = dir.openNextFile();
      while (entry) {
        String name = entry.name();
        if (!entry.isDirectory() && name.endsWith(".json")) {
          int slash = name.lastIndexOf('/');
          profiles.add(slash >= 0 ? name.substring(slash + 1) : name);
        }
        entry = dir.openNextFile();
      }
      dir.close();
    }

    String payload;
    serializeJson(doc, payload);
    request->send(200, "application/json", payload);
  }));

  startHandler_.setMethod(HTTP_POST);
  startHandler_.setMaxContentLength(config::kMaxJsonBodyBytes);
  server_.addHandler(&startHandler_);

  wifiHandler_.setMethod(HTTP_POST);
  wifiHandler_.setMaxContentLength(config::kMaxJsonBodyBytes);
  server_.addHandler(&wifiHandler_);

  apPasswordHandler_.setMethod(HTTP_POST);
  apPasswordHandler_.setMaxContentLength(config::kMaxJsonBodyBytes);
  server_.addHandler(&apPasswordHandler_);

  server_.on("/api/wifi-forget", HTTP_POST, apOriginOnly([this](AsyncWebServerRequest *request) {
    if (!network_.forgetStation()) {
      request->send(500, "application/json", "{\"error\":\"Could not clear saved Wi-Fi settings\"}");
      return;
    }
    request->send(200, "application/json", "{\"message\":\"Home Wi-Fi forgotten; the access point stays on\"}");
  }));

  server_.on("/api/network", HTTP_GET, apOriginOnly([this](AsyncWebServerRequest *request) {
    const bool connected = WiFi.status() == WL_CONNECTED;
    JsonDocument doc;
    doc["configured"] = network_.stationConfigured();
    doc["connected"] = connected;
    doc["ssid"] = connected ? WiFi.SSID() : "";
    doc["ip"] = connected ? WiFi.localIP().toString() : "";
    doc["hostname"] = String(config::kWifiHostname) + ".local";
    doc["apIp"] = WiFi.softAPIP().toString();
    doc["apSsid"] = network_.apSsid();
    String payload;
    serializeJson(doc, payload);
    request->send(200, "application/json", payload);
  }));

  server_.on("/api/next", HTTP_POST, sameOriginOnly([this](AsyncWebServerRequest *request) {
    String payload;
    {
      StateLock lock;
      const uint32_t nowMs = millis();
      agitator_.stop();
      workflow_.nextStep(nowMs);
      payload = statusJson(nowMs);
    }
    request->send(200, "application/json", payload);
  }));

  server_.on("/api/restart_step", HTTP_POST, sameOriginOnly([this](AsyncWebServerRequest *request) {
    String payload;
    {
      StateLock lock;
      const uint32_t nowMs = millis();
      agitator_.stop();
      workflow_.restartCurrentStep(nowMs);
      payload = statusJson(nowMs);
    }
    request->send(200, "application/json", payload);
  }));

  server_.on("/api/pause", HTTP_POST, sameOriginOnly([this](AsyncWebServerRequest *request) {
    String payload;
    {
      StateLock lock;
      const uint32_t nowMs = millis();
      agitator_.stop();
      workflow_.togglePause(nowMs);
      payload = statusJson(nowMs);
    }
    request->send(200, "application/json", payload);
  }));

  server_.on("/api/stop", HTTP_POST, sameOriginOnly([this](AsyncWebServerRequest *request) {
    String payload;
    {
      StateLock lock;
      workflow_.stop();
      agitator_.stop();
      payload = statusJson(millis());
    }
    request->send(200, "application/json", payload);
  }));

  // Bench check for freshly soldered wiring: one sweep without starting a process.
  server_.on("/api/motor/test", HTTP_POST, sameOriginOnly([this](AsyncWebServerRequest *request) {
    String payload;
    const char *error = nullptr;
    {
      StateLock lock;
      if (workflow_.isRunning()) {
        error = "{\"error\":\"stop the process before testing the motor\"}";
      } else if (agitator_.isBusy()) {
        error = "{\"error\":\"motor is already moving\"}";
      } else {
        agitator_.startTimedSweep(config::kAgitationSweepDegrees, config::kAgitationSweepPauseMs, config::kAgitationRpm);
        payload = statusJson(millis());
      }
    }

    if (error != nullptr) {
      request->send(409, "application/json", error);
      return;
    }
    request->send(200, "application/json", payload);
  }));

  server_.on("/api/motor/aggressive", HTTP_POST, sameOriginOnly([this](AsyncWebServerRequest *request) {
    String payload;
    const char *error = nullptr;
    {
      StateLock lock;
      if (!workflow_.isRunning()) {
        error = "{\"error\":\"start a process before aggressive agitation\"}";
      } else if (workflow_.isPaused()) {
        error = "{\"error\":\"resume the active step before aggressive agitation\"}";
      } else if (workflow_.isWaitingForNext()) {
        error = "{\"error\":\"advance to the next step before aggressive agitation\"}";
      } else if (workflow_.isWaitingForStart()) {
        error = "{\"error\":\"start the step before aggressive agitation\"}";
      } else if (agitator_.isBusy()) {
        error = "{\"error\":\"motor is already moving\"}";
      } else {
        agitator_.startTimedSweep(config::kAggressiveAgitationDegrees,
                      config::kAggressiveAgitationPauseMs,
                      config::kAggressiveAgitationRpm,
                      config::kAggressiveAgitationCycles);
        payload = statusJson(millis());
      }
    }

    if (error != nullptr) {
      request->send(409, "application/json", error);
      return;
    }
    request->send(200, "application/json", payload);
  }));

  server_.on("/api/status", HTTP_GET, controlHostOnly([this](AsyncWebServerRequest *request) {
    String payload;
    {
      StateLock lock;
      payload = statusJson(millis());
    }
    request->send(200, "application/json", payload);
  }));

  server_.onNotFound([](AsyncWebServerRequest *request) {
    if (request->url().startsWith("/api/")) {
      request->send(404, "application/json", "{\"error\":\"not found\"}");
      return;
    }
    if (isControlHost(request)) {
      request->redirect("/");
    } else {
      request->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
    }
  });
}

void WebUI::handleWifiConfig(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!isApOriginRequest(request)) {
    request->send(403, "application/json", "{\"error\":\"Wi-Fi settings can only be changed from the ESP32 access point\"}");
    return;
  }

  const JsonObjectConst body = json.as<JsonObjectConst>();
  const String ssid = body["ssid"] | "";
  const String password = body["password"] | "";
  if (ssid.isEmpty() || ssid.length() > config::kMaxWifiSsidLength ||
      password.length() > config::kMaxWifiPasswordLength ||
      (!password.isEmpty() && password.length() < 8)) {
    request->send(400, "application/json",
                  "{\"error\":\"SSID must be 1-32 bytes; password must be blank or 8-63 bytes\"}");
    return;
  }

  if (!network_.saveStation(ssid, password)) {
    request->send(500, "application/json",
                  "{\"error\":\"Could not save Wi-Fi settings\"}");
    return;
  }

  request->send(202, "application/json",
                "{\"saved\":true,\"message\":\"Credentials saved locally; connecting to the router\"}");
}

void WebUI::handleApPassword(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!isApOriginRequest(request)) {
    request->send(403, "application/json", "{\"error\":\"Wi-Fi settings can only be changed from the ESP32 access point\"}");
    return;
  }

  const JsonObjectConst body = json.as<JsonObjectConst>();
  const String password = body["password"] | "";
  if (password.length() < 8 || password.length() > config::kMaxWifiPasswordLength) {
    request->send(400, "application/json", "{\"error\":\"Access point password must be 8-63 bytes\"}");
    return;
  }

  if (!network_.saveApPassword(password)) {
    request->send(500, "application/json", "{\"error\":\"Could not save the access point password\"}");
    return;
  }

  request->send(202, "application/json",
                "{\"saved\":true,\"message\":\"Saved. The access point restarts now; rejoin it with the new password.\"}");
}

void WebUI::handleStart(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!isSameOriginRequest(request)) {
    request->send(403, "application/json", "{\"error\":\"cross-origin request rejected\"}");
    return;
  }

  String payload;
  JsonObjectConst body = json.as<JsonObjectConst>();
  const String filmId = body["film"] | "";
  const String developerId = body["developer"] | "";
  const String fixer = body["fixer"] | config::kDefaultFixer;
  const String dilution = body["dilution"] | "Custom";
  const bool hasOversizedField =
      filmId.length() > config::kMaxStartFieldLength ||
      developerId.length() > config::kMaxStartFieldLength ||
      fixer.length() > config::kMaxStartFieldLength ||
      dilution.length() > config::kMaxStartFieldLength;
  float customTimeMinutes = body["customTimeMinutes"] | 10.0f;
  if (!isfinite(customTimeMinutes) || customTimeMinutes <= 0.0f) {
    customTimeMinutes = 10.0f;
  }
  customTimeMinutes = min(customTimeMinutes, config::kMaxDeveloperTimeMinutes);

  const uint32_t devSeconds = static_cast<uint32_t>(lroundf(customTimeMinutes * 60.0f));
  bool invalidInput = false;

  {
    StateLock lock;
    const uint32_t nowMs = millis();
    if (workflow_.isRunning()) {
      workflow_.start(nowMs);
    } else if (hasOversizedField) {
      invalidInput = true;
    } else {
      workflow_.configureDeveloper(filmId, developerId, fixer, dilution, devSeconds);
      workflow_.start(nowMs);
    }

    if (!invalidInput) {
      payload = statusJson(nowMs);
    }
  }

  if (invalidInput) {
    request->send(400, "application/json", "{\"error\":\"process fields must be at most 64 characters\"}");
    return;
  }

  request->send(200, "application/json", payload);
}

String WebUI::statusJson(uint32_t nowMs) const {
  JsonDocument doc;
  doc["firmwareVersion"] = config::kFirmwareVersion;
  doc["running"] = workflow_.isRunning();
  doc["paused"] = workflow_.isPaused();
  doc["waitingForNext"] = workflow_.isWaitingForNext();
  doc["waitingForStart"] = workflow_.isWaitingForStart();
  doc["motorActive"] = workflow_.isAgitating() || agitator_.isBusy();
  doc["stepIndex"] = workflow_.currentStepIndex();
  doc["stepName"] = workflow_.currentStep().name;
  doc["stepRemainingSec"] = workflow_.remainingStepSec(nowMs);
  doc["stepDurationSec"] = workflow_.currentStep().durationSec;

  doc["film"] = workflow_.filmId();
  doc["developer"] = workflow_.developerId();
  doc["fixer"] = workflow_.fixerName();
  doc["dilution"] = workflow_.dilution();

  JsonArray steps = doc["steps"].to<JsonArray>();
  for (uint8_t i = 0; i < workflow_.totalSteps(); ++i) {
    const ProcessStep &step = workflow_.stepAt(i);
    JsonObject s = steps.add<JsonObject>();
    s["name"] = step.name;
    s["durationSec"] = step.durationSec;
  }

  String payload;
  serializeJson(doc, payload);
  return payload;
}

void WebUI::broadcastStatus() {
  String payload;
  {
    StateLock lock;
    payload = statusJson(millis());
  }
  ws_.textAll(payload);
}
