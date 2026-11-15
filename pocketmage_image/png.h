#pragma once

#include <cstdint>

#include <PNGdec.h>

#include "gray_downscaler.h"

// PNG decode using bitbank2/PNGdec
class PngDecoder {
public:
  // Decode [off, off+len) of path (len 0 = rest of file) into a 1bpp,
  // MSB-first framebuffer of bufW x bufH
  bool decodeTo1Bit(const char *path, uint32_t off, uint32_t len, uint8_t *buf, int bufW,
                    int bufH, int boxX, int boxY, int boxSize, bool fill);

  // Image size from IHDR, without constructing a decoder.
  bool getDimensions(const char *path, uint32_t off, uint32_t len, int *w, int *h);

private:
  struct DrawCtx {
    PNG *png;
    GrayDownscaler ds;
    uint16_t *rgb = nullptr; // one line of RGB565
    uint8_t *gray = nullptr; // one line of gray
  };

  static void *openCb(const char *filename, int32_t *pSize);
  static void closeCb(void *pHandle);
  static int32_t readCb(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen);
  static int32_t seekCb(PNGFILE *pFile, int32_t iPosition);
  static int drawCb(PNGDRAW *pDraw);
};
