# PlatformIO extra script (library.json "extraScript"): compiles the MicroCS
# board port that matches the project's framework.
from os.path import realpath

Import("env")  # noqa: F821  (provided by PlatformIO)

PORTS = {               # framework -> (source filter, port folder)
    "arduino": ("+<ports/arduino/*.cpp>", "arduino"),
    "espidf": ("+<ports/esp32/mcs_port_esp32.c>", "esp32"),
    "stm32cube": ("+<ports/stm32/mcs_port_stm32.c>", "stm32"),
    "zephyr": ("+<ports/zephyr/mcs_port_zephyr.c>", "zephyr"),
}

src = ["+<src/*.c>", "+<modules/*/*.c>"]
for fw in env.get("PIOFRAMEWORK", []):  # noqa: F821
    if fw in PORTS:
        flt, folder = PORTS[fw]
        src.append(flt)
        env.Append(CPPPATH=[realpath("ports/" + folder)])  # noqa: F821
        break
env.Append(CPPPATH=[realpath("include")])  # noqa: F821
env.Replace(SRC_FILTER=src)  # noqa: F821
