#include "pocketmage_image.h"

#include "jpeg.h"
#include "png.h"
#include "region_stream.h"

static JpegDecoder s_jpeg;
static PngDecoder s_png;

PocketMageImage &PocketMageImage::getInstance() {
  static PocketMageImage instance;
  return instance;
}

PocketMageImage &P_IMAGE = PocketMageImage::getInstance();

PocketMageImage::PocketMageImage() : _info{0, 0, ImageFormat::None}, _loaded(false) {
}

PocketMageImage::~PocketMageImage() {
  clear();
}

void PocketMageImage::clear() {
  _info = {0, 0, ImageFormat::None};
  _loaded = false;
}

// Identify the format from the region's leading magic bytes.
static ImageFormat readFormat_(const char *path, uint32_t off, uint32_t len) {
  regionSet(off, len);
  RegionStream *rs = regionOpen(path);
  regionClear();
  if (!rs)
    return ImageFormat::None;

  uint8_t m[4];
  int32_t n = regionRead(rs, m, sizeof(m));
  regionClose(rs);
  if (n < 4)
    return ImageFormat::None;
  if (m[0] == 0xFF && m[1] == 0xD8)
    return ImageFormat::JPEG;
  if (m[0] == 0x89 && m[1] == 'P' && m[2] == 'N' && m[3] == 'G')
    return ImageFormat::PNG;
  return ImageFormat::None;
}

bool PocketMageImage::probe(const char *path, ImageInfo &out, uint32_t off, uint32_t len) {
  if (!path)
    return false;

  out = {0, 0, readFormat_(path, off, len)};
  bool ok = false;
  if (out.format == ImageFormat::JPEG)
    ok = s_jpeg.getDimensions(path, off, len, &out.width, &out.height);
  else if (out.format == ImageFormat::PNG)
    ok = s_png.getDimensions(path, off, len, &out.width, &out.height);
  if (!ok)
    return false;

  _info = out;
  _loaded = true;
  return true;
}

bool PocketMageImage::decodeRegionTo1Bit(const char *path, uint32_t off, uint32_t len,
                                         uint8_t *buf, int bufW, int bufH, int outX, int outY,
                                         int maxSize, bool fill) {
  if (!path || !buf)
    return false;
  switch (readFormat_(path, off, len)) {
  case ImageFormat::JPEG:
    return s_jpeg.decodeTo1Bit(path, off, len, buf, bufW, bufH, outX, outY, maxSize, fill);
  case ImageFormat::PNG:
    return s_png.decodeTo1Bit(path, off, len, buf, bufW, bufH, outX, outY, maxSize, fill);
  default:
    return false;
  }
}

bool PocketMageImage::decodeTo1Bit(const char *path, uint8_t *buf, int bufW, int bufH, int outX,
                                   int outY, int maxSize, bool fill) {
  return decodeRegionTo1Bit(path, 0, 0, buf, bufW, bufH, outX, outY, maxSize, fill);
}

bool PocketMageImage::decodeFromFile(const char *path, uint8_t *buf, int bufW, int bufH, int outX,
                                     int outY, int maxSize, bool fill) {
  return decodeTo1Bit(path, buf, bufW, bufH, outX, outY, maxSize, fill);
}
