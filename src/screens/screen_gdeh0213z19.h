// screens/screen_gdeh0213z19.h — 2.13" 三色 B/W/R 屏幕（GoodDisplay GDEH0213Z19）。
//
// 每种屏幕一个独立文件（如同驱动 IC 各自成文件）。
// 三色屏：不加灰阶（灰度只能靠黑 / 白抖动，见前端"简单 / FS"路径）。

#pragma once

#include "screen.h"

class ScreenGdeh0213z19 : public Screen {
public:
  ScreenGdeh0213z19();
  static const ScreenDef& def();  // 供注册表 / 网页列出型号
};
