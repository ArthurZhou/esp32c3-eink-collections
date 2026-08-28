// screens/screen.cpp — Screen 层基类实现。
//
// 所有逻辑基于 ScreenDef 数据，对全屏通用；若某屏需要特殊行为
// （不同的局刷 LUT 编排 / 刷新策略），在其子类覆写即可。

#include "screen.h"

#include <string.h>

void Screen::renderIndexed(const uint8_t* pixels, uint8_t* plane0,
                           uint8_t* plane1) const {
  const uint16_t W = def_.width, H = def_.height;
  memset(plane0, 0xFF, def_.planeLength());            // 默认白
  if (plane1) memset(plane1, 0x00, def_.planeLength());

  for (uint16_t y = 0; y < H; y++)
    for (uint16_t x = 0; x < W; x++)
      setPixelInk(plane0, plane1, def_, (int16_t)x, (int16_t)y,
                  pixels[y * W + x]);
}

void Screen::renderIndexedCycle(const uint8_t* pixels, uint8_t cycle,
                                uint8_t* plane0, uint8_t* plane1) const {
  // 第 cycle 轮的位平面：ink.cycle <= cycle 的像素按其墨色写入，
  // 其余（含白 kCycleNever）保持白 → 每轮黑像素为上一轮超集，只增不删。
  const uint16_t W = def_.width, H = def_.height;
  memset(plane0, 0xFF, def_.planeLength());
  if (plane1) memset(plane1, 0x00, def_.planeLength());

  const InkColor* pal = def_.palette;
  const uint8_t   n   = def_.paletteCount;
  for (uint16_t y = 0; y < H; y++)
    for (uint16_t x = 0; x < W; x++) {
      uint8_t idx = pixels[y * W + x];
      if (idx >= n) idx = n - 1;
      if (pal[idx].cycle > cycle) continue;    // 该轮还没轮到它出现
      setPixelInk(plane0, plane1, def_, (int16_t)x, (int16_t)y, idx);
    }
}

bool Screen::usesGrayCycles(const uint8_t* pixels) const {
  // 只有 PARTIAL_CYCLES 屏且索引图里真的用到了"延迟写入"墨色
  // （cycle>0 且非白）才需要多轮局刷；纯色图走普通全刷路径。
  if (def_.grayMode != GrayMode::PARTIAL_CYCLES) return false;

  bool used[256] = { false };
  const uint16_t count = def_.pixelCount();
  for (uint16_t i = 0; i < count; i++) used[pixels[i]] = true;

  for (uint8_t i = 0; i < def_.paletteCount; i++) {
    const InkColor& ink = def_.palette[i];
    if (used[i] && ink.cycle > 0 && ink.cycle != kCycleNever) return true;
  }
  return false;
}

void Screen::clearPlanes(uint8_t* plane0, uint8_t* plane1) const {
  memset(plane0, 0xFF, def_.planeLength());
  if (plane1) memset(plane1, 0x00, def_.planeLength());
}

// ---------------------------------------------------------------------------
// 软件灰阶参数（PARTIAL_CYCLES）
// ---------------------------------------------------------------------------
uint8_t Screen::grayCycleCount() const {
  if (def_.grayMode != GrayMode::PARTIAL_CYCLES) return 0;
  // 轮数 = 调色板中最大"有限 cycle"+1（黑=0、深灰=1、浅灰=2 → 共 3 轮）。
  uint8_t rounds = 1;
  for (uint8_t i = 0; i < def_.paletteCount; i++) {
    uint8_t c = def_.palette[i].cycle;
    if (c != kCycleNever && (uint8_t)(c + 1) > rounds) rounds = c + 1;
  }
  return rounds;
}

uint32_t Screen::grayDelayMs() const {
  // 每轮局刷后建议等待：让墨水迁移稳定（典型 EPD 局刷 ≈ 300 ms 足够）。
  return 300;
}
