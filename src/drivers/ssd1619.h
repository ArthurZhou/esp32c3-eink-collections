// drivers/ssd1619.h — SSD1619 驱动 IC（SSD16xx 经典指令集）。
//
// 注意：SSD1619 与 UC8151D 指令集【不】兼容！关键差异（对照板厂例程
// EL5104 / GoodDisplay EPD_W21 demo，400×300 实测时序）：
//   1) 显存命令：黑白 = 0x24、第二显存 = 0x26（UC8151D 是 0x10 / 0x13）；
//   2) 刷新流程：写 0x22=0xC7 后发 0x20 主激活（UC8151D 是直接发 0x12）；
//   3) BUSY 极性：LOW=忙、HIGH=空闲（与本板 UC8151D 接法一致，共用一个
//      FPC 连接器/BUSY 引脚）；注意不是 HIGH=忙。
//   4) 无 0x00 Panel Setting，需逐项配置 MUX(0x01)/扫描方向/窗口(0x44/45)
//      /边界(0x3C)/门源电压(0x03/0x04)/温度波形加载(0x22=0xB1 + 0x20)。
//
// 因此不复用 EpdSpiBase 的 UC8151D 时序，只继承其底层 SPI 收发与引脚管理，
// init/display/displayPartial/sleep 全部按 SSD16xx 流程覆写。
//
// 本项目的 4.2" 黑白屏用它：单色 1 平面；支持局刷承载软件灰阶
// （每轮局刷只驱动与上一帧相比变化了的像素，多轮叠加成灰阶，见 kLutPart）。

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
  void _waitBusyLow(const char* what, uint32_t timeout_ms);   // LOW=忙
  void _setCursor();                    // 数据指针回到窗口起点 (0, h-1)
  void _loadLut(const uint8_t* lut70);  // 经 0x32 写 70 字节波形
  void _update(const char* what);       // 0x22=0xC7 + 0x20 主激活
};
