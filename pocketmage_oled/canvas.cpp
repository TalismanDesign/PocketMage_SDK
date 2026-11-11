#include "pocketmage_oled.h"

#include <limits>

void PocketmageOled::init() { setupOled(); }
void PocketmageOled::send() { u8g2_.sendBuffer(); }
void PocketmageOled::clear() { u8g2_.clearBuffer(); }
int PocketmageOled::width() const { return u8g2_.getDisplayWidth(); }
int PocketmageOled::height() const { return u8g2_.getDisplayHeight(); }

void PocketmageOled::box(int x, int y, int w, int h, bool fill) {
  constexpr int kMaxCoordinate = std::numeric_limits<u8g2_uint_t>::max();
  if (x < 0 || y < 0 || x > kMaxCoordinate || y > kMaxCoordinate ||
      w <= 0 || h <= 0 || w > kMaxCoordinate || h > kMaxCoordinate)
    return;
  if (fill)
    u8g2_.drawBox(x, y, w, h);
  else
    u8g2_.drawFrame(x, y, w, h);
}

void PocketmageOled::triangle(int x0, int y0, int x1, int y1, int x2, int y2, bool fill) {
  constexpr int kMaxVertex = std::numeric_limits<int16_t>::max();
  for (int coordinate : {x0, y0, x1, y1, x2, y2}) {
    if (coordinate < 0 || coordinate > kMaxVertex ||
        coordinate > std::numeric_limits<u8g2_uint_t>::max())
      return;
  }
  if (fill) {
    u8g2_.drawTriangle(x0, y0, x1, y1, x2, y2);
  } else {
    u8g2_.drawLine(x0, y0, x1, y1);
    u8g2_.drawLine(x1, y1, x2, y2);
    u8g2_.drawLine(x2, y2, x0, y0);
  }
}
