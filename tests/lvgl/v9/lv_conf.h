/* LVGL 9.x configuration of the MicroCS host tests (make lvgl-test). Everything not
 * set here keeps LVGL's default from lv_conf_internal.h. */
#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 32
#define LV_MEM_SIZE (512U * 1024U)
#define LV_DEF_REFR_PERIOD 33
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_OBJ 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_USE_LODEPNG 1
#define LV_USE_BMP 1
#define LV_USE_SNAPSHOT 1
#define LV_USE_OBSERVER 1
#define LV_BIN_DECODER_RAM_LOAD 1
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 1
#endif
