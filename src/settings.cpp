// settings.cpp — NVS 持久化实现。

#include "settings.h"

#include <string.h>

void SettingsStore::begin() {
  // begin() 惰性；load/save 各自打开。
}

bool SettingsStore::load() {
  bool ok = prefs_.begin(NS_, true);
  if (!ok) return false;
  uint8_t ver = prefs_.getUChar("ver", 0);
  size_t  sz  = prefs_.getBytesLength("cfg");
  if (ver == VER_ && sz == sizeof(s_)) {
    prefs_.getBytes("cfg", (uint8_t*)&s_, sizeof(s_));
    prefs_.end();
    return true;
  }
  prefs_.end();
  return false;
}

void SettingsStore::save() {
  prefs_.begin(NS_, false);
  prefs_.putUChar("ver", VER_);
  prefs_.putBytes("cfg", (const uint8_t*)&s_, sizeof(s_));
  prefs_.end();
}

void SettingsStore::reset() {
  s_ = DeviceSettings();
  save();
}

// ---------------------------------------------------------------------------
// WLAN 凭据（独立键：wifi_ssid / wifi_pass，与 cfg 结构体分开存）
// ---------------------------------------------------------------------------
namespace {
constexpr const char* K_WIFI_SSID = "wifi_ssid";
constexpr const char* K_WIFI_PASS = "wifi_pass";
constexpr size_t      SSID_MAX    = 32;   // IEEE 802.11 SSID 上限
constexpr size_t      PASS_MAX    = 64;   // WPA2 口令上限
}

String SettingsStore::wifiSsid() {
  if (!prefs_.begin(NS_, true)) return String();
  String v = prefs_.getString(K_WIFI_SSID, "");
  prefs_.end();
  return v;
}

String SettingsStore::wifiPassword() {
  if (!prefs_.begin(NS_, true)) return String();
  String v = prefs_.getString(K_WIFI_PASS, "");
  prefs_.end();
  return v;
}

bool SettingsStore::saveWifi(const String& ssid, const String& pass) {
  if (ssid.length() == 0 || ssid.length() > SSID_MAX) return false;
  if (pass.length() > PASS_MAX) return false;
  prefs_.begin(NS_, false);
  prefs_.putString(K_WIFI_SSID, ssid);
  prefs_.putString(K_WIFI_PASS, pass);
  prefs_.end();
  return true;
}
