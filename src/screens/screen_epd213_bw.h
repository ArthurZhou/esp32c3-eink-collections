// screens/screen_epd213_bw.h — 2.13" 单色 B/W 屏幕（128×250）。
//
// 单色屏：无灰阶（如需灰度可改 PARTIAL_CYCLES，但此面板未声明局刷能力）。

#pragma once

#include "screen.h"

class ScreenEpd213Bw : public Screen {
public:
  ScreenEpd213Bw();
  static const ScreenDef& def();  // 供注册表 / 网页列出型号
};
