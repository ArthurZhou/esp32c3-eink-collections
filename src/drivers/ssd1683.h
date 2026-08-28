// drivers/ssd1683.h — SSD1683 驱动（400×300，GDEY042T81 / GoodDisplay EPD_W21 时序）。
//
// SSD1683 与 SSD1619/UC8151D 同属 SSD16xx 家族，但初始化更简单：用内部
// OTP 波形，无需像 SSD1619 那样写 70 字节自定义 LUT（0x32）。刷新：
//   全刷 = 写 0x21=0x40,0x00 → 0x22=0xF7 → 0x20 主激活
//   局刷 = 写 0x21=0x00,0x00 → 0x22=0xFC → 0x20 主激活
// 显存：0x24=当前 BW，0x26=上一帧/前 BW（局刷差分用）。
//
// BUSY 极性：本板实测为 LOW=忙、HIGH=空闲（与 UC8151D 共用连接器/BUSY 引脚），
// 因此复用一个 INPUT_PULLUP + 等 LOW 的流程（同 EpdSpiBase::_waitBusy 语义）。
//
// 参考：GxEPD2 GxEPD2_420_GDEY042T81（SSD1683, 400×300）。
//   init : 0x12 SWR → 0x01 MUX=300 → 0x3C=0x01 → 0x18=0x80 → 设 RAM 窗口
//   full : 0x21=0x40,0x00; 0x22=0xF7; 0x20
//   part : 0x21=0x00,0x00; 0x22=0xFC; 0x20
//   sleep: 0x22=0x83;0x20; 0x10=0x01 deep sleep

#pragma once

#include <stdint.h>

#include "epd_driver.h"

class Ssd1683 : public EpdSpiBase {
public:
  Ssd1683(uint16_t w, uint16_t h, uint8_t planeCount, bool invertPlane1,
          int8_t cs, int8_t dc, int8_t rst, int8_t busy,
          uint32_t spi_freq = 4000000UL);

  void init() override;
  void display(const uint8_t* plane0, const uint8_t* plane1) override;
  void displayPartial(const uint8_t* plane0, const uint8_t* plane1) override;
  void sleep() override;
  // clear() 沿用基类实现（内部走本类覆写的 display()）；
  // bufferLength()/partialSupported() 同样沿用基类。

private:
  void _setRamArea();                              // 0x11 入口模式 + 0x44/0x45 窗口 + 指针复位
  void _waitBusySsd(const char* what, uint32_t timeout_ms);  // LOW=忙
  void _Update(uint8_t ctrl21, uint8_t ctrl22,
               const char* what, uint32_t timeout_ms);       // 0x21 + 0x22 + 0x20 主激活
};
