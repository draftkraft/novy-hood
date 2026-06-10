# Novy Hood Controller

[![Build and test firmware](https://github.com/draftkraft/novy-hood/actions/workflows/build-firmware.yml/badge.svg)](https://github.com/draftkraft/novy-hood/actions/workflows/build-firmware.yml)
[![Latest firmware](https://img.shields.io/github/v/release/draftkraft/novy-hood?label=firmware)](https://github.com/draftkraft/novy-hood/releases/latest)

Control a **Novy Pureline** (or compatible **433.92 MHz**) cooker hood over WiFi from your phone
or computer — and optionally via **MQTT** — using a tiny **ESP32-C3 + CC1101** board.

- 📱 **Clean web UI** (mobile + desktop, light/dark): Power, fan speed, Novy auto, Light
- 📶 **No-recompile WiFi setup** — the device makes its own hotspot; you pick your network from a phone
- 🏠 **MQTT + Home Assistant auto-discovery** (optional) — buttons appear automatically as entities
- 🔁 **OTA updates** over WiFi after the first flash
- 🔒 **No credentials baked in** — you configure WiFi at setup time

<img width="594" height="527" alt="image" src="https://github.com/user-attachments/assets/3141e1d0-d6d7-4c3d-8203-c4e5b92369fa" />
<img width="592" height="443" alt="image" src="https://github.com/user-attachments/assets/98f991fd-0662-4534-856e-2c78cc879be5" />
<img width="583" height="631" alt="image" src="https://github.com/user-attachments/assets/8cdae0a5-f74e-488e-aa31-a01cfbd83999" />

## What you need

- An ESP32-C3 board (e.g. "SuperMini") + a CC1101 433 MHz module wired per [the table below](#wiring)
- A USB-C cable (a real **data** cable, not charge-only)

## Flash it (≈3 lines)

Download the latest firmware from **[Releases](../../releases/latest)**, then:

```bash
pip install esptool
curl -LO https://github.com/draftkraft/novy-hood/releases/latest/download/novy-hood.factory.bin
esptool --chip esp32c3 write-flash 0x0 novy-hood.factory.bin
```

> No Python? Open **[ESP Web Tools](https://esphome.github.io/esp-web-tools/)** in Chrome/Edge and
> flash `novy-hood.factory.bin` with one click — no install needed.

## First-time setup

1. Power the board. It creates an open WiFi hotspot named **`novy-hood-setup`**.
2. Join it from your phone — a setup page opens automatically (if not, visit `http://192.168.4.1`).
3. Pick your WiFi network, enter the password, **Save**. The device reboots and joins your network.
4. Open **`http://novy-hood.local`** (or the IP shown) to control the hood. The device sets its
   WiFi hostname, mDNS name, and OTA hostname to **`novy-hood`**.

To change networks later: open the web UI → **WiFi**, or hold the **BOOT** button while powering on
to force the setup hotspot again.

## Updating

The web UI shows the running firmware version and checks GitHub for newer builds. When an update
is available, click **Update** — the device downloads the latest release and reboots into it, no
cable needed.

> The **first** install must be the USB factory flash above — and after any change that alters the
> partition layout, you must USB-flash `novy-hood.factory.bin` once more. Routine updates after
> that are one click.

## Home Assistant (optional)

Web UI → **MQTT** → enter your broker, enable, save. The five buttons appear in Home Assistant
automatically via MQTT discovery. (Leave MQTT off if you only use the web UI.)

The device also **listens** for the physical remote: when its **Light** button is pressed nearby,
the firmware logs it in the activity log and publishes `pressed` to `<prefix>/remote/light`
(exposed in Home Assistant as a *Remote Light* event entity). Use it to keep other automations
in sync with manual remote use.

## Wiring

All connections are **3.3 V** (the CC1101 is **not** 5 V tolerant). Use the labels printed on
your boards; the table below matches the common 8-pin CC1101 module and the ESP32-C3 SuperMini
style pin labels:

| CC1101 pin | CC1101 label | ESP32-C3 board label |
|------------|--------------|----------------------|
| 1          | GND          | GND / GD             |
| 2          | VCC          | 3.3V                 |
| 3          | GDO0         | 10                   |
| 4          | CSN / CS     | 7                    |
| 5          | SCK          | 4                    |
| 6          | MOSI / SI    | 6                    |
| 7          | MISO / SO    | 5                    |
| 8          | GDO2         | not connected        |

Do not connect the CC1101 to `5V`. Connect the antenna pad to a 433 MHz antenna.

## Build from source

```bash
python3 -m venv .venv
.venv/bin/pip install -U platformio esptool
PLATFORMIO_CORE_DIR=.platformio .venv/bin/pio run -e hood            # build
PLATFORMIO_CORE_DIR=.platformio .venv/bin/pio run -e hood -t upload  # build + flash over USB
```

## Versioning

The firmware version lives in [`VERSION`](VERSION) and is baked into the web UI/update checks at
build time. Local interactive builds ask to bump the patch version when firmware files changed but
`VERSION` did not.

```bash
python tools/bump_version.py patch
python tools/test_project.py
```

RF protocol and credits: based on [renedis/ESP32_Novy_Controller](https://github.com/renedis/ESP32_Novy_Controller).
