#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "Config.h"
#include "WifiController.h"
#include "StateLock.h"
#include "StepperAgitator.h"
#include "WebUI.h"
#include "Workflow.h"

namespace {
AsyncWebServer server(80);
DNSServer dnsServer;
WiFiUDP discoveryUdp;
StepperAgitator agitator(config::kStepperIn1, config::kStepperIn2, config::kStepperIn3, config::kStepperIn4);
Workflow workflow;
WifiController network;
WebUI webUi(server, workflow, agitator, network);

void processDiscoveryPackets() {
  const int packetSize = discoveryUdp.parsePacket();
  if (packetSize <= 0) {
    return;
  }

  char packet[sizeof(config::kDiscoveryRequest)] = {};
  if (packetSize != sizeof(config::kDiscoveryRequest) - 1) {
    discoveryUdp.clear();
    return;
  }
  const int received = discoveryUdp.read(packet, sizeof(packet) - 1);
  if (received != packetSize ||
      memcmp(packet, config::kDiscoveryRequest, sizeof(config::kDiscoveryRequest) - 1) != 0) {
    return;
  }

  const IPAddress remoteAddress = discoveryUdp.remoteIP();
  const uint16_t remotePort = discoveryUdp.remotePort();
  if (!discoveryUdp.beginPacket(remoteAddress, remotePort)) {
    Serial.println("LAN discovery response could not be started");
    return;
  }
  const size_t responseSize =
      sizeof(config::kDiscoveryResponse) - 1;
  if (discoveryUdp.write(
          reinterpret_cast<const uint8_t *>(config::kDiscoveryResponse),
          responseSize) != responseSize) {
    Serial.println("LAN discovery response could not be queued");
    discoveryUdp.clear();
    return;
  }
  if (!discoveryUdp.endPacket()) {
    Serial.println("LAN discovery response failed to send");
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("agitate-esp firmware %s\n", config::kFirmwareVersion);

  agitator.begin();
  agitator.setSpeedRpm(config::kAgitationRpm);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed: run 'pio run -t uploadfs'");
  } else if (!LittleFS.exists("/index.html")) {
    Serial.println("LittleFS mounted but /index.html is missing: run 'pio run -t uploadfs'");
  }

  network.begin(millis());
  if (network.apStarted()) {
    dnsServer.start(config::kDnsPort, "*", WiFi.softAPIP());
  }
  if (MDNS.begin(config::kWifiHostname)) {
    if (!MDNS.addService("http", "tcp", 80)) {
      Serial.println("mDNS HTTP service registration failed");
    }
    Serial.printf("mDNS hostname: http://%s.local/\n",
                  config::kWifiHostname);
  } else {
    Serial.println("mDNS startup failed; use the displayed IP address");
  }
  if (!discoveryUdp.begin(config::kDiscoveryPort)) {
    Serial.println("LAN discovery UDP server failed to start");
  }

  webUi.begin();
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  processDiscoveryPackets();
  network.loop(millis());
  {
    // Read the clock only once the lock is held: a web handler may have just
    // started a step with a later timestamp than one taken before waiting.
    StateLock lock;
    const uint32_t nowMs = millis();
    agitator.update(nowMs);
    workflow.update(nowMs, agitator);
  }
  webUi.loop(millis());
}
