/* Optional real LVGL 8.4/9.4 integration (OFF by default). docs/LVGL.md. */
#ifndef MCS_LVGL_H
#define MCS_LVGL_H
#include "mcs.h"
#ifdef __cplusplus
extern "C" {
#endif
#if MCS_ENABLE_LVGL
/* Registers LvGui / LvObject. Call on the GUI-owning, single-threaded VM.
 * Upstream LVGL is global: only one MicroCS-owned GUI can be live at a time.
 * No library initialization until a script constructs LvGui. */
void mcs_lvgl_open_lib(mcs_vm_t* vm);
/* Foreground scopes dispose GUIs constructed in the scope after any result,
 * including exceptions/OOM/Stop. Embedders can retain an older GUI explicitly. */
uint32_t mcs_lvgl_scope_begin(mcs_vm_t* vm);
void mcs_lvgl_scope_end(mcs_vm_t* vm, uint32_t checkpoint);
/* Nonblocking service from the owner VM hook/main loop; at most once per
 * millisecond. 0 success/no work; -1 backend failure. No script callbacks. */
int mcs_lvgl_poll(mcs_vm_t* vm);
/* Rendering sink: synchronous consumption of LVGL native RGB565 bytes (no
 * retained pointer/DMA); stride includes any per-row padding, bytes=stride*height.
 * Return 0 on success, negative on failure. NULL is a
 * headless sink. This is NOT proof of an attached physical display. */
typedef int (*mcs_lvgl_flush_fn)(void* ud, int x, int y, int width, int height,
                               const void* pixels, size_t stride, size_t bytes);
/* Install before constructing LvGui. The config must outlive the VM/GUI.
 * Existing callback replaced only when no GUI on that VM is live. */
typedef int (*mcs_lvgl_pointer_fn)(void* ud, int* x, int* y, bool* pressed);
typedef struct {
    mcs_lvgl_flush_fn flush;
    void* ud;
    mcs_lvgl_pointer_fn read_pointer; /* NULL: script-fed Input; 0 success, negative failure */
} mcs_lvgl_backend_t;
int mcs_lvgl_set_backend(mcs_vm_t* vm, const mcs_lvgl_backend_t* backend);
#endif
#ifdef __cplusplus
}
#endif
#endif
