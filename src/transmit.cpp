// Novy remote TRANSMITTER — replays codes captured from THIS remote.
//
//   pio run -e novy -t upload -t monitor
//
// Captured 2026 from the physical remote (rc-switch protocol 11, ~444us pulse):
//   LIGHT   = 87505 (18 bits)
//   POWER   = 87507 (18 bits)
//   PLUS +  =  1365 (12 bits)
//   MINUS - =  1366 (12 bits)
//   BTN5    =  1364 (12 bits)   <- confirm what this button does
//
// Serial keys (open the monitor):  l=light  p=power  +=plus  -=minus  n=btn5
// Each key transmits that button's code REPEATS times.

#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include "pins.h"

struct Btn { const char* name; unsigned long code; int bits; };

static const Btn LIGHT = {"LIGHT",  87505, 18};
static const Btn POWER = {"POWER",  87507, 18};
static const Btn PLUS  = {"PLUS",    1365, 12};
static const Btn MINUS = {"MINUS",   1366, 12};
static const Btn BTN5  = {"BTN5",    1364, 12};

static const int REPEATS = 20;   // how many times to resend per press

RCSwitch tx = RCSwitch();

void sendRaw(unsigned long code, int bits) {
  ELECHOUSE_cc1101.SetTx();
  delayMicroseconds(500);          // let the PLL/oscillator settle before keying
  for (int i = 0; i < REPEATS; i++) tx.send(code, bits);
  ELECHOUSE_cc1101.SetRx();
  Serial.printf("sent %lu (%d bits) x%d\n", code, bits, REPEATS);
}

void press(const Btn& b) {
  Serial.printf("[%s] ", b.name);
  sendRaw(b.code, b.bits);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Novy RF transmitter ===");

  ELECHOUSE_cc1101.setSpiPin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  ELECHOUSE_cc1101.setGDO0(PIN_GDO0);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setCCMode(0);       // async serial: RCSwitch drives raw OOK on GDO0
  ELECHOUSE_cc1101.setModulation(2);   // ASK/OOK
  ELECHOUSE_cc1101.setMHZ(NOVY_FREQ_MHZ);
  ELECHOUSE_cc1101.setPA(12);          // TX power (dBm) — max for 433 MHz

  tx.enableTransmit(PIN_GDO0);
  tx.setProtocol(NOVY_PROTOCOL);       // 11
  tx.setPulseLength(NOVY_PULSE_LEN);   // 444 us

  Serial.println(ELECHOUSE_cc1101.getCC1101()
                 ? "CC1101 ready. Keys: l=light p=power +=plus -=minus n=btn5"
                 : "!! CC1101 NOT detected. Check wiring.");
}

// Accumulates digits and transmits the typed value as an 18-bit code on newline.
// e.g. send "87504\n" over serial to test an arbitrary candidate code.
void loop() {
  static unsigned long acc = 0;
  static bool haveDigits = false;

  if (!Serial.available()) return;
  char c = Serial.read();

  if (c >= '0' && c <= '9') { acc = acc * 10 + (c - '0'); haveDigits = true; return; }

  if (c == '\n' || c == '\r' || c == ' ') {
    if (haveDigits) { sendRaw(acc, 18); acc = 0; haveDigits = false; }
    return;
  }

  switch (c) {
    case 'l': press(LIGHT); break;
    case 'p': press(POWER); break;
    case '+': press(PLUS);  break;
    case '-': press(MINUS); break;
    case 'n': press(BTN5);  break;
    default: break;
  }
}
