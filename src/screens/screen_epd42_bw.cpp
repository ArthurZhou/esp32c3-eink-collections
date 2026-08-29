// screens/screen_epd42_bw.cpp — 4.2" 单色 B/W 屏幕定义（SSD1683，DEPG0420BNS830F20）。
//
// 面板型号 MT-DEPG0420BNS830F20（4.2" 400×300），数据手册明确 DRIVER IC = SSD1683：
// OTP 波形直接显示，无需自定义 LUT；BUSY 高=忙。

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
  /*partialRefresh*/ true,            // 支持局刷（SSD1683 0x22=0xFC 局刷叠加灰阶）
  /*driver*/       DriverType::SSD1683,
};
}  // namespace

ScreenEpd42Bw::ScreenEpd42Bw() : Screen(def()) {}
const ScreenDef& ScreenEpd42Bw::def() { return kDef; }
