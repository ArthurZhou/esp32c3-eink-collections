// drivers/pditc.h — Pervasive Displays iTC 驱动 IC（E2213JS0C1 等小尺寸 Spectra 屏）。
//
// 指令集与 UC8151D/SSD1619 同族（0x00/0x10/0x13/0x12/0x04/0x02/0x07），
// 共享 EpdSpiBase。与 UC8151D 的关键差异：
//   1) panel 字节 = 0xCF（B/W/R，LUT from OTP，扫描方向按 PDI 应用笔记
//      "Other Size"（含 2.13"）推荐值；3.7"/4.2"/4.37" 用 0x0F,0x89 两字节）；
//   2) 0x50 VCOM/数据间隔需写两字节 0x89,0x07；
//   3) BUSY 极性相反：LOW=空闲，HIGH=忙（BUSY_N，上拉输入读出为高=忙）。
//
// 时序参考：PDI《Application Note for small size Spectra EPD with iTC (OTP LUT)》
// v06 (2022/06/06) 第 17~22 页驱动流程。

#pragma once

#include <stdint.h>

#include "epd_driver.h"

class Pditc : public EpdSpiBase {
public:
  // planeCount 固定 2：iTC 全刷必须先后送 0x10 黑白帧 + 0x13 红色帧。
  Pditc(uint16_t w, uint16_t h, bool invertPlane1,
          int8_t cs, int8_t dc, int8_t rst, int8_t busy,
          uint32_t spi_freq = 4000000UL);

  void init() override;
  bool partialSupported() const override { return false; }

protected:
  using EpdSpiBase::_waitBusy;
  void _waitBusy(const char* what, uint32_t timeout_ms);  // BUSY 极性相反
};
