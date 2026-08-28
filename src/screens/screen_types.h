// screens/screen_types.h — Screen 层共享类型（不依赖驱动 / 渲染）。
//
// 关键设计：调色板通用化（修复旧版"写死黑白 / 黑白红"的问题）。
//   旧设计把颜色硬编码成黑 / 白 / 红三个墨码，加黄、绿等任意墨色或 4 色
//   组合都得改绘制层和前端。新设计把每种"物理墨色"抽象成两路位平面值：
//     InkColor { hex, p0, p1 }
//   1 平面屏（纯黑白）最多 2 色；2 平面屏最多 4 色（黑 / 白 / 红 / 黄…）。
//
//   按 GUIDE：
//     · 渲染器只输出"索引像素数据"（每像素 = 调色板内某一级色阶或某一颜色）
//     · Screen 层把索引拆解为发给驱动 IC 的位平面
//     · 前端从 GET /screenmodels 的 colors[].hex 读取调色板做减色，不再硬编码
//
// 驱动层不依赖本文件（只接收裸尺寸 / 平面数），见 drivers/epd_driver.h。

#pragma once

#include <stdint.h>
#include <stddef.h>  // size_t（arrLen 模板用）

// ---------------------------------------------------------------------------
// 墨色：一种物理墨水 = 前端减色目标的 RGB hex + 两路位平面值。
//   p0/p1 = 该墨色在 plane0 / plane1 上的位值（0 或 1）。
//   plane0 = 0x10 黑/白通道（位1=白，位0=黑，与 UC8151D/SSD1619 一致）
//   plane1 = 0x13 彩色通道（位1=有色，位0=无色）
//
//   cycle = 软件灰阶（PARTIAL_CYCLES）下该墨色"出现"的局刷轮次：
//     · 0   → 第 0 轮就写入（纯色，如黑 / 红）
//     · n>0 → 前 n 轮保持白，第 n 轮才变黑（叠加出中间灰度）
//     · 255 → 永不写入（白）；非灰阶屏所有条目均为 0 / 255。
//
// 典型映射（与 UC8151D/SSD1619 驱动语义一致）：
//   黑白(B/W)      黑{p0=0,cycle=0}       白{p0=1,cycle=255}
//   黑白红(B/W/R)  黑{0,0,0} 红{1,1,0} 白{1,0,255}
//   四色(B/W/R/Y)  黑{0,0,0} 红{1,1,0} 黄{0,1,0} 白{1,0,255}
// ---------------------------------------------------------------------------
struct InkColor {
  uint32_t hex;    // 24-bit RGB，前端减色 / 预览用
  uint8_t  p0;     // plane0 位值
  uint8_t  p1;     // plane1 位值（1 平面屏忽略）
  uint8_t  cycle;  // 软件灰阶层叠轮次（见上；255=永不显示）
};

// cycle 特殊值：永不显示（白）。
constexpr uint8_t kCycleNever = 255;

// 常用 24-bit RGB 常量（便于注册表填写）。
namespace inkRgb {
  constexpr uint32_t BLACK  = 0x000000;
  constexpr uint32_t WHITE  = 0xFFFFFF;
  constexpr uint32_t RED    = 0xFF0000;
  constexpr uint32_t YELLOW = 0xFFFF00;
  constexpr uint32_t GREEN  = 0x00FF00;
  constexpr uint32_t BLUE   = 0x0000FF;
}

// 通用调色板（constexpr 数据，各屏幕文件复用；单一事实源）。
static constexpr InkColor kPalBW[] = {   // 单色 B/W（无灰阶屏）
  { inkRgb::BLACK, 0, 0, 0 },
  { inkRgb::WHITE, 1, 0, kCycleNever },
};
static constexpr InkColor kPalRBW[] = {  // 三色 B/W/R
  { inkRgb::BLACK, 0, 0, 0 },
  { inkRgb::RED,   1, 1, 0 },
  { inkRgb::WHITE, 1, 0, kCycleNever },
};
static constexpr InkColor kPalRBWY[] = { // 四色 B/W/R/Y（示例，当前无屏用）
  { inkRgb::BLACK,  0, 0, 0 },
  { inkRgb::RED,    1, 1, 0 },
  { inkRgb::YELLOW, 0, 1, 0 },
  { inkRgb::WHITE,  1, 0, kCycleNever },
};
// 单色软件灰阶调色板（PARTIAL_CYCLES 屏用）：4 级 = 黑/深灰/浅灰/白。
// hex 取均匀亮度台阶，前端 Floyd-Steinberg 直接对这 4 个目标色抖动。
static constexpr InkColor kPalBWGray[] = {
  { inkRgb::BLACK,  0, 0, 0 },           // 纯黑：第 0 轮写入
  { 0x555555,      0, 0, 1 },           // 深灰：第 1 轮写入（少驱动一轮→浅）
  { 0xAAAAAA,      0, 0, 2 },           // 浅灰：第 2 轮写入
  { inkRgb::WHITE,  1, 0, kCycleNever }, // 白：永不写入
};

// 辅助：取 constexpr 数组长度。
template<typename T, size_t N> constexpr size_t arrLen(const T(&)[N]) { return N; }

// ---------------------------------------------------------------------------
// 灰阶实现模式（GUIDE 的"灰度实现策略"）
//   NONE           → 无灰阶（纯 B/W 或三 / 四色）
//   NATIVE         → 原生硬件灰阶（驱动 LUT 直接驱动粒子至中间电压，本项目暂无面板）
//   PARTIAL_CYCLES → 软件时序叠加：仅原生 1bit 的屏，多轮局刷叠加合成中间灰阶
// ---------------------------------------------------------------------------
enum class GrayMode : uint8_t {
  NONE           = 0,
  NATIVE         = 1,
  PARTIAL_CYCLES = 2,
};

// ---------------------------------------------------------------------------
// 驱动 IC（与屏幕绑定，前端不可单独选）
// ---------------------------------------------------------------------------
enum class DriverType : uint8_t {
  UC8151D = 0,
  SSD1619 = 1,
  PDITC   = 2,   // Pervasive Displays iTC（E2213JS0C1 等小尺寸 Spectra 屏）
  SSD1683 = 3,   // SSD1683（GDEY042T81 400×300 等，用内部 OTP 波形）
  _COUNT
};
struct DriverEntry { const char* label; };
static constexpr DriverEntry kDriverModels[] = {
  { "UC8151D" },
  { "SSD1619" },
  { "PDITC" },
  { "SSD1683" },
};
static constexpr uint8_t kDriverModelCount =
    sizeof(kDriverModels) / sizeof(kDriverModels[0]);

inline const char* driverLabel(DriverType d) {
  uint8_t i = (uint8_t)d;
  if (i >= kDriverModelCount) i = 0;
  return kDriverModels[i].label;
}

// ---------------------------------------------------------------------------
// 屏幕定义（Screen 层数据）：一张屏 = 一个 ScreenDef。
// 每种屏幕一个单独文件（.h + .cpp），如同驱动 IC 各自成文件。
// ---------------------------------------------------------------------------
struct ScreenDef {
  const char*     label;          // 友好名称（前端下拉）
  uint16_t        width;          // 水平像素（8 的倍数）
  uint16_t        height;         // 垂直像素
  const InkColor* palette;        // 该屏支持的墨色调色板
  uint8_t         paletteCount;   // 调色板长度（arrLen 推导，勿手填）
  uint8_t         planeCount;     // 位平面数（1=黑白, 2=黑白+彩色）
  bool            invertPlane1;   // 0x13 通道反相（面板相关；1 平面屏忽略）
  GrayMode        grayMode;       // 灰阶实现模式
  uint8_t         grayLevels;     // 灰阶级数（= 调色板长度；软件灰阶用）
  bool            partialRefresh; // 是否支持局刷（软件灰阶必需）
  DriverType      driver;         // 绑定的驱动 IC

  uint16_t planeLength() const { return width * height / 8; }  // 单平面字节数
  uint16_t pixelCount()  const { return width * height; }
  bool hasGray() const { return grayMode != GrayMode::NONE; }
};

// 按调色板索引写一个像素的位平面（Screen 层与绘制层共用的小工具）。
// plane1 可为 nullptr（1 平面屏）。
inline void setPixelInk(uint8_t* plane0, uint8_t* plane1,
                        const ScreenDef& d, int16_t x, int16_t y, uint8_t idx) {
  if (x < 0 || x >= (int16_t)d.width || y < 0 || y >= (int16_t)d.height) return;
  if (idx >= d.paletteCount) idx = d.paletteCount - 1;  // 越界 → 最后一种色（保守）
  const InkColor& ink = d.palette[idx];
  uint16_t byteIdx = (uint16_t(y) * d.width + uint16_t(x)) >> 3;
  uint8_t  bit     = 0x80 >> (x & 7);
  if (ink.p0) plane0[byteIdx] |= bit;  else plane0[byteIdx] &= ~bit;
  if (plane1) {
    if (ink.p1) plane1[byteIdx] |= bit; else plane1[byteIdx] &= ~bit;
  }
}
