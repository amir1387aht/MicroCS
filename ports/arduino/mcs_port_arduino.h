/*
 * MicroCS port for the Arduino API - any 32-bit Arduino core: ESP32, RP2040
 * (Arduino-Pico / Mbed), SAMD21/51, nRF52, STM32duino, Teensy, Renesas
 * (UNO R4), Portenta ... (needs ~48 KB of free RAM for the VM; AVR boards are
 * too small).
 *
 *     #include <MicroCS.h>
 *     static mcs_hal_t hal;
 *     void setup() {
 *         Serial.begin(115200);
 *         mcs_arduino_cfg_t cfg = MCS_ARDUINO_CFG_DEFAULT;   // Serial, Wire, SPI
 *         cfg.uart[1] = MCS_ARDUINO_UART(Serial1);           // UART.Open(1, ...)
 *         mcs_arduino_hal_init(&hal, &cfg);
 *         ...
 *     }
 *
 * C# numbering = Arduino pin numbers: GPIO.Write(13, 1), ADC.Read(GPIO.Pin("A0")),
 * PWM.Set(pin, freq, duty) uses analogWrite on that pin. Timer.Start runs in
 * software (dispatched from Hal.Poll / the REPL loop), interrupts use
 * attachInterrupt().
 *
 * Extras where the core has them:
 *   files     any fs::FS (ESP32: LittleFS, SPIFFS, FFat, SD, SD_MMC; RP2040:
 *             LittleFS, SDFS) or the Arduino SD library (other boards) - see
 *             mcs_arduino_fs_t below; boot.cs / jobs.cfg / main.cs run from it
 *   Watchdog  ESP32 (task watchdog), RP2040, AVR
 *   CAN       ESP32 TWAI  - mcs_arduino_can_pins(tx, rx)
 *   I2S       ESP32, RP2040 - mcs_arduino_i2s_pins(bus, bclk, ws, dout, din)
 *   ADC.ReadMillivolts calibrated on ESP32; Hal.UniqueId on ESP32, RP2040,
 *   nRF52, SAMD and STM32.
 */
#ifndef MCS_PORT_ARDUINO_H
#define MCS_PORT_ARDUINO_H
#include <Arduino.h>
#include "mcs.h"
#include "mcs_hal.h"
#include "mcs_shell.h"
#include "mcs_vfs.h"

#ifndef MCS_ARDUINO_NO_WIRE
#include <Wire.h>
#endif
#ifndef MCS_ARDUINO_NO_SPI
#include <SPI.h>
#endif

/* Port options (MCS_ARDUINO_UARTS/_TIMERS/_ADC_BITS, MCS_ARDUINO_NO_WIRE/_NO_SPI): set them in
 * the library's src/mcs_user_config.h (tools/make_arduino.py --define / --config). */
#ifndef MCS_ARDUINO_UARTS
#define MCS_ARDUINO_UARTS 4
#endif
#define MCS_ARDUINO_IRQS 8            /* pins with attachInterrupt at the same time */
#ifndef MCS_ARDUINO_TIMERS
#define MCS_ARDUINO_TIMERS 4
#endif

/* A serial port of any class (HardwareSerial, USB CDC, SerialUART ...):
 * build entries with MCS_ARDUINO_UART(Serial1). */
struct mcs_arduino_uart_t {
    Stream* stream;
    void (*begin)(Stream*, uint32_t baud);
    void (*end)(Stream*);
};
template <class T> void mcs_arduino_begin(Stream* s, uint32_t baud) { static_cast<T*>(s)->begin(baud); }
template <class T> void mcs_arduino_end(Stream* s) { static_cast<T*>(s)->end(); }
#define MCS_ARDUINO_UART(port) { &(port), mcs_arduino_begin<decltype(port)>, mcs_arduino_end<decltype(port)> }
#define MCS_ARDUINO_NO_UART { NULL, NULL, NULL }

struct mcs_arduino_cfg_t {
    const char* name;                              /* Hal.Board */
    mcs_arduino_uart_t uart[MCS_ARDUINO_UARTS];    /* UART.Open(n); stream NULL = not available */
#ifndef MCS_ARDUINO_NO_WIRE
    TwoWire* i2c[2];
#endif
#ifndef MCS_ARDUINO_NO_SPI
    SPIClass* spi[2];
#endif
};

#if !defined(MCS_ARDUINO_NO_WIRE) && !defined(MCS_ARDUINO_NO_SPI)
#define MCS_ARDUINO_CFG_DEFAULT { "Arduino", { MCS_ARDUINO_UART(Serial), MCS_ARDUINO_NO_UART, MCS_ARDUINO_NO_UART, MCS_ARDUINO_NO_UART }, { &Wire, NULL }, { &SPI, NULL } }
#elif !defined(MCS_ARDUINO_NO_WIRE)
#define MCS_ARDUINO_CFG_DEFAULT { "Arduino", { MCS_ARDUINO_UART(Serial), MCS_ARDUINO_NO_UART, MCS_ARDUINO_NO_UART, MCS_ARDUINO_NO_UART }, { &Wire, NULL } }
#elif !defined(MCS_ARDUINO_NO_SPI)
#define MCS_ARDUINO_CFG_DEFAULT { "Arduino", { MCS_ARDUINO_UART(Serial), MCS_ARDUINO_NO_UART, MCS_ARDUINO_NO_UART, MCS_ARDUINO_NO_UART }, { &SPI, NULL } }
#else
#define MCS_ARDUINO_CFG_DEFAULT { "Arduino", { MCS_ARDUINO_UART(Serial), MCS_ARDUINO_NO_UART, MCS_ARDUINO_NO_UART, MCS_ARDUINO_NO_UART } }
#endif

void mcs_arduino_hal_init(mcs_hal_t* hal, const mcs_arduino_cfg_t* cfg);
/* CAN.Open(0) on ESP32 (TWAI controller + a transceiver on these GPIOs). */
void mcs_arduino_can_pins(int tx, int rx);
/* I2S.Open(bus) pins (-1 = unused). ESP32: any GPIOs, mclk optional.
 * RP2040: ws must be bclk + 1; dout for TX, din for RX. */
void mcs_arduino_i2s_pins(int bus, int bclk, int ws, int dout, int din, int mclk = -1);

/* ---------------------------------------------------------------- files
 * A filesystem the core already mounted, as MicroCS files:
 *
 *     LittleFS.begin(true);                                     // ESP32: format on first use
 *     static mcs_arduino_fs_t files = MCS_ARDUINO_FS(LittleFS, "littlefs");
 *     cfg.fs_ops = &mcs_arduino_fs_ops; cfg.fs_ctx = &files;    // mcs_runtime_cfg_t
 *
 * ESP32 / RP2040 take any fs::FS object (LittleFS, SPIFFS, FFat, SD, SD_MMC,
 * SDFS). Other cores use the Arduino SD library when the sketch includes
 * <SD.h>: MCS_ARDUINO_SD_FS(SD) after SD.begin(cs). */
#if MCS_ENABLE_FS
#if defined(ESP32) || defined(ESP8266) || (defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED))
#include <FS.h>
#define MCS_ARDUINO_FSAPI 1
struct mcs_arduino_fs_t {
    fs::FS* fs;
    const char* format;                 /* shown by `df` / DriveInfo */
    uint64_t (*total)(void);            /* may be NULL */
    uint64_t (*used)(void);
};
#if defined(ESP32)
#define MCS_ARDUINO_FS(obj, fmt) mcs_arduino_fs_t{ &(obj), fmt, []() -> uint64_t { return (uint64_t)(obj).totalBytes(); }, \
                                                  []() -> uint64_t { return (uint64_t)(obj).usedBytes(); } }
#else
#define MCS_ARDUINO_FS(obj, fmt) mcs_arduino_fs_t{ &(obj), fmt, []() -> uint64_t { fs::FSInfo i; return (obj).info(i) ? (uint64_t)i.totalBytes : 0; }, \
                                                  []() -> uint64_t { fs::FSInfo i; return (obj).info(i) ? (uint64_t)i.usedBytes : 0; } }
#endif
extern const mcs_vfs_ops_t mcs_arduino_fs_ops;
#elif defined(__has_include)
#if __has_include(<SD.h>)
#include <SD.h>
#define MCS_ARDUINO_SDLIB 1
struct mcs_arduino_fs_t { SDClass* sd; const char* format; };
#define MCS_ARDUINO_SD_FS(obj) mcs_arduino_fs_t{ &(obj), "fat" }
#define MCS_ARDUINO_FS(obj, fmt) mcs_arduino_fs_t{ &(obj), fmt }
extern const mcs_vfs_ops_t mcs_arduino_fs_ops;
#endif
#endif
#endif
/* Console on any Stream (Serial, SerialUSB, Serial1 ...) for the REPL / mcs_runtime */
mcs_transport_t mcs_arduino_console(Stream* s);
extern "C" uint32_t mcs_arduino_ticks(void* ud);
extern "C" void mcs_arduino_delay(void* ud, uint32_t ms);
#endif
