// screens/screen_epd42_bw.cpp — 4.2" 单色 B/W 屏幕定义（软件灰阶，SSD1683）。
//
// SSD1683（GDEY042T81 400×300）：黑白，内部 OTP 波形即可显示，无需自定义 LUT；
// 支持局刷 → 多轮局刷叠加出软件灰阶。

#include "screen_epd42_bw.h"

namespace {
constexpr ScreenDef kDef = {
  /*label*/        "4.2\" BW 400×300 (局刷灰阶)",
  /*width*/        400,
  /*height*/       300,
  /*palette*/      kPalBWGray, arrLen(kPalBWGray),
  /*planeCount*/   1,                 // 纯黑白
  /*invertPlane1*/ false,
  /*grayMode*/     GrayMode::PARTIAL_CYCLES,  // 软件时序叠加灰阶
  /*grayLevels*/   4,                 // 黑 / 深灰 / 浅灰 / 白
  /*partialRefresh*/ true,            // 支持局刷（SSD1683 局刷 0x22=0xFC 叠加灰阶）
  /*driver*/       DriverType::SSD1683,
};
}  // namespace

ScreenEpd42Bw::ScreenEpd42Bw() : Screen(def()) {}
const ScreenDef& ScreenEpd42Bw::def() { return kDef; }
