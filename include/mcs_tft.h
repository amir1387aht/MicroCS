/* Optional Arduino TFT_eSPI integration, OFF by default. docs/TFT_ESPI.md. */
#ifndef MCS_TFT_H
#define MCS_TFT_H
#include "mcs.h"
#if MCS_ENABLE_TFT_ESPI
#if !defined(ARDUINO)
#error                                                                                             \
    "MCS_ENABLE_TFT_ESPI requires the Arduino port and an explicit TFT_eSPI dependency/setup; no host or vendor fallback is implemented"
#endif
#ifdef __cplusplus
extern "C" {
#endif
void mcs_tft_open_lib(mcs_vm_t *vm);
/* Foreground file scope; objects created before begin are left alive.
 * End LVGL scope first. end returns -1 without disposal if a GUI still uses TFT.
 * Direct embedding/REPL may deliberately retain objects until Dispose/VM free. */
uint64_t mcs_tft_scope_begin(mcs_vm_t *vm);
int mcs_tft_scope_end(mcs_vm_t *vm, uint64_t checkpoint);
#ifdef __cplusplus
}
#endif
#endif
#endif
