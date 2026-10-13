#!/usr/bin/env python3
"""ESP BLE menu gating / values, with minimal IDF dependency symbols.

Requires kconfiglib (pip install kconfiglib). Real chip configurations are built in CI.
"""
import subprocess
from pathlib import Path
import tempfile
import kconfiglib as kc

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = ["SOC_BLE_SUPPORTED", "SOC_WIFI_SUPPORTED", "IDF_TARGET_ESP32C2",
                "BT_ENABLED", "BT_NIMBLE_ENABLED", "BT_CONTROLLER_ENABLED", "BT_NIMBLE_EXT_ADV"]
ROLES = ["CENTRAL", "PERIPHERAL", "BROADCASTER", "OBSERVER"]

def main():
    with tempfile.TemporaryDirectory() as tmp:
        stub = Path(tmp) / "Kconfig"
        stub.write_text("\n".join(f'config {s}\n    bool "{s}"\n' for s in DEPENDENCIES) +
                        "\n".join(f'config BT_NIMBLE_ROLE_{r}\n    bool "{r}"\n    depends on BT_NIMBLE_ENABLED\n' for r in ROLES) +
                        f'\nsource "{ROOT / "Kconfig"}"\n')
        k = kc.Kconfig(str(stub), warn_to_stderr=True)
        def setv(s, v): k.syms[s].set_value(v)
        def value(s): return k.syms[s].str_value
        ble = k.syms["MICROCS_BLE"]
        menu = ble.nodes[0].parent
        assert menu.prompt[0] == "Bluetooth LE"
        assert kc.expr_value(menu.prompt[1]) == 0  # no onboard BLE radio
        setv("SOC_BLE_SUPPORTED", "y")
        assert kc.expr_value(menu.prompt[1]) == 2  # discoverable before NimBLE
        assert ble.visibility == 0
        for s in ["BT_ENABLED", "BT_NIMBLE_ENABLED", "BT_CONTROLLER_ENABLED"]: setv(s, "y")
        assert ble.visibility == 2 and value("MICROCS_BLE") == "n"  # opt-in
        setv("MICROCS_BLE", "y")
        assert value("MICROCS_BLE") == "y"
        assert value("MICROCS_BLE_NAME") == "MicroCS"
        assert value("MICROCS_BLE_QUEUE") == "2048"
        assert value("MICROCS_BLE_RESERVE_KB") == "48"
        assert value("MICROCS_BLE_SCAN_INTERVAL") == "100" and value("MICROCS_BLE_SCAN_WINDOW") == "50"
        setv("MICROCS_BLE_SCAN_INTERVAL", "200"); setv("MICROCS_BLE_SCAN_WINDOW", "75")
        assert value("MICROCS_BLE_SCAN_WINDOW") == "75"
        setv("MICROCS_BLE_SCAN_WINDOW", "201")
        assert value("MICROCS_BLE_SCAN_WINDOW") == "50"  # invalid range restores default
        for r in ROLES: assert value("BT_NIMBLE_ROLE_" + r) == "y"
        setv("MICROCS_BLE_QUEUE", "4096")
        setv("MICROCS_BLE_NAME", "S3 Test")
        assert value("MICROCS_BLE_QUEUE") == "4096" and value("MICROCS_BLE_NAME") == "S3 Test"
        setv("BT_NIMBLE_EXT_ADV", "y")
        assert ble.visibility == 0 and value("MICROCS_BLE") == "n"
        setv("BT_NIMBLE_EXT_ADV", "n")
        setv("BT_CONTROLLER_ENABLED", "n")
        assert ble.visibility == 0
        setv("BT_CONTROLLER_ENABLED", "y")
        setv("SOC_BLE_SUPPORTED", "n")  # S2 / P4 cannot enable the backend
        assert value("MICROCS_BLE") == "n"
        for flags in [[], ["-DCONFIG_IDF_TARGET_ESP32=1"], ["-DCONFIG_IDF_TARGET_ESP32C6=1"], ["-DCONFIG_IDF_TARGET_ESP32C61=1"]]:
            exe = str(Path(tmp) / "power")
            subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", *flags,
                            "-I" + str(ROOT / "ports/esp32"),
                            str(ROOT / "tests/c/test_esp_ble_power.c"), "-o", exe], check=True)
            subprocess.run([exe], check=True)
        k.write_config(str(Path(tmp) / "sdkconfig"))
        assert "CONFIG_MICROCS_BLE=y" not in (Path(tmp) / "sdkconfig").read_text()
    print("PASS ESP BLE menuconfig: discoverability, prerequisites, limits, roles, opt-in")

if __name__ == "__main__": main()
