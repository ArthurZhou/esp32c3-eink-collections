// inkscreen — ESP32 e-Paper 固件（装配层）。
//
// 架构（GUIDE.md 三层）：
//   前端(renderer/) → 索引像素数据 → Screen 层(screens/) → 位平面 → IC 驱动层(drivers/)
//   装配：型号（来自 NVS / 网页）→ createScreen() 建 Screen；
//         再按 ScreenDef.driver 建对应驱动（UC8151D / SSD1619）。
//   前端唯一可改配置 = 屏幕型号；驱动 IC 由型号绑定得出。
//
// 接线默认(ESP32-C3): SCK=6, MOSI=7, CS=10, DC=5, RST=4, BUSY=3

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>

#include "config.h"
#include "log.h"
#include "settings.h"
#include "screens/screen_registry.h"
#include "screens/screen.h"
#include "drivers/epd_driver.h"
#include "drivers/uc8151d.h"
#include "drivers/ssd1619.h"
#include "drivers/ssd1683.h"
#include "drivers/pditc.h"
#include "renderer/font5x7.h"
#include "renderer/draw.h"
#include "webpage.h"

// 位平面缓冲上限（覆盖最大面板 400×300 = 15000 B/平面）。
static constexpr uint16_t MAX_PLANE = 400 * 300 / 8 + 64;
uint8_t bufPlane0[MAX_PLANE];
uint8_t bufPlane1[MAX_PLANE];

SettingsStore store;
DeviceSettings* cfg = &store.get();

// ---------------------------------------------------------------------------
// Wi-Fi 配网状态（NVS 凭据 + 失败回落 AP 热点，见文件尾 connectWiFi 一节）
// ---------------------------------------------------------------------------
DNSServer     g_dns;                 // captive portal DNS（AP 模式下劫持解析）
bool          g_provMode  = false;   // 配网热点活跃
bool          g_staUp     = false;   // STA 已连接
bool          g_haveCreds = false;   // NVS 中存有凭据
unsigned long g_lastTry   = 0;       // 上次连接尝试（后台周期重试用）

Screen*    screen  = nullptr;
EpdDriver* display = nullptr;

WebServer server(80);
HTTPUpdateServer httpUpdater;

// 当前屏幕定义（由型号查注册表得到）。
const ScreenDef& curDef() { return screen->def(); }

// ---------------------------------------------------------------------------
// Display lifecycle
// ---------------------------------------------------------------------------
void applyDisplayConfig() {
  if (display) { display->sleep(); delete display; display = nullptr; }
  if (screen)  { delete screen;  screen  = nullptr; }

  screen = createScreen(cfg->screen);
  const ScreenDef& d = screen->def();
  const uint32_t freq = 4000000UL;

  switch (d.driver) {  // 驱动 IC 由屏幕型号绑定得出
    case DriverType::SSD1619:
      display = new Ssd1619(d.width, d.height, d.planeCount, d.invertPlane1,
                            cfg->effCs(), cfg->effDc(), cfg->effRst(),
                            cfg->effBusy(), freq);
      break;
    case DriverType::PDITC:  // PDI iTC（E2213JS0C1）：BUSY_N 高电平=忙
      display = new Pditc(d.width, d.height, d.invertPlane1,
                          cfg->effCs(), cfg->effDc(), cfg->effRst(),
                          cfg->effBusy(), freq);
      break;
    case DriverType::SSD1683:  // SSD1683（GDEY042T81 400×300，内部 OTP 波形）
      display = new Ssd1683(d.width, d.height, d.planeCount, d.invertPlane1,
                            cfg->effCs(), cfg->effDc(), cfg->effRst(),
                            cfg->effBusy(), freq);
      break;
    case DriverType::UC8151D:
    default:
      display = new Uc8151d(d.width, d.height, d.planeCount, d.invertPlane1,
                            cfg->effCs(), cfg->effDc(), cfg->effRst(),
                            cfg->effBusy(), freq);
      break;
  }

  SPI.begin(cfg->effSck(), -1, cfg->effMosi(), cfg->effCs());
  logf("[main] applyDisplayConfig: %s (%ux%u) driver=%s spi=%luHz\n",
       d.label, d.width, d.height, driverLabel(d.driver), (unsigned long)freq);
  display->init();
}

void showOnScreen() {
  display->display(bufPlane0, curDef().planeCount > 1 ? bufPlane1 : nullptr);
}

// ---------------------------------------------------------------------------
// Web helpers
// ---------------------------------------------------------------------------
// JSON 字符串转义（label 可能含引号/反斜杠，防止破坏 JSON 结构）
String jsonEscape(const char* s) {
  String out;
  for (const char* p = s; *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else out += c;
  }
  return out;
}

void handleRoot() {
  // 配网模式（无凭据/连不上）下，根路径直接跳到配网页。
  if (g_provMode && !g_staUp) {
    server.sendHeader("Location", "/wifi", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send_P(200, PSTR("text/html"), kPageHtml);
}

void handleStatus() {
  IPAddress ip = g_staUp ? WiFi.localIP() : WiFi.softAPIP();
  String json = "{\"ip\":\"" + ip.toString() + "\","
                "\"ap\":" + String(g_provMode ? "true" : "false") + ","
                "\"uptime\":" + String(millis() / 1000) + ","
                "\"rssi\":" + String(WiFi.RSSI()) + ","
                "\"heap\":" + String(ESP.getFreeHeap()) + "}";
  server.send(200, "application/json", json);
}

// 把调色板序列化成 JSON 数组：[{"hex":"#RRGGBB"}, ...]
String colorsJson(const ScreenDef& d) {
  String colors = "[";
  for (uint8_t c = 0; c < d.paletteCount; c++) {
    if (c) colors += ",";
    char hexStr[8];
    snprintf(hexStr, sizeof(hexStr), "#%06lX", (unsigned long)d.palette[c].hex);
    colors += "{\"hex\":\"" + String(hexStr) + "\"}";
  }
  colors += "]";
  return colors;
}

// GET /screenmodels — 返回屏幕型号列表（含调色板 hex、灰阶信息），
// 供前端下拉 + 抖动目标（前端据此量化，不再硬编码黑白/黑白红）。
void handleScreenModelsGet() {
  String json = "[";
  for (uint8_t i = 0; i < kScreenModelCount; i++) {
    if (i) json += ",";
    const ScreenDef& d = screenDefFor((ScreenModel)i);
    json += "{\"id\":"           + String(i) +
            ",\"label\":\""       + jsonEscape(d.label) + "\"" +
            ",\"width\":"         + String(d.width) +
            ",\"height\":"        + String(d.height) +
            ",\"planeCount\":"    + String(d.planeCount) +
            ",\"grayMode\":"      + String((uint8_t)d.grayMode) +
            ",\"grayLevels\":"    + String(d.grayLevels) +
            ",\"partialRefresh\":" + String(d.partialRefresh ? "true" : "false") +
            ",\"colors\":"        + colorsJson(d) +
            ",\"driver\":"        + String((uint8_t)d.driver) +
            ",\"driverLabel\":\"" + driverLabel(d.driver) + "\"" +
            "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

// GET /settings — 返回当前设备配置（屏幕型号 + 派生参数 + 固定引脚）
void handleSettingsGet() {
  const ScreenDef& d = curDef();
  String json = "{"
    "\"driver\":"        + String((int)cfg->driver()) + ","
    "\"driverLabel\":\"" + cfg->driverLabel() + "\","
    "\"screen\":"        + String((int)cfg->screen) + ","
    "\"screenLabel\":\"" + jsonEscape(screenLabel(cfg->screen)) + "\","
    "\"width\":"         + String(d.width) + ","
    "\"height\":"        + String(d.height) + ","
    "\"planeCount\":"    + String(d.planeCount) + ","
    "\"grayMode\":"      + String((uint8_t)d.grayMode) + ","
    "\"grayLevels\":"    + String(d.grayLevels) + ","
    "\"partialRefresh\":" + String(d.partialRefresh ? "true" : "false") + ","
    "\"colors\":"        + colorsJson(d) + ","
    "\"sck\":"           + String(cfg->effSck()) + ","
    "\"mosi\":"          + String(cfg->effMosi()) + ","
    "\"cs\":"            + String(cfg->effCs()) + ","
    "\"dc\":"            + String(cfg->effDc()) + ","
    "\"rst\":"           + String(cfg->effRst()) + ","
    "\"busy\":"          + String(cfg->effBusy()) + "}";
  server.send(200, "application/json", json);
}

// POST /settings — 仅允许修改"屏幕型号"这一项。
// 驱动 IC 由型号绑定得出；SPI 引脚固定写死，均不接受覆盖。
void handleSettingsPost() {
  int scr = server.hasArg("screen") ? server.arg("screen").toInt() : (int)cfg->screen;
  if (scr < 0 || scr >= kScreenModelCount) scr = (int)cfg->screen;
  cfg->screen = (ScreenModel)scr;
  // 驱动 IC 由型号绑定，无需单独存储；引脚不允许修改。
  cfg->sck = cfg->mosi = cfg->cs = cfg->dc = cfg->rst = cfg->busy = -1;
  store.save();
  applyDisplayConfig();
  server.send(200, "text/plain; charset=utf-8", "已保存并应用");
}

void handleSettingsReset() {
  store.reset();
  applyDisplayConfig();
  server.send(200, "text/plain; charset=utf-8", "已恢复默认并应用");
}

// ---------------------------------------------------------------------------
// 图片上传（POST /image）— 渲染器输出"索引像素数据"
// ---------------------------------------------------------------------------
// 接收 width×height 字节：每字节 = 调色板索引（0..paletteCount-1）。
// Screen 层负责把索引拆成位平面（renderIndexed），驱动全刷显示。
// 前端从 /screenmodels 的 colors 读调色板量化，不再传写死的黑白/黑白红位平面。

uint16_t g_imgTotal = 0;
bool     g_imgReady = false;
static uint8_t* s_idxBuf  = nullptr;   // 索引像素缓冲（heap，最大 400×300 = 120 KB）
static uint16_t s_idxSize = 0;

void handleImageUpload() {
  HTTPUpload& up = server.upload();
  const ScreenDef& d = curDef();
  uint16_t need = d.pixelCount();

  if (up.status == UPLOAD_FILE_START) {
    g_imgTotal = 0;
    g_imgReady = false;
    if (s_idxBuf && s_idxSize != need) { delete[] s_idxBuf; s_idxBuf = nullptr; }
    if (!s_idxBuf) { s_idxBuf = new uint8_t[need]; s_idxSize = need; }
    if (!s_idxBuf) { Serial.println("idx buf alloc failed"); return; }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!s_idxBuf) return;
    for (size_t i = 0; i < up.currentSize && g_imgTotal < need; i++)
      s_idxBuf[g_imgTotal++] = up.buf[i];
  } else if (up.status == UPLOAD_FILE_END) {
    g_imgReady = (g_imgTotal >= need);
  }
}

void handleImage() {
  const ScreenDef& d = curDef();
  uint16_t need = d.pixelCount();
  if (!g_imgReady || !s_idxBuf || g_imgTotal < need) {
    server.send(400, "text/plain; charset=utf-8", "图片数据无效");
    return;
  }

  // 统一管线：前端始终上传"调色板索引"图（灰阶屏的调色板含灰阶条目）。
  // 若索引图用到了延迟写入的墨色（软件灰阶），先全刷白再逐轮局刷叠加；
  // 否则与普通图一样走一次全刷。对前端而言只有 /image 一个端点。
  if (screen->usesGrayCycles(s_idxBuf)) {
    display->clear();
    uint8_t n = screen->grayCycleCount();
    for (uint8_t c = 0; c < n; c++) {
      screen->renderIndexedCycle(s_idxBuf, c, bufPlane0, bufPlane1);
      display->displayPartial(bufPlane0, nullptr);   // 局刷（只驱动变化像素）
      delay(screen->grayDelayMs());
    }
    server.send(200, "text/plain; charset=utf-8",
                "已显示（局刷×" + String(n) + "轮）");
  } else {
    screen->renderIndexed(s_idxBuf, bufPlane0, bufPlane1);
    showOnScreen();
    server.send(200, "text/plain; charset=utf-8", "已显示");
  }
}

void handleTest() {
  const ScreenDef& d = curDef();
  screen->clearPlanes(bufPlane0, bufPlane1);
  uint8_t* p1 = d.planeCount > 1 ? bufPlane1 : nullptr;

  // 1) 色带区（上半屏）：每种调色板色各画一条横带，验证驱动输出。
  const uint16_t colorH = d.height / 2;
  const uint16_t bandH  = colorH / d.paletteCount;
  for (uint8_t i = 0; i < d.paletteCount; i++)
    draw::fillRect(bufPlane0, p1, d, 0, i * bandH, d.width, bandH, i);

  // 2) 字形区（下半屏）：5×7 ASCII 字符全览。
  const uint8_t scale    = 1;
  const uint16_t cw      = kFont5x7.advance * scale;
  const uint16_t rowPitch = kFont5x7.height * scale + 2;
  uint16_t perRow = d.width / cw;
  if (perRow == 0) perRow = 1;
  uint16_t idx = 0;
  for (uint8_t ch = (uint8_t)kFont5x7.first; ch <= (uint8_t)kFont5x7.last; ch++) {
    char s[2] = { (char)ch, '\0' };
    int16_t gx = (idx % perRow) * cw;
    int16_t gy = (int16_t)colorH + (int16_t)(idx / perRow) * rowPitch;
    draw::drawString(bufPlane0, p1, d, kFont5x7, gx, gy, s, 0 /*黑*/, scale);
    idx++;
  }

  showOnScreen();
  server.send(200, "text/plain; charset=utf-8", "已显示测试图案");
}

void handleClear() {
  display->clear();
  server.send(200, "text/plain; charset=utf-8", "已清屏");
}

// ---------------------------------------------------------------------------
// Wi-Fi 配网：NVS 凭据 → 启动尝试连接 → 无凭据/失败回落 AP 热点
// ---------------------------------------------------------------------------
// 常见 IoT 配网方式：
//   1) 凭据存 NVS（settings.h 的 saveWifi/wifiSsid），不写死在代码里；
//   2) 启动时尝试连接（阻塞 ~15 s，正常上电体验）；
//   3) 失败则发射开放热点 inkscreen-XXXXXX + DNS 劫持（captive portal，
//      手机连上自动弹页），配网页 /wifi 可扫描/手输网络并保存；
//   4) 保存后同步验证连接，成功即重启进入 STA；失败保留热点供重试。
//   5) 运行中掉线自动重新开热点；后台每 WIFI_RETRY_INTERVAL_MS 重试一次。

static constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr unsigned long WIFI_SAVE_TIMEOUT_MS    = 18000;
static constexpr unsigned long WIFI_RETRY_INTERVAL_MS  = 30000;

// 配网页面（captive portal）。JSON 交互：GET /wifi/scan、POST /wifi/save。
static const char kWifiPortalHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>InkScreen Wi-Fi 配置</title>
<style>
 body{font-family:system-ui,-apple-system,sans-serif;background:#f2f4f7;margin:0;
      display:flex;justify-content:center;padding:24px}
 .card{background:#fff;border-radius:12px;box-shadow:0 2px 10px rgba(0,0,0,.08);
       padding:24px;width:100%;max-width:380px;height:fit-content}
 h1{font-size:20px;margin:0 0 4px} p.sub{color:#667;margin:0 0 18px;font-size:13px}
 label{display:block;font-size:13px;color:#334;margin:12px 0 4px}
 input,select{width:100%;box-sizing:border-box;padding:10px;border:1px solid #ccd;
              border-radius:8px;font-size:15px}
 button{width:100%;margin-top:18px;padding:12px;background:#2563eb;color:#fff;
        border:0;border-radius:8px;font-size:16px;cursor:pointer}
 button:disabled{background:#9ab}
 .msg{margin-top:14px;font-size:14px;display:none;padding:10px;border-radius:8px}
 .ok{background:#e6f6e9;color:#116622;display:block}
 .err{background:#fdeaea;color:#a11;display:block}
</style></head><body><div class="card">
<h1>InkScreen</h1><p class="sub">为设备配置 Wi-Fi 网络</p>
<label>选择网络</label><select id="net"><option value="">正在扫描…</option></select>
<label>手动输入 SSID（可选，优先使用）</label>
<input id="ssid" placeholder="留空则使用上方选择" maxlength="32">
<label>密码</label><input id="pass" type="password" maxlength="64"
       placeholder="开放网络可留空">
<button id="go">保存并连接</button>
<div id="msg" class="msg"></div>
</div>
<script>
const $=id=>document.getElementById(id);
function show(t,c){const m=$('msg');m.textContent=t;m.className='msg '+c;}
fetch('/wifi/scan').then(r=>r.json()).then(list=>{
  const sel=$('net');sel.innerHTML='';
  list.forEach(n=>{
    const o=document.createElement('option');o.value=n.ssid;
    o.textContent=(n.ssid||'(隐藏网络)')+' ('+n.rssi+'dBm'+(n.locked?' \u{1F512}':'')+')';
    sel.appendChild(o);
  });
}).catch(()=>{$('net').innerHTML='<option value="">扫描失败，请手动输入</option>';});
$('go').onclick=async()=>{
  const ssid=$('ssid').value.trim()||$('net').value;
  if(!ssid){show('请选择或输入网络名称','err');return;}
  $('go').disabled=true;$('go').textContent='连接中…（最长约20秒）';
  show('正在尝试连接…','ok');
  try{
    const r=await fetch('/wifi/save',{method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:new URLSearchParams({ssid:ssid,pass:$('pass').value})});
    const j=await r.json();
    if(j.ok){show(j.ip?('连接成功，IP '+j.ip+'，设备即将重启…'):j.msg,'ok');
             setTimeout(()=>location.reload(),5000);}
    else{show(j.msg||('失败('+r.status+')'),'err');
         $('go').disabled=false;$('go').textContent='保存并连接';}
  }catch(e){show('请求失败：'+e,'err');
            $('go').disabled=false;$('go').textContent='保存并连接';}
};
</script></body></html>)HTML";

String apSsidName() {
  String mac = WiFi.macAddress();          // "AABBCCDDEEFF"
  mac.toUpperCase();
  return "inkscreen-" + mac.substring(6);  // 后 6 位十六进制
}

void startProvisioningAp(const char* reason) {
  if (g_provMode) return;
  g_provMode = true;
  g_lastTry = millis();
  WiFi.mode(WIFI_AP_STA);                  // 保持 STA 能力以便后台重试
  String ssid = apSsidName();
  WiFi.softAP(ssid.c_str());               // 开放热点
  g_dns.start(53, "*", WiFi.softAPIP());   // 劫持所有域名 → 本机（captive portal）
  Serial.printf("[WiFi] %s -> AP '%s' @ %s\n",
                reason, ssid.c_str(), WiFi.softAPIP().toString().c_str());
}

void stopProvisioningAp() {
  g_dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  g_provMode = false;
  Serial.println("[WiFi] AP closed");
}

// 阻塞式连接已存凭据（仅启动时调用一次；后续重试在 loop 中非阻塞进行）。
bool connectStoredWiFi(uint32_t timeout_ms) {
  String ssid = store.wifiSsid(), pass = store.wifiPassword();
  if (!ssid.length()) { g_haveCreds = false; return false; }
  g_haveCreds = true;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.print("Connecting to '" + ssid + "'");
  unsigned long deadline = millis() + timeout_ms;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

// GET /wifi — 配网页（任何模式都可用，便于日后换网）
void handleWifiPage() {
  server.send_P(200, PSTR("text/html"), kWifiPortalHtml);
}

// GET /wifi/scan — 周围网络列表 [{ssid,rssi,locked}]
void handleWifiScan() {
  // AP 活跃时需切 AP_STA 才能扫描；纯 STA 模式下无副作用
  if (WiFi.getMode() == WIFI_MODE_AP) WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
    json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i).c_str()) + "\"," +
            "\"rssi\":"   + String(WiFi.RSSI(i)) + "," +
            "\"locked\":" +
            String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

// POST /wifi/save — 保存凭据并立即验证连接；成功后重启进入 STA。
void handleWifiSave() {
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  ssid.trim();
  if (!ssid.length() || ssid.length() > 32 || pass.length() > 64) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"SSID 或密码长度无效\"}");
    return;
  }
  if (!store.saveWifi(ssid, pass)) {
    server.send(500, "application/json", "{\"ok\":false,\"msg\":\"写入 NVS 失败\"}");
    return;
  }
  g_haveCreds = true;

  Serial.println("[WiFi] /wifi/save -> connecting to '" + ssid + "'");
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long deadline = millis() + WIFI_SAVE_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) delay(250);

  if (WiFi.status() == WL_CONNECTED) {
    g_lastTry = millis();
    server.send(200, "application/json",
                "{\"ok\":true,\"ip\":\"" + WiFi.localIP().toString() + "\"}");
    Serial.println("[WiFi] connected, restarting into STA mode");
    delay(800);
    ESP.restart();                          // 干净地收起 AP / DNS
  } else {
    g_lastTry = millis();                   // loop 会按周期继续重试
    server.send(200, "application/json",
                "{\"ok\":false,\"msg\":\"已保存，但连接失败：请检查密码或信号后重试\"}");
    Serial.println("[WiFi] connect failed after save");
  }
}

// GET /log?from=N[&clear=1] — 设备日志（串口诊断的镜像，供网页远程查看）。
// 返回自 seq>from 起的增量纯文本；每行带 [uptime_ms] 前缀。
// 响应头 X-Log-From 给出本次已取到的最大 seq+1，前端据此增量轮询。
void handleLog() {
  if (server.hasArg("clear")) {
    logClear();
    server.send(200, "text/plain; charset=utf-8", "");
    return;
  }
  uint32_t from = server.hasArg("from") ? (uint32_t)server.arg("from").toInt() : 0;
  char buf[2048];
  uint16_t n = logFetch(&from, buf, sizeof(buf));
  buf[n] = '\0';
  server.sendHeader("X-Log-From", String(from));
  server.send(200, "text/plain; charset=utf-8", String(buf));
}

void handleNotFound() {
  // captive portal：未知路径一律引到配网页（配网模式）；否则普通 404。
  if (g_provMode && !g_staUp) {
    server.sendHeader("Location", "/wifi", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain; charset=utf-8", "Not Found");
}


void setupArduinoOTA() {
  ArduinoOTA.setHostname(config::otaHostname);
  ArduinoOTA.setPassword(config::otaPassword);
  ArduinoOTA.onStart([]() { Serial.println("OTA update started"); });
  ArduinoOTA.onEnd([]()   { Serial.println("OTA update finished"); });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    Serial.printf("OTA progress: %u%%\r", p * 100 / t);
  });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA error[%u]\n", e); });
  ArduinoOTA.begin();
}

void setupWeb() {
  server.on("/",            HTTP_GET,  handleRoot);
  server.on("/status",      HTTP_GET,  handleStatus);
  server.on("/screenmodels",HTTP_GET,  handleScreenModelsGet);
  server.on("/settings",    HTTP_GET,  handleSettingsGet);
  server.on("/settings",    HTTP_POST, handleSettingsPost);
  server.on("/reset",       HTTP_POST, handleSettingsReset);
  server.on("/image",       HTTP_POST, handleImage, handleImageUpload);
  server.on("/test",        HTTP_POST, handleTest);
  server.on("/clear",       HTTP_POST, handleClear);
  server.on("/log",         HTTP_GET,  handleLog);

  // Wi-Fi 配网（captive portal）
  server.on("/wifi",        HTTP_GET,  handleWifiPage);
  server.on("/wifi/scan",   HTTP_GET,  handleWifiScan);
  server.on("/wifi/save",   HTTP_POST, handleWifiSave);
  server.onNotFound(handleNotFound);

  httpUpdater.setup(&server, "/update", "", "");
  server.begin();
  Serial.println("HTTP server ready: http://" +
                 (g_staUp ? WiFi.localIP() : WiFi.softAPIP()).toString());
}

void setup() {
  Serial.begin(115200);
  delay(200);

  store.begin();
  bool loaded = store.load();
  const ScreenDef& d = cfg->def();
  logf("[main] boot: settings=%s screen=%d (%s %ux%u) driver=%s\n",
       loaded ? "loaded" : "defaults",
       (int)cfg->screen,
       screenLabel(cfg->screen),
       d.width, d.height, cfg->driverLabel());

  applyDisplayConfig();

  // 必须先拉起 Wi-Fi 栈（STA 连接或 AP 热点），再启动 Web 服务：
  // server.begin() 要创建监听 socket，而 lwIP 的 tcpip 线程/邮箱要到
  // 首次调用 WiFi API（mode/begin/softAP）才初始化；提前 socket() 会触发
  // lwIP 断言 "Invalid mbox" → abort 重启（死循环）。
  if (connectStoredWiFi(WIFI_CONNECT_TIMEOUT_MS)) {
    g_staUp = true;
    Serial.println("Wi-Fi IP: " + WiFi.localIP().toString());
  } else {
    startProvisioningAp(g_haveCreds ? "stored Wi-Fi unreachable"
                                    : "no stored Wi-Fi credentials");
  }

  // Web 服务任何模式都启动（配网页 /wifi 常驻，便于日后换网）。
  setupWeb();

  MDNS.begin(config::otaHostname);
  MDNS.addService("http", "tcp", 80);
  setupArduinoOTA();
}

void loop() {
  if (g_provMode) g_dns.processNextRequest();   // captive portal 解析

  // 有凭据但未连上：后台周期重试（热点保持可用，不阻塞 Web 服务）
  if (g_haveCreds && !g_staUp &&
      millis() - g_lastTry > WIFI_RETRY_INTERVAL_MS) {
    g_lastTry = millis();
    Serial.println("[WiFi] background retry...");
    WiFi.begin(store.wifiSsid().c_str(), store.wifiPassword().c_str());
  }

  if (WiFi.status() == WL_CONNECTED && !g_staUp) {
    g_staUp = true;
    Serial.println("[WiFi] connected: " + WiFi.localIP().toString());
    if (g_provMode) stopProvisioningAp();       // 重试成功 → 收起热点
  } else if (WiFi.status() != WL_CONNECTED && g_staUp && !g_provMode) {
    g_staUp = false;
    startProvisioningAp("connection lost");     // 运行中掉线 → 回落热点
  }

  ArduinoOTA.handle();
  server.handleClient();
  delay(10);
}
