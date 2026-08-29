// drivers/ssd1619.cpp — SSD1619 驱动实现（Heltec DEPG0420BNS19 官方 EPAPER.c 时序）。
//
// 时序逐条对照面板官方驱动（EPAPER.c，见 C:\Users\AZ\Documents\DEPG0420BxS19AFxX）：
//   Epaper_Init        → init()
//   Epaper_Load_Image  → 写 0x4E/0x4F 指针 + 0x24 黑白显存
//   Epaper_Update      → _update()（0x22=0xC7 + 0x20 主激活）
//   Epaper_READBUSY    → _waitBusyHigh（HIGH=忙，等回低=就绪）
//
// 关键事实（对照官方 EPAPER.c）：
//   · BUSY 极性 = 高=忙、低=就绪，官方用【下拉】(IPD)。本驱动用 INPUT_PULLDOWN，
//     空闲读低=就绪，刷新时面板拉高=忙。
//   · 官方【不写自定义 LUT】——直接用面板内部 OTP 波形（0x22=0xB1 加载），
//     因此无需 0x32 写 70 字节波形。
//   · init 只设 0x74/0x7E/0x01/0x11/0x44/0x45/0x3C/0x18，无额外门/源电压。

#include "ssd1619.h"

#include <Arduino.h>

#include "log.h"

Ssd1619::Ssd1619(uint16_t w, uint16_t h, uint8_t planeCount, bool invertPlane1,
                 int8_t cs, int8_t dc, int8_t rst, int8_t busy,
                 uint32_t spi_freq)
    : EpdSpiBase(w, h, planeCount, invertPlane1,
                 /*panelByte=*/0x00,  // SSD16xx 无 0x00 Panel Setting，占位不用
                 cs, dc, rst, busy, spi_freq) {}

void Ssd1619::init() {
  pinMode(_cs, OUTPUT);
  digitalWrite(_cs, HIGH);
  pinMode(_dc, OUTPUT);
  digitalWrite(_dc, LOW);
  pinMode(_rst, OUTPUT);
  digitalWrite(_rst, HIGH);
  pinMode(_busy, INPUT_PULLDOWN);   // BUSY 高=忙、低=就绪；官方用下拉(IPD)

  logf("[SSD1619] init %ux%u planes=%u cs=%d dc=%d rst=%d busy=%d spi=%luHz (BUSY 高=忙, 下拉)\n",
       _w, _h, _planes, _cs, _dc, _rst, _busy, (unsigned long)_freq);

  // 硬复位（官方：EN=1 供电 + 10ms 低 + 10ms 高，然后等 BUSY 回低=就绪）
  digitalWrite(_rst, LOW);
  delay(10);
  digitalWrite(_rst, HIGH);
  delay(10);
  if (digitalRead(_busy) == HIGH)
    logf("[SSD1619] reset: 复位后 BUSY=高（忙），等待回落…\n");
  else
    logf("[SSD1619] reset: 复位后 BUSY=低（就绪/未响应），若刷新不出图则查供电/接线\n");
  _waitBusyHigh("_Reset", 1000);          // 等 BUSY 由高回落（就绪）

  _writeCmd(0x12);                        // 软复位
  _waitBusyHigh("_SWReset", 1000);

  _writeCmd(0x74); _writeData(0x54);      // Analog block control
  _writeCmd(0x7E); _writeData(0x3B);      // Digital block control

  _writeCmd(0x01);                        // Driver output：MUX=行数
  _writeData((_h - 1) & 0xFF);
  _writeData(((_h - 1) >> 8) & 0x01);
  _writeData(0x00);

  _writeCmd(0x11); _writeData(0x01);      // Data entry：X 增、Y 减

  _writeCmd(0x44);                        // RAM X start/end（0..49）
  _writeData(0x00);
  _writeData(_w / 8 - 1);
  _writeCmd(0x45);                        // RAM Y start/end（299..0）
  _writeData((_h - 1) & 0xFF);
  _writeData(((_h - 1) >> 8) & 0x01);
  _writeData(0x00);
  _writeData(0x00);

  _writeCmd(0x3C); _writeData(0x01);      // Border waveform

  _writeCmd(0x18); _writeData(0x80);      // 用内部温度传感器
  _writeCmd(0x22); _writeData(0xB1);      // 加载温度波形（OTP）
  _writeCmd(0x20);                        // 主激活执行加载
  _waitBusyHigh("_LoadTemp", 5000);

  _setCursor();                           // 数据指针回到窗口起点
  logf("[SSD1619] init done: 窗口 X=0..%u, Y=%u..0（官方 EPAPER.c 时序，OTP 波形）\n",
       _w / 8 - 1, _h - 1);
}

void Ssd1619::display(const uint8_t* plane0, const uint8_t* /*plane1*/) {
  // 官方 Epaper_Load_Image：写指针 + 0x24 黑白显存，再 Epaper_Update 全刷。
  _setCursor();
  _writeCmd(0x24);                        // 黑白显存（官方只写 0x24）
  _writeData(plane0, _w * _h / 8);
  _update("_Update_Full");
}

void Ssd1619::displayPartial(const uint8_t* plane0, const uint8_t* /*plane1*/) {
  // 官方例程未提供局刷波形；先以全刷兜底（保证能显示，灰阶后续再优化）。
  _setCursor();
  _writeCmd(0x24);
  _writeData(plane0, _w * _h / 8);
  _update("_Update_Full");
}

void Ssd1619::sleep() {
  _writeCmd(0x10);                        // Deep sleep mode 1
  _writeData(0x01);                       // 唤醒需硬件复位（init 会做）
}

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------
void Ssd1619::_waitBusyHigh(const char* what, uint32_t timeout_ms) {
  uint32_t start = millis();
  while (digitalRead(_busy) == HIGH) {    // SSD1619：HIGH=忙，等回低=就绪
    if (millis() - start > timeout_ms) {
      logf("[SSD1619] %s: BUSY 超时(%lums)，当前电平=%d -> 面板未响应/未上电/接线错\n",
           what, (unsigned long)(millis() - start), digitalRead(_busy));
      return;
    }
    delay(10);
  }
  // 打印实际忙时长：全刷应达秒级；若恒为 0ms 说明芯片根本没动 → 查通信。
  logf("[SSD1619] %s: busy %lums\n",
       what, (unsigned long)(millis() - start));
  delay(200);                             // 官方在 BUSY 释放后再稳
}

void Ssd1619::_setCursor() {
  const uint16_t yTop = _h - 1;
  _writeCmd(0x4E);                        // RAM X address counter
  _writeData(0x00);
  _writeCmd(0x4F);                        // RAM Y address counter → 窗口起点
  _writeData(yTop & 0xFF);
  _writeData(yTop >> 8);
}

void Ssd1619::_update(const char* what) {
  _writeCmd(0x22); _writeData(0xC7);      // 官方 Epaper_Update：0x22=0xC7
  _writeCmd(0x20);                        // 主激活
  _waitBusyHigh(what, 15000);
}
