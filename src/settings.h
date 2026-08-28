// settings.h — 运行期设备配置 + NVS 持久化。
//
// 存的是"型号枚举 + 引脚"，不存原始 width/height。
// 屏幕参数通过 screens/screen_registry.h 的 screenDefFor() 按型号查表得到，
// 驱动 IC 通过 screenDriver() 由型号绑定得出，保证参数来源唯一。
// 前端唯一可改的配置项 = 屏幕型号。

#pragma once

#include <stdint.h>
#include <WString.h>
#include <Preferences.h>

#include "screens/screen_registry.h"

// ---------------------------------------------------------------------------
// 设备配置（= 网页设置页的数据模型）
// ---------------------------------------------------------------------------
struct DeviceSettings {
  ScreenModel screen = ScreenModel::GDEH0213Z19;

  // SPI 引脚（-1 = 用默认接法；当前固件固定写死，前端不可改）。
  int8_t sck = -1, mosi = -1, cs = -1, dc = -1, rst = -1, busy = -1;

  // 本机默认接法（ESP32-C3）
  static constexpr int8_t DEF_SCK  = 6;
  static constexpr int8_t DEF_MOSI = 7;
  static constexpr int8_t DEF_CS   = 10;
  static constexpr int8_t DEF_DC   = 5;
  static constexpr int8_t DEF_RST  = 4;
  static constexpr int8_t DEF_BUSY = 3;

  int8_t effSck()  const { return sck  >= 0 ? sck  : DEF_SCK;  }
  int8_t effMosi() const { return mosi >= 0 ? mosi : DEF_MOSI; }
  int8_t effCs()   const { return cs   >= 0 ? cs   : DEF_CS;   }
  int8_t effDc()   const { return dc   >= 0 ? dc   : DEF_DC;   }
  int8_t effRst()  const { return rst  >= 0 ? rst  : DEF_RST;  }
  int8_t effBusy() const { return busy >= 0 ? busy : DEF_BUSY; }

  // 驱动 IC 由型号绑定得出（前端不可单独选择）。
  DriverType driver() const { return screenDriver(screen); }
  const char* driverLabel() const { return ::driverLabel(driver()); }

  // 取当前屏幕的 ScreenDef（查注册表，不含自由字段）。
  const ScreenDef& def() const { return screenDefFor(screen); }
};

// ---------------------------------------------------------------------------
// NVS 持久化
// ---------------------------------------------------------------------------
class SettingsStore {
public:
  void begin();
  bool load();   // 成功返回 true；无/版本不符保持默认
  void save();
  void reset();  // 恢复默认并写回

  DeviceSettings& get() { return s_; }

  // ------------------------------------------------------------------
  // WLAN 凭据（独立 NVS 键，不进 cfg 结构体 → 配网不随结构体升级丢失）。
  // 空密码 = 开放网络；ssid 非空才算已配置。
  // ------------------------------------------------------------------
  String wifiSsid();
  String wifiPassword();
  bool   hasWifi() { return wifiSsid().length() > 0; }
  bool   saveWifi(const String& ssid, const String& pass);  // 校验长度后写入

private:
  DeviceSettings s_;
  Preferences   prefs_;
  static constexpr const char* NS_ = "epd_cfg";
  static constexpr uint8_t    VER_ = 3;  // 结构体改变 → 版本号+1
};
