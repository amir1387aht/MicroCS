// MicroCS device firmware: the board becomes a C# computer on its USB serial port.
//   * Serial Monitor (115200, "Newline"): type C# -   >>> GPIO.Mode(13, GPIO.Output)
//   * MicroCS Studio (https://amir1387aht.github.io/MicroCS/) or tools/mcs_remote.py:
//     edit, upload and run .cs files, see jobs, plot values
//   * Files live on LittleFS (ESP32, RP2040 - pick a Flash Size / Partition Scheme
//     with a filesystem), so /boot.cs, /jobs.cfg and /main.cs run at every start.
// Needs a 32-bit board with >= 64 KB RAM (ESP32, RP2040, SAMD51, nRF52840, STM32F4, Teensy ...).
#include <MicroCS.h>
#if defined(ESP32) || (defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED))
#include <LittleFS.h>
#define HAVE_LITTLEFS 1
#endif

#if defined(ESP32)
static uint8_t* heap;                                    // ESP32: taken from the system heap in setup()
static size_t heap_size;
#elif defined(ARDUINO_ARCH_RP2040)
static uint8_t heap[128 * 1024] __attribute__((aligned(8)));
#else
static uint8_t heap[64 * 1024] __attribute__((aligned(8)));
#endif
static mcs_runtime_t rt;
static mcs_hal_t hal;
#ifdef HAVE_LITTLEFS
static mcs_arduino_fs_t files;
#endif

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  mcs_arduino_cfg_t pins = MCS_ARDUINO_CFG_DEFAULT;      // UART 0 = Serial, I2C 0 = Wire, SPI 0 = SPI
  // pins.uart[1] = MCS_ARDUINO_UART(Serial1);           // UART.Open(1, ...)
  mcs_arduino_hal_init(&hal, &pins);
  // mcs_arduino_can_pins(5, 4);                         // ESP32: CAN.Open(0, 500000) via TWAI
  // mcs_arduino_i2s_pins(0, 26, 25, 22, -1);            // I2S.Open(0, ...): bclk, ws, dout, din

  mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
#if defined(ESP32)
  heap_size = 160 * 1024;                                // leave ~40 KB for Wi-Fi-free sketches' drivers
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (largest < heap_size + 40 * 1024) heap_size = largest > 96 * 1024 ? (largest - 40 * 1024) & ~(size_t)1023 : largest / 2;
  heap = (uint8_t*)heap_caps_malloc(heap_size, MALLOC_CAP_8BIT);
  cfg.heap = heap;
  cfg.heap_size = heap ? heap_size : 0;
#else
  cfg.heap = heap;
  cfg.heap_size = sizeof heap;
#endif
  cfg.console = mcs_arduino_console(&Serial);
  cfg.ticks = mcs_arduino_ticks;
  cfg.delay = mcs_arduino_delay;
  cfg.hal = &hal;
#ifdef HAVE_LITTLEFS
#if defined(ESP32)
  bool mounted = LittleFS.begin(true);                   // formats the partition on first use
#else
  bool mounted = LittleFS.begin();
#endif
  if (mounted) {
    files = MCS_ARDUINO_FS(LittleFS, "littlefs");
    cfg.fs_ops = &mcs_arduino_fs_ops;
    cfg.fs_ctx = &files;
  } else {
    Serial.println("MicroCS: no LittleFS partition (Tools > Flash Size / Partition Scheme) - files are kept in RAM");
    cfg.ramfs_size = 16 * 1024;
  }
#else
  cfg.ramfs_size = 8 * 1024;                             // RAM files (see MicroCS_SD for an SD card)
#endif
  mcs_runtime_start(&rt, &cfg);                          // runs /boot.cs, /jobs.cfg, /main.cs
}

void loop() {
  mcs_runtime_step(&rt, 10);   // console (REPL / Studio), C# jobs, interrupt callbacks
}
