#pragma once
// CC1101 <-> ESP32-C3 wiring. Change here if you wired differently.
// SPI (the CC1101 is configured over these 4 lines):
#define PIN_SCK   4   // CC1101 SCK
#define PIN_MISO  5   // CC1101 MISO (SO)
#define PIN_MOSI  6   // CC1101 MOSI (SI)
#define PIN_CSN   7   // CC1101 CSN (chip select)
// Async OOK data line (RCSwitch bit-bangs / reads this; same pin for TX and RX):
#define PIN_GDO0  10  // CC1101 GDO0

// Novy RF protocol (community-decoded; matches FHEM SD_Protocols ID 86):
// data bit unit ~365 us, sync/start gap = 44 units (~16 ms), inverted PWM.
// NOTE: rc-switch RX reports this remote as "proto 11, pulse ~446us" — an artifact:
// it divides the 16 ms sync gap by its assumed 36 sync units (446 = 365 * 44/36).
// Do NOT transmit with 444+us data bits (hood rejects them); use these instead:
#define NOVY_PROTOCOL     11    // as decoded by rc-switch on this remote (RX matching only)
#define NOVY_PULSE_LEN    365   // microseconds, true data-bit unit
#define NOVY_SYNC_LOW     44    // sync gap length in units (rc-switch protos 11/12 use 36)
#define NOVY_FREQ_MHZ     433.92
