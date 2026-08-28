// drivers/ssd1619.cpp — SSD1619 驱动实现（SSD16xx 经典指令集）。
//
// 寄存器时序逐条对照板厂例程（EL5104，GoodDisplay EPD_W21 demo，400×300）：
//   EPD_init      → init()
//   PIC_display   → _writeRam()（黑白 0x24 + 第二显存 0x26 清零）
//   updata_LUT_*  → _loadLut()（0x32 写 70 字节波形）
//   EPD_refresh   → _update()（0x22=0xC7 + 0x20 主激活）
//   lcd_chkstatus → _waitBusyLow（LOW=忙；释放后再稳 200ms）
//
// 注意：BUSY 极性实测为 LOW=忙、HIGH=空闲（与本板 UC8151D 接法一致，
// 共用一个 FPC 连接器/BUSY 引脚）。旧实现误写成 HIGH=忙，导致把空闲的
// 高电平当“一直忙”而全部超时——面板其实活着但从不真正等 BUSY。
//
// 与 UC8151D 的差异详见 ssd1619.h 头注释。

#include "ssd1619.h"

#include <Arduino.h>
#include <string.h>   // memset

#include "log.h"

namespace {

// 全刷波形（板厂 LUT_DATA_Full）：前 70 字节为相位/时序表，经 0x32 写入；
// 尾部 6 字节同时是门/源电压与 dummy/gateline 参数（init 里经 0x03/0x04/
// 0x3A/0x3B 逐一写入）。
const uint8_t kLutFull[70] = {
    0x80, 0x60, 0x40, 0x00, 0x00, 0x00, 0x00, // LUT0: BB:     VS 0 ~7
    0x10, 0x60, 0x20, 0x00, 0x00, 0x00, 0x00, // LUT1: BW:     VS 0 ~7
    0x80, 0x60, 0x40, 0x00, 0x00, 0x00, 0x00, // LUT2: WB:     VS 0 ~7
    0x10, 0x60, 0x20, 0x00, 0x00, 0x00, 0x00, // LUT3: WW:     VS 0 ~7
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT4: VCOM:   VS 0 ~7

    0x03, 0x03, 0x00, 0x00, 0x02,             // TP0 A~D RP0
    0x09, 0x09, 0x00, 0x00, 0x02,             // TP1 A~D RP1
    0x03, 0x03, 0x00, 0x00, 0x02,             // TP2 A~D RP2
    0x00, 0x00, 0x00, 0x00, 0x00,             // TP3 A~D RP3
    0x00, 0x00, 0x00, 0x00, 0x00,             // TP4 A~D RP4
    0x00, 0x00, 0x00, 0x00, 0x00,             // TP5 A~D RP5
    0x00, 0x00, 0x00, 0x00, 0x00,             // TP6 A~D RP6
};
// 尾部参数：VGH=0x15 | VSH=0x41,0xA8,0x32 | dummy=0x30 | gateline=0x0A
constexpr uint8_t kGateVoltage = 0x15;
constexpr uint8_t kSourceV[3]  = { 0x41, 0xA8, 0x32 };
constexpr uint8_t kDummyLine   = 0x30;
constexpr uint8_t kGateWidth   = 0x0A;

// 局刷波形（板厂 LUT_DATA_Gs）：只驱动与上一帧相比变化了的像素
// （BW 转换一轮短脉冲，其余转换不动），多轮叠加即可合成软件灰阶。
const uint8_t kLutPart[70] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT0: BB 不动
    0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT1: BW 变黑
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT2: WB 不动
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT3: WW 不动
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LUT4: VCOM

    0x00, 0x01, 0x00, 0x00, 0x01,             // TP0 A~D RP0
    0x00, 0x00, 0x00, 0x00, 0x00,             // TP1 A~D RP1
    0x00, 0x00, 0x00, 0x00, 0x00,             // TP2 A~D RP2
    0x00, 0x00, 0x00, 0x00, 0x00,             // TP3 A~D RP3
    0x00, 0x00, 0x00, 0x00, 0x00,             // TP4 A~D RP4
    0x00, 0x00, 0x00, 0x00, 0x00,             // TP5 A~D RP5
    0x00, 0x00, 0x00, 0x00, 0x00,             // TP6 A~D RP6
};

}  // namespace

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
  pinMode(_busy, INPUT_PULLUP);   // BUSY 低电平=忙；空闲=高（内部上拉，同 UC8151D）

  logf("[SSD1619] init %ux%u planes=%u cs=%d dc=%d rst=%d busy=%d spi=%luHz (BUSY 低=忙)\n",
       _w, _h, _planes, _cs, _dc, _rst, _busy,
       (unsigned long)_freq);
  logf("[SSD1619] SPI pins: SCK=%d MOSI=%d (CS=%d DC=%d RST=%d BUSY=%d)\n",
       -1, -1, _cs, _dc, _rst, _busy);

  // 硬复位；释放后探测 boot-BUSY 脉冲（SSD16xx 复位期间 BUSY 拉高）。
  // 这是“面板是否活着/接线是否对”的免费探针：
  //   · 出现脉冲并回落 → 面板与 BUSY 线正常
  //   · 一直低          → BUSY 线接错脚/接地，或面板未上电
  //   · 一直高(>300ms)  → BUSY 线悬空（仅内部上拉在拉高）
  digitalWrite(_rst, LOW);
  delay(100);
  digitalWrite(_rst, HIGH);

  // LOW=忙：复位后面板应把 BUSY 拉回高电平（就绪）。若一直为低 → 卡忙/未响应。
  uint32_t t0 = millis();
  while (digitalRead(_busy) == LOW && millis() - t0 < 300) {}
  if (digitalRead(_busy) == HIGH)
    logf("[SSD1619] reset: BUSY %lums 内回到高电平（就绪）-> 面板有响应\n",
         (unsigned long)(millis() - t0));
  else
    logf("[SSD1619] reset: BUSY 持续低电平>300ms -> 面板未响应/未上电/接线错\n");
  delay(20);

  // 软复位（软复位后必须等 BUSY 释放再继续配置）
  _writeCmd(0x12);                        // SWRESET
  _waitBusyLow("_SWReset", 2000);

  _writeCmd(0x74); _writeData(0x54);      // Analog block control
  _writeCmd(0x7E); _writeData(0x3B);      // Digital block control

  _writeCmd(0x2B);                        // 降低 ACVCOM 毛刺
  _writeData(0x04);
  _writeData(0x63);

  _writeCmd(0x0C);                        // Booster soft start
  _writeData(0x8B);
  _writeData(0x9C);
  _writeData(0x96);
  _writeData(0x0F);

  _writeCmd(0x01);                        // Driver output：MUX=门极行数，
  _writeData((_h - 1) & 0xFF);            // 第三字节 GD=1 与例程一致
  _writeData(((_h - 1) >> 8) & 0x01);
  _writeData(0x01);

  _writeCmd(0x11); _writeData(0x01);      // Data entry：X 向增、Y 向减

  const uint8_t  xEnd = _w / 8 - 1;       // (49+1)*8 = 400 @ 4.2"
  const uint16_t yTop = _h - 1;           // 例程窗口 Y 从底部向上递减
  _writeCmd(0x44);                        // RAM X start/end
  _writeData(0x00);
  _writeData(xEnd);
  _writeCmd(0x45);                        // RAM Y start/end（start=yTop, end=0）
  _writeData(yTop & 0xFF);
  _writeData(yTop >> 8);
  _writeData(0x00);
  _writeData(0x00);

  _writeCmd(0x3C); _writeData(0x01);      // Border waveform → HIZ

  _writeCmd(0x03); _writeData(kGateVoltage);        // Gate voltage
  _writeCmd(0x04);                                  // Source voltage ×3
  _writeData(kSourceV[0]);
  _writeData(kSourceV[1]);
  _writeData(kSourceV[2]);
  _writeCmd(0x3A); _writeData(kDummyLine);          // Dummy line period
  _writeCmd(0x3B); _writeData(kGateWidth);          // Gate line width

  _writeCmd(0x18); _writeData(0x80);      // 用内部温度传感器
  _writeCmd(0x22); _writeData(0xB1);      // 加载温度对应波形
  _writeCmd(0x20);                        // 主激活执行加载
  _waitBusyLow("_LoadTemp", 5000);

  _setCursor();                           // 数据指针回到窗口起点
  logf("[SSD1619] init done: 窗口 X=0..%u, Y=%u..0，数据入口 X增/Y减\n",
       _w / 8 - 1, _h - 1);
}

void Ssd1619::display(const uint8_t* plane0, const uint8_t* plane1) {
  _setCursor();
  _writeCmd(0x24);                        // 黑白显存
  _writeData(plane0, _w * _h / 8);

  _writeCmd(0x26);                        // 第二显存：单色屏清零（对齐例程）
  if (_planes >= 2 && plane1) {
    if (_invertPlane1) {
      for (uint16_t i = 0; i < _w * _h / 8; i++) _writeData(~plane1[i]);
    } else {
      _writeData(plane1, _w * _h / 8);
    }
  } else {
    static uint8_t zeros[64];
    memset(zeros, 0, sizeof(zeros));
    for (uint16_t left = _w * _h / 8; left;) {
      const uint16_t n = left < sizeof(zeros) ? left : sizeof(zeros);
      _writeData(zeros, n);
      left -= n;
    }
  }

  _loadLut(kLutFull);                     // 全刷波形
  _update("_Update_Full");
}

void Ssd1619::displayPartial(const uint8_t* plane0, const uint8_t* /*plane1*/) {
  // 局刷配方同例程：重写整帧显存 + 只驱动变化像素的温和波形，
  // 由上层多轮调用叠加出软件灰阶。
  _setCursor();
  _writeCmd(0x24);
  _writeData(plane0, _w * _h / 8);

  _writeCmd(0x26);                        // 第二显存清零（例程同样处理）
  static uint8_t zeros[64];
  memset(zeros, 0, sizeof(zeros));
  for (uint16_t left = _w * _h / 8; left;) {
    const uint16_t n = left < sizeof(zeros) ? left : sizeof(zeros);
    _writeData(zeros, n);
    left -= n;
  }

  _loadLut(kLutPart);                     // 局刷波形
  _update("_Update_Part");
}

void Ssd1619::sleep() {
  _writeCmd(0x10);                        // Deep sleep mode 1
  _writeData(0x01);                       // 唤醒需硬件复位（init 会做）
}

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------
void Ssd1619::_waitBusyLow(const char* what, uint32_t timeout_ms) {
  uint32_t start = millis();
  while (digitalRead(_busy) == LOW) {     // SSD1619：LOW = 忙（同 UC8151D）
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
  delay(200);                             // 例程在 BUSY 释放后再稳 200ms
}

void Ssd1619::_setCursor() {
  const uint16_t yTop = _h - 1;
  _writeCmd(0x4E);                        // RAM X address counter
  _writeData(0x00);
  _writeCmd(0x4F);                        // RAM Y address counter → 窗口起点
  _writeData(yTop & 0xFF);
  _writeData(yTop >> 8);
}

void Ssd1619::_loadLut(const uint8_t* lut70) {
  _writeCmd(0x32);                        // Write LUT register
  _writeData(lut70, 70);
}

void Ssd1619::_update(const char* what) {
  _writeCmd(0x22); _writeData(0xC7);      // 时钟+ booster + 显示全开
  _writeCmd(0x20);                        // 主激活
  _waitBusyLow(what, 15000);
}
