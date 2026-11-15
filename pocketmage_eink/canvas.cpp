#include "pocketmage_eink.h"

#include <algorithm>
#include <limits>

namespace {

bool driverCoordinate(int value) {
  return value >= std::numeric_limits<int16_t>::min() &&
         value <= std::numeric_limits<int16_t>::max();
}

} // namespace

int PocketmageEink::width() const { return display_.width(); }
int PocketmageEink::height() const { return display_.height(); }
void PocketmageEink::clear() { display_.fillScreen(GxEPD_WHITE); }

void PocketmageEink::pixel(int x, int y, bool ink) {
  if (x < 0 || y < 0 || x >= width() || y >= height())
    return;
  display_.drawPixel(x, y, ink ? GxEPD_BLACK : GxEPD_WHITE);
}

void PocketmageEink::rect(int x0, int y0, int x1, int y1, bool fill, bool ink) {
  if (!driverCoordinate(x0) || !driverCoordinate(y0) ||
      !driverCoordinate(x1) || !driverCoordinate(y1))
    return;
  if (x0 > x1)
    std::swap(x0, x1);
  if (y0 > y1)
    std::swap(y0, y1);
  const int w = x1 - x0 + 1;
  const int h = y1 - y0 + 1;
  if (!driverCoordinate(w) || !driverCoordinate(h))
    return;
  const uint16_t color = ink ? GxEPD_BLACK : GxEPD_WHITE;
  if (fill)
    display_.fillRect(x0, y0, w, h, color);
  else
    display_.drawRect(x0, y0, w, h, color);
}

void PocketmageEink::triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                             bool fill, bool ink) {
  for (int coordinate : {x0, y0, x1, y1, x2, y2}) {
    if (!driverCoordinate(coordinate))
      return;
  }
  const uint16_t color = ink ? GxEPD_BLACK : GxEPD_WHITE;
  if (fill)
    display_.fillTriangle(x0, y0, x1, y1, x2, y2, color);
  else
    display_.drawTriangle(x0, y0, x1, y1, x2, y2, color);
}
