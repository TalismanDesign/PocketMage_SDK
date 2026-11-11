#include "jpeg.h"

#include <cstdlib>
#include <cstring>
#include <new>

#include "region_stream.h"

void *JpegDecoder::openCb(const char *filename, int32_t *pSize) {
  RegionStream *rs = regionOpen(filename);
  if (rs)
    *pSize = (int32_t)rs->len;
  return rs;
}

void JpegDecoder::closeCb(void *pHandle) { regionClose(static_cast<RegionStream *>(pHandle)); }

int32_t JpegDecoder::readCb(JPEGFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  return regionRead(static_cast<RegionStream *>(pFile->fHandle), pBuf, iLen);
}

int32_t JpegDecoder::seekCb(JPEGFILE *pFile, int32_t iPosition) {
  return regionSeek(static_cast<RegionStream *>(pFile->fHandle), iPosition) ? iPosition : -1;
}

void JpegDecoder::flushStrip(DrawCtx *c) {
  for (int r = 0; r < c->stripRows && c->stripY + r < c->decH; r++)
    c->ds.pushRow(c->strip + r * c->decW);
  c->stripRows = 0;
}

// JPEGDEC draws MCU blocks left to right; collect one MCU row, then feed
// its rows to the downscaler in order.
int JpegDecoder::drawCb(JPEGDRAW *d) {
  auto *c = static_cast<DrawCtx *>(d->pUser);
  if (c->stripRows && d->y != c->stripY)
    flushStrip(c);
  c->stripY = d->y;
  int rows = d->iHeight < 16 ? d->iHeight : 16;
  if (rows > c->stripRows)
    c->stripRows = rows;

  const uint8_t *src = (const uint8_t *)d->pPixels; // 8-bit gray, stride = iWidth
  int w = d->iWidthUsed;
  if (d->x + w > c->decW)
    w = c->decW - d->x;
  for (int r = 0; r < rows && w > 0; r++)
    memcpy(c->strip + r * c->decW + d->x, src + r * d->iWidth, w);
  return 1;
}

bool JpegDecoder::decodeTo1Bit(const char *path, uint32_t off, uint32_t len, uint8_t *buf,
                               int bufW, int bufH, int boxX, int boxY, int boxSize, bool fill) {
  if (!buf || boxSize <= 0)
    return false;
  auto *jpg = new (std::nothrow) JPEGDEC(); // ~17.5 KB: never on the stack
  if (!jpg)
    return false;

  regionSet(off, len);
  bool ok = jpg->open(path, openCb, closeCb, readCb, seekCb, drawCb) == 1;
  regionClear();
  if (!ok) {
    delete jpg;
    return false;
  }

  int imgW = jpg->getWidth(), imgH = jpg->getHeight();
  // The side that has to cover the box: shortest for fill, longest for fit.
  int side = fill ? (imgW < imgH ? imgW : imgH) : (imgW > imgH ? imgW : imgH);

  // Largest library reduction that still leaves >= boxSize pixels, so the
  // downscaler only ever shrinks. Progressive files decode DC-only (1/8).
  int shift = 0;
  if (jpg->getJPEGType() == JPEG_MODE_PROGRESSIVE)
    shift = 3;
  else
    while (shift < 3 && (side >> (shift + 1)) >= boxSize)
      shift++;
  static const int kScaleOpt[] = {0, JPEG_SCALE_HALF, JPEG_SCALE_QUARTER, JPEG_SCALE_EIGHTH};

  DrawCtx ctx;
  ctx.decW = (imgW + (1 << shift) - 1) >> shift;
  ctx.decH = (imgH + (1 << shift) - 1) >> shift;
  ctx.strip = (uint8_t *)malloc((size_t)ctx.decW * 16);
  ok = ctx.strip &&
       ctx.ds.begin(buf, bufW, bufH, boxX, boxY, boxSize, ctx.decW, ctx.decH, fill);

  if (ok) {
    jpg->setPixelType(EIGHT_BIT_GRAYSCALE); // must come after open()
    jpg->setUserPointer(&ctx);
    ok = jpg->decode(0, 0, kScaleOpt[shift] | JPEG_LUMA_ONLY) == 1;
    flushStrip(&ctx);
    ctx.ds.finish();
  }

  jpg->close();
  delete jpg;
  free(ctx.strip);
  return ok;
}

bool JpegDecoder::getDimensions(const char *path, uint32_t off, uint32_t len, int *w, int *h) {
  regionSet(off, len);
  RegionStream *rs = regionOpen(path);
  regionClear();
  if (!rs)
    return false;

  bool ok = false;
  uint8_t b[5];
  int32_t pos = 2; // after FF D8
  while (regionSeek(rs, pos) && regionRead(rs, b, 4) == 4 && b[0] == 0xFF) {
    uint8_t m = b[1];
    uint16_t segLen = (uint16_t)((b[2] << 8) | b[3]);
    // SOF0..SOF15 except DHT (C4), JPG (C8), DAC (CC)
    if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
      if (regionRead(rs, b, 5) == 5) {
        int ih = (b[1] << 8) | b[2];
        int iw = (b[3] << 8) | b[4];
        ok = iw > 0 && ih > 0;
        if (ok && w)
          *w = iw;
        if (ok && h)
          *h = ih;
      }
      break;
    }
    if (m == 0xD9 || m == 0xDA || segLen < 2)
      break; // EOI / start of scan: no SOF found
    pos += 2 + segLen;
  }

  regionClose(rs);
  return ok;
}
