// drivers/pditc.cpp — Pervasive Displays iTC 驱动 IC 实现。
//
// 与 UC8151D/SSD1619 的差异集中在此文件：
//   · init()：panel=0xCF 两字节 + 0x50 写两字节（PDI 应用笔记 §3）；
//   · _waitBusy()：BUSY_N 低电平=空闲、高电平=忙，与 UC8151D 相反，
//     故覆写等待逻辑并禁用基类局刷路径（iTC OTP LUT 不支持本项目的
//     寄存器 LUT 软灰阶配方）。

#include "pditc.h"

#include <Arduino.h>
#include <SPI.h>

Pditc::Pditc(uint16_t w, uint16_t h, bool invertPlane1,
             int8_t cs, int8_t dc, int8_t rst, int8_t busy,
             uint32_t spi_freq)
    : EpdSpiBase(w, h, /*planeCount*/ 2, invertPlane1,
                 /*panelByte*/ 0xCF, cs, dc, rst, busy, spi_freq) {}

void Pditc::init() {
  pinMode(_cs, OUTPUT);
  digitalWrite(_cs, HIGH);
  pinMode(_dc, OUTPUT);
  digitalWrite(_dc, LOW);
  pinMode(_rst, OUTPUT);
  digitalWrite(_rst, HIGH);
  pinMode(_busy, INPUT_PULLUP);   // BUSY_N：上拉后读出高=忙

  // 复位：PDI 应用笔记 §2 —— RES# 拉低 → 延时 → 拉高 → 软复位 0x00,0x0E
  digitalWrite(_rst, LOW);
  delay(10);
  digitalWrite(_rst, HIGH);
  delay(10);
  _writeCmd(0x00);                // Soft-reset（PSR = 0x0E）
  _writeData(0x0E);
  delay(10);

  // Panel setting：0xCF B/W/R，LUT from OTP（PDI 笔记 §3 "Other Size"）
  _writeCmd(0x00);
  _writeData(0xCF);

  // 显存窗口 — 与 framebuffer 一一对应（X: 0..w/8-1，Y: 0..h-1）
  uint16_t xEnd = _w / 8 - 1;
  uint16_t yEnd = _h - 1;
  _writeCmd(0x44);                // Set RAM X start/end
  _writeData(0x00);
  _writeData(xEnd & 0xFF);
  _writeCmd(0x45);                // Set RAM Y start/end
  _writeData(0x00);
  _writeData(0x00);
  _writeData(yEnd & 0xFF);
  _writeData((yEnd >> 8) & 0x01);

  // 数据指针复位到 (0,0)
  _writeCmd(0x4E);
  _writeData(0x00);
  _writeCmd(0x4F);
  _writeData(0x00);
  _writeData(0x00);

  // VCOM and data interval：iTC 需两字节（PDI 笔记推荐 0x89, 0x07）
  _writeCmd(0x50);
  _writeData(0x89);
  _writeData(0x07);
}

void Pditc::_waitBusy(const char* what, uint32_t timeout_ms) {
  uint32_t start = millis();
  while (digitalRead(_busy) == HIGH) {   // iTC：HIGH = busy（与 UC8151D 相反）
    if (millis() - start > timeout_ms) {
      Serial.print("Busy Timeout! ");
      Serial.println(what);
      return;
    }
    delay(10);
  }
}
