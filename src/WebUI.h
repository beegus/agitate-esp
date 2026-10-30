#pragma once

#include <Arduino.h>
#include <AsyncJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "WifiController.h"
#include "StepperAgitator.h"
#include "Workflow.h"

class WebUI {
 public:
  WebUI(AsyncWebServer &server, Workflow &workflow, StepperAgitator &agitator, WifiController &network);

  void begin();
  void loop(uint32_t nowMs);

  String statusJson(uint32_t nowMs) const;

 private:
  void registerRoutes();
  void broadcastStatus();
  void handleStart(AsyncWebServerRequest *request, JsonVariant &json);
  void handleWifiConfig(AsyncWebServerRequest *request, JsonVariant &json);
  void handleApPassword(AsyncWebServerRequest *request, JsonVariant &json);

  AsyncWebServer &server_;
  Workflow &workflow_;
  StepperAgitator &agitator_;
  WifiController &network_;
  AsyncWebSocket ws_;
  AsyncCallbackJsonWebHandler startHandler_;
  AsyncCallbackJsonWebHandler wifiHandler_;
  AsyncCallbackJsonWebHandler apPasswordHandler_;

  uint32_t lastBroadcastMs_;
};
