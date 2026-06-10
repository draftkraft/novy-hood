#ifndef CONFIG_H
#define CONFIG_H

// Firmware version — normally injected by tools/version.py from VERSION.
// This fallback is used for plain/IDE builds with no git context.
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// GitHub repo ("owner/repo") the one-click web Update pulls firmware from.
// EMPTY disables the self-update feature (no network calls, button hidden).
// Not a secret — it's a public repo slug; set your own before flashing.
const char* FW_UPDATE_REPO = "draftkraft/novy-hood";

// ============== WiFi DEFAULTS (optional fallback) ==============
// Leave EMPTY for normal use: with no baked-in credentials the device boots
// straight into its setup hotspot (AP_SSID) so you provision WiFi from a phone.
// Only set these if you want a network hard-coded into the firmware image.
const char* DEFAULT_SSID     = "";
const char* DEFAULT_PASSWORD = "";
// SSID of the temporary setup hotspot raised when not connected (open network).
const char* AP_SSID          = "novy-hood-setup";
// ===============================================================

const String HOSTNAME    = "novy-hood";
// Password for over-the-air (ArduinoOTA) firmware updates. Set your own before flashing.
const String OTAPASSWORD = "";

// MQTT is OPTIONAL. Leave disabled for plain web control.
const bool  MQTT_ENABLED = false;
const String MQTT_PREFIX = "novy-hood";
const char* MQTT_SERVER  = "0.0.0.0";
const int   MQTT_PORT    = 1883;
const char* MQTT_USER    = "";
const char* MQTT_PASSWORD = "";
const char* MQTT_CLIENT_ID = "novy-hood";

// RF data pin = the CC1101 GDO0 line (we wired it to GPIO10).
const int TRANSMIT_433MHZ_PIN = 10;

// --- Novy protocol (from renedis ESP32_Novy_Controller; matches our captures) ---
// Frame = NOVY_DEVICE_CODE[channel] + NOVY_PREFIX + command. Channel 0 = pairing code 1.
static const String NOVY_DEVICE_CODE[] = {
    "0101", "1001", "0001", "1110", "0110",
    "1010", "0010", "1100", "0100", "1000",
};
static const String NOVY_PREFIX = "0101";

static const String NOVY_COMMAND_LIGHT = "0111010001";  // -> 87505 (18b), verified
static const String NOVY_COMMAND_POWER = "0111010011";  // -> 87507 (18b), verified WORKS
static const String NOVY_COMMAND_PLUS  = "0101";        // -> 1365  (12b)
static const String NOVY_COMMAND_MINUS = "0110";        // -> 1366  (12b)
static const String NOVY_COMMAND_NOVY  = "0100";        // -> 1364  (12b)

#endif  // CONFIG_H
