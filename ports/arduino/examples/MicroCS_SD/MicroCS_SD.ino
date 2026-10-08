// MicroCS with its files on an SD card (any board with the SD library: SAMD,
// nRF52, STM32, Teensy, Mbed, ESP32 ...). Wire the card to the SPI pins and set
// SD_CS. C# sees the card as / : File.ReadAllText("/log.txt"), Directory.GetFiles("/")
// and MicroCS Studio / tools/mcs_remote.py upload scripts to it.
#include <SPI.h>
#if defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
#include <SDFS.h>                                       // Arduino-Pico: the fs::FS flavour of SD
#else
#include <SD.h>
#endif
#include <MicroCS.h>

#ifndef SD_CS
#define SD_CS 10
#endif

static uint8_t heap[64 * 1024] __attribute__((aligned(8)));
static mcs_runtime_t rt;
static mcs_hal_t hal;
static mcs_arduino_fs_t card;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  mcs_arduino_cfg_t pins = MCS_ARDUINO_CFG_DEFAULT;
  mcs_arduino_hal_init(&hal, &pins);

  mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
  cfg.heap = heap;
  cfg.heap_size = sizeof heap;
  cfg.console = mcs_arduino_console(&Serial);
  cfg.ticks = mcs_arduino_ticks;
  cfg.delay = mcs_arduino_delay;
  cfg.hal = &hal;
#if defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
  SDFS.setConfig(SDFSConfig(SD_CS));
  bool ok = SDFS.begin();
  if (ok) card = MCS_ARDUINO_FS(SDFS, "fat");
#elif defined(ESP32)
  bool ok = SD.begin(SD_CS);
  if (ok) card = MCS_ARDUINO_FS(SD, "fat");              // fs::FS
#else
  bool ok = SD.begin(SD_CS);
  if (ok) card = MCS_ARDUINO_SD_FS(SD);                  // Arduino SD library
#endif
  if (ok) {
    cfg.fs_ops = &mcs_arduino_fs_ops;
    cfg.fs_ctx = &card;
  } else {
    Serial.println("MicroCS: no SD card - files are kept in RAM");
    cfg.ramfs_size = 8 * 1024;
  }
  mcs_runtime_start(&rt, &cfg);
}

void loop() {
  mcs_runtime_step(&rt, 10);
}
