/* MicroCS - Arduino side of the optional u8g2 support (docs/U8G2.md).
 *
 * With MCS_ENABLE_U8G2 1 (src/mcs_user_config.h of the installed library, or
 * `python3 tools/make_arduino.py --u8g2`) this include makes the Arduino IDE / arduino-cli /
 * PlatformIO put olikraus' U8g2 library on the include path and compile it; the driver
 * (mcs_drv_u8g2.c) then finds <clib/u8g2.h>. Install it with Tools > Manage Libraries > "U8g2"
 * (PlatformIO: lib_deps = olikraus/U8g2). With MCS_ENABLE_U8G2 0 the library is not needed. */
#include "mcs_config.h"
#if MCS_ENABLE_U8G2
#include <U8x8lib.h>
#endif
