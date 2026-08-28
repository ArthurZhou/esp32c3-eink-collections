// renderer/draw.cpp — 渲染层绘制工具实现。

#include "draw.h"

#include <Arduino.h>  // pgm_read_byte

namespace draw {

void fillRect(uint8_t* plane0, uint8_t* plane1, const ScreenDef& d,
              int16_t x, int16_t y, int16_t w, int16_t h, uint8_t idx) {
  for (int16_t yy = y; yy < y + h; yy++)
    for (int16_t xx = x; xx < x + w; xx++)
      setPixelInk(plane0, plane1, d, xx, yy, idx);
}

uint16_t textWidth(const Font& font, const char* text, uint8_t scale) {
  uint16_t w = 0;
  while (*text++) w += font.advance;
  return w * scale;
}

void drawString(uint8_t* plane0, uint8_t* plane1, const ScreenDef& d,
                const Font& font, int16_t x, int16_t y, const char* text,
                uint8_t idx, uint8_t scale) {
  int16_t cx = x;
  while (*text) {
    uint8_t ch = (uint8_t)*text++;
    if (ch < font.first || ch > font.last) ch = '?';
    const uint8_t* glyph = font.glyphs[ch - font.first];
    for (uint8_t row = 0; row < font.height; row++) {
      uint8_t bits = pgm_read_byte(&glyph[row]);
      for (uint8_t col = 0; col < font.width; col++) {
        if (bits & (0x40 >> col)) {
          if (scale > 1) {
            for (uint8_t sy = 0; sy < scale; sy++)
              for (uint8_t sx = 0; sx < scale; sx++)
                setPixelInk(plane0, plane1, d, cx + col * scale + sx,
                            y + row * scale + sy, idx);
          } else {
            setPixelInk(plane0, plane1, d, cx + col, y + row, idx);
          }
        }
      }
    }
    cx += font.advance * scale;
  }
}

}  // namespace draw
