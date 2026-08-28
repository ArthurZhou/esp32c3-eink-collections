// drivers/epd_driver.h — IC 驱动层：抽象接口 + 共享 SPI 底层。
//
// GUIDE：IC 驱动层是"纯粹的硬件命令翻译与发送器，不参与任何色阶算法或
// 图像处理逻辑"。它只接收来自 Screen 层的标准化抽象命令（设显存区域、
// 写打包好的像素数据、触发刷新、等待状态恢复）。
// 因此驱动不依赖屏幕类型（不 include screen_types.h），只接收裸尺寸 / 平面数。
//
// UC8151D 与 SSD1619 指令集高度兼容（同为 0x00/0x10/0x13/0x12/0x50 系，
// 0x10=黑白平面、0x13=彩色平面、0x12=刷新、0x04=上电、0x02/0x07=下电），
// 仅默认 panel 字节不同（UC8151D=0x0F BWR、SSD1619=0x1F B/W）。
// 共享实现放 EpdSpiBase，各芯片类只声明自己的 panel 字节。

#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// 抽象 IC 接口
// ---------------------------------------------------------------------------
class EpdDriver {
public:
  virtual ~EpdDriver() {}

  // 上电 + 初始化面板。必须在 SPI.begin() 之后调用。
  virtual void init() = 0;

  // 送两路位平面并触发全刷。
  // plane0/plane1 各 bufferLength() 字节；1 平面屏 plane1 传 nullptr。
  virtual void display(const uint8_t* plane0, const uint8_t* plane1) = 0;

  // 局刷（partial refresh）：只驱动与上次刷新相比"变化"的像素，
  // 未变化的像素保留，用于软件灰阶的多轮叠加。plane1 忽略（B/W 路径）。
  virtual void displayPartial(const uint8_t* plane0, const uint8_t* plane1) = 0;

  // 清屏（白）+ 刷新。
  virtual void clear() = 0;

  // 下电 + 深度睡眠。
  virtual void sleep() = 0;

  // 单平面 framebuffer 字节数。
  virtual uint16_t bufferLength() const = 0;

  // 是否支持局刷（软件灰阶需要）。
  virtual bool partialSupported() const { return false; }
};

// ---------------------------------------------------------------------------
// 共享 SPI 实现基类（UC8151D / SSD1619 共用，见文件头注释）
// ---------------------------------------------------------------------------
class EpdSpiBase : public EpdDriver {
public:
  // w/h: 面板分辨率（px）；planeCount: 位平面数（1 或 2）；
  // invertPlane1: 0x13 彩色通道反相（面板相关）；panelByte: 0x00 panel 设置。
  EpdSpiBase(uint16_t w, uint16_t h, uint8_t planeCount, bool invertPlane1,
             uint8_t panelByte, int8_t cs, int8_t dc, int8_t rst, int8_t busy,
             uint32_t spi_freq = 4000000UL);

  void init() override;
  void display(const uint8_t* plane0, const uint8_t* plane1) override;
  void displayPartial(const uint8_t* plane0, const uint8_t* plane1) override;
  void clear() override;
  void sleep() override;
  uint16_t bufferLength() const override { return _w * _h / 8; }
  bool partialSupported() const override { return true; }

protected:
  uint16_t _w, _h;
  uint8_t  _planes;        // 1 或 2
  bool     _invertPlane1;
  uint8_t  _panelByte;     // 0x00 panel 设置（UC8151D=0x0F, SSD1619=0x1F）
  int8_t   _cs, _dc, _rst, _busy;
  uint32_t _freq;

  void _writeCmd(uint8_t c);
  void _writeData(uint8_t d);
  void _writeData(const uint8_t* buf, uint16_t n);
  void _waitBusy(const char* what, uint32_t timeout_ms);
  void _reset();
  void _powerOn();
  void _setPointer();
  void _loadPartialLut();   // 把局刷 LUT 写入寄存器 0x20..0x24
  void _writePlane0(const uint8_t* plane0);
  void _writePlane1(const uint8_t* plane1);
};
