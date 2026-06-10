// RAW edge-timing sniffer — protocol-agnostic capture of the OOK waveform.
//
//   pio run -e rawsniff -t upload -t monitor
//
// Logs the microsecond gap between every GDO0 transition. A long pulse
// (> SYNC_GAP) delimits one repeated frame. Prints the raw durations plus a
// "units" view (each duration / estimated base pulse) so the bit pattern can
// be reconstructed regardless of how rc-switch interprets it.
//
// Use this to capture the TRUE +/- codes (rc-switch only saw their preamble).

#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include "pins.h"

#define MAXE      300
#define SYNC_GAP  4000   // us: gap/sync longer than this = frame boundary
#define MIN_EDGES 16     // ignore short noise bursts

volatile unsigned long lastEdge = 0;
volatile unsigned int  buf[MAXE];
volatile int           idx = 0;
volatile unsigned int  frame[MAXE];
volatile int           frameLen = 0;
volatile bool          ready = false;
volatile unsigned long totalEdges = 0;   // diagnostic

void IRAM_ATTR onEdge() {
  unsigned long now = micros();
  unsigned int d = (unsigned int)(now - lastEdge);
  lastEdge = now;
  totalEdges++;
  if (d > SYNC_GAP || idx >= MAXE) {
    if (idx >= MIN_EDGES && !ready) {
      for (int i = 0; i < idx; i++) frame[i] = buf[i];
      frameLen = idx;
      ready = true;
    }
    idx = 0;
  } else {
    buf[idx++] = d;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== RAW edge sniffer (protocol-agnostic) ===");

  ELECHOUSE_cc1101.setSpiPin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  ELECHOUSE_cc1101.setGDO0(PIN_GDO0);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setCCMode(0);       // async serial: raw OOK envelope on GDO0
  ELECHOUSE_cc1101.setModulation(2);   // ASK/OOK
  ELECHOUSE_cc1101.setMHZ(NOVY_FREQ_MHZ);
  ELECHOUSE_cc1101.setRxBW(58);        // narrow RX bandwidth -> suppress idle noise
  ELECHOUSE_cc1101.SetRx();

  Serial.println(ELECHOUSE_cc1101.getCC1101()
                 ? "CC1101 OK — press ONE button repeatedly."
                 : "!! CC1101 NOT detected.");
  attachInterrupt(digitalPinToInterrupt(PIN_GDO0), onEdge, CHANGE);
}

void loop() {
  // Diagnostic heartbeat: shows whether edges are arriving at all.
  static unsigned long last = 0;
  if (millis() - last >= 3000) {
    last = millis();
    Serial.printf("[hb] totalEdges=%lu  (press a button — should jump up)\n", totalEdges);
  }

  if (!ready) return;

  // Estimate base pulse = smallest plausible duration in the frame.
  unsigned int mn = 65535;
  for (int i = 0; i < frameLen; i++)
    if (frame[i] > 80 && frame[i] < mn) mn = frame[i];
  if (mn < 80) mn = 80;

  Serial.printf("FRAME edges=%d base~%uus\n", frameLen, mn);
  Serial.print("raw:   ");
  for (int i = 0; i < frameLen; i++) Serial.printf("%u ", frame[i]);
  Serial.println();
  Serial.print("units: ");
  for (int i = 0; i < frameLen; i++) Serial.printf("%u ", (frame[i] + mn / 2) / mn);
  Serial.println();

  ready = false;
}
