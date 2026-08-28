// screens/screen_epd42_bw.h — 4.2" 单色 B/W 屏幕（400×300，SSD1619）。
//
// SSD1619：黑白，不支持原生多级灰度 → 加"软件灰阶"（多轮局刷叠加）。

#pragma once

#include "screen.h"

class ScreenEpd42Bw : public Screen {
public:
  ScreenEpd42Bw();
  static const ScreenDef& def();  // 供注册表 / 网页列出型号
};
