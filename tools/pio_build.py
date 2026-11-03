# PlatformIO extra script (library.json "extraScript"): compiles the MicroCS
# board port that matches the project's framework, and uses the project's
# config header include/mcs_user_config.h (template: config/mcs_user_config.h)
# when it exists - the application sees the same file through the project's
# include/ folder. build_flags = -D... options still work and win over it.
import subprocess
import sys
from os.path import isdir, isfile, join, realpath

Import("env")  # noqa: F821  (provided by PlatformIO)

PORTS = {               # framework -> (source filter, port folder)
    "arduino": ("+<ports/arduino/*.cpp>", "arduino"),
    "espidf": ("+<ports/esp32/mcs_port_esp32.c> +<ports/esp32/mcs_port_esp32_net.c>", "esp32"),
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


# LVGL (docs/LVGL.md): lib_deps = lvgl/lvgl and build_flags = -DMCS_ENABLE_LVGL=1 turn the binding on. The API tables
# are generated for the LVGL found (python3 + pycparser) into the build folder, unless the platformio.ini has
#     custom_microcs_lvgl_api_inc = path/to/mcs_lvgl_api.inc   (the all-versions file of the releases: no Python)
#     custom_microcs_lvgl_dir     = path/to/lvgl               (default: the lvgl library of this environment, lib/lvgl)
def _lvgl_on():
    for d in env.get("CPPDEFINES", []):  # noqa: F821
        n, v = (d[0], d[1] if len(d) > 1 else 1) if isinstance(d, (list, tuple)) else (d, 1)
        if str(n) == "MCS_ENABLE_LVGL" and str(v) not in ("0", ""):
            return True
    return False


if _lvgl_on():
    api = env.GetProjectOption("custom_microcs_lvgl_api_inc", "")  # noqa: F821
    if not api:
        lv = env.GetProjectOption("custom_microcs_lvgl_dir", "")  # noqa: F821
        if not lv:
            for cand in (join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "lvgl"),  # noqa: F821
                         join(env.subst("$PROJECT_DIR"), "lib", "lvgl")):  # noqa: F821
                if isfile(join(cand, "lvgl.h")):
                    lv = cand
                    break
        if not lv or not isfile(join(lv, "lvgl.h")):
            raise SystemExit("MicroCS: MCS_ENABLE_LVGL=1 needs the lvgl library (lib_deps = lvgl/lvgl) or custom_microcs_lvgl_dir / "
                             "custom_microcs_lvgl_api_inc in platformio.ini (docs/LVGL.md)")
        try:
            import pycparser  # noqa: F401
        except ImportError:
            subprocess.check_call([sys.executable, "-m", "pip", "install", "pycparser"])
        out = join(env.subst("$BUILD_DIR"), "microcs_lvgl")  # noqa: F821
        api = join(out, "mcs_lvgl_api.inc")
        subprocess.check_call([sys.executable, realpath(join("tools", "lvgl_gen", "build_api.py")), "--lvgl", lv,
                               "--work", join(out, "work"), "--out", api])
        env.Append(CPPPATH=[lv])  # noqa: F821
    env.Append(CPPDEFINES=[("MCS_LVGL_API_INC", env.StringifyMacro(realpath(api)))])  # noqa: F821
