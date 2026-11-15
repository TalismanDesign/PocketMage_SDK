#include "png.h"

#include <cstdlib>
#include <cstring>
#include <new>

#include "esp_log.h"
#include "region_stream.h"

static const char *TAG = "PngDecoder";

// Alpha is blended onto this colour by getLineAsRGB565 (0x00RRGGBB).
static constexpr uint32_t kBackground = 0x00FFFFFF;

void *PngDecoder::openCb(const char *filename, int32_t *pSize) {
  RegionStream *rs = regionOpen(filename);
  if (rs)
    *pSize = (int32_t)rs->len;
  return rs;
}

void PngDecoder::closeCb(void *pHandle) { regionClose(static_cast<RegionStream *>(pHandle)); }

int32_t PngDecoder::readCb(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  return regionRead(static_cast<RegionStream *>(pFile->fHandle), pBuf, iLen);
}

int32_t PngDecoder::seekCb(PNGFILE *pFile, int32_t iPosition) {
  return regionSeek(static_cast<RegionStream *>(pFile->fHandle), iPosition) ? iPosition : -1;
}

int PngDecoder::drawCb(PNGDRAW *d) {
  auto *c = static_cast<DrawCtx *>(d->pUser);
  c->png->getLineAsRGB565(d, c->rgb, PNG_RGB565_LITTLE_ENDIAN, kBackground);
  for (int x = 0; x < d->iWidth; x++) {
    uint16_t p = c->rgb[x];
    int r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
    c->gray[x] = (uint8_t)((r * 527 * 77 + g * 259 * 150 + b * 527 * 29) >> 14);
  }
  c->ds.pushRow(c->gray);
  return 1;
}

bool PngDecoder::decodeTo1Bit(const char *path, uint32_t off, uint32_t len, uint8_t *buf,
                              int bufW, int bufH, int boxX, int boxY, int boxSize, bool fill) {
  auto *png = new (std::nothrow) PNG(); // ~45 KB: never on the stack
  if (!png)
    return false;

  regionSet(off, len);
  int rc = png->open(path, openCb, closeCb, readCb, seekCb, drawCb);
  regionClear();
  if (rc != PNG_SUCCESS || png->isInterlaced()) {
    if (rc == PNG_TOO_BIG)
      ESP_LOGW(TAG, "too wide for PNG_MAX_BUFFERED_PIXELS: %s", path);
    if (rc == PNG_SUCCESS)
      png->close();
    delete png;
    return false;
  }

  int imgW = png->getWidth(), imgH = png->getHeight();
  DrawCtx ctx;
  ctx.png = png;
  ctx.rgb = (uint16_t *)malloc((size_t)imgW * sizeof(uint16_t));
  ctx.gray = (uint8_t *)malloc((size_t)imgW);
  bool ok = ctx.rgb && ctx.gray && ctx.ds.begin(buf, bufW, bufH, boxX, boxY, boxSize, imgW, imgH, fill);

  if (ok) {
    ok = png->decode(&ctx, 0) == PNG_SUCCESS;
    ctx.ds.finish();
  }

  png->close();
  delete png;
  free(ctx.rgb);
  free(ctx.gray);
  return ok;
}

// IHDR is always the first chunk, so read it directly.
bool PngDecoder::getDimensions(const char *path, uint32_t off, uint32_t len, int *w, int *h) {
  regionSet(off, len);
  RegionStream *rs = regionOpen(path);
  regionClear();
  if (!rs)
    return false;

  uint8_t b[24];
  bool ok = regionRead(rs, b, 24) == 24 && b[0] == 0x89 && memcmp(b + 1, "PNG", 3) == 0 &&
            memcmp(b + 12, "IHDR", 4) == 0;
  regionClose(rs);
  if (!ok)
    return false;

  int iw = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
  int ih = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
  if (iw <= 0 || ih <= 0)
    return false;
  if (w)
    *w = iw;
  if (h)
    *h = ih;
  return true;
}
