/* Arduino dependency discovery only. Never initialize or build a second LVGL. */
#include "mcs_config.h"
#if MCS_ENABLE_LVGL
#include "mcs_lvgl.h"
#endif
