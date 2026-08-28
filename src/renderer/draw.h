// renderer/draw.h — 渲染层绘制工具（纯帧缓冲操作，不触碰硬件 / SPI）。
//
// 颜色一律用"调色板索引"（index），具体墨色与位平面的映射交给
// screen_types.h 的 setPixelInk——因此任意黑白 / 黑白红 / 四色屏都通用，
// 不再写死墨码。用于测试图案等内置绘制。

#pragma once

#include <stdint.h>

#include "../screens/screen_types.h"
#include "font5x7.h"

namespace draw {

// 实心矩形。idx = 调色板索引。
void fillRect(uint8_t* plane0, uint8_t* plane1, const ScreenDef& d,
              int16_t x, int16_t y, int16_t w, int16_t h, uint8_t idx);

// 画字符串（可缩放）。idx = 调色板索引。
void drawString(uint8_t* plane0, uint8_t* plane1, const ScreenDef& d,
                const Font& font, int16_t x, int16_t y, const char* text,
                uint8_t idx, uint8_t scale = 1);

// 字符串像素宽（用于居中 / 对齐）。
uint16_t textWidth(const Font& font, const char* text, uint8_t scale = 1);

}  // namespace draw
