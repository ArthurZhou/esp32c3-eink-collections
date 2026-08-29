// drivers/ssd1619.h — SSD1619 驱动 IC（SSD16xx 经典指令集）。
//
// 注意：SSD1619 与 UC8151D 指令集【不】兼容！关键差异（对照板厂例程
// EL5104 / GoodDisplay EPD_W21 demo，400×300 实测时序）：
//   1) 显存命令：黑白 = 0x24（UC8151D 是 0x10）；
//   2) 刷新流程：写 0x22=0xC7 后发 0x20 主激活（UC8151D 是直接发 0x12）；
//   3) BUSY 极性：高=忙、低=就绪，且需【下拉】(IPD)——官方 EPAPER.c 用
//      GPIO_Mode_IPD。本驱动用 INPUT_PULLDOWN，空闲读低=就绪。
//   4) 无 0x00 Panel Setting，需逐项配置 MUX(0x01)/扫描方向/窗口(0x44/45)
//      /边界(0x3C)/温度波形加载(0x22=0xB1 + 0x20)。
//   5) 官方【不写自定义 LUT】，直接用内部 OTP 波形（本驱动也不写 0x32）。
//
// 因此不复用 EpdSpiBase 的 UC8151D 时序，只继承其底层 SPI 收发与引脚管理，
// init/display/displayPartial/sleep 全部按官方 EPAPER.c 流程覆写。
//
// 本项目的 4.2" 黑白屏（DEPG0420BNS19）用它：单色 1 平面；官方无局刷波形，
// displayPartial 暂以全刷兜底。

#pragma once

#include <stdint.h>

#include "epd_driver.h"

class Ssd1619 : public EpdSpiBase {
public:
  Ssd1619(uint16_t w, uint16_t h, uint8_t planeCount, bool invertPlane1,
          int8_t cs, int8_t dc, int8_t rst, int8_t busy,
          uint32_t spi_freq = 4000000UL);

  void init() override;
  void display(const uint8_t* plane0, const uint8_t* plane1) override;
  void displayPartial(const uint8_t* plane0, const uint8_t* plane1) override;
  void sleep() override;
  // clear() 沿用基类实现（内部走本类覆写的 display()），
  // bufferLength()/partialSupported() 同样沿用基类。

private:
  void _waitBusyHigh(const char* what, uint32_t timeout_ms);  // HIGH=忙
  void _setCursor();                    // 数据指针回到窗口起点 (0, h-1)
  void _update(const char* what);       // 0x22=0xC7 + 0x20 主激活
};
