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

    def test_platformio_hood_env_has_version_script(self):
        ini = read("platformio.ini")
        self.assertIn("[env:hood]", ini)
        self.assertIn("extra_scripts = pre:tools/version.py", ini)
        self.assertIn("board_build.partitions = min_spiffs.csv", ini)

    def test_ci_runs_tests_and_builds_firmware(self):
        workflow = read(".github/workflows/build-firmware.yml")
        self.assertIn("python tools/test_project.py", workflow)
        self.assertIn("pio run -e hood", workflow)
        self.assertIn("novy-hood.factory.bin", workflow)
        self.assertIn("novy-hood.ota.bin", workflow)


if __name__ == "__main__":
    unittest.main(verbosity=2)
