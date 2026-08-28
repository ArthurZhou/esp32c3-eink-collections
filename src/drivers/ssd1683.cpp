// drivers/ssd1683.cpp — SSD1683 驱动实现（GDEY042T81 400×300，GxEPD2 参考时序）。
//
// 与 SSD1619 的关键差异：SSD1683 使用内部 OTP 波形，不需要写 70 字节自定义
// LUT；显存 0x24=当前/0x26=上一帧；刷新用 0x22 不同值区分全刷/局刷。

#include "ssd1683.h"

#include <Arduino.h>
#include <string.h>   // memset

#include "log.h"

Ssd1683::Ssd1683(uint16_t w, uint16_t h, uint8_t planeCount, bool invertPlane1,
                 int8_t cs, int8_t dc, int8_t rst, int8_t busy,
                 uint32_t spi_freq)
    : EpdSpiBase(w, h, planeCount, invertPlane1,
                 /*panelByte=*/0x00,  // SSD1683 无 0x00 Panel Setting，占位不用
                 cs, dc, rst, busy, spi_freq) {}

void Ssd1683::init() {
  pinMode(_cs, OUTPUT);
  digitalWrite(_cs, HIGH);
  pinMode(_dc, OUTPUT);
  digitalWrite(_dc, LOW);
  pinMode(_rst, OUTPUT);
  digitalWrite(_rst, HIGH);
  pinMode(_busy, INPUT_PULLUP);   // 本板 BUSY 低=忙；空闲=高（内部上拉）

  logf("[SSD1683] init %ux%u planes=%u cs=%d dc=%d rst=%d busy=%d spi=%luHz (BUSY 低=忙)\n",
       _w, _h, _planes, _cs, _dc, _rst, _busy, (unsigned long)_freq);

  // 硬复位；复位后面板应把 BUSY 拉回高（就绪）。一直低 → 未响应/未上电。
  digitalWrite(_rst, LOW);
  delay(100);
  digitalWrite(_rst, HIGH);
  uint32_t t0 = millis();
  while (digitalRead(_busy) == LOW && millis() - t0 < 300) {}
  if (digitalRead(_busy) == HIGH)
    logf("[SSD1683] reset: BUSY %lums 内回到高电平（就绪）-> 面板有响应\n",
         (unsigned long)(millis() - t0));
  else
    logf("[SSD1683] reset: BUSY 持续低电平>300ms -> 面板未响应/未上电/接线错\n");
  delay(20);

  _writeCmd(0x12);                // SWRESET
  delay(10);                      // 规格要求 ≥10ms

  _writeCmd(0x01);                // Driver output / MUX = 门极行数
  _writeData((_h - 1) & 0xFF);
  _writeData(((_h - 1) >> 8) & 0x01);
  _writeData(0x00);

  _writeCmd(0x3C); _writeData(0x01);   // Border waveform → 固定电平
  _writeCmd(0x18); _writeData(0x80);   // 用内置温度传感器

  _setRamArea();                  // 窗口 0..(w/8-1) × 0..(h-1)，指针复位
  logf("[SSD1683] init done: 窗口 X=0..%u, Y=0..%u\n", _w / 8 - 1, _h - 1);
}

void Ssd1683::display(const uint8_t* plane0, const uint8_t* /*plane1*/) {
  // 全刷：当前帧写 0x24，上一帧写 0x26（保持一致，便于后续局刷差分）。
  _setRamArea();
  _writeCmd(0x24);
  _writeData(plane0, _w * _h / 8);
  _writeCmd(0x26);
  _writeData(plane0, _w * _h / 8);

  _Update(0x40, 0xF7, "_Update_Full", 1200);   // 全刷（0x21=0x40,0x00; 0x22=0xF7）
}

void Ssd1683::displayPartial(const uint8_t* plane0, const uint8_t* /*plane1*/) {
  // 局刷：写当前帧 + 上一帧相同，快速差分刷新（0x22=0xFC），用于软件灰阶叠加。
  _setRamArea();
  _writeCmd(0x24);
  _writeData(plane0, _w * _h / 8);
  _writeCmd(0x26);
  _writeData(plane0, _w * _h / 8);

  _Update(0x00, 0xFC, "_Update_Part", 400);
}

void Ssd1683::sleep() {
  _writeCmd(0x22); _writeData(0x83);   // 下电
  _writeCmd(0x20);
  _waitBusySsd("_PowerOff", 300);
  _writeCmd(0x10); _writeData(0x01);   // Deep sleep（唤醒需硬复位，init 会做）
}

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------
void Ssd1683::_setRamArea() {
  _writeCmd(0x11); _writeData(0x03);        // 数据入口：X 增、Y 增（正常模式）
  const uint8_t xEnd = _w / 8 - 1;          // 49 @ 400px
  _writeCmd(0x44);                          // RAM X start/end
  _writeData(0x00);
  _writeData(xEnd);
  _writeCmd(0x45);                          // RAM Y start/end = 0 .. h-1
  _writeData(0x00);
  _writeData(0x00);
  _writeData((_h - 1) & 0xFF);
  _writeData(((_h - 1) >> 8) & 0x01);
  _writeCmd(0x4E); _writeData(0x00);        // X 指针复位
  _writeCmd(0x4F); _writeData(0x00); _writeData(0x00);  // Y 指针复位
}

void Ssd1683::_waitBusySsd(const char* what, uint32_t timeout_ms) {
  uint32_t start = millis();
  while (digitalRead(_busy) == LOW) {       // 本板 LOW=忙
    if (millis() - start > timeout_ms) {
      logf("[SSD1683] %s: BUSY 超时(%lums)，当前电平=%d -> 面板未响应/未上电/接线错\n",
           what, (unsigned long)(millis() - start), digitalRead(_busy));
      return;
    }
    delay(10);
  }
  logf("[SSD1683] %s: busy %lums\n", what, (unsigned long)(millis() - start));
  delay(200);                               // 例程在 BUSY 释放后再稳 200ms
}

void Ssd1683::_Update(uint8_t ctrl21, uint8_t ctrl22,
                      const char* what, uint32_t timeout_ms) {
  _writeCmd(0x21);                          // Display Update Control
  _writeData(ctrl21);
  _writeData(0x00);
  _writeCmd(0x22);                          // Display Update Sequence Options
  _writeData(ctrl22);
  _writeCmd(0x20);                          // DRF 主激活
  _waitBusySsd(what, timeout_ms);
}
