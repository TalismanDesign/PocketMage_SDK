#pragma once

#include <cstdint>

#include <JPEGDEC.h>

#include "gray_downscaler.h"

// JPEG decode using bitbank2/JPEGDEC
class JpegDecoder {
public:
  // Decode [off, off+len) of path (len 0 = rest of file) into a 1bpp,
  // MSB-first framebuffer of bufW x bufH
  bool decodeTo1Bit(const char *path, uint32_t off, uint32_t len, uint8_t *buf, int bufW,
                    int bufH, int boxX, int boxY, int boxSize, bool fill);

  // Image size from the SOF marker, without constructing a decoder.
  bool getDimensions(const char *path, uint32_t off, uint32_t len, int *w, int *h);

private:
  // One MCU row of gray pixels, fed to the downscaler in row order.
  struct DrawCtx {
    GrayDownscaler ds;
    uint8_t *strip = nullptr; // decW x 16
    int decW = 0, decH = 0;
    int stripY = 0, stripRows = 0;
  };

  static void *openCb(const char *filename, int32_t *pSize);
  static void closeCb(void *pHandle);
  static int32_t readCb(JPEGFILE *pFile, uint8_t *pBuf, int32_t iLen);
  static int32_t seekCb(JPEGFILE *pFile, int32_t iPosition);
  static int drawCb(JPEGDRAW *pDraw);
  static void flushStrip(DrawCtx *c);
};
