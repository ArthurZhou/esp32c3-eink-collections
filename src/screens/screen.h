// screens/screen.h — Screen 层抽象基类。
//
// GUIDE 中 Screen 层的职责（硬件抽象 / HAL）：
//   1) 声明屏幕属性：型号、友好名、支持的墨色集合、总色阶、分辨率、驱动 IC
//      —— 全部在 ScreenDef 里。
//   2) 声明刷新方式（全刷 / 局刷）—— partialRefresh。
//   3) 灰度实现策略—— grayMode（原生 LUT / 软件多轮局刷叠加）。
//   4) 把渲染器输出的"索引像素数据"拆解为驱动可用的位平面缓冲。
//
// 每种屏幕一个单独文件（.h + .cpp），如同驱动 IC 各自成文件。
// 装配在 main.cpp：createScreen(model) 得到 Screen，再按 def.driver 建驱动。
//
// 颜色通用化：本层不关心具体是黑 / 白 / 红 / 黄，只按调色板索引工作，
// 具体墨色与位平面的映射在 screen_types.h 的 InkColor / setPixelInk 里。

#pragma once

#include <stdint.h>

#include "screen_types.h"

class Screen {
public:
  explicit Screen(const ScreenDef& def) : def_(def) {}
  virtual ~Screen() {}

  const ScreenDef& def() const { return def_; }
  uint16_t width()  const { return def_.width; }
  uint16_t height() const { return def_.height; }
  uint16_t planeLength() const { return def_.planeLength(); }

  // 渲染器输出（每像素一个调色板索引）→ 拆成位平面缓冲。
  // plane0 / plane1 由调用方分配（各长 planeLength()）；1 平面屏 plane1 传 nullptr。
  virtual void renderIndexed(const uint8_t* pixels, uint8_t* plane0,
                             uint8_t* plane1) const;

  // 同上，但只渲染"到第 cycle 轮为止"的状态（软件灰阶层叠）：
  // 调色板条目 ink.cycle > cycle 的像素保持白，<= cycle 的按其位平面值写入。
  // cycle 从 0 递增逐轮调用 → 每轮黑像素是上一轮的超集（只增不删），
  // 配合局刷 LUT 只驱动变化像素，多轮叠加出中间灰阶。
  virtual void renderIndexedCycle(const uint8_t* pixels, uint8_t cycle,
                                  uint8_t* plane0, uint8_t* plane1) const;

  // 索引图中是否用到"延迟写入"的墨色（ink.cycle > 0 且非白）
  // → 决定 /image 是走一次全刷还是多轮局刷叠加。
  virtual bool usesGrayCycles(const uint8_t* pixels) const;

  // 清为白（plane0 全 1，plane1 全 0）。
  virtual void clearPlanes(uint8_t* plane0, uint8_t* plane1) const;

  // ------------------------------------------------------------------
  // 软件灰阶参数（GrayMode::PARTIAL_CYCLES）。调用方典型流程：
  //   if (screen->usesGrayCycles(idx)) {
  //     display->clear();                       // 先全刷白
  //     for c in [0, grayCycleCount()):
  //       screen->renderIndexedCycle(idx, c, p0, p1);
  //       display->displayPartial(p0, nullptr); // 局刷
  //       delay(screen->grayDelayMs());
  //   } else {
  //     screen->renderIndexed(idx, p0, p1);
  //     display->display(p0, p1);              // 普通全刷
  //   }
  // ------------------------------------------------------------------
  virtual uint8_t  grayCycleCount() const;    // 轮数 = max(ink.cycle)+1
  virtual uint32_t grayDelayMs() const;       // 每轮局刷之间等待（ms）

protected:
  const ScreenDef& def_;
};