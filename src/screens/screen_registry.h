// screens/screen_registry.h — 屏幕型号枚举 + 注册表（新增/删除屏唯一入口）。
//
// 每种型号对应一个单独文件中的 Screen 类（如 ScreenGdeh0213z19）。
// NVS 持久化存的是枚举下标；废弃型号改名 _DEPRECATED_x，不可重排、不可删除。
// 新增屏：1) 枚举末尾加项  2) screen_xxx.h/.cpp 新文件  3) 下面两个函数加 case。

#pragma once

#include <stdint.h>

#include "screen_types.h"

enum class ScreenModel : uint8_t {
  GDEH0213Z19 = 0,   // 2.13" 三色 B/W/R  104×212（UC8151D）
  EPD_2_13_BW = 1,   // 2.13" 单色 B/W    128×250（UC8151D）
  EPD_2_9_BW  = 2,   // 2.9"  单色 B/W    128×296（UC8151D，软件灰阶）
  EPD_4_2_BW  = 3,   // 4.2"  单色 B/W    400×300（SSD1619，软件灰阶）
  E2213JS0C1  = 4,   // PDI 2.13" 三色 B/W/R 212×104（iTC，仅全刷）
  _COUNT
};
constexpr uint8_t kScreenModelCount = (uint8_t)ScreenModel::_COUNT;

class Screen;

// 按型号取屏幕定义（越界 → 返回 0 号默认）。
const ScreenDef& screenDefFor(ScreenModel m);
// 按型号创建 Screen 实例（调用方负责 delete）。
Screen* createScreen(ScreenModel m);

inline DriverType screenDriver(ScreenModel m) { return screenDefFor(m).driver; }
inline const char* screenLabel(ScreenModel m) { return screenDefFor(m).label; }
