// screens/screen_registry.cpp — 型号 → ScreenDef / Screen 工厂。

#include "screen_registry.h"

#include "screen_gdeh0213z19.h"
#include "screen_epd213_bw.h"
#include "screen_epd29_bw.h"
#include "screen_epd42_bw.h"
#include "screen_e2213js0c1.h"

const ScreenDef& screenDefFor(ScreenModel m) {
  switch (m) {
    case ScreenModel::GDEH0213Z19: return ScreenGdeh0213z19::def();
    case ScreenModel::EPD_2_13_BW: return ScreenEpd213Bw::def();
    case ScreenModel::EPD_2_9_BW:  return ScreenEpd29Bw::def();
    case ScreenModel::EPD_4_2_BW:  return ScreenEpd42Bw::def();
    case ScreenModel::E2213JS0C1:  return ScreenE2213js0c1::def();
    default:                       return ScreenGdeh0213z19::def();
  }
}

Screen* createScreen(ScreenModel m) {
  switch (m) {
    case ScreenModel::GDEH0213Z19: return new ScreenGdeh0213z19();
    case ScreenModel::EPD_2_13_BW: return new ScreenEpd213Bw();
    case ScreenModel::EPD_2_9_BW:  return new ScreenEpd29Bw();
    case ScreenModel::EPD_4_2_BW:  return new ScreenEpd42Bw();
    case ScreenModel::E2213JS0C1:  return new ScreenE2213js0c1();
    default:                       return new ScreenGdeh0213z19();
  }
}
