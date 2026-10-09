/*
 * MicroCS - driver registry and the C# Drivers class (include/mcs_driver.h).
 *
 *   Drivers.Has("ws2812")  true when a driver of that name (or one adding a
 *                          class of that name, e.g. "LedStrip") is registered
 *   Drivers.List           string[] of the registered driver names
 */
#include "mcs_driver.h"
#if MCS_ENABLE_DRIVERS
#include <string.h>

static const mcs_driver_t* g_drivers[MCS_MAX_DRIVERS];
static int g_count;

static int find_slot(const char* name) {
    for (int i = 0; i < g_count; i++)
        if (!strcmp(g_drivers[i]->name, name)) return i;
    return -1;
}
static int add(const mcs_driver_t* d, bool replace) {
    if (!d || !d->name || !d->name[0]) return MCS_HAL_EINVAL;
    int i = find_slot(d->name);
    if (i >= 0) {
        if (replace) g_drivers[i] = d;
        return 0;
    }
    if (g_count >= MCS_MAX_DRIVERS) return MCS_HAL_EBUSY;
    g_drivers[g_count++] = d;
    return 0;
}
int mcs_driver_register(const mcs_driver_t* d) { return add(d, true); }
int mcs_driver_register_default(const mcs_driver_t* d) { return add(d, false); }
int mcs_driver_unregister(const char* name) {
    int i = name ? find_slot(name) : -1;
    if (i < 0) return MCS_HAL_ENODEV;
    memmove(&g_drivers[i], &g_drivers[i + 1], (size_t)(g_count - i - 1) * sizeof g_drivers[0]);
    g_count--;
    return 0;
}
const mcs_driver_t* mcs_driver_find(const char* name) {
    int i = name ? find_slot(name) : -1;
    return i < 0 ? NULL : g_drivers[i];
}
int mcs_driver_count(void) { return g_count; }
const mcs_driver_t* mcs_driver_at(int i) { return i >= 0 && i < g_count ? g_drivers[i] : NULL; }

/* `word` is one of the space separated names in `list` */
static bool has_word(const char* list, const char* word) {
    size_t n = strlen(word);
    if (!list || !n) return false;
    for (const char* p = list; *p;) {
        while (*p == ' ') p++;
        const char* e = p;
        while (*e && *e != ' ') e++;
        if ((size_t)(e - p) == n && !memcmp(p, word, n)) return true;
        p = e;
    }
    return false;
}
bool mcs_driver_provides(const char* name) {
    if (!name) return false;
    for (int i = 0; i < g_count; i++)
        if (!strcmp(g_drivers[i]->name, name) || has_word(g_drivers[i]->classes, name)) return true;
    return false;
}

/* ------------------------------------------------------------ C# Drivers */
static mcs_value_t drv_has(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    const char* n = mcs_to_cstr(vm, argv[0]);
    if (mcs_has_exception(vm)) return mcs_null();
    return mcs_bool(mcs_driver_provides(n));
}
static mcs_value_t drv_list(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc; (void)argv;
    mcs_value_t a = mcs_new_array(vm, (uint32_t)g_count);
    if (mcs_is_null(a)) return a;
    mcs_push_root(vm, a);
    for (int i = 0; i < g_count && i < (int)mcs_len(a); i++) {
        mcs_value_t s = mcs_string(vm, g_drivers[i]->name);
        if (mcs_has_exception(vm)) break;
        mcs_set_index(a, (uint32_t)i, s);
    }
    mcs_pop_root(vm, 1);
    return a;
}
static const mcs_reg_t drivers_fns[] = { MCS_FN("Has", drv_has, 1), MCS_GET("List", drv_list), MCS_REG_END };

void mcs_drivers_open(mcs_vm_t* vm) {
    mcs_register_module(vm, "Drivers", drivers_fns);
    for (int i = 0; i < g_count; i++)
        if (g_drivers[i]->open) g_drivers[i]->open(vm, g_drivers[i]);
}
#endif
