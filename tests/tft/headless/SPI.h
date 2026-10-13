#ifndef MCS_TEST_SPI_H
#define MCS_TEST_SPI_H
#include "Arduino.h"
#define SPI_HAS_TRANSACTION 1
struct SPISettings {
    SPISettings(unsigned long, int, int) {}
};
class SPIClass {
  public:
    void begin() {
        mcs_test_io("SPI.begin");
    }
    void end() {
        mcs_test_io("SPI.end");
    }
    void beginTransaction(SPISettings) {
        mcs_test_io("SPI.beginTransaction");
    }
    void endTransaction() {
        mcs_test_io("SPI.endTransaction");
    }
    uint8_t transfer(uint8_t) {
        mcs_test_io("SPI.transfer");
    }
    uint16_t transfer16(uint16_t) {
        mcs_test_io("SPI.transfer16");
    }
    void setFrequency(unsigned long) {
        mcs_test_io("SPI.setFrequency");
    }
};
extern SPIClass SPI;
#endif
