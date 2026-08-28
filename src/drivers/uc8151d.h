// drivers/uc8151d.h — UC8151D 驱动 IC。
//
// 指令集与 SSD1619 高度兼容（共享 EpdSpiBase），仅默认 panel 字节不同：
// UC8151D = 0x0F（BWR，LUT from OTP）。三色屏（如 GDEH0213Z19）用 2 平面，
// 单色屏用 1 平面（planeCount 决定是否写 0x13 彩色通道）。

#pragma once

#include <stdint.h>

#include "epd_driver.h"

class Uc8151d : public EpdSpiBase {
public:
  Uc8151d(uint16_t w, uint16_t h, uint8_t planeCount, bool invertPlane1,
          int8_t cs, int8_t dc, int8_t rst, int8_t busy,
          uint32_t spi_freq = 4000000UL);
};
