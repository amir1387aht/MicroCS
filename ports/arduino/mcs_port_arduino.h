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
 */
#ifndef MCS_PORT_ARDUINO_H
#define MCS_PORT_ARDUINO_H
#include <Arduino.h>
#include "mcs.h"
#include "mcs_hal.h"
#include "mcs_shell.h"

#ifndef MCS_ARDUINO_NO_WIRE
#include <Wire.h>
#endif
#ifndef MCS_ARDUINO_NO_SPI
#include <SPI.h>
#endif

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
/* Console on any Stream (Serial, SerialUSB, Serial1 ...) for the REPL / mcs_runtime */
mcs_transport_t mcs_arduino_console(Stream* s);
extern "C" uint32_t mcs_arduino_ticks(void* ud);
extern "C" void mcs_arduino_delay(void* ud, uint32_t ms);
#endif
