// screens/screen_epd29_bw.h — 2.9" 单色 B/W 屏幕（128×296，UC8151D）。
//
// 支持局刷（partial refresh）→ 软件灰阶（多轮局刷叠加）。

#pragma once

#include "screen.h"

class ScreenEpd29Bw : public Screen {
public:
  ScreenEpd29Bw();
  static const ScreenDef& def();  // 供注册表 / 网页列出型号
};
