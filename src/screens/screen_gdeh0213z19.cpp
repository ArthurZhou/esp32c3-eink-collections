// screens/screen_gdeh0213z19.cpp — 2.13" 三色 B/W/R 屏幕定义。

#include "screen_gdeh0213z19.h"

namespace {
constexpr ScreenDef kDef = {
  /*label*/        "GDEH0213Z19 2.13\" RBW 104×212",
  /*width*/        104,
  /*height*/       212,
  /*palette*/      kPalRBW, arrLen(kPalRBW),
  /*planeCount*/   2,                 // 黑白 + 红
  /*invertPlane1*/ false,
  /*grayMode*/     GrayMode::NONE,    // 三色屏，不加灰阶
  /*grayLevels*/   0,
  /*partialRefresh*/ false,           // 三色屏不做局刷灰阶
  /*driver*/       DriverType::UC8151D,
};
}  // namespace

ScreenGdeh0213z19::ScreenGdeh0213z19() : Screen(def()) {}
const ScreenDef& ScreenGdeh0213z19::def() { return kDef; }
