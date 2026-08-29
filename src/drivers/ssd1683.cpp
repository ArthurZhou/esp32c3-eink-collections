// drivers/ssd1683.cpp — SSD1683 驱动实现（MT-DEPG0420BNS830F20 / GDEY042T81 400×300）。
//
// 本项目的 4.2" 面板（DEPG0420BNS830F20）数据手册明确：DRIVER IC = SSD1683，
// BUSY 高=忙（“When Busy is High... command should not be sent”）。
//
// 与 SSD1619 的关键差异：SSD1683 使用内部 OTP 波形，不需要写 70 字节自定义
// LUT；显存 0x24=当前/0x26=上一帧；刷新用 0x22 不同值区分全刷/局刷。
// 刷新：全刷 = 0x21=0x40,0x00; 0x22=0xF7; 0x20（参考 GxEPD2 GDEY042T81）。

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
  pinMode(_busy, INPUT_PULLUP);   // SSD1683 BUSY：高=忙、低=就绪（面板驱动）

  logf("[SSD1683] init %ux%u planes=%u cs=%d dc=%d rst=%d busy=%d spi=%luHz (BUSY 高=忙)\n",
       _w, _h, _planes, _cs, _dc, _rst, _busy, (unsigned long)_freq);

  // 硬复位；复位后 BUSY 应拉高（忙）再回落低（就绪）。一直低 → 未响应/未上电。
  digitalWrite(_rst, LOW);
  delay(10);
  digitalWrite(_rst, HIGH);
  delay(10);
  if (digitalRead(_busy) == HIGH)
    logf("[SSD1683] reset: 复位后 BUSY=高（忙），等待回落…\n");
  else
    logf("[SSD1683] reset: 复位后 BUSY=低（就绪/未响应），若刷新不出图则查供电/接线\n");
  _waitBusySsd("_Reset", 1000);          // 等 BUSY 由高回落（就绪）

  _writeCmd(0x12);                // SWRESET
  delay(10);                      // 规格要求 ≥10ms

  _writeCmd(0x01);                // Driver output / MUX = 门极行数
  _writeData((_h - 1) & 0xFF);
  _writeData(((_h - 1) >> 8) & 0x01);
  _writeData(0x00);

  _writeCmd(0x3C); _writeData(0x01);   // Border waveform → 固定电平
  _writeCmd(0x18); _writeData(0x80);   // 用内置温度传感器

  _probeBusyRaw();                // 采样 BUSY 原始电平，判断悬空/卡死

  _setRamArea();                  // 窗口 0..(w/8-1) × 0..(h-1)，指针复位
  _powerOn();                     // 开启时钟 + DC/DC 升压（生成驱动电压）
  logf("[SSD1683] init done: 窗口 X=0..%u, Y=0..%u\n", _w / 8 - 1, _h - 1);
}

// 采样 BUSY 原始电平 2s，报告跳变次数与最终电平（SSD1683 高=忙）：
//   · 几乎无跳变且恒低   → 就绪（空闲），正常
//   · 几乎无跳变且恒高   → 面板卡忙/未回落/悬空被拉高
//   · 频繁跳变           → 悬空漂移或面板在动（异常）
void Ssd1683::_probeBusyRaw() {
  uint32_t t0 = millis();
  int last = -1;
  uint16_t transitions = 0;
  uint32_t samples = 0;
  while (millis() - t0 < 2000) {
    int v = digitalRead(_busy);
    samples++;
    if (last >= 0 && v != last) transitions++;
    last = v;
    delay(10);
  }
  logf("[SSD1683] busy 原始电平采样 2s: %u 次采样, %u 次跳变, 当前=%d\n",
       samples, transitions, last);
}

void Ssd1683::display(const uint8_t* plane0, const uint8_t* /*plane1*/) {
  // 全刷：当前帧写 0x24，上一帧写 0x26（保持一致，便于后续局刷差分）。
  _setRamArea();
  _writeCmd(0x24);
  _writeData(plane0, _w * _h / 8);
  _writeCmd(0x26);
  _writeData(plane0, _w * _h / 8);

  _powerOn();                     // 刷新前确保 DC/DC 已开启（生成驱动电压）
  _Update(0x40, 0xF7, "_Update_Full", 1200);   // 全刷（0x21=0x40,0x00; 0x22=0xF7）
}

void Ssd1683::displayPartial(const uint8_t* plane0, const uint8_t* /*plane1*/) {
  // 局刷：写当前帧 + 上一帧相同，快速差分刷新（0x22=0xFC），用于软件灰阶叠加。
  _setRamArea();
  _writeCmd(0x24);
  _writeData(plane0, _w * _h / 8);
  _writeCmd(0x26);
  _writeData(plane0, _w * _h / 8);

  _powerOn();                     // 刷新前确保 DC/DC 已开启
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
// 开启振荡器时钟 + DC/DC 升压，生成面板驱动电压（GxEPD2 SSD1683 的 _PowerOn）。
void Ssd1683::_powerOn() {
  _writeCmd(0x22); _writeData(0xE0);
  _writeCmd(0x20);
  _waitBusySsd("_PowerOn", 5000);
}

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
  while (digitalRead(_busy) == HIGH) {      // SSD1683：HIGH=忙，等回落低=就绪
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
