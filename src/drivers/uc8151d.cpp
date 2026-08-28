// drivers/uc8151d.cpp — UC8151D 驱动实现（纯寄存器时序，见 epd_driver.cpp 基类）。

#include "uc8151d.h"

Uc8151d::Uc8151d(uint16_t w, uint16_t h, uint8_t planeCount, bool invertPlane1,
                 int8_t cs, int8_t dc, int8_t rst, int8_t busy, uint32_t spi_freq)
    : EpdSpiBase(w, h, planeCount, invertPlane1, /*panel=*/0x0F,  // BWR, LUT from OTP
                 cs, dc, rst, busy, spi_freq) {}
