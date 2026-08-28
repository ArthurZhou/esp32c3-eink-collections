// screens/screen_e2213js0c1.h — PDI 2.13" 三色 B/W/R（E2213JS0C1，212×104）。
//
// 龙亭新技（Pervasive Displays）Spectra Red R2.0 膜，iTC 驱动 IC。
// 横向 212 × 纵向 104；全刷约 15 s，不支持局刷 → 无软件灰阶。

#pragma once

#include "screen.h"

class ScreenE2213js0c1 : public Screen {
public:
  ScreenE2213js0c1();
  static const ScreenDef& def();  // 供注册表 / 网页列出型号
};
