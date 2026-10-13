#ifndef MCS_TEST_PRINT_H
#define MCS_TEST_PRINT_H
#include "Arduino.h"
class Print {
  public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;
    size_t print(const char *s) {
        return write((const uint8_t *)s, strlen(s));
    }
    size_t write(const uint8_t *p, size_t n) {
        size_t out = 0;
        for (size_t i = 0; i < n; i++)
            out += write(p[i]);
        return out;
    }
};
#endif
