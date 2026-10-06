/*
 * Option 1 - drop MicroCS into an existing firmware (the smallest useful example).
 *
 * Your project keeps its own main(), clocks, RTOS and drivers. MicroCS is just a
 * library: give it a heap and a print function, expose what you want, run C#.
 * On a board, replace the simulator lines with your port, e.g.
 *     mcs_stm32_hal_init(&hal, &board);   (ports/stm32)
 *     mcs_rp2_hal_init(&hal, &pins);      (ports/rp2)
 *     mcs_esp32_hal_init(&hal, &pins);    (ports/esp32)
 * Build on the host:  make quickstart
 */
#include <stdio.h>
#include "MicroCS.h"

static void print(void* ud, const char* text, size_t len) { (void)ud; fwrite(text, 1, len, stdout); }

/* a C function callable from C# as Board.Beep(times) */
static mcs_value_t board_beep(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    printf("[C] beep x%d\n", (int)mcs_to_int(vm, argv[0]));
    return mcs_null();
}
static const mcs_reg_t board_fns[] = { MCS_FN("Beep", board_beep, 1), MCS_REG_END };

static const char* app =
    "var led = new Pin(\"LED\", GPIO.Output);\n"
    "for (int i = 0; i < 3; i++) { led.Toggle(); Thread.Sleep(100); }\n"
    "I2C.Open(0, 400000);\n"
    "Console.WriteLine($\"I2C devices: {string.Join(\", \", I2C.Scan(0).Select(a => $\"0x{a:X2}\"))}\");\n"
    "Board.Beep(2);\n"
    "int Add(int a, int b) => a + b;     // callable from C with mcs_call\n";

/* the VM never calls malloc; 64 KB is plenty on a 32-bit MCU (64-bit hosts need more) */
static uint8_t heap[sizeof(void*) == 8 ? 160 * 1024 : 64 * 1024];

int main(void) {
    static mcs_pool_t pool;
    static mcs_hal_t hal;
    static mcs_hal_sim_t sim;              /* <- replace with your board port */
    mcs_hal_sim_init(&hal, &sim);

    mcs_config_t cfg;
    mcs_config_default(&cfg);
    mcs_pool_init(&pool, heap, sizeof heap);
    cfg.realloc_fn = mcs_pool_realloc;
    cfg.alloc_ud = &pool;
    cfg.write_fn = print;

    mcs_vm_t* vm = mcs_new(&cfg);
    mcs_hal_open_lib(vm, &hal);                           /* GPIO, Pin, I2C, SPI, UART, ... */
    mcs_register_module(vm, "Board", board_fns);          /* your own C API */

    if (mcs_exec_source(vm, "app.cs", app) != MCS_OK) printf("error: %s\n", mcs_last_error(vm));

    mcs_value_t args[2] = { mcs_int(40), mcs_int(2) }, result;
    if (mcs_call(vm, "Add", 2, args, &result) == MCS_OK)
        printf("[C] Add(40, 2) = %d\n", (int)mcs_to_int(vm, result));
    mcs_free(vm);
    return 0;
}
