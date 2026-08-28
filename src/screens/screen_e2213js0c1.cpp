// screens/screen_e2213js0c1.cpp — PDI E2213JS0C1 屏幕定义。
//
// 212×104（宽×高），B/W/R 三色，iTC（OTP LUT）驱动，仅全刷。
// 驱动实例化在 main.cpp 的 applyDisplayConfig()（按 def().driver 分派）。

#include "screen_e2213js0c1.h"

namespace {
constexpr ScreenDef kDef = {
  /*label*/        "PDI 2.13\" RBW 212×104",
  /*width*/        212,
  /*height*/       104,
  /*palette*/      kPalRBW, arrLen(kPalRBW),
  /*planeCount*/   2,                 // 黑白 + 红两路位平面
  /*invertPlane1*/ false,             // iTC：0x13 位1=红
  /*grayMode*/     GrayMode::NONE,    // 无灰阶（Spectra 仅全刷）
  /*grayLevels*/   0,
  /*partialRefresh*/ false,
  /*driver*/       DriverType::PDITC,
};
}  // namespace

ScreenE2213js0c1::ScreenE2213js0c1() : Screen(def()) {}
const ScreenDef& ScreenE2213js0c1::def() { return kDef; }
