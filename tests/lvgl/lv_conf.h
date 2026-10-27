#ifndef LV_CONF_H
#define LV_CONF_H
#if LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 5
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#else
#define LV_COLOR_DEPTH 16
#endif
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (256U * 1024U)
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC realloc
#define LV_USE_OS LV_OS_NONE
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_USE_LOG 0
#ifdef MCS_LVGL_TEST_NO_COMPRESSED
#define LV_USE_FONT_COMPRESSED 0
#else
#define LV_USE_FONT_COMPRESSED 1 /* upstream binary font fixture is compressed */
#endif
#if !defined(__ASSEMBLER__)
#ifdef __cplusplus
extern "C"
#endif
void mcs_test_lvgl_assert(const char*, int);
#endif
#define LV_ASSERT_HANDLER mcs_test_lvgl_assert(__FILE__, __LINE__);
#define LV_USE_SYSMON 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0
#define LV_BUILD_TEST 0
#ifdef MCS_LVGL_TEST_MINIMAL
#define LV_USE_LABEL 0
#define LV_USE_CHECKBOX 0
#define LV_USE_SLIDER 0
#define LV_USE_BAR 0
#define LV_USE_ARC 0
#define LV_USE_SWITCH 0
#define LV_USE_BTN 0
#define LV_USE_BUTTON 0
#define LV_USE_DROPDOWN 0
#define LV_USE_ROLLER 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_TABVIEW 0
#define LV_USE_WIN 0
#define LV_USE_TEXTAREA 0
#define LV_USE_SPINBOX 0
#define LV_USE_KEYBOARD 0
#define LV_USE_CALENDAR 0
#define LV_USE_SPAN 0
#define LV_USE_IMAGE 0
#define LV_USE_IMG 0
#define LV_USE_ANIMIMG 0
#define LV_USE_ANIMIMAGE 0
#define LV_USE_SPINNER 0
#define LV_USE_TABLE 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_BTNMATRIX 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_IMGBTN 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_SCALE 0
#endif
#endif
