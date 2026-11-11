#include <pocketmage.h>

#include <algorithm>

void drawTransportIcon(DisplayTarget target, int x, int y, int size, TransportIcon icon) {
  if (target != DisplayTarget::OLED && target != DisplayTarget::EINK)
    return;
  const int width = target == DisplayTarget::OLED ? OLED().width() : EINK().width();
  const int height = target == DisplayTarget::OLED ? OLED().height() : EINK().height();
  if (x < 0 || y < 0 || x >= width || y >= height || size < 3 ||
      size > width - x || size > height - y)
    return;
  const int edge = size - 1;
  auto box = [=](int left, int boxWidth) {
    if (target == DisplayTarget::OLED)
      OLED().box(left, y, boxWidth, size, true);
    else
      EINK().rect(left, y, left + boxWidth - 1, y + edge, true, true);
  };
  switch (icon) {
  case TransportIcon::Play:
    if (target == DisplayTarget::OLED)
      OLED().triangle(x, y, x, y + edge, x + edge, y + edge / 2, true);
    else
      EINK().triangle(x, y, x, y + edge, x + edge, y + edge / 2, true, true);
    break;
  case TransportIcon::Pause: {
    const int barWidth = std::max(1, size / 3);
    box(x, barWidth);
    box(x + size - barWidth, barWidth);
    break;
  }
  case TransportIcon::Stop:
    box(x, size);
    break;
  }
}
