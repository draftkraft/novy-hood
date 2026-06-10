# Novy Pureline RF bridge — project notes

Control a **Novy Pureline cooker hood** over **433.92 MHz** from an **ESP32-C3 + CC1101**,
with a WiFi web UI, runtime WiFi provisioning, and optional MQTT / Home Assistant.
No secrets live in this repo — WiFi is configured at runtime, not compiled in.

================================================================================
## HARDWARE
================================================================================
- MCU: **ESP32-C3** ("SuperMini" class board, 4 MB flash), native USB-Serial/JTAG.
  - macOS serial port enumerates as `/dev/cu.usbmodem*` (re-glob after each reset).
- Radio: **CC1101** 433 MHz transceiver — **3.3 V ONLY** (5 V destroys it). Coil/helical antenna.

### Wiring (CC1101 → ESP32-C3), all 3.3 V  [see src/pins.h]
| CC1101 | ESP32-C3 |   | CC1101 | ESP32-C3 |
|--------|----------|---|--------|----------|
| VCC    | 3V3      |   | CSN    | GPIO7    |
| GND    | GND      |   | GDO0   | GPIO10   |
| SCK    | GPIO4    |   | GDO2   | (unused) |
| MISO   | GPIO5    |   | ANT    | coil     |
| MOSI   | GPIO6    |   |        |          |
GPIO9 = BOOT button (free; used as a "force setup portal" trigger at reset).

================================================================================
## RF PROTOCOL  (decoded; matches renedis/ESP32_Novy_Controller)
================================================================================
- 433.92 MHz **OOK/ASK**. CC1101 async mode: `setCCMode(0)` + `setModulation(2)`.
  For clean RX sniffing also `setRxBW(58)` (narrow BW suppresses idle noise).
- Frame = `NOVY_DEVICE_CODE[channel]` + `NOVY_PREFIX` + `COMMAND` (sent as a bit-STRING).
- TX via rc-switch **protocol 12, pulse 350**, repeat counts: light=2, others=3, 50 ms gaps.
- Command bit codes + channel/pairing codes live in `src/config.h` (generic Novy protocol values).
- POWER toggles the fan (1 press = on, 2 = off). Speed +/- only visibly change while the fan is ON.
  (Light output is not present on every hood model.)

================================================================================
## FIRMWARE FEATURES
================================================================================
- **Runtime WiFi provisioning** — boot loads saved credentials from NVS (`Preferences`,
  namespace `wifi`); empty/failed → raises an **open SoftAP `novy-hood-setup`** + a DNSServer
  captive portal. iOS/Android auto-open the setup page (scan-and-pick SSID + password), which
  saves to flash and reboots. Re-provision later from the web UI (`/wifi`), `/forget`, or by
  holding **BOOT (GPIO9)** at reset. No recompile to change networks.
- **Web UI** — responsive (mobile + desktop), light/dark, served fully inline (no CDN/fonts,
  works offline / during the portal). Controls: Power, Slower, Faster, Novy (auto), Light.
  Activity log updates in realtime (command routes return the fresh log in their response).
- **MQTT (optional, default OFF)** — PubSubClient. Mirrors renedis: LWT availability
  `<prefix>/<hostname>/status` (online/offline, retained); command topics
  `<prefix>/<hostname>/button/<id>/set`; **Home Assistant auto-discovery** (retained config at
  `homeassistant/button/<hostname>/<id>/config`) so the 5 buttons appear as HA entities.
  Settings (enable/host/port/user/pass/prefix) are runtime-configurable at `/mqtt`, persisted in
  NVS (namespace `mqtt`); `config.h` values are defaults. Reconnects every 5 s while on WiFi.
- **OTA (push)** — ArduinoOTA enabled (set your own OTA password in `config.h` before flashing).
  Once flashed over USB, future updates can go over WiFi from PlatformIO (`--upload-port <ip>`).
- **Self-update (pull-OTA)** — the web UI shows the running version (`FW_VERSION` = VERSION,
  injected by `tools/version.py`) and checks GitHub's rolling `latest` release `version.txt`; when
  it differs, a one-click **Update** downloads `novy-hood.ota.bin` over HTTPS (`WiFiClientSecure`
  `setInsecure()`, follow-redirects) via `HTTPUpdate` and reboots. Repo slug = `FW_UPDATE_REPO`
  in `config.h` (empty = disabled). Endpoints: `GET /update/check` (JSON), `POST /update` (deferred
  to loop()). The update runs blocking in loop() (web/MQTT/ArduinoOTA pause meanwhile).

================================================================================
## STATUS / OUTSTANDING
================================================================================
- All firmware features above are implemented and compile (env hood; ~64% of the 1.875 MB app
  slot). Verified on the bench board: WiFi provisioning (incl. fail→portal + recovery), web UI
  (5 buttons, realtime log), MQTT settings page + endpoints, version reporting + `/update/check`
  (+ `POST /update` guarded 403 while disabled). Build-time `FW_VERSION` injection works (`dev`
  locally without git).
- Repo meta present: `README.md` (end-user, ~3-line flash + setup), `.github/workflows/build-firmware.yml`
  (CI builds env hood on push to main, publishes `novy-hood.factory.bin` + `novy-hood.ota.bin` +
  `version.txt` to the rolling `latest` release), `.gitignore`.
- **Not yet done / needs the user:**
  1. Push the initialized git repo to GitHub so CI can publish the first `latest` release.
  2. **First install of this build = USB factory flash** (partition layout changed to min_spiffs).
  3. **CC1101 not detected** on the bench board (`!! CC1101 NOT detected`) — RF won't transmit until the
     wiring is reseated (GPIO4/5/6/7/10, 3.3 V). Not a code issue (RF code unchanged).
  4. Untested end-to-end (need user infra): live MQTT broker / Home Assistant; the actual pull-OTA
     download (needs a published repo + `FW_UPDATE_REPO` set).
- Bench board currently runs the latest min_spiffs firmware, connected to WiFi via creds saved in NVS.

================================================================================
## WiFi RELIABILITY NOTE (hard-won)
================================================================================
- Use the **pioarduino platform (arduino-esp32 3.x)** pinned in `platformio.ini`. The older
  official `espressif32` / arduino-esp32 2.0.x was suspected of a C3 association bug, but the
  real lesson: **persistent assoc failures (deauth reason=2) on the minimal `wifitest` sketch
  mean a BAD BOARD — swap hardware before changing code.** Don't lower TX power or BSSID-lock
  (caused reason=15 handshake timeouts). Plain `WiFi.begin()` at default power + 802.11b-only PHY
  (`esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B)`) is the proven-good config — keep it.
- reason codes seen: 2 = AUTH_EXPIRE (here = flaky board, not core), 15 = handshake timeout
  (too-low TX power / wrong password), 3/WL_CONNECTED = success.

================================================================================
## PROJECT LAYOUT + COMMANDS
================================================================================
- `platformio.ini` — platform = pioarduino (arduino-esp32 3.x). Envs: sniffer / novy / rawsniff /
  **hood** (the controller) / wifitest. lib_deps: rc-switch, SmartRC-CC1101-Driver-Lib, PubSubClient.
  hood uses `board_build.partitions = min_spiffs.csv` (1.875 MB app slots — needed so TLS+HTTPUpdate
  fit) and `extra_scripts = pre:tools/version.py` (bakes FW_VERSION). **Partition change ⇒ the first
  install must be a USB factory flash; OTA can't rewrite the partition table.**
- `tools/version.py` — pre-build hook: sets `-DFW_VERSION` from `git rev-parse --short HEAD` (else "dev").
- `src/config.h`     — defaults (WiFi creds intentionally EMPTY), HOSTNAME, OTA pw, MQTT defaults,
                       Novy RF codes. Included by hoodserver/wifitest.
- `src/pins.h`       — CC1101 pin map + protocol constants.
- `src/hoodserver.cpp` [env hood] — THE controller: WiFi provisioning + web UI + RF TX + MQTT.
- `src/wifitest.cpp`   [wifitest] — bare `WiFi.begin` isolation test (diagnose board vs firmware).
- `src/sniffer.cpp` / `transmit.cpp` / `rawsniff.cpp` — RF capture / replay dev tools.
- `.venv/` — local PlatformIO + esptool (not relocatable).

```bash
cd ~/novy
.venv/bin/pio run -e hood              # build the controller
.venv/bin/pio run -e hood -t upload    # build + flash over USB
.venv/bin/esptool --port <port> erase-flash   # wipe NVS (clears saved WiFi/MQTT) if stranded
# Read serial (native USB; boot prints once, then a 3 s heartbeat):
PORT=$(ls /dev/cu.usbmodem* | head -1)
.venv/bin/python -c "import serial,time,sys;p=serial.Serial('$PORT',115200,timeout=.3);p.setDTR(True);e=time.time()+15
while time.time()<e:
 d=p.read(256)
 if d: sys.stdout.write(d.decode('utf-8','replace'));sys.stdout.flush()"
```

================================================================================
## GOTCHAS
================================================================================
- CC1101: 3.3 V only; `setCCMode(0)` (async) required. If serial prints `!! CC1101 NOT detected`,
  it's a wiring/contact issue — reseat SCK/MISO/MOSI/CSN/GDO0 (GPIO4/5/6/7/10) and check 3V3.
  Web/MQTT commands still log but won't transmit until the radio is detected.
- ESP32-C3 native USB: boot output prints ONCE and the port re-enumerates on reset — every
  firmware here prints a periodic heartbeat in loop() for race-free serial capture.
- macOS has no `timeout` cmd; use the pyserial snippet above. Use `~/novy/.venv/bin/esptool`.
- `Preferences`/NVS survives a plain reflash — to truly reset saved WiFi/MQTT, `erase-flash`.
- Do NOT point the device at a public MQTT broker; use the user's own broker / Home Assistant.
- No secrets in the repo: WiFi via the portal, OTA/MQTT passwords set locally in `config.h`
  (kept out of commits). Never commit real credentials, MAC addresses, or LAN IPs.
