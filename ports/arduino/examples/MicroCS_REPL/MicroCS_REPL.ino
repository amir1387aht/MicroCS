// MicroCS REPL: open the Serial Monitor (115200, "Newline") and type C#:
//   >>> GPIO.Mode(LED, GPIO.Output)      (LED = GPIO.Pin("LED"))
//   >>> for (int i = 0; i < 10; i++) { GPIO.Toggle(13); Thread.Sleep(200); }
// Needs a 32-bit board with >= 64 KB RAM (ESP32, RP2040, SAMD51, nRF52840, STM32F4, Teensy ...).
#include <MicroCS.h>

static uint8_t heap[64 * 1024] __attribute__((aligned(8)));
static mcs_runtime_t rt;
static mcs_hal_t hal;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  mcs_arduino_cfg_t pins = MCS_ARDUINO_CFG_DEFAULT;
  mcs_arduino_hal_init(&hal, &pins);

  mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
  cfg.heap = heap;
  cfg.heap_size = sizeof heap;
  cfg.ramfs_size = 8 * 1024;
  cfg.console = mcs_arduino_console(&Serial);
  cfg.ticks = mcs_arduino_ticks;
  cfg.delay = mcs_arduino_delay;
  cfg.hal = &hal;
  mcs_runtime_start(&rt, &cfg);
}

void loop() {
  mcs_runtime_step(&rt, 10);   // REPL input, C# jobs, interrupt callbacks
}
