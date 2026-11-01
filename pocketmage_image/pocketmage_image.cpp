#include "pocketmage_image.h"

#include <pocketmage.h>
#include <pocketmage_globals.h>

#include <cstring>
#include <cstdlib>

extern "C" {
#include "esp_jpeg_dec.h"
#include "esp_jpeg_common.h"
}

#include "PNGdec.h"

// 1-bit pixel packing: bit 7 is leftmost in the byte

static const char* TAG = "PocketMageImage";

PocketMageImage& PocketMageImage::getInstance() {
  static PocketMageImage instance;
  return instance;
}

PocketMageImage& P_IMAGE = PocketMageImage::getInstance();

PocketMageImage::PocketMageImage() : _info{0, 0, ImageFormat::None}, _loaded(false) {}

PocketMageImage::~PocketMageImage() {
  clear();
}

void PocketMageImage::clear() {
  _info = {0, 0, ImageFormat::None};
  _loaded = false;
}

bool PocketMageImage::probeFile_(File& f, ImageInfo& out) {
  if (!f) return false;
  uint8_t magic[12];
  int n = f.read(magic, sizeof(magic));
  f.seek(0);
  if (n < 4) return false;
  if (magic[0] == 0xFF && magic[1] == 0xD8) {
    out.format = ImageFormat::JPEG;
    out.width = 0;
    out.height = 0;
    return true;
  }
  if (n >= 8 && magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G') {
    out.format = ImageFormat::PNG;
    // Parse IHDR for dimensions
    if (n >= 24 && memcmp(magic + 12, "IHDR", 4) == 0) {
      out.width = ((int)magic[16] << 24) | ((int)magic[17] << 16) | ((int)magic[18] << 8) | (int)magic[19];
      out.height = ((int)magic[20] << 24) | ((int)magic[21] << 16) | ((int)magic[22] << 8) | (int)magic[23];
    } else {
      out.width = 0;
      out.height = 0;
    }
    return true;
  }
  return false;
}

bool PocketMageImage::probe(const char* path, ImageInfo& out) {
  if (!global_fs || !path) return false;
  File f = global_fs->open(path, FILE_READ);
  if (!f) return false;
  bool ok = probeFile_(f, out);
  f.close();
  if (ok) {
    _info = out;
    _loaded = true;
  }
  return ok;
}

// PNG callbacks
static void* pngOpenCb(const char* filename, int32_t* pSize) {
  File* f = new File(global_fs->open(filename, FILE_READ));
  if (!*f) { delete f; return nullptr; }
  *pSize = f->size();
  return f;
}

static void pngCloseCb(void* pHandle) {
  if (!pHandle) return;
  delete static_cast<File*>(pHandle);
}

static int32_t pngReadCb(PNGFILE* pFile, uint8_t* buf, int32_t iLen) {
  auto* f = static_cast<File*>(pFile->fHandle);
  return f ? f->read(buf, iLen) : 0;
}

static int32_t pngSeekCb(PNGFILE* pFile, int32_t iPos) {
  auto* f = static_cast<File*>(pFile->fHandle);
  if (!f) return -1;
  return f->seek(iPos) ? iPos : -1;
}

struct ImgRenderCtx {
  uint8_t* einkBuf;
  int bufW, bufH;
  int outX, outY;
  int fullW, fullH;
  int outW, outH;
};

struct PngImgRenderCtx : ImgRenderCtx {
  int pixelType;
  int16_t* errCur;
  int16_t* errNext;
};

static int pixelToGray(PNGDRAW* pDraw, int x) {
  uint8_t* p = pDraw->pPixels;
  PngImgRenderCtx* ctx = (PngImgRenderCtx*)pDraw->pUser;
  int type = ctx->pixelType;

  if (type == PNG_PIXEL_INDEXED) {
    uint8_t* pal = pDraw->pPalette;
    int idx = p[x];
    int r = pal[idx * 3];
    int g = pal[idx * 3 + 1];
    int b = pal[idx * 3 + 2];
    return (r * 77 + g * 150 + b * 29) >> 8;
  }
  if (type == PNG_PIXEL_TRUECOLOR || type == PNG_PIXEL_TRUECOLOR_ALPHA) {
    int i = x * (type == PNG_PIXEL_TRUECOLOR_ALPHA ? 4 : 3);
    int r = p[i];
    int g = p[i + 1];
    int b = p[i + 2];
    return (r * 77 + g * 150 + b * 29) >> 8;
  }
  if (type == PNG_PIXEL_GRAY_ALPHA)
    return p[x * 2];
  return p[x];
}

static int pngDrawCb(PNGDRAW* pDraw) {
  PngImgRenderCtx* ctx = (PngImgRenderCtx*)pDraw->pUser;
  if (!ctx || !ctx->einkBuf) return 0;

  int y = pDraw->y;
  int w = pDraw->iWidth;
  int fullW = ctx->fullW;
  int outH = ctx->outH;
  int outW = ctx->outW;
  int16_t* err = ctx->errCur;
  int16_t* nxt = ctx->errNext;

  int oy = y * outH / ctx->fullH;
  bool writeOutput = (oy < outH);

  int dstRowBytes = (ctx->bufW + 7) / 8;
  int prevOx = -1;

  for (int x = 0; x < w; x++) {
    int gray = pixelToGray(pDraw, x);
    int v = gray + err[x + 1];
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    int bit = v > 127 ? 1 : 0;
    int qErr = v - (bit ? 255 : 0);

    err[x + 2] += (qErr * 7) / 16;
    nxt[x] += (qErr * 5) / 16;
    nxt[x + 1] += (qErr * 1) / 16;
    if (x > 0) nxt[x - 1] += (qErr * 3) / 16;

    if (!writeOutput) continue;

    int ox = x * outW / fullW;
    if (ox == prevOx) continue;
    prevOx = ox;
    if (ox >= outW) continue;

    int einkX = ctx->outX + ox;
    int einkY = ctx->outY + oy;
    if (einkX >= ctx->bufW || einkY >= ctx->bufH) continue;

    int idx = einkY * dstRowBytes + (einkX >> 3);
    int totalBytes = ctx->bufH * dstRowBytes;
    if (idx < 0 || idx >= totalBytes) continue;

    if (!bit) {
      ctx->einkBuf[idx] |= (1 << (7 - (einkX & 7)));
    } else {
      ctx->einkBuf[idx] &= ~(1 << (7 - (einkX & 7)));
    }
  }

  ctx->errCur = nxt;
  ctx->errNext = err;
  memset(ctx->errNext, 0, (fullW + 2) * sizeof(int16_t));
  return 1;
}

bool PocketMageImage::decodePNG_(const char* path, uint8_t* buf, int bufW, int bufH,
                                 int outX, int outY, int maxSize) {
  auto* png = new (std::nothrow) PNG();
  if (!png) return false;

  if (png->open(path, pngOpenCb, pngCloseCb, pngReadCb, pngSeekCb, pngDrawCb) != PNG_SUCCESS) {
    delete png;
    return false;
  }

  int imgW = png->getWidth();
  int imgH = png->getHeight();
  if (imgW <= 0 || imgH <= 0) {
    png->close();
    delete png;
    return false;
  }

  float scale = (float)maxSize / (imgW > imgH ? imgW : imgH);
  int outW = (int)(imgW * scale);
  int outH = (int)(imgH * scale);
  if (outW < 1) outW = 1;
  if (outH < 1) outH = 1;

  int errSize = (imgW + 2) * sizeof(int16_t);
  auto* err0 = (int16_t*)malloc(errSize);
  auto* err1 = (int16_t*)malloc(errSize);
  if (!err0 || !err1) {
    if (err0) free(err0);
    if (err1) free(err1);
    png->close();
    delete png;
    return false;
  }
  memset(err0, 0, errSize);
  memset(err1, 0, errSize);

  PngImgRenderCtx ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.einkBuf = buf;
  ctx.bufW = bufW;
  ctx.bufH = bufH;
  ctx.outX = outX;
  ctx.outY = outY;
  ctx.fullW = imgW;
  ctx.fullH = imgH;
  ctx.outW = outW;
  ctx.outH = outH;
  ctx.pixelType = png->getPixelType();
  ctx.errCur = err0;
  ctx.errNext = err1;

  int rc = png->decode(&ctx, PNG_FAST_PALETTE);
  png->close();
  delete png;

  free(err0);
  free(err1);

  return rc == PNG_SUCCESS;
}

static bool isRgb565LittleEndian(void) {
  return true;
}

bool PocketMageImage::decodeJPEG_(const char* path, uint8_t* buf, int bufW, int bufH,
                                  int outX, int outY, int maxSize) {
  if (!global_fs) return false;
  File file = global_fs->open(path, FILE_READ);
  if (!file) return false;
  size_t fsize = file.size();
  if (fsize == 0 || fsize > 10 * 1024 * 1024) {
    file.close();
    return false;
  }
  uint8_t* jpgData = (uint8_t*)malloc(fsize);
  if (!jpgData) {
    file.close();
    return false;
  }
  size_t rd = file.read(jpgData, fsize);
  file.close();
  if (rd != fsize) {
    free(jpgData);
    return false;
  }

  jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
  config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
  config.block_enable = true;

  jpeg_dec_handle_t jpeg_dec = nullptr;
  jpeg_error_t ret = jpeg_dec_open(&config, &jpeg_dec);
  if (ret != JPEG_ERR_OK || !jpeg_dec) {
    free(jpgData);
    return false;
  }

  jpeg_dec_io_t io = {};
  io.inbuf = jpgData;
  io.inbuf_len = (int)fsize;
  io.inbuf_remain = (int)fsize;

  jpeg_dec_header_info_t out_info;
  ret = jpeg_dec_parse_header(jpeg_dec, &io, &out_info);
  if (ret != JPEG_ERR_OK) {
    jpeg_dec_close(jpeg_dec);
    free(jpgData);
    return false;
  }

  int imgW = (int)out_info.width;
  int imgH = (int)out_info.height;
  if (imgW <= 0 || imgH <= 0) {
    jpeg_dec_close(jpeg_dec);
    free(jpgData);
    return false;
  }

  float scale = (float)maxSize / (imgW > imgH ? imgW : imgH);
  int outW = (int)(imgW * scale);
  int outH = (int)(imgH * scale);
  if (outW < 1) outW = 1;
  if (outH < 1) outH = 1;

  int dstRowBytes = (bufW + 7) / 8;
  int totalDstBytes = bufH * dstRowBytes;

  // Allocate error buffers for Floyd-Steinberg at full image width
  int16_t* err0 = (int16_t*)malloc((imgW + 2) * sizeof(int16_t));
  int16_t* err1 = (int16_t*)malloc((imgW + 2) * sizeof(int16_t));
  if (!err0 || !err1) {
    if (err0) free(err0);
    if (err1) free(err1);
    jpeg_dec_close(jpeg_dec);
    free(jpgData);
    return false;
  }
  memset(err0, 0, (imgW + 2) * sizeof(int16_t));
  memset(err1, 0, (imgW + 2) * sizeof(int16_t));
  int16_t* errCur = err0;
  int16_t* errNext = err1;

  // Block mode: process in MCU strips
  int processCount = 0;
  jpeg_dec_get_process_count(jpeg_dec, &processCount);
  if (processCount == 0) processCount = 1;

  // Determine strip height from first call? Block decoder gives variable heights
  // We'll allocate a generous block buffer and let jpeg_dec_process tell us size
  int maxBlockH = 16;
  int blockPixels = imgW * maxBlockH;
  uint8_t* blockBuf = (uint8_t*)jpeg_calloc_align(blockPixels * 2, 16);
  if (!blockBuf) {
    free(err0); free(err1);
    jpeg_dec_close(jpeg_dec);
    free(jpgData);
    return false;
  }

  io.outbuf = blockBuf;
  int curY = 0;
  bool ok = true;

  for (int p = 0; p < processCount && curY < imgH && ok; p++) {
    ret = jpeg_dec_process(jpeg_dec, &io);
    if (ret != JPEG_ERR_OK) {
      ok = false;
      break;
    }
    int blockH = io.out_size / (imgW * 2);
    if (blockH <= 0) blockH = maxBlockH;
    if (blockH > maxBlockH) blockH = maxBlockH;
    if (curY + blockH > imgH) blockH = imgH - curY;

    // Process this strip: RGB565 -> grayscale -> Floyd-Steinberg -> scale to 1-bit
    for (int by = 0; by < blockH; by++) {
      int srcY = curY + by;
      int oy = srcY * outH / imgH;
      bool writeOutput = (oy < outH);

      uint8_t* rowPtr = blockBuf + by * imgW * 2;
      int prevOx = -1;

      for (int x = 0; x < imgW; x++) {
        uint8_t b0 = rowPtr[x * 2 + 0];
        uint8_t b1 = rowPtr[x * 2 + 1];
        // RGB565 LE: b0=LSB (G[2:0]<<5 | B[4:0]), b1=MSB (R[4:0]<<3 | G[5:3])
        uint16_t pix = (uint16_t)b0 | ((uint16_t)b1 << 8);
        int r = ((pix >> 11) & 0x1F) << 3;
        int g = ((pix >> 5) & 0x3F) << 2;
        int b = (pix & 0x1F) << 3;
        int gray = (r * 77 + g * 150 + b * 29) >> 8;

        int v = gray + errCur[x + 1];
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        int bit = v > 127 ? 1 : 0;
        int qErr = v - (bit ? 255 : 0);

        errCur[x + 2] += (qErr * 7) / 16;
        errNext[x] += (qErr * 5) / 16;
        errNext[x + 1] += (qErr * 1) / 16;
        if (x > 0) errNext[x - 1] += (qErr * 3) / 16;

        if (!writeOutput) continue;

        int ox = x * outW / imgW;
        if (ox == prevOx) continue;
        prevOx = ox;
        if (ox >= outW) continue;

        int einkX = outX + ox;
        int einkY = outY + oy;
        if (einkX < 0 || einkX >= bufW || einkY < 0 || einkY >= bufH) continue;

        int idx = einkY * dstRowBytes + (einkX >> 3);
        if (idx < 0 || idx >= totalDstBytes) continue;

        if (!bit) {
          buf[idx] |= (1 << (7 - (einkX & 7)));
        } else {
          buf[idx] &= ~(1 << (7 - (einkX & 7)));
        }
      }
      // swap error rows
      int16_t* tmpErr = errCur;
      errCur = errNext;
      errNext = tmpErr;
      memset(errNext, 0, (imgW + 2) * sizeof(int16_t));
    }

    curY += blockH;
  }

  jpeg_free_align(blockBuf);
  free(err0);
  free(err1);
  jpeg_dec_close(jpeg_dec);
  free(jpgData);
  return ok;
}

bool PocketMageImage::decodeFromFile(const char* path, uint8_t* buf, int bufW, int bufH,
                                     int outX, int outY, int maxSize) {
  if (!global_fs || !path || !buf) return false;
  File f = global_fs->open(path, FILE_READ);
  if (!f) return false;
  ImageInfo info;
  bool ok = probeFile_(f, info);
  f.close();
  if (!ok) return false;
  if (info.format == ImageFormat::JPEG) return decodeJPEG_(path, buf, bufW, bufH, outX, outY, maxSize);
  if (info.format == ImageFormat::PNG) return decodePNG_(path, buf, bufW, bufH, outX, outY, maxSize);
  return false;
}

bool PocketMageImage::decodeTo1Bit(const char* path, uint8_t* buf, int bufW, int bufH,
                                   int outX, int outY, int maxSize) {
  return decodeFromFile(path, buf, bufW, bufH, outX, outY, maxSize);
}
