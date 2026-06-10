// Absolute-minimum WiFi test — nothing but WiFi.begin(). Isolates board vs firmware.
//   pio run -e wifitest -t upload
#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "config.h"

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== minimal WiFi test ===");
  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info) {
    if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
      Serial.printf("  disconnected reason=%d\n", info.wifi_sta_disconnected.reason);
    if (e == ARDUINO_EVENT_WIFI_STA_CONNECTED)
      Serial.println("  ASSOCIATED (link up)");
    if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP)
      Serial.println("  GOT IP");
  });
  WiFi.mode(WIFI_STA);
  // Force 802.11b-only: slowest, most robust modulation (best link margin for weak RF).
  esp_err_t pr = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B);
  Serial.printf("set 11b-only protocol: %s\n", pr == ESP_OK ? "ok" : "FAILED");
  WiFi.begin(DEFAULT_SSID, DEFAULT_PASSWORD);
  Serial.printf("begin('%s') ...\n", DEFAULT_SSID);
}

void loop() {
  static unsigned long t = 0;
  if (millis() - t > 2500) {
    t = millis();
    Serial.printf("[hb] status=%d  ip=%s\n", WiFi.status(), WiFi.localIP().toString().c_str());
  }
}
