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
#ifdef __cplusplus
}
#endif
#endif
#endif
