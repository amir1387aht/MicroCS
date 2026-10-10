/*
 * MicroCS - optional "u8g2" driver: C# U8g2 (graphics) and U8x8 (text) classes for
 * monochrome OLED / LCD / e-paper displays, built on olikraus' u8g2 library
 * (https://github.com/olikraus/u8g2, 2-clause BSD; not bundled - see docs/U8G2.md).
 *
 *   var d = new U8g2("ssd1306_i2c_128x64_noname");        I2C bus 0, the display's default address
 *   var d = U8g2.I2C("sh1106_i2c_128x64_noname", 1, 0x3D); bus 1, address 0x3D
 *   var d = U8g2.SPI("ssd1306_128x64_noname", 0, cs, dc, reset);
 *   d.Begin();  d.SetFont("helvB10_tr");  d.DrawStr(0, 20, "Hello");  d.SendBuffer();
 *   d.Draw(() => { d.DrawFrame(0, 0, 128, 64); });        page-mode picture loop ("..._1" / "..._2" names)
 *   var t = new U8x8("ssd1306_i2c_128x64_noname");  t.Begin();  t.DrawString(0, 0, "Hi");
 *
 * Every display, bus and font is chosen when the firmware is built:
 *   MCS_U8G2_DISPLAYS  U8G2_DISPLAY(name)... - u8g2_Setup_<name>_1/_2/_f setups compiled in
 *   MCS_U8G2_FONTS     U8G2_FONT(u8g2_font_x)... - fonts in flash for SetFont("x")
 *   MCS_U8X8_FONTS     U8X8_FONT(u8x8_font_x)... - the same for U8x8
 * Any other of the ~2000 u8g2 fonts loads at run time from a file
 * (MCS_U8G2_FONT_DIR/u8g2_font_<name>.bin, written by tools/u8g2.py), and a firmware
 * can add fonts and displays with mcs_u8g2_add_font() / mcs_u8g2_add_display().
 * The buses are the board's HAL (I2C, SPI, GPIO) or u8g2's bit-banged ones on GPIO pins.
 */
#include "mcs_driver.h"
#if MCS_ENABLE_U8G2

#if defined(MCS_U8G2_INCLUDE)
#include MCS_U8G2_INCLUDE
#elif defined(__has_include)
#if __has_include(<u8g2.h>)
#include <u8g2.h>                 /* u8g2/csrc on the include path (CMake, ESP-IDF, Zephyr, make) */
#elif __has_include(<clib/u8g2.h>)
#include <clib/u8g2.h>            /* the Arduino U8g2 library (Library Manager / PlatformIO olikraus/U8g2) */
#else
#define MCS_U8G2_MISSING 1
#endif
#else
#include <u8g2.h>
#endif

#if defined(MCS_U8G2_MISSING)
#if defined(ARDUINO)
/* Arduino: ports/arduino/mcs_arduino_u8g2.cpp includes <U8x8lib.h>, which makes the IDE
 * find the U8g2 library (or report that it is missing); nothing to build here until then. */
void mcs_u8g2_use_hal(const mcs_hal_t* hal) { (void)hal; }
#else
#error "MCS_ENABLE_U8G2=1 needs the u8g2 library (not bundled with MicroCS): put its csrc/ folder on the include path and compile its *.c files - CMake/ESP-IDF/Zephyr do it for you with MICROCS_U8G2_DIR=/path/to/u8g2 or MICROCS_U8G2_DOWNLOAD=ON, `make mcs-u8g2 U8G2_DIR=...`; to download it: python3 tools/u8g2.py fetch (docs/U8G2.md)"
#endif
#else /* u8g2 found */

#include <string.h>
#include <stdio.h>

/* u8x8.h defines U8X8_PIN_CNT only where it keeps pin numbers itself (Arduino, Linux); the
   driver keeps its own HAL pin table, the same size everywhere */
#define MCS_U8X8_PIN_CNT (U8X8_PIN_OUTPUT_CNT + U8X8_PIN_INPUT_CNT)
#if MCS_ENABLE_FS
#include "mcs_vfs.h"
#endif
#if MCS_ENABLE_THREADS
#include "mcs_os.h"
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define NATIVE(name) static mcs_value_t name(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv)

typedef void (*setup_fn_t)(u8g2_t*, const u8g2_cb_t*, u8x8_msg_cb, u8x8_msg_cb);
typedef struct { const char* name; setup_fn_t setup; } disp_ent_t;
typedef struct { const char* name; const uint8_t* data; } font_ent_t;

/* ------------------------------------------------------------ build-time tables */
#define U8G2_DISPLAY(n) { #n, u8g2_Setup_##n##_1 },
static const disp_ent_t g_displays[] = { MCS_U8G2_DISPLAYS { NULL, NULL } };
#undef U8G2_DISPLAY
#define U8G2_FONT(n) { #n, n },
static const font_ent_t g_fonts[] = { MCS_U8G2_FONTS { NULL, NULL } };
#undef U8G2_FONT
#define U8X8_FONT(n) { #n, n },
static const font_ent_t g_xfonts[] = { MCS_U8X8_FONTS { NULL, NULL } };
#undef U8X8_FONT

/* added by the firmware (mcs_u8g2_add_*) */
static disp_ent_t g_ext_disp[MCS_U8G2_EXTRA];
static font_ent_t g_ext_font[MCS_U8G2_EXTRA], g_ext_xfont[MCS_U8G2_EXTRA];

static int add_ent(font_ent_t* t, const char* name, const uint8_t* data) {
    if (!name || !data) return MCS_HAL_EINVAL;
    for (int i = 0; i < MCS_U8G2_EXTRA; i++)
        if (!t[i].name || !strcmp(t[i].name, name)) { t[i].name = name; t[i].data = data; return 0; }
    return MCS_HAL_EBUSY;
}
int mcs_u8g2_add_font(const char* name, const uint8_t* font) { return add_ent(g_ext_font, name, font); }
int mcs_u8x8_add_font(const char* name, const uint8_t* font) { return add_ent(g_ext_xfont, name, font); }
int mcs_u8g2_add_display(const char* name, mcs_u8g2_setup_fn setup) {
    if (!name || !setup) return MCS_HAL_EINVAL;
    for (int i = 0; i < MCS_U8G2_EXTRA; i++)
        if (!g_ext_disp[i].name || !strcmp(g_ext_disp[i].name, name)) {
            g_ext_disp[i].name = name; g_ext_disp[i].setup = (setup_fn_t)setup; return 0;
        }
    return MCS_HAL_EBUSY;
}

/* ------------------------------------------------------------ instance */
enum { BUS_I2C = 1, BUS_SW_I2C, BUS_SPI, BUS_SW_SPI, BUS_3W_SPI, BUS_8080, BUS_6800, BUS_KS0108 };
static const char* const bus_names[] = { "", "I2C", "SoftI2C", "SPI", "SoftSPI", "Soft3WireSPI", "Parallel8080", "Parallel6800", "KS0108" };
#define UG_MAGIC 0x55384732u
#define FONT_NAME_MAX 48

typedef struct { char name[FONT_NAME_MAX]; uint8_t* data; size_t size; } loaded_font_t;

typedef struct {
    u8g2_t u8g2;                     /* first: the u8x8 callbacks cast u8x8_t* back to ug_t* */
    uint32_t magic;
    mcs_vm_t* vm;
    const mcs_hal_t* hal;
    const char* display;             /* name in the display table */
    const char* font_name;           /* current font (table entry or loaded slot) */
    uint8_t is_x;                    /* U8x8 object (no frame buffer) */
    uint8_t kind;                    /* BUS_* */
    uint8_t mode;                    /* 'f' full buffer, '1' / '2' page buffer of 1 / 2 tile rows */
    uint8_t began, disposed, od, aborted, in_ui;
    int16_t bus;
    int16_t pin[MCS_U8X8_PIN_CNT];       /* HAL pin for every u8x8 pin, -1 = none */
    int err;                         /* first HAL error of the current operation */
    const char* err_op;
    uint8_t* buf; size_t buf_size;   /* frame / page buffer (VM heap) */
    uint8_t i2c[MCS_U8G2_I2C_BUF]; uint16_t i2c_n;
    mcs_spi_cfg_t spi;
    loaded_font_t fonts[MCS_U8G2_MAX_FONTS]; uint8_t font_next;
    u8log_t log; uint8_t* log_buf; size_t log_size;
    int16_t cx, cy, cx0;             /* Print cursor (pixels for U8g2, tiles for U8x8) */
    int menu_pin;                    /* pinned menu-input callback, -1 = none */
    uint8_t inj_pin, inj_count;      /* menu event being injected as a simulated key press */
} ug_t;

static const mcs_class_def_t u8g2_def, u8x8_def;
#define UG(u8x8) ((ug_t*)(void*)(u8x8))
#define X8(d) (&(d)->u8g2.u8x8)
#define G2(d) (&(d)->u8g2)

static ug_t* ug_self(mcs_vm_t* vm, mcs_value_t self, int want_g) {
    ug_t* d = (ug_t*)mcs_userdata(self);
    if (!d || d->magic != UG_MAGIC) { mcs_raise(vm, "InvalidCastException", "U8g2 / U8x8 object expected"); return NULL; }
    if (d->disposed) { mcs_raise(vm, "ObjectDisposedException", "%s", d->is_x ? "U8x8" : "U8g2"); return NULL; }
    if (want_g && !d->buf) { mcs_raise(vm, "InvalidOperationException", "U8g2: no frame buffer"); return NULL; }
    return d;
}
#define SELF() ug_t* d = ug_self(vm, self, 0); if (!d) return mcs_null()
#define GSELF() ug_t* d = ug_self(vm, self, 1); if (!d) return mcs_null()

#if MCS_ENABLE_THREADS
static mcs_os_mutex_t* g_poly_lock;  /* u8g2's polygon state is global */
#endif

/* ------------------------------------------------------------ errors */
static void fail(ug_t* d, const char* op, int rc) { if (!d->err) { d->err = rc ? rc : MCS_HAL_ERR; d->err_op = op; } }
/* raise the first HAL error of the operation that just ran; true when one was raised */
static bool io_check(mcs_vm_t* vm, ug_t* d) {
    if (!d->err) return false;
    int rc = d->err; const char* op = d->err_op;
    d->err = 0;
    if (mcs_has_exception(vm)) return true;
    if ((d->kind == BUS_I2C) && (rc == MCS_HAL_ENODEV || rc == MCS_HAL_ERR))
        mcs_raise(vm, "IOException", "%s: no display answers at I2C address 0x%02X on bus %d (%s)",
                  d->is_x ? "U8x8" : "U8g2", X8(d)->i2c_address >> 1, d->bus, d->display);
    else mcs_hal_raise(vm, op, rc);
    return true;
}
#define IO_DONE(d) do { if (io_check(vm, d)) return mcs_null(); } while (0)

/* ------------------------------------------------------------ GPIO and delays */
static void dly(ug_t* d, uint32_t us) { if (us && d->hal && d->hal->delay_us) d->hal->delay_us(d->hal->ctx, us); }
static void pin_out(ug_t* d, int idx, int v) {
    const mcs_hal_t* h = d->hal; int p = d->pin[idx];
    if (p < 0 || !h || !h->gpio_write) return;
    if (idx == U8X8_PIN_I2C_CLOCK || idx == U8X8_PIN_I2C_DATA) {
        if (d->od) { h->gpio_write(h->ctx, p, v); return; }
        /* no open-drain mode: 1 = release (input with pull-up), 0 = drive low */
        if (v) { if (h->gpio_mode) h->gpio_mode(h->ctx, p, MCS_GPIO_INPUT_PULLUP); }
        else { if (h->gpio_mode) h->gpio_mode(h->ctx, p, MCS_GPIO_OUTPUT); h->gpio_write(h->ctx, p, 0); }
        return;
    }
    int rc = h->gpio_write(h->ctx, p, v);
    if (rc < 0) fail(d, "U8g2 GPIO", rc);
}
static void menu_poll_start(ug_t* d);
static int menu_level(ug_t* d, int idx) {
    if (idx == U8X8_PIN_MENU_SELECT) menu_poll_start(d);     /* u8x8 reads SELECT first in every poll */
    if (d->inj_count && d->inj_pin == idx) return 0;         /* injected key: held down (active low) */
    const mcs_hal_t* h = d->hal; int p = d->pin[idx];
    if (p < 0 || !h || !h->gpio_read) return 1;
    int v = h->gpio_read(h->ctx, p);
    return v < 0 ? 1 : v != 0;
}
static uint8_t gpio_cb(u8x8_t* u8x8, uint8_t msg, uint8_t arg, void* ptr) {
    ug_t* d = UG(u8x8);
    const mcs_hal_t* h = d->hal;
    switch (msg) {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        if (!h) return 1;
        for (int i = 0; i < MCS_U8X8_PIN_CNT; i++) {
            int p = d->pin[i];
            if (p < 0 || !h->gpio_mode) continue;
            int rc;
            if (i >= U8X8_PIN_OUTPUT_CNT) rc = h->gpio_mode(h->ctx, p, MCS_GPIO_INPUT_PULLUP);
            else if (i == U8X8_PIN_I2C_CLOCK || i == U8X8_PIN_I2C_DATA) {
                d->od = h->gpio_mode(h->ctx, p, MCS_GPIO_OPEN_DRAIN) >= 0;
                rc = 0; pin_out(d, i, 1);
            } else rc = h->gpio_mode(h->ctx, p, MCS_GPIO_OUTPUT);
            if (rc < 0) fail(d, "U8g2 GPIO", rc);
        }
        return 1;
    case U8X8_MSG_DELAY_NANO: case U8X8_MSG_DELAY_100NANO:
        return 1;                    /* a HAL GPIO call takes longer than that */
    case U8X8_MSG_DELAY_10MICRO: dly(d, 10u * arg); return 1;
    case U8X8_MSG_DELAY_MILLI: dly(d, 1000u * arg); return 1;
    case U8X8_MSG_DELAY_I2C: dly(d, arg >= 4 ? 1 : 5); return 1;   /* arg = bus clock in 100 kHz units */
    default:
        if (msg >= U8X8_MSG_GPIO(0) && msg < U8X8_MSG_GPIO(MCS_U8X8_PIN_CNT)) {
            int idx = msg - U8X8_MSG_GPIO(0);
            if (idx >= U8X8_PIN_OUTPUT_CNT) u8x8_SetGPIOResult(u8x8, (uint8_t)menu_level(d, idx));
            else pin_out(d, idx, arg);
            return 1;
        }
        return 0;
    }
}

/* ------------------------------------------------------------ menu input
 * u8x8_GetMenuEvent() debounces the menu pins (active low) and reports a key on release.
 * Keys from SetMenuInput(callback) are injected as a simulated press: held for 7 polls,
 * then released. Every poll is also a safepoint, so an aborted script leaves the
 * UserInterface* loops (as if HOME was pressed). */
static void inject(ug_t* d, int idx) { d->inj_pin = (uint8_t)idx; d->inj_count = 7; }
static void menu_poll_start(ug_t* d) {
    mcs_vm_t* vm = d->vm;
    if (d->inj_count) { d->inj_count--; return; }
    if (X8(d)->debounce_state != 0) return;
    if (d->aborted) { inject(d, U8X8_PIN_MENU_HOME); return; }
    if (d->in_ui && (mcs_safepoint(vm) || mcs_has_exception(vm))) { d->aborted = 1; inject(d, U8X8_PIN_MENU_HOME); return; }
    if (d->menu_pin >= 0) {
        mcs_value_t fn = mcs_pinned(vm, d->menu_pin), r = mcs_null();
        if (mcs_call_value(vm, fn, 0, NULL, &r) != MCS_OK || mcs_has_exception(vm)) { d->aborted = 1; inject(d, U8X8_PIN_MENU_HOME); return; }
        int ev = mcs_is_number(r) ? (int)mcs_to_int(vm, r) : 0;
        if (ev >= U8X8_MSG_GPIO_MENU_SELECT && ev <= U8X8_MSG_GPIO_MENU_DOWN) { inject(d, ev - U8X8_MSG_GPIO(0)); return; }
    }
    if (d->in_ui) dly(d, 1000);      /* ~1 ms per poll: debounces real buttons, keeps the loop cheap */
}

/* ------------------------------------------------------------ buses */
static void i2c_flush(ug_t* d, int more) {
    const mcs_hal_t* h = d->hal;
    if (d->i2c_n && !d->err) {
        int rc = h && h->i2c_write ? h->i2c_write(h->ctx, d->bus, X8(d)->i2c_address >> 1, d->i2c, d->i2c_n) : MCS_HAL_ENOTSUP;
        if (rc < 0) fail(d, "U8g2 I2C", rc);
    }
    /* a transfer larger than the buffer goes out in pieces, each starting with the
     * same control byte (0x00 commands / 0x40 data on SSD13xx-style controllers) */
    d->i2c_n = more && d->i2c_n ? 1 : 0;
}
static uint8_t byte_hw_i2c(u8x8_t* u8x8, uint8_t msg, uint8_t arg, void* ptr) {
    ug_t* d = UG(u8x8);
    const mcs_hal_t* h = d->hal;
    switch (msg) {
    case U8X8_MSG_BYTE_SEND: {
        const uint8_t* b = (const uint8_t*)ptr;
        while (arg--) {
            if (d->i2c_n >= sizeof d->i2c) i2c_flush(d, 1);
            d->i2c[d->i2c_n++] = *b++;
        }
        return 1;
    }
    case U8X8_MSG_BYTE_INIT:
        if (h && h->i2c_open) {
            uint32_t f = u8x8->bus_clock ? u8x8->bus_clock : (uint32_t)u8x8->display_info->i2c_bus_clock_100kHz * 100000u;
            int rc = h->i2c_open(h->ctx, d->bus, f ? f : 400000u);
            if (rc < 0 && rc != MCS_HAL_ENOTSUP) fail(d, "U8g2 I2C.Open", rc);
        }
        return 1;
    case U8X8_MSG_BYTE_SET_DC: return 1;
    case U8X8_MSG_BYTE_START_TRANSFER: d->i2c_n = 0; return 1;
    case U8X8_MSG_BYTE_END_TRANSFER: i2c_flush(d, 0); return 1;
    default: return 0;
    }
}
static uint8_t byte_hw_spi(u8x8_t* u8x8, uint8_t msg, uint8_t arg, void* ptr) {
    ug_t* d = UG(u8x8);
    const mcs_hal_t* h = d->hal;
    const u8x8_display_info_t* di = u8x8->display_info;
    switch (msg) {
    case U8X8_MSG_BYTE_SEND:
        if (!d->err) {
            int rc = h && h->spi_transfer ? h->spi_transfer(h->ctx, d->bus, (const uint8_t*)ptr, NULL, arg) : MCS_HAL_ENOTSUP;
            if (rc < 0) fail(d, "U8g2 SPI", rc);
        }
        return 1;
    case U8X8_MSG_BYTE_INIT:
        u8x8_gpio_SetCS(u8x8, di->chip_disable_level);
        memset(&d->spi, 0, sizeof d->spi);
        d->spi.freq_hz = u8x8->bus_clock ? u8x8->bus_clock : di->sck_clock_hz;
        d->spi.mode = di->spi_mode & 3;
        d->spi.bits = 8;
        return 1;
    case U8X8_MSG_BYTE_SET_DC: u8x8_gpio_SetDC(u8x8, arg); return 1;
    case U8X8_MSG_BYTE_START_TRANSFER: {
        int rc = mcs_hal_spi_config(d->vm, d->bus, &d->spi);   /* re-applied only after another device changed it */
        if (rc < 0 && rc != MCS_HAL_ENOTSUP) fail(d, "U8g2 SPI.Open", rc);
        u8x8_gpio_SetCS(u8x8, di->chip_enable_level);
        u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO, di->post_chip_enable_wait_ns, NULL);
        return 1;
    }
    case U8X8_MSG_BYTE_END_TRANSFER:
        u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO, di->pre_chip_disable_wait_ns, NULL);
        u8x8_gpio_SetCS(u8x8, di->chip_disable_level);
        return 1;
    default: return 0;
    }
}
static u8x8_msg_cb byte_cb_of(int kind) {
    switch (kind) {
    case BUS_I2C: return byte_hw_i2c;
    case BUS_SW_I2C: return u8x8_byte_sw_i2c;
    case BUS_SPI: return byte_hw_spi;
    case BUS_SW_SPI: return u8x8_byte_4wire_sw_spi;
    case BUS_3W_SPI: return u8x8_byte_3wire_sw_spi;
    case BUS_8080: return u8x8_byte_8bit_8080mode;
    case BUS_6800: return u8x8_byte_8bit_6800mode;
    case BUS_KS0108: return u8x8_byte_ks0108;
    default: return u8x8_byte_empty;
    }
}

/* ------------------------------------------------------------ display names
 * Accepted: the setup name ("ssd1306_i2c_128x64_noname"), with a buffer suffix
 * ("..._1", "..._2", "..._f"), with a "u8g2_Setup_" prefix, or an Arduino class name
 * ("U8G2_SSD1306_128X64_NONAME_F_HW_I2C", "U8X8_SH1106_128X64_NONAME_HW_I2C").
 * Case does not matter; the "i2c" part may be left out (the bus decides). */
static void lower_copy(char* o, size_t cap, const char* s) {
    size_t i = 0;
    for (; s[i] && i + 1 < cap; i++) o[i] = (char)(s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i]);
    o[i] = 0;
}
static bool ends_with(char* s, const char* suf) {
    size_t n = strlen(s), m = strlen(suf);
    if (n <= m || strcmp(s + n - m, suf)) return false;
    s[n - m] = 0;
    return true;
}
static bool starts_cut(char* s, const char* pre) {
    size_t m = strlen(pre);
    if (strncmp(s, pre, m)) return false;
    memmove(s, s + m, strlen(s + m) + 1);
    return true;
}
static void drop_i2c(char* o, const char* s) {     /* "ssd1306_i2c_128x64" -> "ssd1306_128x64" */
    for (; *s; ) {
        if (!strncmp(s, "_i2c_", 5)) { *o++ = '_'; s += 5; continue; }
        *o++ = *s++;
    }
    *o = 0;
}
static const disp_ent_t* disp_at(int i) {
    int n = (int)(sizeof g_displays / sizeof g_displays[0]) - 1;
    if (i < n) return &g_displays[i];
    i -= n;
    return i < MCS_U8G2_EXTRA && g_ext_disp[i].name ? &g_ext_disp[i] : NULL;
}
/* want_i2c: 1 I2C bus, 0 other bus. *mode gets 'f' / '1' / '2' when the name says so */
static const disp_ent_t* find_display(const char* user, int want_i2c, uint8_t* mode) {
    char n[72], a[72], b[72];
    lower_copy(n, sizeof n, user);
    if (!starts_cut(n, "u8g2_setup_") && !starts_cut(n, "u8g2_")) starts_cut(n, "u8x8_");
    static const char* const sufs[] = { "_2nd_4w_hw_spi", "_2nd_hw_i2c", "_4w_hw_spi", "_4w_sw_spi", "_3w_hw_spi",
                                        "_3w_sw_spi", "_hw_i2c", "_sw_i2c", "_8080", "_6800", "_2nd_hw_spi", "_hw_spi", "_sw_spi" };
    for (unsigned i = 0; i < sizeof sufs / sizeof sufs[0]; i++) if (ends_with(n, sufs[i])) break;
    if (ends_with(n, "_f")) *mode = 'f';
    else if (ends_with(n, "_1")) *mode = '1';
    else if (ends_with(n, "_2")) *mode = '2';
    /* exact name on the right bus, then the same panel on the right bus ("ssd1306_128x64_noname" for
       I2C finds "ssd1306_i2c_128x64_noname"), then the exact name anyway (the caller reports the bus) */
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; disp_at(i); i++) {
            const disp_ent_t* e = disp_at(i);
            lower_copy(a, sizeof a, e->name);
            bool bus_ok = (strstr(a, "_i2c_") != NULL) == (want_i2c != 0);
            bool hit = pass == 0 ? bus_ok && !strcmp(a, n)
                     : pass == 1 ? bus_ok && (drop_i2c(b, a), drop_i2c(a, n), !strcmp(a, b))
                     : !strcmp(a, n);
            if (hit) return e;
        }
    return NULL;
}
static const char* short_name(const char* s, int is_x);
static void list_names(char* o, size_t cap, int fonts_of_x) {   /* for error messages (VM messages are < 200 chars) */
    size_t k = 0; o[0] = 0;
    for (int i = 0;; i++) {
        const char* nm;
        if (fonts_of_x < 0) { const disp_ent_t* e = disp_at(i); if (!e) break; nm = e->name; }
        else {
            const font_ent_t* t = fonts_of_x ? g_xfonts : g_fonts;
            nm = t[i].name;
            if (!nm) break;
            nm = short_name(nm, fonts_of_x);
        }
        size_t l = strlen(nm);
        if (k + l + 3 >= cap) { if (k + 4 < cap) strcpy(o + k, "..."); break; }
        if (k) { o[k++] = ','; o[k++] = ' '; }
        memcpy(o + k, nm, l + 1); k += l;
    }
    if (!k && cap > 7) strcpy(o, "(none)");
}

/* ------------------------------------------------------------ fonts */
static bool font_ok(const uint8_t* f, size_t n, int is_x) {
    if (is_x) {
        if (n < 4 || f[0] > f[1] || !f[2] || !f[3]) return false;
        return n >= 4u + (size_t)(f[1] - f[0] + 1) * f[2] * f[3] * 8u;
    }
    size_t i = 23;                   /* the walk of u8g2_GetFontSize(), bounded */
    for (;;) {
        if (i + 2 > n) return false;
        if (!f[i + 1]) break;
        i += f[i + 1];
    }
    i += 2;
    if (i + 2 > n) return false;
    i += (size_t)f[i] << 8 | f[i + 1];
    for (;;) {
        if (i + 3 > n) return i + 2 <= n && !f[i] && !f[i + 1];
        if (!f[i] && !f[i + 1]) return true;
        if (!f[i + 2]) return false;
        i += f[i + 2];
    }
}
static const char* short_name(const char* s, int is_x) {
    const char* pre = is_x ? "u8x8_font_" : "u8g2_font_";
    return strncmp(s, pre, 10) ? s : s + 10;
}
static const font_ent_t* find_builtin(const char* name, int is_x) {
    const char* want = short_name(name, is_x);
    const font_ent_t* t = is_x ? g_xfonts : g_fonts;
    for (int i = 0; t[i].name; i++) if (!strcmp(short_name(t[i].name, is_x), want)) return &t[i];
    const font_ent_t* e = is_x ? g_ext_xfont : g_ext_font;
    for (int i = 0; i < MCS_U8G2_EXTRA && e[i].name; i++) if (!strcmp(short_name(e[i].name, is_x), want)) return &e[i];
    return NULL;
}
static void apply_font(ug_t* d, const uint8_t* data, const char* name) {
    if (d->is_x) u8x8_SetFont(X8(d), data);
    else u8g2_SetFont(G2(d), data);
    d->font_name = name;
}
static void free_font(mcs_vm_t* vm, loaded_font_t* f) {
    if (f->data) mcs_mem_realloc(vm, f->data, f->size + 4, 0);
    f->data = NULL; f->size = 0; f->name[0] = 0;
}
/* a free cache slot (evicts the oldest that is not the current font) */
static loaded_font_t* font_slot(mcs_vm_t* vm, ug_t* d) {
    for (int i = 0; i < MCS_U8G2_MAX_FONTS; i++) if (!d->fonts[i].data) return &d->fonts[i];
    for (int k = 0; k < MCS_U8G2_MAX_FONTS; k++) {
        loaded_font_t* f = &d->fonts[d->font_next];
        d->font_next = (uint8_t)((d->font_next + 1) % MCS_U8G2_MAX_FONTS);
        if (f->data != (d->is_x ? X8(d)->font : G2(d)->font)) { free_font(vm, f); return f; }
    }
    return NULL;
}
/* bytes -> a loaded font; raises and returns false on a bad font */
static bool load_bytes(mcs_vm_t* vm, ug_t* d, const char* name, const uint8_t* src, size_t n, mcs_value_t arr) {
    loaded_font_t* f = font_slot(vm, d);
    if (!f) { mcs_raise(vm, "InvalidOperationException", "U8g2: font cache full"); return false; }
    uint8_t* p = (uint8_t*)mcs_mem_realloc(vm, NULL, 0, n + 4);
    if (!p) { mcs_raise(vm, "OutOfMemoryException", "U8g2: no memory for font '%s' (%u bytes)", name, (unsigned)n); return false; }
    if (src) memcpy(p, src, n);
    else for (size_t i = 0; i < n; i++) p[i] = (uint8_t)mcs_to_int(vm, mcs_index(arr, (uint32_t)i));
    memset(p + n, 0, 4);
    if (mcs_has_exception(vm) || !font_ok(p, n, d->is_x)) {
        mcs_mem_realloc(vm, p, n + 4, 0);
        if (!mcs_has_exception(vm))
            mcs_raise(vm, "ArgumentException", "U8g2: '%s' is not a valid %s font", name, d->is_x ? "u8x8" : "u8g2");
        return false;
    }
    f->data = p; f->size = n;
    strncpy(f->name, name, sizeof f->name - 1); f->name[sizeof f->name - 1] = 0;
    apply_font(d, p, f->name);
    return true;
}
#if MCS_ENABLE_FS
static bool load_file(mcs_vm_t* vm, ug_t* d, const char* name, const char* path, bool quiet) {
    mcs_vfs_t* vfs = (mcs_vfs_t*)mcs_get_ext(vm, MCS_EXT_VFS);
    if (!vfs) { if (!quiet) mcs_raise(vm, "IOException", "U8g2: no filesystem to load font '%s' from", path); return false; }
    mcs_vfs_stat_t st;
    if (mcs_vfs_stat(vfs, path, &st) || st.is_dir) {
        if (!quiet) mcs_raise(vm, "FileNotFoundException", "U8g2: font file '%s' not found", path);
        return false;
    }
    if (st.size > MCS_U8G2_MAX_FONT_SIZE) { mcs_raise(vm, "ArgumentException", "U8g2: font file '%s' too large (%u bytes)", path, (unsigned)st.size); return false; }
    char* data; size_t len;
    int e = mcs_vfs_read_file(vfs, path, &data, &len);
    if (e) { mcs_raise(vm, "IOException", "U8g2: cannot read '%s': %s", path, mcs_vfs_strerror(e)); return false; }
    bool ok = load_bytes(vm, d, name, (const uint8_t*)data, len, mcs_null());
    mcs_vfs_free(vfs, data, len);
    return ok;
}
#endif
/* SetFont(name | path | byte[]) */
static bool set_font(mcs_vm_t* vm, ug_t* d, mcs_value_t v) {
    int k = mcs_obj_kind(v);
    if (k == MCS_O_ARRAY || k == MCS_O_LIST) {
        uint32_t n = mcs_len(v);
        if (n > MCS_U8G2_MAX_FONT_SIZE) { mcs_raise(vm, "ArgumentException", "U8g2: font too large"); return false; }
        return load_bytes(vm, d, "(bytes)", NULL, n, v);
    }
    const char* name = mcs_to_cstr(vm, v);
    if (mcs_has_exception(vm)) return false;
    const font_ent_t* b = find_builtin(name, d->is_x);
    if (b) { apply_font(d, b->data, short_name(b->name, d->is_x)); return true; }
    for (int i = 0; i < MCS_U8G2_MAX_FONTS; i++)
        if (d->fonts[i].data && !strcmp(d->fonts[i].name, name)) { apply_font(d, d->fonts[i].data, d->fonts[i].name); return true; }
#if MCS_ENABLE_FS
    if (strchr(name, '/') || strstr(name, ".bin")) return load_file(vm, d, name, name, false);
    char path[128];
    snprintf(path, sizeof path, "%s/%s%s.bin", MCS_U8G2_FONT_DIR, d->is_x ? "u8x8_font_" : "u8g2_font_", short_name(name, d->is_x));
    if (load_file(vm, d, name, path, true)) return true;
    if (mcs_has_exception(vm)) return false;
#endif
    char list[72];
    list_names(list, sizeof list, d->is_x);
#if MCS_ENABLE_FS
    mcs_raise(vm, "ArgumentException", "%s: font '%s' is not built in (%s) and %s is missing", d->is_x ? "U8x8" : "U8g2", name, list, path);
#else
    mcs_raise(vm, "ArgumentException", "%s: font '%s' is not built in (%s); add it to %s", d->is_x ? "U8x8" : "U8g2", name, list,
              d->is_x ? "MCS_U8X8_FONTS" : "MCS_U8G2_FONTS");
#endif
    return false;
}

/* ------------------------------------------------------------ construction */
static const u8g2_cb_t* rotation_of(int r) {
    switch (r) {
    case 1: return U8G2_R1;
    case 2: return U8G2_R2;
    case 3: return U8G2_R3;
    case 4: return U8G2_MIRROR;
    case 5: return U8G2_MIRROR_VERTICAL;
    default: return U8G2_R0;
    }
}
static void ug_free(mcs_vm_t* vm, void* data) {
    ug_t* d = (ug_t*)data;
    if (d->magic != UG_MAGIC) return;
    if (d->buf) mcs_mem_realloc(vm, d->buf, d->buf_size, 0);
    d->buf = NULL; d->buf_size = 0;
    if (d->log_buf) mcs_mem_realloc(vm, d->log_buf, d->log_size, 0);
    d->log_buf = NULL; d->log_size = 0;
    for (int i = 0; i < MCS_U8G2_MAX_FONTS; i++) free_font(vm, &d->fonts[i]);
    if (d->menu_pin >= 0) mcs_unpin(vm, d->menu_pin);
    d->menu_pin = -1;
    d->font_name = NULL;
}
/* bus kinds the constructor gets from the static factories (as a negative "bus") */
#define KIND_ARG(kind) (-1000 - (kind))

/* new U8g2(display[, bus[, address[, resetPin]]]) - I2C; the statics choose other buses */
static void ug_ctor_common(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv, int is_x) {
    ug_t* d = (ug_t*)mcs_userdata(self);
    const char* cls = is_x ? "U8x8" : "U8g2";
    d->magic = UG_MAGIC; d->vm = vm; d->is_x = (uint8_t)is_x; d->menu_pin = -1;
    for (int i = 0; i < MCS_U8X8_PIN_CNT; i++) d->pin[i] = -1;
    if (argc < 1 || argc > 4) { mcs_raise(vm, "ArgumentException", "%s(display[, bus[, address[, resetPin]]]) expected", cls); return; }
    const char* name = mcs_to_cstr(vm, argv[0]);
    if (mcs_has_exception(vm)) return;
    int kind = BUS_I2C, bus = 0, addr = -1;
    if (argc >= 2) {
        int b = (int)mcs_to_int(vm, argv[1]); if (mcs_has_exception(vm)) return;
        if (b <= KIND_ARG(BUS_I2C)) kind = -1000 - b;
        else bus = b;
    }
    if (kind == BUS_I2C) {
        if (argc >= 3 && !mcs_is_null(argv[2])) { addr = (int)mcs_to_int(vm, argv[2]); if (mcs_has_exception(vm)) return; }
        if (argc >= 4 && !mcs_is_null(argv[3])) { d->pin[U8X8_PIN_RESET] = (int16_t)mcs_hal_pin_arg(vm, argv[3]); if (mcs_has_exception(vm)) return; }
        if (bus < 0 || bus > 15) { mcs_raise(vm, "ArgumentOutOfRangeException", "%s: I2C bus out of range", cls); return; }
        if (addr != -1 && (addr < 0x08 || addr > 0xFE)) { mcs_raise(vm, "ArgumentOutOfRangeException", "%s: I2C address out of range", cls); return; }
    }
    uint8_t mode = 'f';
    int want_i2c = kind == BUS_I2C || kind == BUS_SW_I2C;
    const disp_ent_t* e = find_display(name, want_i2c, &mode);
    if (!e) {
        char list[80];
        list_names(list, sizeof list, -1);
        mcs_raise(vm, "ArgumentException", "%s: display '%s'%s is not compiled in (have: %s); add it to MCS_U8G2_DISPLAYS",
                  cls, name, want_i2c ? " (I2C)" : " (SPI / parallel)", list);
        return;
    }
    bool is_i2c = strstr(e->name, "_i2c") != NULL;
    if (is_i2c != (want_i2c != 0)) {
        mcs_raise(vm, "ArgumentException", is_i2c ? "%s: '%s' is an I2C display - use new %s(...) / %s.I2C / %s.SoftI2C"
                                                  : "%s: '%s' is not an I2C display - use %s.SPI / %s.SoftSPI / ... (or its _i2c_ variant)",
                  cls, e->name, cls, cls, cls);
        return;
    }
    d->display = e->name; d->kind = (uint8_t)kind; d->bus = (int16_t)bus;
    d->hal = mcs_hal_get(vm);
    e->setup(G2(d), U8G2_R0, byte_cb_of(kind), gpio_cb);
    if (addr >= 0) X8(d)->i2c_address = (uint8_t)(addr < 0x78 ? addr << 1 : addr);   /* 7-bit, or Arduino's 8-bit 0x78 / 0x7A */
    if (is_x) { d->mode = 0; return; }
    /* own frame buffer: the whole display, or 1 / 2 tile rows for the "_1" / "_2" names */
    const u8x8_display_info_t* di = X8(d)->display_info;
    uint8_t rows = mode == '1' ? 1 : mode == '2' ? 2 : di->tile_height;
    if (rows > di->tile_height) rows = di->tile_height;
    size_t sz = (size_t)di->tile_width * 8u * rows;
    d->buf = (uint8_t*)mcs_mem_realloc(vm, NULL, 0, sz);
    if (!d->buf) { mcs_raise(vm, "OutOfMemoryException", "U8g2: no memory for a %u byte frame buffer", (unsigned)sz); return; }
    memset(d->buf, 0, sz);
    d->buf_size = sz; d->mode = mode;
    u8g2_SetupBuffer(G2(d), d->buf, rows, G2(d)->ll_hvline, G2(d)->cb);
    if (g_fonts[0].name) apply_font(d, g_fonts[0].data, short_name(g_fonts[0].name, 0));
}
static void g_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) { ug_ctor_common(vm, self, argc, argv, 0); }
static void x_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    ug_ctor_common(vm, self, argc, argv, 1);
    ug_t* d = (ug_t*)mcs_userdata(self);
    if (!mcs_has_exception(vm) && g_xfonts[0].name) apply_font(d, g_xfonts[0].data, short_name(g_xfonts[0].name, 1));
}

/* static factories: create the object, then wire the bus pins */
static int pins_arg(mcs_vm_t* vm, ug_t* d, const int* idx, int n, mcs_value_t* argv, int argc, int first) {
    for (int i = 0; i < n; i++) {
        int a = first + i;
        if (a >= argc || mcs_is_null(argv[a])) continue;
        int p = mcs_hal_pin_arg(vm, argv[a]);
        if (mcs_has_exception(vm)) return -1;
        if (p >= 0) d->pin[idx[i]] = (int16_t)p;
    }
    return 0;
}
static mcs_value_t make(mcs_vm_t* vm, int is_x, int kind, mcs_value_t name, ug_t** out) {
    mcs_value_t a[2] = { name, mcs_int(KIND_ARG(kind)) }, obj = mcs_null();
    if (mcs_new_object(vm, is_x ? "U8x8" : "U8g2", 2, a, &obj) != MCS_OK) return mcs_null();
    *out = (ug_t*)mcs_userdata(obj);
    return obj;
}
static mcs_value_t factory(mcs_vm_t* vm, int argc, mcs_value_t* argv, int is_x, int kind) {
    static const char* const usage[] = { "", "I2C(display[, bus[, address[, resetPin]]])", "SoftI2C(display, clockPin, dataPin[, resetPin])",
        "SPI(display, bus, csPin, dcPin[, resetPin])", "SoftSPI(display, clockPin, dataPin, csPin, dcPin[, resetPin])",
        "Soft3WireSPI(display, clockPin, dataPin, csPin[, resetPin])", "Parallel8080(display, int[] d0..d7, wrPin, csPin, dcPin[, resetPin])",
        "Parallel6800(display, int[] d0..d7, enablePin, csPin, dcPin[, resetPin])",
        "KS0108(display, int[] d0..d7, enablePin, dcPin, cs0Pin, cs1Pin, cs2Pin[, resetPin])" };
    static const int8_t minargs[] = { 0, 1, 3, 4, 5, 4, 5, 5, 7 };
    static const int8_t maxargs[] = { 0, 4, 4, 5, 6, 5, 6, 6, 8 };
    const char* cls = is_x ? "U8x8" : "U8g2";
    if (argc < minargs[kind] || argc > maxargs[kind]) { mcs_raise(vm, "ArgumentException", "%s.%s expected", cls, usage[kind]); return mcs_null(); }
    if (kind == BUS_I2C) {
        mcs_value_t obj = mcs_null();
        if (mcs_new_object(vm, cls, argc, argv, &obj) != MCS_OK) return mcs_null();
        return obj;
    }
    ug_t* d = NULL;
    mcs_value_t obj = make(vm, is_x, kind, argv[0], &d);
    if (!d) return mcs_null();
    mcs_push_root(vm, obj);
    int rc = 0;
    switch (kind) {
    case BUS_SW_I2C: { static const int ix[] = { U8X8_PIN_I2C_CLOCK, U8X8_PIN_I2C_DATA, U8X8_PIN_RESET }; rc = pins_arg(vm, d, ix, 3, argv, argc, 1); break; }
    case BUS_SPI: {
        int bus = (int)mcs_to_int(vm, argv[1]);
        if (mcs_has_exception(vm)) { rc = -1; break; }
        if (bus < 0 || bus > 15) { mcs_raise(vm, "ArgumentOutOfRangeException", "%s: SPI bus out of range", cls); rc = -1; break; }
        d->bus = (int16_t)bus;
        static const int ix[] = { U8X8_PIN_CS, U8X8_PIN_DC, U8X8_PIN_RESET }; rc = pins_arg(vm, d, ix, 3, argv, argc, 2); break;
    }
    case BUS_SW_SPI: { static const int ix[] = { U8X8_PIN_SPI_CLOCK, U8X8_PIN_SPI_DATA, U8X8_PIN_CS, U8X8_PIN_DC, U8X8_PIN_RESET }; rc = pins_arg(vm, d, ix, 5, argv, argc, 1); break; }
    case BUS_3W_SPI: { static const int ix[] = { U8X8_PIN_SPI_CLOCK, U8X8_PIN_SPI_DATA, U8X8_PIN_CS, U8X8_PIN_RESET }; rc = pins_arg(vm, d, ix, 4, argv, argc, 1); break; }
    default: {                       /* parallel: data pins first */
        mcs_value_t pins = argv[1];
        int k = mcs_obj_kind(pins);
        if ((k != MCS_O_ARRAY && k != MCS_O_LIST) || mcs_len(pins) != 8) { mcs_raise(vm, "ArgumentException", "%s: 8 data pins (d0..d7) expected", cls); rc = -1; break; }
        for (int i = 0; i < 8 && !rc; i++) {
            int p = mcs_hal_pin_arg(vm, mcs_index(pins, (uint32_t)i));
            if (mcs_has_exception(vm)) rc = -1; else d->pin[U8X8_PIN_D0 + i] = (int16_t)p;
        }
        if (rc) break;
        if (kind == BUS_KS0108) { static const int ix[] = { U8X8_PIN_E, U8X8_PIN_DC, U8X8_PIN_CS, U8X8_PIN_CS1, U8X8_PIN_CS2, U8X8_PIN_RESET }; rc = pins_arg(vm, d, ix, 6, argv, argc, 2); }
        else { static const int ix[] = { U8X8_PIN_E, U8X8_PIN_CS, U8X8_PIN_DC, U8X8_PIN_RESET }; rc = pins_arg(vm, d, ix, 4, argv, argc, 2); }
    }
    }
    mcs_pop_root(vm, 1);
    return rc ? mcs_null() : obj;
}
#define FACTORY(fn, is_x, kind) NATIVE(fn) { return factory(vm, argc, argv, is_x, kind); }
FACTORY(gf_i2c, 0, BUS_I2C) FACTORY(gf_swi2c, 0, BUS_SW_I2C) FACTORY(gf_spi, 0, BUS_SPI) FACTORY(gf_swspi, 0, BUS_SW_SPI)
FACTORY(gf_3w, 0, BUS_3W_SPI) FACTORY(gf_8080, 0, BUS_8080) FACTORY(gf_6800, 0, BUS_6800) FACTORY(gf_ks, 0, BUS_KS0108)
FACTORY(xf_i2c, 1, BUS_I2C) FACTORY(xf_swi2c, 1, BUS_SW_I2C) FACTORY(xf_spi, 1, BUS_SPI) FACTORY(xf_swspi, 1, BUS_SW_SPI)
FACTORY(xf_3w, 1, BUS_3W_SPI) FACTORY(xf_8080, 1, BUS_8080) FACTORY(xf_6800, 1, BUS_6800) FACTORY(xf_ks, 1, BUS_KS0108)

static mcs_value_t names_list(mcs_vm_t* vm, int what) {   /* -1 displays, 0 u8g2 fonts, 1 u8x8 fonts */
    mcs_value_t l = mcs_new_list(vm);
    mcs_push_root(vm, l);
    for (int i = 0;; i++) {
        const char* nm = NULL;
        if (what < 0) { const disp_ent_t* e = disp_at(i); if (!e) break; nm = e->name; }
        else {
            const font_ent_t* t = what ? g_xfonts : g_fonts;
            const font_ent_t* x = what ? g_ext_xfont : g_ext_font;
            int nb = 0; while (t[nb].name) nb++;
            if (i < nb) nm = t[i].name;
            else if (i - nb < MCS_U8G2_EXTRA && x[i - nb].name) nm = x[i - nb].name;
            else break;
            nm = short_name(nm, what);          /* the names SetFont() takes */
        }
        mcs_list_add(vm, l, mcs_string(vm, nm));
    }
    mcs_pop_root(vm, 1);
    return l;
}
NATIVE(s_displays) { return names_list(vm, -1); }
NATIVE(s_gfonts) { return names_list(vm, 0); }
NATIVE(s_xfonts) { return names_list(vm, 1); }

/* ------------------------------------------------------------ argument helpers */
static bool ints(mcs_vm_t* vm, int argc, mcs_value_t* argv, int from, int n, int* out) {
    for (int i = 0; i < n; i++) {
        out[i] = from + i < argc ? (int)mcs_to_int(vm, argv[from + i]) : 0;
        if (mcs_has_exception(vm)) return false;
    }
    return true;
}
#define INTS(n) int a[(n) > 0 ? (n) : 1]; if (!ints(vm, argc, argv, 0, (n), a)) return mcs_null()
/* any value as text (strings as they are, numbers etc. via ToString) */
static const char* text_of(mcs_vm_t* vm, mcs_value_t v) {
    if (mcs_is_string(v)) return mcs_cstr(v);
    if (mcs_is_null(v)) return "";
    mcs_value_t s = mcs_tostring(vm, v);
    return mcs_is_string(s) ? mcs_cstr(s) : "";
}
/* byte[] / List / string -> VM heap copy (free with mcs_mem_realloc(vm, p, n, 0)) */
static uint8_t* bytes_of(mcs_vm_t* vm, mcs_value_t v, size_t need, size_t* n_out) {
    size_t n;
    int k = mcs_obj_kind(v);
    if (mcs_is_string(v)) n = mcs_strlen(v);
    else if (k == MCS_O_ARRAY || k == MCS_O_LIST) n = mcs_len(v);
    else { mcs_raise(vm, "ArgumentException", "byte[] expected"); return NULL; }
    if (n < need) { mcs_raise(vm, "ArgumentException", "bitmap too short: %u bytes, %u needed", (unsigned)n, (unsigned)need); return NULL; }
    uint8_t* p = (uint8_t*)mcs_mem_realloc(vm, NULL, 0, n ? n : 1);
    if (!p) { mcs_raise(vm, "OutOfMemoryException", "U8g2: no memory for %u bytes", (unsigned)n); return NULL; }
    if (mcs_is_string(v)) memcpy(p, mcs_cstr(v), n);
    else for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)mcs_to_int(vm, mcs_index(v, (uint32_t)i));
        if (mcs_has_exception(vm)) { mcs_mem_realloc(vm, p, n ? n : 1, 0); return NULL; }
    }
    *n_out = n ? n : 1;
    return p;
}

/* ------------------------------------------------------------ shared lifecycle */
static void set_menu_pins(mcs_vm_t* vm, ug_t* d, int argc, mcs_value_t* argv) {
    static const int ix[] = { U8X8_PIN_MENU_SELECT, U8X8_PIN_MENU_NEXT, U8X8_PIN_MENU_PREV, U8X8_PIN_MENU_UP, U8X8_PIN_MENU_DOWN, U8X8_PIN_MENU_HOME };
    pins_arg(vm, d, ix, argc < 6 ? argc : 6, argv, argc, 0);   /* Arduino's begin(select, next, prev, up, down, home) */
}
NATIVE(u_begin) {
    SELF();
    if (argc != 0 && (argc < 3 || argc > 6)) { mcs_raise(vm, "ArgumentException", "Begin([selectPin, nextPin, prevPin[, upPin, downPin, homePin]]) expected"); return mcs_null(); }
    set_menu_pins(vm, d, argc, argv);
    if (mcs_has_exception(vm)) return mcs_null();
    if (!d->hal) { mcs_raise(vm, "NotSupportedException", "%s: no board HAL", d->is_x ? "U8x8" : "U8g2"); return mcs_null(); }
    u8x8_InitDisplay(X8(d));
    if (!d->err) u8x8_ClearDisplay(X8(d));
    if (!d->err) u8x8_SetPowerSave(X8(d), 0);
    if (!d->is_x) u8g2_ClearBuffer(G2(d));
    d->cx = d->cy = d->cx0 = 0;
    IO_DONE(d);
    d->began = 1;
    return mcs_null();
}
NATIVE(u_initdisplay) { SELF(); u8x8_InitDisplay(X8(d)); IO_DONE(d); d->began = 1; return mcs_null(); }
NATIVE(u_cleardisplay) { SELF(); u8x8_ClearDisplay(X8(d)); IO_DONE(d); return mcs_null(); }
NATIVE(u_clear) {                    /* Arduino clear(): home, clearDisplay, clearBuffer */
    SELF();
    d->cx = d->cy = d->cx0 = 0;
    u8x8_ClearDisplay(X8(d));
    if (!d->is_x) u8g2_ClearBuffer(G2(d));
    IO_DONE(d);
    return mcs_null();
}
NATIVE(u_powersave) { SELF(); u8x8_SetPowerSave(X8(d), (uint8_t)mcs_truthy(argv[0])); IO_DONE(d); return mcs_null(); }
NATIVE(u_sleep) { SELF(); u8x8_SetPowerSave(X8(d), 1); IO_DONE(d); return mcs_null(); }
NATIVE(u_wake) { SELF(); u8x8_SetPowerSave(X8(d), 0); IO_DONE(d); return mcs_null(); }
NATIVE(u_contrast) {
    SELF(); INTS(1);
    if (a[0] < 0 || a[0] > 255) { mcs_raise(vm, "ArgumentOutOfRangeException", "contrast must be 0..255"); return mcs_null(); }
    u8x8_SetContrast(X8(d), (uint8_t)a[0]); IO_DONE(d); return mcs_null();
}
NATIVE(u_flip) { SELF(); u8x8_SetFlipMode(X8(d), (uint8_t)mcs_truthy(argv[0])); IO_DONE(d); return mcs_null(); }
NATIVE(u_refresh) { SELF(); u8x8_RefreshDisplay(X8(d)); IO_DONE(d); return mcs_null(); }
NATIVE(u_get_clock) { SELF(); return mcs_int((mcs_int_t)X8(d)->bus_clock); }
NATIVE(u_set_clock) {
    SELF();
    mcs_int_t f = mcs_to_int(vm, argv[0]); if (mcs_has_exception(vm)) return mcs_null();
    if (f < 0 || f > 100000000) { mcs_raise(vm, "ArgumentOutOfRangeException", "BusClock out of range"); return mcs_null(); }
    X8(d)->bus_clock = (uint32_t)f;          /* used by the next Begin() / InitDisplay() */
    return mcs_null();
}
NATIVE(u_get_addr) {                 /* 7-bit; before Begin() the u8g2 default 0x3C unless set */
    SELF();
    if (d->kind != BUS_I2C && d->kind != BUS_SW_I2C) return mcs_int(0);
    return mcs_int(X8(d)->i2c_address == 255 ? 0x3C : X8(d)->i2c_address >> 1);
}
NATIVE(u_set_addr) {
    SELF(); INTS(1);
    if (a[0] < 0x08 || a[0] > 0xFE) { mcs_raise(vm, "ArgumentOutOfRangeException", "I2C address out of range"); return mcs_null(); }
    X8(d)->i2c_address = (uint8_t)(a[0] < 0x78 ? a[0] << 1 : a[0]);
    return mcs_null();
}
NATIVE(u_display) { SELF(); return mcs_string(vm, d->display); }
NATIVE(u_bus) { SELF(); return mcs_string(vm, bus_names[d->kind]); }
NATIVE(u_rows) { SELF(); return mcs_int(u8x8_GetRows(X8(d))); }
NATIVE(u_cols) { SELF(); return mcs_int(u8x8_GetCols(X8(d))); }
NATIVE(u_fontname) { SELF(); return d->font_name ? mcs_string(vm, d->font_name) : mcs_null(); }
NATIVE(u_setfont) { SELF(); set_font(vm, d, argv[0]); return mcs_null(); }
NATIVE(u_dispose) {
    ug_t* d = (ug_t*)mcs_userdata(self);
    if (!d || d->magic != UG_MAGIC) return mcs_null();
    if (!d->disposed) { ug_free(vm, d); d->disposed = 1; }
    return mcs_null();
}
NATIVE(u_menuinput) {                /* SetMenuInput(Func<int>) - null to remove */
    SELF();
    if (d->menu_pin >= 0) mcs_unpin(vm, d->menu_pin);
    d->menu_pin = -1;
    if (!mcs_is_null(argv[0])) {
        d->menu_pin = mcs_pin(vm, argv[0]);
        if (d->menu_pin < 0) mcs_raise(vm, "InvalidOperationException", "U8g2: too many pinned handles");
    }
    return mcs_null();
}
NATIVE(u_menuevent) {
    SELF();
    d->aborted = 0;
    int ev = u8x8_GetMenuEvent(X8(d));
    if (mcs_has_exception(vm)) return mcs_null();
    IO_DONE(d);
    return mcs_int(ev);
}
/* the UserInterface* loops: true when the run was aborted / the callback threw */
static bool ui_enter(ug_t* d) { d->aborted = 0; d->in_ui = 1; d->inj_count = 0; return true; }
static bool ui_leave(mcs_vm_t* vm, ug_t* d) {
    d->in_ui = 0; d->inj_count = 0;
    X8(d)->debounce_state = 0;
    if (d->aborted || mcs_has_exception(vm)) { d->aborted = 0; return true; }
    return io_check(vm, d);
}
static int ui_select(mcs_vm_t* vm, ug_t* d, int argc, mcs_value_t* argv) {
    const char* title = mcs_to_cstr(vm, argv[0]);
    int start = (int)mcs_to_int(vm, argv[1]);
    const char* list = mcs_to_cstr(vm, argv[2]);
    if (mcs_has_exception(vm)) return -1;
    if (start < 1 || start > 255) start = 1;
    ui_enter(d);
    int r = d->is_x ? u8x8_UserInterfaceSelectionList(X8(d), title, (uint8_t)start, list)
                    : u8g2_UserInterfaceSelectionList(G2(d), title, (uint8_t)start, list);
    return ui_leave(vm, d) ? -1 : r;
}
static int ui_message(mcs_vm_t* vm, ug_t* d, int argc, mcs_value_t* argv) {
    const char* t1 = mcs_to_cstr(vm, argv[0]); const char* t2 = mcs_to_cstr(vm, argv[1]);
    const char* t3 = mcs_to_cstr(vm, argv[2]); const char* b = mcs_to_cstr(vm, argv[3]);
    if (mcs_has_exception(vm)) return -1;
    ui_enter(d);
    int r = d->is_x ? u8x8_UserInterfaceMessage(X8(d), t1, t2, t3, b) : u8g2_UserInterfaceMessage(G2(d), t1, t2, t3, b);
    return ui_leave(vm, d) ? -1 : r;
}
static int ui_input(mcs_vm_t* vm, ug_t* d, int argc, mcs_value_t* argv) {
    const char* title = mcs_to_cstr(vm, argv[0]); const char* pre = mcs_to_cstr(vm, argv[1]);
    int v[4]; if (!ints(vm, argc, argv, 2, 4, v)) return -1;
    const char* post = mcs_to_cstr(vm, argv[6]);
    if (mcs_has_exception(vm)) return -1;
    if (v[1] < 0 || v[2] > 255 || v[1] > v[2] || v[3] < 1 || v[3] > 3) { mcs_raise(vm, "ArgumentOutOfRangeException", "UserInterfaceInputValue: need 0 <= lo <= hi <= 255, digits 1..3"); return -1; }
    uint8_t val = (uint8_t)(v[0] < v[1] ? v[1] : v[0] > v[2] ? v[2] : v[0]);
    ui_enter(d);
    int r = d->is_x ? u8x8_UserInterfaceInputValue(X8(d), title, pre, &val, (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3], post)
                    : u8g2_UserInterfaceInputValue(G2(d), title, pre, &val, (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3], post);
    if (ui_leave(vm, d)) return -2;
    return r ? val : -1;
}
NATIVE(u_ui_select) { SELF(); int r = ui_select(vm, d, argc, argv); return r < 0 ? mcs_null() : mcs_int(r); }
NATIVE(u_ui_message) { SELF(); int r = ui_message(vm, d, argc, argv); return r < 0 ? mcs_null() : mcs_int(r); }
NATIVE(u_ui_input) { SELF(); int r = ui_input(vm, d, argc, argv); return r == -2 ? mcs_null() : mcs_int(r); }

/* ------------------------------------------------------------ log (u8log) */
static void log_nocb(u8log_t* l) { (void)l; }
NATIVE(u_logbegin) {                 /* LogBegin(width, height[, autoRedraw = true]) - in characters */
    SELF(); INTS(2);
    if (a[0] < 1 || a[0] > 255 || a[1] < 1 || a[1] > 255) { mcs_raise(vm, "ArgumentOutOfRangeException", "LogBegin: width / height must be 1..255"); return mcs_null(); }
    bool autodraw = argc < 3 || mcs_truthy(argv[2]);
    size_t sz = (size_t)a[0] * (size_t)a[1];
    uint8_t* p = (uint8_t*)mcs_mem_realloc(vm, NULL, 0, sz);
    if (!p) { mcs_raise(vm, "OutOfMemoryException", "LogBegin: no memory"); return mcs_null(); }
    if (d->log_buf) mcs_mem_realloc(vm, d->log_buf, d->log_size, 0);
    d->log_buf = p; d->log_size = sz;
    u8log_Init(&d->log, (uint8_t)a[0], (uint8_t)a[1], p);
    if (autodraw) u8log_SetCallback(&d->log, d->is_x ? u8log_u8x8_cb : u8log_u8g2_cb, d->is_x ? (void*)X8(d) : (void*)G2(d));
    else u8log_SetCallback(&d->log, log_nocb, NULL);
    return mcs_null();
}
static bool log_ready(mcs_vm_t* vm, ug_t* d) {
    if (d->log_buf) return true;
    mcs_raise(vm, "InvalidOperationException", "call LogBegin(width, height) first");
    return false;
}
static mcs_value_t log_write(mcs_vm_t* vm, ug_t* d, mcs_value_t v, bool nl) {
    if (!log_ready(vm, d)) return mcs_null();
    const char* s = text_of(vm, v);
    if (mcs_has_exception(vm)) return mcs_null();
    u8log_WriteString(&d->log, s);
    if (nl) u8log_WriteChar(&d->log, '\n');
    IO_DONE(d);
    return mcs_null();
}
NATIVE(u_log) { SELF(); return log_write(vm, d, argv[0], false); }
NATIVE(u_logline) { SELF(); return log_write(vm, d, argc ? argv[0] : mcs_null(), true); }
NATIVE(u_logredraw) {                /* LogRedrawMode = 0 redraw on every char, 1 only on newline */
    SELF(); if (!log_ready(vm, d)) return mcs_null();
    u8log_SetRedrawMode(&d->log, (uint8_t)!mcs_truthy(argv[0]));
    return mcs_null();
}
NATIVE(u_loglineoffset) {
    SELF(); INTS(1); if (!log_ready(vm, d)) return mcs_null();
    u8log_SetLineHeightOffset(&d->log, (int8_t)a[0]);
    return mcs_null();
}
NATIVE(u_drawlog) {                  /* DrawLog(x, y): into the buffer (U8g2, pixels) / onto the display (U8x8, tiles) */
    SELF(); INTS(2); if (!log_ready(vm, d)) return mcs_null();
    if (d->is_x) u8x8_DrawLog(X8(d), (uint8_t)a[0], (uint8_t)a[1], &d->log);
    else u8g2_DrawLog(G2(d), (u8g2_uint_t)a[0], (u8g2_uint_t)a[1], &d->log);
    IO_DONE(d);
    return mcs_null();
}

/* ------------------------------------------------------------ U8g2: buffer and picture loop */
#define U(v) ((u8g2_uint_t)(v))
NATIVE(g_clearbuffer) { GSELF(); u8g2_ClearBuffer(G2(d)); return mcs_null(); }
NATIVE(g_sendbuffer) { GSELF(); u8g2_SendBuffer(G2(d)); IO_DONE(d); return mcs_null(); }
NATIVE(g_firstpage) { GSELF(); u8g2_FirstPage(G2(d)); IO_DONE(d); return mcs_null(); }
NATIVE(g_nextpage) { GSELF(); uint8_t more = u8g2_NextPage(G2(d)); IO_DONE(d); return mcs_bool(more != 0); }
NATIVE(g_draw) {                     /* Draw(Action | Action<U8g2>): FirstPage / NextPage loop */
    GSELF();
    mcs_value_t fn = argv[0];
    int ar = mcs_arity(fn);
    u8g2_FirstPage(G2(d));
    do {
        if (io_check(vm, d)) return mcs_null();
        mcs_value_t r;
        if (mcs_call_value(vm, fn, ar == 1 ? 1 : 0, &self, &r) != MCS_OK || mcs_has_exception(vm)) return mcs_null();
        if (d->disposed || !d->buf) return mcs_null();
    } while (u8g2_NextPage(G2(d)));
    IO_DONE(d);
    return mcs_null();
}
NATIVE(g_update) { GSELF(); u8g2_UpdateDisplay(G2(d)); IO_DONE(d); return mcs_null(); }
NATIVE(g_updatearea) {
    GSELF(); INTS(4);
    for (int i = 0; i < 4; i++) if (a[i] < 0 || a[i] > 255) { mcs_raise(vm, "ArgumentOutOfRangeException", "UpdateDisplayArea: tile values 0..255"); return mcs_null(); }
    u8g2_UpdateDisplayArea(G2(d), (uint8_t)a[0], (uint8_t)a[1], (uint8_t)a[2], (uint8_t)a[3]);
    IO_DONE(d); return mcs_null();
}
NATIVE(g_autoclear) { GSELF(); u8g2_SetAutoPageClear(G2(d), (uint8_t)mcs_truthy(argv[0])); return mcs_null(); }
NATIVE(g_rotation) {
    GSELF(); INTS(1);
    if (a[0] < 0 || a[0] > 5) { mcs_raise(vm, "ArgumentOutOfRangeException", "rotation: U8g2.R0..R3, Mirror, MirrorVertical"); return mcs_null(); }
    u8g2_SetDisplayRotation(G2(d), rotation_of(a[0]));
    return mcs_null();
}
NATIVE(g_tilew) { GSELF(); return mcs_int(u8g2_GetBufferTileWidth(G2(d))); }
NATIVE(g_tileh) { GSELF(); return mcs_int(u8g2_GetBufferTileHeight(G2(d))); }
NATIVE(g_currow) { GSELF(); return mcs_int(u8g2_GetBufferCurrTileRow(G2(d))); }
NATIVE(g_setcurrow) { GSELF(); INTS(1); u8g2_SetBufferCurrTileRow(G2(d), (uint8_t)a[0]); return mcs_null(); }
NATIVE(g_bufsize) { GSELF(); return mcs_int((mcs_int_t)d->buf_size); }
NATIVE(g_fullbuffer) { GSELF(); return mcs_bool(d->mode == 'f'); }
NATIVE(g_getbuffer) {
    GSELF();
    mcs_value_t arr = mcs_new_array(vm, (uint32_t)d->buf_size);
    for (size_t i = 0; i < d->buf_size; i++) mcs_set_index(arr, (uint32_t)i, mcs_int(d->buf[i]));
    return arr;
}
NATIVE(g_setbuffer) {                /* SetBuffer(byte[]): raw frame buffer bytes (GetBuffer's layout) */
    GSELF();
    size_t n;
    uint8_t* p = bytes_of(vm, argv[0], d->buf_size, &n);
    if (!p) return mcs_null();
    memcpy(d->buf, p, d->buf_size);
    mcs_mem_realloc(vm, p, n, 0);
    return mcs_null();
}
/* pixel of the buffer (buffer coordinates) */
static int buf_px(ug_t* d, int x, int y) {
    u8g2_t* g = G2(d);
    uint8_t tw = u8g2_GetBufferTileWidth(g);
    if (g->ll_hvline == u8g2_ll_hvline_vertical_top_lsb) return d->buf[(y / 8) * tw * 8 + x] >> (y & 7) & 1;
    return d->buf[y * tw + (x >> 3)] >> (7 - (x & 7)) & 1;
}
NATIVE(g_getpixel) {                 /* GetPixel(x, y) of the buffer (display coordinates, R0, full buffer) */
    GSELF(); INTS(2);
    u8g2_t* g = G2(d);
    int x = a[0], y = a[1] - (int)g->pixel_curr_row;
    if (x < 0 || a[1] < 0 || x >= (int)g->pixel_buf_width || y < 0 || y >= (int)g->pixel_buf_height) return mcs_int(0);
    return mcs_int(buf_px(d, x, y));
}
static mcs_value_t render(mcs_vm_t* vm, ug_t* d, int fmt) {   /* 0 ASCII art, 1 PBM (P1), 2 XBM */
    u8g2_t* g = G2(d);
    int w = g->pixel_buf_width, h = g->pixel_buf_height;
    size_t cap = fmt == 0 ? (size_t)(w + 1) * (size_t)((h + 1) / 2) * 3 + 16
               : fmt == 1 ? (size_t)w * (size_t)h * 2 + (size_t)h + 32 : (size_t)w * (size_t)h / 8 * 6 + 160;
    char* o = (char*)mcs_mem_realloc(vm, NULL, 0, cap);
    if (!o) { mcs_raise(vm, "OutOfMemoryException", "U8g2: no memory"); return mcs_null(); }
    size_t k = 0;
    if (fmt == 0) {                  /* two pixel rows per text line: ' ' '▀' '▄' '█' */
        static const char* const blk[4] = { " ", "\xE2\x96\x80", "\xE2\x96\x84", "\xE2\x96\x88" };
        for (int y = 0; y < h; y += 2) {
            for (int x = 0; x < w; x++) {
                int v = buf_px(d, x, y) | (y + 1 < h ? buf_px(d, x, y + 1) << 1 : 0);
                size_t l = strlen(blk[v]); memcpy(o + k, blk[v], l); k += l;
            }
            o[k++] = '\n';
        }
    } else if (fmt == 1) {
        k += (size_t)snprintf(o, cap, "P1\n%d %d\n", w, h);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) { o[k++] = (char)('0' + buf_px(d, x, y)); o[k++] = x + 1 < w ? ' ' : '\n'; }
        }
    } else {
        k += (size_t)snprintf(o, cap, "#define xbm_width %d\n#define xbm_height %d\nstatic unsigned char xbm_bits[] = {\n", w, h);
        int bpr = (w + 7) / 8, n = 0, total = bpr * h;
        for (int y = 0; y < h; y++)
            for (int bx = 0; bx < bpr; bx++) {
                int v = 0;
                for (int i = 0; i < 8 && bx * 8 + i < w; i++) v |= buf_px(d, bx * 8 + i, y) << i;
                k += (size_t)snprintf(o + k, cap - k, "0x%02x%s", v, ++n < total ? (n % 12 ? ", " : ",\n") : "\n");
            }
        k += (size_t)snprintf(o + k, cap - k, "};\n");
    }
    mcs_value_t s = mcs_string_n(vm, o, k);
    mcs_mem_realloc(vm, o, cap, 0);
    return s;
}
NATIVE(g_dump) { GSELF(); return render(vm, d, 0); }
NATIVE(g_pbm) { GSELF(); return render(vm, d, 1); }
NATIVE(g_xbm) { GSELF(); return render(vm, d, 2); }

/* ------------------------------------------------------------ U8g2: drawing */
NATIVE(g_pixel) { GSELF(); INTS(2); u8g2_DrawPixel(G2(d), U(a[0]), U(a[1])); return mcs_null(); }
NATIVE(g_hline) { GSELF(); INTS(3); u8g2_DrawHLine(G2(d), U(a[0]), U(a[1]), U(a[2])); return mcs_null(); }
NATIVE(g_vline) { GSELF(); INTS(3); u8g2_DrawVLine(G2(d), U(a[0]), U(a[1]), U(a[2])); return mcs_null(); }
NATIVE(g_hvline) { GSELF(); INTS(4); u8g2_DrawHVLine(G2(d), U(a[0]), U(a[1]), U(a[2]), (uint8_t)(a[3] & 3)); return mcs_null(); }
NATIVE(g_line) { GSELF(); INTS(4); u8g2_DrawLine(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3])); return mcs_null(); }
NATIVE(g_box) { GSELF(); INTS(4); u8g2_DrawBox(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3])); return mcs_null(); }
NATIVE(g_frame) { GSELF(); INTS(4); u8g2_DrawFrame(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3])); return mcs_null(); }
static bool rbox_ok(mcs_vm_t* vm, int* a) {   /* u8g2 needs w, h >= 2 * (r + 1) */
    if (a[4] < 0 || a[2] < 2 * (a[4] + 1) || a[3] < 2 * (a[4] + 1)) { mcs_raise(vm, "ArgumentOutOfRangeException", "rounded box: width and height must be >= 2 * (radius + 1)"); return false; }
    return true;
}
NATIVE(g_rbox) { GSELF(); INTS(5); if (!rbox_ok(vm, a)) return mcs_null(); u8g2_DrawRBox(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3]), U(a[4])); return mcs_null(); }
NATIVE(g_rframe) { GSELF(); INTS(5); if (!rbox_ok(vm, a)) return mcs_null(); u8g2_DrawRFrame(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3]), U(a[4])); return mcs_null(); }
#define OPT(i) (argc > (i) ? (uint8_t)(a[i] & U8G2_DRAW_ALL) : U8G2_DRAW_ALL)
NATIVE(g_circle) { GSELF(); INTS(5); u8g2_DrawCircle(G2(d), U(a[0]), U(a[1]), U(a[2]), OPT(3)); return mcs_null(); }
NATIVE(g_disc) { GSELF(); INTS(5); u8g2_DrawDisc(G2(d), U(a[0]), U(a[1]), U(a[2]), OPT(3)); return mcs_null(); }
NATIVE(g_ellipse) { GSELF(); INTS(5); u8g2_DrawEllipse(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3]), OPT(4)); return mcs_null(); }
NATIVE(g_fellipse) { GSELF(); INTS(5); u8g2_DrawFilledEllipse(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3]), OPT(4)); return mcs_null(); }
NATIVE(g_arc) {                      /* DrawArc(x, y, radius, start, end): angles 0..255 = 0..360 degrees, counter-clockwise from 3 o'clock */
    GSELF(); INTS(5);
    u8g2_DrawArc(G2(d), U(a[0]), U(a[1]), U(a[2]), (uint8_t)a[3], (uint8_t)a[4]);
    return mcs_null();
}
NATIVE(g_triangle) { GSELF(); INTS(6); u8g2_DrawTriangle(G2(d), (int16_t)a[0], (int16_t)a[1], (int16_t)a[2], (int16_t)a[3], (int16_t)a[4], (int16_t)a[5]); return mcs_null(); }
NATIVE(g_polygon) {                  /* DrawPolygon(int[] { x0, y0, x1, y1, ... }) - filled */
    GSELF();
    mcs_value_t v = argv[0];
    int k = mcs_obj_kind(v);
    if ((k != MCS_O_ARRAY && k != MCS_O_LIST) || mcs_len(v) < 6 || (mcs_len(v) & 1)) { mcs_raise(vm, "ArgumentException", "DrawPolygon: int[] with x, y pairs (>= 3 points) expected"); return mcs_null(); }
    uint32_t n = mcs_len(v);
    for (uint32_t i = 0; i < n; i++) { (void)mcs_to_int(vm, mcs_index(v, i)); if (mcs_has_exception(vm)) return mcs_null(); }
#if MCS_ENABLE_THREADS
    if (g_poly_lock) mcs_os_mutex_lock(g_poly_lock);
#endif
    u8g2_ClearPolygonXY();
    for (uint32_t i = 0; i < n; i += 2)
        u8g2_AddPolygonXY(G2(d), (int16_t)mcs_to_int(vm, mcs_index(v, i)), (int16_t)mcs_to_int(vm, mcs_index(v, i + 1)));
    u8g2_DrawPolygon(G2(d));
#if MCS_ENABLE_THREADS
    if (g_poly_lock) mcs_os_mutex_unlock(g_poly_lock);
#endif
    return mcs_null();
}
static mcs_value_t bitmap(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv, int kind) {   /* 0 XBM, 1 XBMP, 2 Bitmap */
    GSELF(); INTS(4);
    if (a[2] < 0 || a[3] < 0 || a[2] > 4096 || a[3] > 4096) { mcs_raise(vm, "ArgumentOutOfRangeException", "bitmap size out of range"); return mcs_null(); }
    size_t need = kind == 2 ? (size_t)a[2] * (size_t)a[3] : (size_t)((a[2] + 7) / 8) * (size_t)a[3];
    size_t n;
    uint8_t* p = bytes_of(vm, argv[4], need, &n);
    if (!p) return mcs_null();
    if (kind == 2) u8g2_DrawBitmap(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3]), p);
    else if (kind == 1) u8g2_DrawXBMP(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3]), p);
    else u8g2_DrawXBM(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3]), p);
    mcs_mem_realloc(vm, p, n, 0);
    return mcs_null();
}
NATIVE(g_xbmdraw) { return bitmap(vm, self, argc, argv, 0); }
NATIVE(g_xbmpdraw) { return bitmap(vm, self, argc, argv, 1); }
NATIVE(g_bitmap) { return bitmap(vm, self, argc, argv, 2); }   /* DrawBitmap(x, y, bytesPerRow, h, data): MSB = left pixel */

/* ------------------------------------------------------------ U8g2: text */
NATIVE(g_glyph) { GSELF(); INTS(3); return mcs_int(u8g2_DrawGlyph(G2(d), U(a[0]), U(a[1]), (uint16_t)a[2])); }
NATIVE(g_glyphx2) { GSELF(); INTS(3); return mcs_int(u8g2_DrawGlyphX2(G2(d), U(a[0]), U(a[1]), (uint16_t)a[2])); }
#define TEXT_NATIVE(fn, call) NATIVE(fn) { GSELF(); INTS(2); const char* s = text_of(vm, argv[2]); if (mcs_has_exception(vm)) return mcs_null(); return mcs_int(call(G2(d), U(a[0]), U(a[1]), s)); }
TEXT_NATIVE(g_str, u8g2_DrawStr) TEXT_NATIVE(g_strx2, u8g2_DrawStrX2)
TEXT_NATIVE(g_utf8, u8g2_DrawUTF8) TEXT_NATIVE(g_utf8x2, u8g2_DrawUTF8X2)
NATIVE(g_utf8lines) {                /* DrawUTF8Lines(x, y, width, lineHeight, text) -> height used; \n breaks lines */
    GSELF(); INTS(4);
    const char* s = text_of(vm, argv[4]); if (mcs_has_exception(vm)) return mcs_null();
    return mcs_int(u8g2_DrawUTF8Lines(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3]), s));
}
NATIVE(g_utf8line) {                 /* DrawUTF8Line(x, y, width, text, borderSize, invert): a centered text bar */
    GSELF(); INTS(3);
    const char* s = text_of(vm, argv[3]); if (mcs_has_exception(vm)) return mcs_null();
    int b = (int)mcs_to_int(vm, argv[4]); if (mcs_has_exception(vm)) return mcs_null();
    u8g2_DrawUTF8Line(G2(d), U(a[0]), U(a[1]), U(a[2]), s, (uint8_t)b, (uint8_t)mcs_truthy(argv[5]));
    return mcs_null();
}
NATIVE(g_button) {                   /* DrawButtonUTF8(x, y, flags, width, paddingH, paddingV, text) -> width */
    GSELF(); INTS(6);
    const char* s = text_of(vm, argv[6]); if (mcs_has_exception(vm)) return mcs_null();
    u8g2_DrawButtonUTF8(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3]), U(a[4]), U(a[5]), s);
    return mcs_null();
}
NATIVE(g_buttonframe) { GSELF(); INTS(6); u8g2_DrawButtonFrame(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3]), U(a[4]), U(a[5])); return mcs_null(); }
NATIVE(g_strwidth) { GSELF(); const char* s = text_of(vm, argv[0]); if (mcs_has_exception(vm)) return mcs_null(); return mcs_int(u8g2_GetStrWidth(G2(d), s)); }
NATIVE(g_utf8width) { GSELF(); const char* s = text_of(vm, argv[0]); if (mcs_has_exception(vm)) return mcs_null(); return mcs_int(u8g2_GetUTF8Width(G2(d), s)); }
NATIVE(g_xoffutf8) { GSELF(); const char* s = text_of(vm, argv[0]); if (mcs_has_exception(vm)) return mcs_null(); return mcs_int(u8g2_GetXOffsetUTF8(G2(d), s)); }
NATIVE(g_xoffglyph) { GSELF(); INTS(1); return mcs_int(u8g2_GetXOffsetGlyph(G2(d), (uint16_t)a[0])); }
NATIVE(g_isglyph) { GSELF(); INTS(1); return mcs_bool(u8g2_IsGlyph(G2(d), (uint16_t)a[0]) != 0); }
NATIVE(g_glyphwidth) { GSELF(); INTS(1); return mcs_int(u8g2_GetGlyphWidth(G2(d), (uint16_t)a[0])); }
NATIVE(g_ascent) { GSELF(); return mcs_int(u8g2_GetAscent(G2(d))); }
NATIVE(g_descent) { GSELF(); return mcs_int(u8g2_GetDescent(G2(d))); }
NATIVE(g_maxh) { GSELF(); return mcs_int(u8g2_GetMaxCharHeight(G2(d))); }
NATIVE(g_maxw) { GSELF(); return mcs_int(u8g2_GetMaxCharWidth(G2(d))); }
NATIVE(g_fontmode) { GSELF(); u8g2_SetFontMode(G2(d), (uint8_t)mcs_truthy(argv[0])); return mcs_null(); }
NATIVE(g_fontdir) { GSELF(); INTS(1); u8g2_SetFontDirection(G2(d), (uint8_t)(a[0] & 3)); return mcs_null(); }
NATIVE(g_posbaseline) { GSELF(); u8g2_SetFontPosBaseline(G2(d)); return mcs_null(); }
NATIVE(g_posbottom) { GSELF(); u8g2_SetFontPosBottom(G2(d)); return mcs_null(); }
NATIVE(g_postop) { GSELF(); u8g2_SetFontPosTop(G2(d)); return mcs_null(); }
NATIVE(g_poscenter) { GSELF(); u8g2_SetFontPosCenter(G2(d)); return mcs_null(); }
NATIVE(g_refall) { GSELF(); u8g2_SetFontRefHeightAll(G2(d)); return mcs_null(); }
NATIVE(g_refext) { GSELF(); u8g2_SetFontRefHeightExtendedText(G2(d)); return mcs_null(); }
NATIVE(g_reftext) { GSELF(); u8g2_SetFontRefHeightText(G2(d)); return mcs_null(); }
NATIVE(g_setcolor) { GSELF(); INTS(1); u8g2_SetDrawColor(G2(d), (uint8_t)(a[0] < 0 ? 0 : a[0] > 2 ? 2 : a[0])); return mcs_null(); }
NATIVE(g_getcolor) { GSELF(); return mcs_int(u8g2_GetDrawColor(G2(d))); }
NATIVE(g_bitmapmode) { GSELF(); u8g2_SetBitmapMode(G2(d), (uint8_t)mcs_truthy(argv[0])); return mcs_null(); }
NATIVE(g_clip) { GSELF(); INTS(4); u8g2_SetClipWindow(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3])); return mcs_null(); }
NATIVE(g_maxclip) { GSELF(); u8g2_SetMaxClipWindow(G2(d)); return mcs_null(); }
NATIVE(g_intersect) { GSELF(); INTS(4); return mcs_bool(u8g2_IsIntersection(G2(d), U(a[0]), U(a[1]), U(a[2]), U(a[3])) != 0); }
NATIVE(g_width) { GSELF(); return mcs_int(u8g2_GetDisplayWidth(G2(d))); }
NATIVE(g_height) { GSELF(); return mcs_int(u8g2_GetDisplayHeight(G2(d))); }

/* ------------------------------------------------------------ Print (both classes) */
NATIVE(u_setcursor) { SELF(); INTS(2); d->cx = d->cx0 = (int16_t)a[0]; d->cy = (int16_t)a[1]; return mcs_null(); }
NATIVE(u_home) { SELF(); d->cx = d->cx0 = d->cy = 0; return mcs_null(); }
NATIVE(u_cursorx) { SELF(); return mcs_int(d->cx); }
NATIVE(u_cursory) { SELF(); return mcs_int(d->cy); }
static mcs_value_t print(mcs_vm_t* vm, ug_t* d, int argc, mcs_value_t* argv, bool nl) {
    const char* s = argc ? text_of(vm, argv[0]) : "";
    if (mcs_has_exception(vm)) return mcs_null();
    char line[129];
    for (;;) {
        size_t n = strcspn(s, "\n");
        while (n) {                  /* in pieces of up to 128 bytes, split on UTF-8 boundaries */
            size_t m = n < sizeof line - 1 ? n : sizeof line - 1;
            while (m < n && m > 1 && ((unsigned char)s[m] & 0xC0) == 0x80) m--;
            memcpy(line, s, m); line[m] = 0;
            if (d->is_x) {
                u8x8_t* x = X8(d);
                uint8_t len = u8x8_DrawUTF8(x, (uint8_t)d->cx, (uint8_t)d->cy, line);
                d->cx = (int16_t)(d->cx + len * (x->font ? u8x8_GetFontCharWidth(x) : 1));
            } else if (d->buf) d->cx = (int16_t)(d->cx + u8g2_DrawUTF8(G2(d), U(d->cx), U(d->cy), line));
            s += m; n -= m;
        }
        if (*s != '\n' && !nl) break;
        if (d->is_x) d->cy = (int16_t)(d->cy + (X8(d)->font ? u8x8_GetFontCharHeight(X8(d)) : 1));
        else d->cy = (int16_t)(d->cy + (G2(d)->font ? u8g2_GetAscent(G2(d)) - u8g2_GetDescent(G2(d)) + 1 : 8));
        d->cx = d->cx0;
        if (*s == '\n') { s++; if (!*s && !nl) break; continue; }
        break;
    }
    IO_DONE(d);
    return mcs_null();
}
NATIVE(u_print) { SELF(); return print(vm, d, argc, argv, false); }
NATIVE(u_println) { SELF(); return print(vm, d, argc, argv, true); }

/* ------------------------------------------------------------ U8x8 text */
#define XSELF() SELF(); if (!X8(d)->font) { mcs_raise(vm, "InvalidOperationException", "U8x8: no font - SetFont(...) first"); return mcs_null(); }
#define TILE(v) ((uint8_t)((v) < 0 ? 0 : (v) > 255 ? 255 : (v)))
NATIVE(x_clearline) { SELF(); INTS(1); u8x8_ClearLine(X8(d), TILE(a[0])); IO_DONE(d); return mcs_null(); }
NATIVE(x_fill) { SELF(); u8x8_FillDisplay(X8(d)); IO_DONE(d); return mcs_null(); }
NATIVE(x_glyph) { XSELF(); INTS(3); u8x8_DrawGlyph(X8(d), TILE(a[0]), TILE(a[1]), (uint8_t)a[2]); IO_DONE(d); return mcs_null(); }
NATIVE(x_glyph12) { XSELF(); INTS(3); u8x8_Draw1x2Glyph(X8(d), TILE(a[0]), TILE(a[1]), (uint8_t)a[2]); IO_DONE(d); return mcs_null(); }
NATIVE(x_glyph22) { XSELF(); INTS(3); u8x8_Draw2x2Glyph(X8(d), TILE(a[0]), TILE(a[1]), (uint8_t)a[2]); IO_DONE(d); return mcs_null(); }
#define XTEXT(fn, call) NATIVE(fn) { XSELF(); INTS(2); const char* s = text_of(vm, argv[2]); if (mcs_has_exception(vm)) return mcs_null(); \
    int r = call(X8(d), TILE(a[0]), TILE(a[1]), s); IO_DONE(d); return mcs_int(r); }
XTEXT(x_string, u8x8_DrawString) XTEXT(x_utf8, u8x8_DrawUTF8)
XTEXT(x_string12, u8x8_Draw1x2String) XTEXT(x_utf812, u8x8_Draw1x2UTF8)
XTEXT(x_string22, u8x8_Draw2x2String) XTEXT(x_utf822, u8x8_Draw2x2UTF8)
NATIVE(x_tile) {                     /* DrawTile(x, y, count, byte[] 8 * count): 8x8 tiles, one byte per column, LSB on top */
    SELF(); INTS(3);
    if (a[2] < 1 || a[2] > 255) { mcs_raise(vm, "ArgumentOutOfRangeException", "DrawTile: count 1..255"); return mcs_null(); }
    size_t n;
    uint8_t* p = bytes_of(vm, argv[3], (size_t)a[2] * 8u, &n);
    if (!p) return mcs_null();
    u8x8_DrawTile(X8(d), TILE(a[0]), TILE(a[1]), (uint8_t)a[2], p);
    mcs_mem_realloc(vm, p, n, 0);
    IO_DONE(d);
    return mcs_null();
}
NATIVE(x_inverse) { SELF(); u8x8_SetInverseFont(X8(d), (uint8_t)mcs_truthy(argv[0])); return mcs_null(); }
NATIVE(x_utf8len) { XSELF(); const char* s = text_of(vm, argv[0]); if (mcs_has_exception(vm)) return mcs_null(); return mcs_int(u8x8_GetUTF8Len(X8(d), s)); }
NATIVE(x_fontw) { XSELF(); return mcs_int(u8x8_GetFontCharWidth(X8(d))); }
NATIVE(x_fonth) { XSELF(); return mcs_int(u8x8_GetFontCharHeight(X8(d))); }

/* ------------------------------------------------------------ class tables */
#define COMMON_MEMBERS \
    MCS_FN("Begin", u_begin, -1), MCS_FN("InitDisplay", u_initdisplay, 0), MCS_FN("ClearDisplay", u_cleardisplay, 0), \
    MCS_FN("Clear", u_clear, 0), MCS_FN("SetPowerSave", u_powersave, 1), MCS_FN("Sleep", u_sleep, 0), MCS_FN("Wake", u_wake, 0), \
    MCS_FN("SetContrast", u_contrast, 1), MCS_FN("SetFlipMode", u_flip, 1), MCS_FN("RefreshDisplay", u_refresh, 0), \
    MCS_GET("BusClock", u_get_clock), MCS_SET("BusClock", u_set_clock), MCS_FN("SetBusClock", u_set_clock, 1), \
    MCS_GET("I2CAddress", u_get_addr), MCS_SET("I2CAddress", u_set_addr), MCS_FN("SetI2CAddress", u_set_addr, 1), \
    MCS_GET("Display", u_display), MCS_GET("Bus", u_bus), MCS_GET("Rows", u_rows), MCS_GET("Cols", u_cols), \
    MCS_FN("SetFont", u_setfont, 1), MCS_GET("Font", u_fontname), MCS_SET("Font", u_setfont), \
    MCS_FN("SetCursor", u_setcursor, 2), MCS_FN("Home", u_home, 0), MCS_GET("CursorX", u_cursorx), MCS_GET("CursorY", u_cursory), \
    MCS_FN("Print", u_print, 1), MCS_FN("Println", u_println, -1), \
    MCS_FN("UserInterfaceSelectionList", u_ui_select, 3), MCS_FN("UserInterfaceMessage", u_ui_message, 4), \
    MCS_FN("UserInterfaceInputValue", u_ui_input, 7), MCS_FN("GetMenuEvent", u_menuevent, 0), MCS_FN("SetMenuInput", u_menuinput, 1), \
    MCS_FN("LogBegin", u_logbegin, -1), MCS_FN("Log", u_log, 1), MCS_FN("LogLine", u_logline, -1), \
    MCS_SET("LogRedrawOnNewline", u_logredraw), MCS_SET("LogLineHeightOffset", u_loglineoffset), MCS_FN("DrawLog", u_drawlog, 2), \
    MCS_FN("Dispose", u_dispose, 0)

static const mcs_reg_t g_members[] = {
    COMMON_MEMBERS,
    MCS_FN("ClearBuffer", g_clearbuffer, 0), MCS_FN("SendBuffer", g_sendbuffer, 0), MCS_FN("FirstPage", g_firstpage, 0),
    MCS_FN("NextPage", g_nextpage, 0), MCS_FN("Draw", g_draw, 1), MCS_FN("UpdateDisplay", g_update, 0),
    MCS_FN("UpdateDisplayArea", g_updatearea, 4), MCS_FN("SetAutoPageClear", g_autoclear, 1), MCS_FN("SetDisplayRotation", g_rotation, 1),
    MCS_GET("BufferTileWidth", g_tilew), MCS_GET("BufferTileHeight", g_tileh), MCS_GET("BufferCurrTileRow", g_currow),
    MCS_SET("BufferCurrTileRow", g_setcurrow), MCS_GET("BufferSize", g_bufsize), MCS_GET("FullBuffer", g_fullbuffer),
    MCS_FN("GetBuffer", g_getbuffer, 0), MCS_FN("SetBuffer", g_setbuffer, 1), MCS_FN("GetPixel", g_getpixel, 2),
    MCS_FN("Dump", g_dump, 0), MCS_FN("WriteBufferPBM", g_pbm, 0), MCS_FN("WriteBufferXBM", g_xbm, 0),
    MCS_FN("DrawPixel", g_pixel, 2), MCS_FN("DrawHLine", g_hline, 3), MCS_FN("DrawVLine", g_vline, 3), MCS_FN("DrawHVLine", g_hvline, 4),
    MCS_FN("DrawLine", g_line, 4), MCS_FN("DrawBox", g_box, 4), MCS_FN("DrawFrame", g_frame, 4), MCS_FN("DrawRBox", g_rbox, 5),
    MCS_FN("DrawRFrame", g_rframe, 5), MCS_FN("DrawCircle", g_circle, 3), MCS_FN("DrawCircle", g_circle, 4),
    MCS_FN("DrawDisc", g_disc, 3), MCS_FN("DrawDisc", g_disc, 4), MCS_FN("DrawEllipse", g_ellipse, 4), MCS_FN("DrawEllipse", g_ellipse, 5),
    MCS_FN("DrawFilledEllipse", g_fellipse, 4), MCS_FN("DrawFilledEllipse", g_fellipse, 5), MCS_FN("DrawArc", g_arc, 5),
    MCS_FN("DrawTriangle", g_triangle, 6), MCS_FN("DrawPolygon", g_polygon, 1), MCS_FN("DrawXBM", g_xbmdraw, 5),
    MCS_FN("DrawXBMP", g_xbmpdraw, 5), MCS_FN("DrawBitmap", g_bitmap, 5),
    MCS_FN("DrawGlyph", g_glyph, 3), MCS_FN("DrawGlyphX2", g_glyphx2, 3), MCS_FN("DrawStr", g_str, 3), MCS_FN("DrawStrX2", g_strx2, 3),
    MCS_FN("DrawUTF8", g_utf8, 3), MCS_FN("DrawUTF8X2", g_utf8x2, 3), MCS_FN("DrawUTF8Lines", g_utf8lines, 5),
    MCS_FN("DrawUTF8Line", g_utf8line, 6), MCS_FN("DrawButtonUTF8", g_button, 7), MCS_FN("DrawButtonFrame", g_buttonframe, 6),
    MCS_FN("GetStrWidth", g_strwidth, 1), MCS_FN("GetUTF8Width", g_utf8width, 1), MCS_FN("GetXOffsetUTF8", g_xoffutf8, 1),
    MCS_FN("GetXOffsetGlyph", g_xoffglyph, 1), MCS_FN("IsGlyph", g_isglyph, 1), MCS_FN("GetGlyphWidth", g_glyphwidth, 1),
    MCS_GET("Ascent", g_ascent), MCS_GET("Descent", g_descent), MCS_GET("MaxCharHeight", g_maxh), MCS_GET("MaxCharWidth", g_maxw),
    MCS_FN("SetFontMode", g_fontmode, 1), MCS_FN("SetFontDirection", g_fontdir, 1), MCS_FN("SetFontPosBaseline", g_posbaseline, 0),
    MCS_FN("SetFontPosBottom", g_posbottom, 0), MCS_FN("SetFontPosTop", g_postop, 0), MCS_FN("SetFontPosCenter", g_poscenter, 0),
    MCS_FN("SetFontRefHeightAll", g_refall, 0), MCS_FN("SetFontRefHeightExtendedText", g_refext, 0), MCS_FN("SetFontRefHeightText", g_reftext, 0),
    MCS_FN("SetDrawColor", g_setcolor, 1), MCS_GET("DrawColor", g_getcolor), MCS_SET("DrawColor", g_setcolor),
    MCS_FN("SetBitmapMode", g_bitmapmode, 1), MCS_FN("SetClipWindow", g_clip, 4), MCS_FN("SetMaxClipWindow", g_maxclip, 0),
    MCS_FN("IsIntersection", g_intersect, 4), MCS_GET("Width", g_width), MCS_GET("Height", g_height),
    MCS_REG_END
};
static const mcs_reg_t x_members[] = {
    COMMON_MEMBERS,
    MCS_FN("ClearLine", x_clearline, 1), MCS_FN("FillDisplay", x_fill, 0), MCS_FN("DrawGlyph", x_glyph, 3),
    MCS_FN("Draw1x2Glyph", x_glyph12, 3), MCS_FN("Draw2x2Glyph", x_glyph22, 3), MCS_FN("DrawString", x_string, 3),
    MCS_FN("DrawUTF8", x_utf8, 3), MCS_FN("Draw1x2String", x_string12, 3), MCS_FN("Draw1x2UTF8", x_utf812, 3),
    MCS_FN("Draw2x2String", x_string22, 3), MCS_FN("Draw2x2UTF8", x_utf822, 3), MCS_FN("DrawTile", x_tile, 4),
    MCS_FN("SetInverseFont", x_inverse, 1), MCS_FN("GetUTF8Len", x_utf8len, 1),
    MCS_GET("FontWidth", x_fontw), MCS_GET("FontHeight", x_fonth),
    MCS_REG_END
};
static const mcs_reg_t g_statics[] = {
    MCS_FN("I2C", gf_i2c, -1), MCS_FN("SoftI2C", gf_swi2c, -1), MCS_FN("SPI", gf_spi, -1), MCS_FN("SoftSPI", gf_swspi, -1),
    MCS_FN("Soft3WireSPI", gf_3w, -1), MCS_FN("Parallel8080", gf_8080, -1), MCS_FN("Parallel6800", gf_6800, -1),
    MCS_FN("KS0108", gf_ks, -1), MCS_GET("Displays", s_displays), MCS_GET("Fonts", s_gfonts), MCS_REG_END
};
static const mcs_reg_t x_statics[] = {
    MCS_FN("I2C", xf_i2c, -1), MCS_FN("SoftI2C", xf_swi2c, -1), MCS_FN("SPI", xf_spi, -1), MCS_FN("SoftSPI", xf_swspi, -1),
    MCS_FN("Soft3WireSPI", xf_3w, -1), MCS_FN("Parallel8080", xf_8080, -1), MCS_FN("Parallel6800", xf_6800, -1),
    MCS_FN("KS0108", xf_ks, -1), MCS_GET("Displays", s_displays), MCS_GET("Fonts", s_xfonts), MCS_REG_END
};
static const mcs_class_def_t u8g2_def = { "U8g2", sizeof(ug_t), g_ctor, ug_free, g_members, g_statics };
static const mcs_class_def_t u8x8_def = { "U8x8", sizeof(ug_t), x_ctor, ug_free, x_members, x_statics };

#define EVENT_CONSTS \
    MCS_CONST("EventNone", 0), MCS_CONST("EventSelect", U8X8_MSG_GPIO_MENU_SELECT), MCS_CONST("EventNext", U8X8_MSG_GPIO_MENU_NEXT), \
    MCS_CONST("EventPrev", U8X8_MSG_GPIO_MENU_PREV), MCS_CONST("EventHome", U8X8_MSG_GPIO_MENU_HOME), \
    MCS_CONST("EventUp", U8X8_MSG_GPIO_MENU_UP), MCS_CONST("EventDown", U8X8_MSG_GPIO_MENU_DOWN)
static const mcs_const_t g_consts[] = {
    MCS_CONST("R0", 0), MCS_CONST("R1", 1), MCS_CONST("R2", 2), MCS_CONST("R3", 3), MCS_CONST("Mirror", 4), MCS_CONST("MirrorVertical", 5),
    MCS_CONST("DrawUpperRight", U8G2_DRAW_UPPER_RIGHT), MCS_CONST("DrawUpperLeft", U8G2_DRAW_UPPER_LEFT),
    MCS_CONST("DrawLowerLeft", U8G2_DRAW_LOWER_LEFT), MCS_CONST("DrawLowerRight", U8G2_DRAW_LOWER_RIGHT), MCS_CONST("DrawAll", U8G2_DRAW_ALL),
    MCS_CONST("BtnBW0", U8G2_BTN_BW0), MCS_CONST("BtnBW1", U8G2_BTN_BW1), MCS_CONST("BtnBW2", U8G2_BTN_BW2), MCS_CONST("BtnBW3", U8G2_BTN_BW3),
    MCS_CONST("BtnShadow0", U8G2_BTN_SHADOW0), MCS_CONST("BtnShadow1", U8G2_BTN_SHADOW1), MCS_CONST("BtnShadow2", U8G2_BTN_SHADOW2),
    MCS_CONST("BtnInv", U8G2_BTN_INV), MCS_CONST("BtnHCenter", U8G2_BTN_HCENTER), MCS_CONST("BtnXFrame", U8G2_BTN_XFRAME),
    MCS_CONST("ColorClear", 0), MCS_CONST("ColorSet", 1), MCS_CONST("ColorXor", 2),
    EVENT_CONSTS, MCS_CONST_END
};
static const mcs_const_t x_consts[] = { EVENT_CONSTS, MCS_CONST_END };

static void u8g2_open(mcs_vm_t* vm, const mcs_driver_t* drv) {
    (void)drv;
    mcs_register_class(vm, &u8g2_def);
    mcs_register_consts(vm, "U8g2", g_consts);
    mcs_register_class(vm, &u8x8_def);
    mcs_register_consts(vm, "U8x8", x_consts);
}
static const mcs_driver_t u8g2_driver = { "u8g2", "U8g2 U8x8", u8g2_open, NULL, NULL };
void mcs_u8g2_use_hal(const mcs_hal_t* hal) {
    (void)hal;
#if MCS_ENABLE_THREADS
    if (!g_poly_lock) g_poly_lock = mcs_os_mutex_new();
#endif
    mcs_driver_register_default(&u8g2_driver);
}
#endif /* u8g2 found */
#endif /* MCS_ENABLE_U8G2 */
