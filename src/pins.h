#pragma once
// CC1101 <-> ESP32-C3 wiring. Change here if you wired differently.
// SPI (the CC1101 is configured over these 4 lines):
#define PIN_SCK   4   // CC1101 SCK
#define PIN_MISO  5   // CC1101 MISO (SO)
#define PIN_MOSI  6   // CC1101 MOSI (SI)
#define PIN_CSN   7   // CC1101 CSN (chip select)
// Async OOK data line (RCSwitch bit-bangs / reads this; same pin for TX and RX):
#define PIN_GDO0  10  // CC1101 GDO0

// Novy RF protocol (community-decoded):
#define NOVY_PROTOCOL     11    // as decoded by rc-switch on this remote
#define NOVY_PULSE_LEN    444   // microseconds (measured)
#define NOVY_FREQ_MHZ     433.92
