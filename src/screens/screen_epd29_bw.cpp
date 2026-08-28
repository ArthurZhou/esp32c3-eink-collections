// screens/screen_epd29_bw.cpp — 2.9" 单色 B/W 屏幕定义（软件灰阶）。

#include "screen_epd29_bw.h"

namespace {
constexpr ScreenDef kDef = {
  /*label*/        "2.9\" BW 128×296 (局刷灰阶)",
  /*width*/        128,
  /*height*/       296,
  /*palette*/      kPalBWGray, arrLen(kPalBWGray),
  /*planeCount*/   1,                 // 纯黑白
  /*invertPlane1*/ false,
  /*grayMode*/     GrayMode::PARTIAL_CYCLES,  // 软件时序叠加灰阶
  /*grayLevels*/   4,                 // 黑 / 深灰 / 浅灰 / 白
  /*partialRefresh*/ true,            // 支持局刷
  /*driver*/       DriverType::UC8151D,
};
}  // namespace

ScreenEpd29Bw::ScreenEpd29Bw() : Screen(def()) {}
const ScreenDef& ScreenEpd29Bw::def() { return kDef; }
