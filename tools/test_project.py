#!/usr/bin/env python3
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


class ProjectConfigurationTests(unittest.TestCase):
    def test_version_is_semver(self):
        version = read("VERSION").strip()
        self.assertRegex(version, r"^\d+\.\d+\.\d+$")

    def test_firmware_uses_novy_hood_hostname(self):
        config = read("src/config.h")
        self.assertIn('const String HOSTNAME    = "novy-hood";', config)
        self.assertIn('const char* AP_SSID          = "novy-hood-setup";', config)
        hood = read("src/hoodserver.cpp")
        self.assertIn("WiFi.setHostname(HOSTNAME.c_str())", hood)
        self.assertIn("MDNS.begin(HOSTNAME.c_str())", hood)
        self.assertIn("ArduinoOTA.setHostname(HOSTNAME.c_str())", hood)

    def test_update_repo_is_configured(self):
        config = read("src/config.h")
        self.assertIn('const char* FW_UPDATE_REPO = "draftkraft/novy-hood";', config)
        self.assertNotIn('<OWNER>/<REPO>', read("README.md"))

    def test_header_shows_version_not_ip(self):
        hood = read("src/hoodserver.cpp")
        self.assertIn("v%FW_VERSION%", hood)
        self.assertIn('html.replace("%FW_VERSION%", FW_VER);', hood)
        self.assertIn("updateLine", hood)
        self.assertIn(".pill.update", hood)
        self.assertIn(".pill.error", hood)
        self.assertNotIn("id=ver", hood)
        self.assertNotIn("location.host", hood)

    def test_ota_update_shows_progress_and_reloads(self):
        hood = read("src/hoodserver.cpp")
        self.assertIn("Updating…", hood)
        self.assertIn("Flashing… reconnecting", hood)
        self.assertIn("location.reload()", hood)
        self.assertIn("@keyframes pulse", hood)

    def test_activity_log_has_startup_update_and_radio_diagnostics(self):
        hood = read("src/hoodserver.cpp")
        self.assertIn('logMsg(String("BOOT: firmware v") + FW_VER);', hood)
        self.assertIn('logMsg("WiFi: connecting to " + staSSID);', hood)
        self.assertIn('logMsg("MQTT: enabled " + mqttHost + ":" + String(mqttPort));', hood)
        self.assertIn('logMsg("MQTT: disabled");', hood)
        self.assertIn('logMsg("CC1101: connected");', hood)
        self.assertIn("refreshRadioStatus()", hood)
        self.assertIn('server.on("/log"', hood)
        self.assertIn('server.on("/health"', hood)
        self.assertIn("CC1101: reconnected", hood)
        self.assertIn("CC1101: reconnecting", hood)
        self.assertIn("MQTT: connecting to ", hood)
        self.assertIn("MQTT: connected ", hood)
        self.assertIn("MQTT: connect failed (state=", hood)
        self.assertIn("MQTT: disconnected", hood)
        self.assertIn("blocked: CC1101 missing", hood)
        self.assertIn("ERROR: CC1101 not detected", hood)
        self.assertIn('logMsg(String("UPDATE: v") + g_latestVersion + " available");', hood)

    def test_novy_button_label_is_short(self):
        hood = read("src/hoodserver.cpp")
        self.assertIn('>Novy</button>', hood)
        self.assertIn('route("/toggleNovy",  CMD_NOVY,  "Novy");', hood)
        self.assertNotIn("Novy (auto)", hood)

    def test_platformio_hood_env_has_version_script(self):
        ini = read("platformio.ini")
        self.assertIn("[env:hood]", ini)
        self.assertIn("extra_scripts = pre:tools/version.py", ini)
        self.assertIn("board_build.partitions = min_spiffs.csv", ini)

    def test_ci_runs_tests_and_builds_firmware(self):
        workflow = read(".github/workflows/build-firmware.yml")
        self.assertIn("python tools/test_project.py", workflow)
        self.assertIn("pio run -e hood", workflow)
        self.assertIn("tag_name: v${{ env.FW_VERSION }}", workflow)
        self.assertIn("RELEASE_EXISTS", workflow)
        self.assertIn("novy-hood.factory.bin", workflow)
        self.assertIn("novy-hood.ota.bin", workflow)


if __name__ == "__main__":
    unittest.main(verbosity=2)
