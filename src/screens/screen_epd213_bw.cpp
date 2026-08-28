// screens/screen_epd213_bw.cpp — 2.13" 单色 B/W 屏幕定义。

#include "screen_epd213_bw.h"

namespace {
constexpr ScreenDef kDef = {
  /*label*/        "2.13\" BW 128×250",
  /*width*/        128,
  /*height*/       250,
  /*palette*/      kPalBW, arrLen(kPalBW),
  /*planeCount*/   1,                 // 纯黑白
  /*invertPlane1*/ false,
  /*grayMode*/     GrayMode::NONE,    // 无灰阶
  /*grayLevels*/   0,
  /*partialRefresh*/ false,
  /*driver*/       DriverType::UC8151D,
};
}  // namespace

ScreenEpd213Bw::ScreenEpd213Bw() : Screen(def()) {}
const ScreenDef& ScreenEpd213Bw::def() { return kDef; }
