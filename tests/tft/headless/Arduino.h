#ifndef MCS_TEST_ARDUINO_H
#define MCS_TEST_ARDUINO_H
/* TEST ONLY Arduino boundary, not TFT_eSPI replacement or hardware simulator.
 * All physical IO aborts; actual upstream source implements sprite algorithms. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <string>
#include <type_traits>
using std::max;
using std::min;
#define PROGMEM
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define MSBFIRST 1
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3
#define PI 3.14159265358979323846
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
template <class T> static uintptr_t mcs_test_pgm_dword(const T *p) {
    if constexpr (std::is_pointer<T>::value)
        return reinterpret_cast<uintptr_t>(*p);
    else {
        uint32_t v;
        memcpy(&v, p, 4);
        return v;
    }
}
#define pgm_read_dword(p) mcs_test_pgm_dword(p)
[[noreturn]] inline void mcs_test_io(const char *operation) {
    fprintf(stderr, "FORBIDDEN physical Arduino IO: %s\n", operation);
    abort();
}
inline uint32_t digitalPinToBitMask(int) {
    mcs_test_io("digitalPinToBitMask");
}
inline void digitalWrite(int, int) {
    mcs_test_io("digitalWrite");
}
inline int digitalRead(int) {
    mcs_test_io("digitalRead");
}
inline void pinMode(int, int) {
    mcs_test_io("pinMode");
}
inline void delay(unsigned long) {
    mcs_test_io("delay");
}
inline void delayMicroseconds(unsigned int) {
    mcs_test_io("delayMicroseconds");
}
inline long random(long) {
    mcs_test_io("random unimplemented test boundary");
}
inline void yield() {}
inline char *ltoa(long v, char *s, int base) {
    if (base != 10)
        abort();
    sprintf(s, "%ld", v);
    return s;
}
class String {
    std::string value;

  public:
    String(const char *s = "") : value(s) {}
    bool operator==(const char *s) const {
        return value == s;
    }
    size_t length() const {
        return value.size();
    }
    const char *c_str() const {
        return value.c_str();
    }
    void toCharArray(char *out, size_t n) const {
        if (n) {
            size_t len = std::min(n - 1, value.size());
            memcpy(out, value.data(), len);
            out[len] = 0;
        }
    }
};
struct TestSerial {
    void println(const char *) {
        mcs_test_io("Serial.println");
    }
};
extern TestSerial Serial;
#endif
