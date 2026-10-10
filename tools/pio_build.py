# PlatformIO extra script (library.json "extraScript"): compiles the MicroCS
# board port that matches the project's framework, and uses the project's
# config header include/mcs_user_config.h (template: config/mcs_user_config.h)
# when it exists - the application sees the same file through the project's
# include/ folder. build_flags = -D... options still work and win over it.
from os.path import isfile, join, realpath

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
project_include = env.subst("$PROJECT_INCLUDE_DIR")  # noqa: F821
if project_include and isfile(join(project_include, "mcs_user_config.h")):
    env.Append(CPPPATH=[project_include])  # noqa: F821  (found by mcs_config.h's __has_include)
env.Replace(SRC_FILTER=src)  # noqa: F821
