# inkscreen

ESP32-C3 firmware for the [iDevice](https://blog.csdn.net/jetmie/article/details/157909899) e-paper board, including Wi-Fi OTA updates.

## Architecture

```
前端图片 → 减色+抖动(浏览器端, 目标=屏幕调色板) → 索引像素 → POST /image（唯一上传端点）
         → Screen 层(screens/，拆解为位平面；含灰阶条目时多轮局刷叠加)
         → IC 驱动层(drivers/，翻译为 SPI 指令时序并发送)
```

## Hardware

| E-paper | ESP32-C3 |
| --- | ---: |
| SCK | GPIO6 |
| MOSI | GPIO7 |
| CS | GPIO10 |
| DC | GPIO5 |
| RST | GPIO4 |
| BUSY | GPIO3 |

## Wi-Fi provisioning

Wi-Fi 凭据不写死在代码里，而是保存在设备 NVS 中（常见 IoT 配网方式）：

1. 首次上电（或已存网络连不上）时，设备自动发射开放热点 `inkscreen-XXXXXX`
   （后缀为 MAC 地址后 6 位）。
2. 手机/电脑连接该热点后会自动弹出配网页（captive portal）；
   也可手动访问 `http://192.168.4.1/wifi`。
3. 选择网络（自动扫描）并输入密码 → 设备验证通过后保存并重启接入；
   失败则保留热点供重试。
4. 之后随时可通过 `http://<设备IP>/wifi` 换网。
5. 运行中掉线会重新开热点，同时后台每 30 秒重试已存网络。

`src/config.h` 仅保留 OTA 主机名与密码。

## First upload

1. Set `otaPassword` in `src/config.h` (copy from `src/config.example.h`).
2. Build and upload the `esp32c3` environment over serial:

```text
pio run -e esp32c3 -t erase    # optional
pio run -e esp32c3 -t upload
```

3. Power on and follow the **Wi-Fi provisioning** steps above.

## OTA upload

```text
pio run -e ota -t upload
```

or

directly upload `firmware.bin` thru webpage

## Reference

Hardware & Part of software
https://blog.csdn.net/jetmie/article/details/157909899

