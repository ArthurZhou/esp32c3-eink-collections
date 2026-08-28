// drivers/epd_driver.cpp — EpdSpiBase 共享实现（UC8151D / SSD1619 家族）。
//
// 寄存器时序参考：板厂 MicroPython 驱动 epd.py（全刷）与
// GxEPD2 GDEH0213Z19（局刷 B/W 配方）。BUSY 极性：LOW=busy, HIGH=idle。

#include "epd_driver.h"

#include <Arduino.h>
#include <SPI.h>

EpdSpiBase::EpdSpiBase(uint16_t w, uint16_t h, uint8_t planeCount,
                       bool invertPlane1, uint8_t panelByte,
                       int8_t cs, int8_t dc, int8_t rst, int8_t busy,
                       uint32_t spi_freq)
    : _w(w), _h(h), _planes(planeCount), _invertPlane1(invertPlane1),
      _panelByte(panelByte), _cs(cs), _dc(dc), _rst(rst), _busy(busy),
      _freq(spi_freq) {}

void EpdSpiBase::init() {
  pinMode(_cs, OUTPUT);
  digitalWrite(_cs, HIGH);
  pinMode(_dc, OUTPUT);
  digitalWrite(_dc, LOW);
  pinMode(_rst, OUTPUT);
  digitalWrite(_rst, HIGH);
  pinMode(_busy, INPUT_PULLUP);

  _reset();

  // 1) Panel setting（UC8151D=0x0F BWR，SSD1619=0x1F B/W，均 LUT from OTP, up scan）
  _writeCmd(0x00);
  _writeData(_panelByte);

  // 2) 显存窗口 — 由分辨率推导，保证与 framebuffer 一一对应
  uint16_t xEnd = _w / 8 - 1;
  uint16_t yEnd = _h - 1;
  _writeCmd(0x44);            // Set RAM X start/end
  _writeData(0x00);
  _writeData(xEnd & 0xFF);
  _writeCmd(0x45);            // Set RAM Y start/end
  _writeData(0x00);
  _writeData(0x00);
  _writeData(yEnd & 0xFF);
  _writeData((yEnd >> 8) & 0x01);

  // 3) 数据指针复位
  _writeCmd(0x4E);
  _writeData(0x00);
  _writeCmd(0x4F);
  _writeData(0x00);
  _writeData(0x00);

  // 4) VCOM and data interval + power on
  _writeCmd(0x50);
  _writeData(0x11);
  _writeData(0x07);
  _writeCmd(0x04);            // Power ON
  _waitBusy("_PowerOn", 5000);
}

void EpdSpiBase::display(const uint8_t* plane0, const uint8_t* plane1) {
  // 全刷前恢复 panel 设置（局刷会把 panel 切成 0xBF，见 displayPartial）
  _writeCmd(0x00);
  _writeData(_panelByte);

  _setPointer();
  _writePlane0(plane0);

  // 彩色平面（2 平面屏且给到数据才写；面板要求反相则取反）
  if (_planes >= 2 && plane1) _writePlane1(plane1);

  // Display refresh
  _writeCmd(0x12);
  delay(10);
  _waitBusy("_Update_Full", 10000);
}

void EpdSpiBase::displayPartial(const uint8_t* plane0, const uint8_t* /*plane1*/) {
  // ---- B/W 局刷（仅 0x10 黑白通道），配方参考 GxEPD2 GDEH0213Z19 ----
  // 把 5 组局刷 LUT 写入寄存器 0x20..0x24，panel 设为"LUT from registers, B/W"，
  // 局刷只驱动与上次刷新相比"变化"的像素 → 黑像素一旦写入就保留，
  // 多轮叠加即可合成软件灰阶（GUIDE"多次刷新叠加"）。

  // 1) panel: LUT from registers, B/W（up scan）
  _writeCmd(0x00);
  _writeData(0xBF);

  // 2) VCOM and data interval（WBR 模式）
  _writeCmd(0x50);
  _writeData(0xF7);

  // 3) 载入局刷 LUT（6 字节有效 + 补 0；vcom 补齐 44，其余 42）
  _loadPartialLut();

  // 4) 上电
  _writeCmd(0x04);
  _waitBusy("_PowerOn", 5000);

  // 5) 数据指针 + 只写黑白通道
  _setPointer();
  _writePlane0(plane0);

  // 6) partial in → refresh → partial out
  _writeCmd(0x91);
  _writeCmd(0x12);
  delay(10);
  _waitBusy("_Update_Part", 10000);
  _writeCmd(0x92);

  // 7) 恢复全刷 panel 设置，供下一次全刷使用
  _writeCmd(0x00);
  _writeData(_panelByte);
}

void EpdSpiBase::clear() {
  // 最大面板 400×300 = 15000 B/平面，VLA 会溢出 8KB 栈 → 用堆。
  const uint16_t len = _w * _h / 8;
  uint8_t* white = new uint8_t[len];
  uint8_t* clear1 = new uint8_t[len];
  memset(white, 0xFF, len);
  memset(clear1, 0x00, len);
  display(white, _planes >= 2 ? clear1 : nullptr);
  delete[] white;
  delete[] clear1;
}

void EpdSpiBase::sleep() {
  _writeCmd(0x50);
  _writeData(0xF7);
  _writeCmd(0x02);            // Power OFF
  _waitBusy("_PowerOff", 5000);
  _writeCmd(0x07);            // Deep sleep
  _writeData(0xA5);
}

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------
void EpdSpiBase::_writeCmd(uint8_t c) {
  SPI.beginTransaction(SPISettings(_freq, MSBFIRST, SPI_MODE0));
  digitalWrite(_dc, LOW);
  digitalWrite(_cs, LOW);
  SPI.transfer(c);
  digitalWrite(_cs, HIGH);
  digitalWrite(_dc, HIGH);
  SPI.endTransaction();
}

void EpdSpiBase::_writeData(uint8_t d) {
  SPI.beginTransaction(SPISettings(_freq, MSBFIRST, SPI_MODE0));
  digitalWrite(_cs, LOW);
  SPI.transfer(d);
  digitalWrite(_cs, HIGH);
  SPI.endTransaction();
}

void EpdSpiBase::_writeData(const uint8_t* buf, uint16_t n) {
  SPI.beginTransaction(SPISettings(_freq, MSBFIRST, SPI_MODE0));
  digitalWrite(_cs, LOW);
  for (uint16_t i = 0; i < n; i++) SPI.transfer(buf[i]);
  digitalWrite(_cs, HIGH);
  SPI.endTransaction();
}

void EpdSpiBase::_waitBusy(const char* what, uint32_t timeout_ms) {
  uint32_t start = millis();
  while (digitalRead(_busy) == LOW) {  // LOW = busy（经典 UC8151D 极性）
    if (millis() - start > timeout_ms) {
      Serial.print("Busy Timeout! ");
      Serial.println(what);
      return;
    }
    delay(10);
  }
}

void EpdSpiBase::_reset() {
  digitalWrite(_rst, LOW);
  delay(50);
  digitalWrite(_rst, HIGH);
  delay(50);
  _waitBusy("_Reset", 2000);
}

void EpdSpiBase::_powerOn() {
  _writeCmd(0x04);
  _waitBusy("_PowerOn", 5000);
}

void EpdSpiBase::_setPointer() {
  _writeCmd(0x4E);
  _writeData(0x00);
  _writeCmd(0x4F);
  _writeData(0x00);
  _writeData(0x00);
}

void EpdSpiBase::_writePlane0(const uint8_t* plane0) {
  _writeCmd(0x10);   // 黑白平面：位1=白，位0=黑
  _writeData(plane0, _w * _h / 8);
}

void EpdSpiBase::_writePlane1(const uint8_t* plane1) {
  _writeCmd(0x13);   // 彩色平面：位1=有色（红 / 黄 / 视面板 LUT）
  if (_invertPlane1) {
    for (uint16_t i = 0; i < _w * _h / 8; i++) _writeData(~plane1[i]);
  } else {
    _writeData(plane1, _w * _h / 8);
  }
}

void EpdSpiBase::_loadPartialLut() {
  // 局刷 LUT（GxEPD2 GDEH0213Z19 配方，UC8151D/SSD1619 家族通用）
  // 格式 {电压, T1..T4, repeat}；vcom 补齐 44 字节，其余补齐 42 字节，多余补 0。
  static const uint8_t vcom[44] = { 0x00, 0x1F, 0x01, 0x00, 0x00, 0x01 };
  static const uint8_t ww[42]   = { 0x00, 0x1F, 0x01, 0x00, 0x00, 0x01 };
  static const uint8_t bw[42]   = { 0x80, 0x1F, 0x01, 0x00, 0x00, 0x01 };
  static const uint8_t wb[42]   = { 0x40, 0x1F, 0x01, 0x00, 0x00, 0x01 };
  static const uint8_t bb[42]   = { 0x00, 0x1F, 0x01, 0x00, 0x00, 0x01 };
  _writeCmd(0x20); _writeData(vcom, sizeof(vcom));
  _writeCmd(0x21); _writeData(ww,   sizeof(ww));
  _writeCmd(0x22); _writeData(bw,   sizeof(bw));
  _writeCmd(0x23); _writeData(wb,   sizeof(wb));
  _writeCmd(0x24); _writeData(bb,   sizeof(bb));
}
