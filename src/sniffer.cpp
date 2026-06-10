// Novy remote SNIFFER — captures the RF codes your physical remote sends.
//
//   pio run -e sniffer -t upload -t monitor
//
// Then press ONE button on your Novy remote at a time and write down the
// printed Value / Bits / Protocol for each. Those go into transmit.cpp.
//
// Expected: Protocol 12, ~350us pulse. If you see nothing, check the antenna,
// 3.3V power, and the GDO0 wire.

#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include "pins.h"

RCSwitch rx = RCSwitch();

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Novy RF sniffer ===");

  ELECHOUSE_cc1101.setSpiPin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  ELECHOUSE_cc1101.setGDO0(PIN_GDO0);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setCCMode(0);       // 0 = async serial: raw OOK envelope on GDO0 (required by RCSwitch)
  ELECHOUSE_cc1101.setModulation(2);   // 2 = ASK/OOK
  ELECHOUSE_cc1101.setMHZ(NOVY_FREQ_MHZ);
  ELECHOUSE_cc1101.SetRx();

  if (ELECHOUSE_cc1101.getCC1101()) {
    Serial.println("CC1101 detected. Listening on 433.92 MHz OOK...");
    Serial.println("Press one button on the Novy remote at a time.");
  } else {
    Serial.println("!! CC1101 NOT detected over SPI. Check wiring (SCK/MISO/MOSI/CSN/3V3).");
  }

  rx.enableReceive(PIN_GDO0);  // on ESP32 the GPIO is the interrupt source
}

void loop() {
  if (rx.available()) {
    Serial.printf("Value: %lu  Bits: %u  Protocol: %u  PulseLen: %u us\n",
                  rx.getReceivedValue(),
                  rx.getReceivedBitlength(),
                  rx.getReceivedProtocol(),
                  rx.getReceivedDelay());
    rx.resetAvailable();
  }

  // Heartbeat every 3s so the monitor shows status even with no RF / after a
  // missed boot banner. CC1101 status is re-read live each beat.
  static unsigned long last = 0;
  if (millis() - last >= 3000) {
    last = millis();
    Serial.printf("[hb] CC1101=%s  listening 433.92MHz OOK — press a remote button\n",
                  ELECHOUSE_cc1101.getCC1101() ? "OK" : "NOT DETECTED (check wiring)");
  }
}
