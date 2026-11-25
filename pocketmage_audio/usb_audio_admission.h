#pragma once

#include <cstddef>
#include <cstdint>
#include <usb_stream_limits.h>

/** DMA heap checks for the pinned usb_stream speaker configuration.
 * Each figure mirrors one allocation in the vendored driver. Checks are snapshots,
 * so another task can still allocate in between.
 * See https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_host/usb_stream.html
 */
namespace pm_audio {

/** Free DMA-capable heap and its largest block, as reported by heap_caps. */
struct DmaSnapshot {
  size_t freeBytes;
  size_t largestBlock;
};

/** Speaker URB sizes for one PCM format. */
struct SpeakerFootprint {
  uint32_t packetBytes; ///< One 1 ms isochronous packet.
  uint32_t urbBytes;    ///< Largest single allocation: one URB data buffer.
  uint32_t totalBytes;  ///< All URB headers, data buffers, and the URB list.
};

/** Per-block TLSF bookkeeping and alignment rounding in multi_heap. */
constexpr uint32_t kHeapBlockOverhead = 16;
/** Heap kept free after the check so decoder and UI allocations still succeed. */
constexpr uint32_t kAdmissionHeadroom = 4 * 1024;
/** sizeof(urb_t) without isochronous descriptors, rounded up (usb_private.h). */
constexpr uint32_t kUrbHeaderBytes = 96;
/** sizeof(usb_isoc_packet_desc_t): three 32-bit fields (usb_types_stack.h). */
constexpr uint32_t kIsocDescriptorBytes = 12;
/** usb_stream task stacks: processing and stream handler (usb_stream_wrapper/library.json). */
constexpr uint32_t kUsbTaskStackBytes = 3072;
constexpr uint32_t kUsbTaskCount = 2;
/** Pre-allocated control URB: setup packet plus CONFIG_CTRL_TRANSFER_DATA_MAX_BYTES. */
constexpr uint32_t kControlUrbBytes = kUrbHeaderBytes + 8 + 1024;
/** Event queues (8 + 32 entries of 16 bytes), event group, mutexes, device structs, HCD port. */
constexpr uint32_t kHostObjectBytes = 2 * 1024;
/** Heap the driver takes once a speaker enumerates. */
constexpr uint32_t kEnumerationBytes = 8 * 1024;
/** FreeRTOS TCB plus the stack and TCB block overheads of one task. */
constexpr uint32_t kTaskControlBytes = 512;

/** Heap the lazily created decode task takes: its stack plus control block. */
constexpr uint32_t decodeTaskBytes(uint32_t stackBytes) {
  return stackBytes + kTaskControlBytes;
}

/** Return the speaker URB footprint, or false for a PCM format the wrapper does not stream.
 * packetBytes matches the driver's integer calculation in _apply_stream_config().
 */
inline bool speakerFootprint(int rate, int bits, int channels, SpeakerFootprint &out) {
  constexpr uint32_t kMillisecondsPerSecond = 1000;
  if (rate < 1000 || rate > 48000 || bits != 16 || (channels != 1 && channels != 2))
    return false;
  const uint32_t packetBytes =
      uint32_t(channels) * uint32_t(rate) / kMillisecondsPerSecond * uint32_t(bits) / 8;
  const uint32_t dataBytes = CONFIG_UAC_SPK_ST_MAX_MS_DEFAULT * packetBytes + kHeapBlockOverhead;
  const uint32_t headerBytes = kUrbHeaderBytes +
                               CONFIG_UAC_SPK_ST_MAX_MS_DEFAULT * kIsocDescriptorBytes +
                               kHeapBlockOverhead;
  const uint32_t listBytes = CONFIG_NUM_ISOC_SPK_URBS * sizeof(uint32_t) + kHeapBlockOverhead;
  out.packetBytes = packetBytes;
  out.urbBytes = dataBytes;
  out.totalBytes = CONFIG_NUM_ISOC_SPK_URBS * (dataBytes + headerBytes) + listBytes;
  return true;
}

/** Bytes usb_streaming_start() allocates for a speaker-only host with this ring size. */
constexpr uint32_t hostStartBytes(uint32_t ringBytes) {
  return ringBytes + kHeapBlockOverhead +
         kUsbTaskCount * (kUsbTaskStackBytes + kHeapBlockOverhead) + kControlUrbBytes +
         kHeapBlockOverhead + kHostObjectBytes;
}

/** Host start plus the speaker's enumeration, held before the first resume. */
constexpr uint32_t hostConnectedBytes(uint32_t ringBytes) {
  return hostStartBytes(ringBytes) + kEnumerationBytes;
}

/** True when CTRL_RESUME can allocate every speaker URB with headroom to spare. */
constexpr bool canResumeSpeaker(DmaSnapshot heap, const SpeakerFootprint &speaker) {
  return heap.largestBlock >= speaker.urbBytes &&
         heap.freeBytes >= speaker.totalBytes + kAdmissionHeadroom;
}

/** DMA heap the MP3 decoder takes from open to first PCM, measured on PM_BETA.
 * Used for every codec, since only MP3 has been measured.
 */
constexpr uint32_t kMp3DecoderBytes = 29616;

/** Heap held by a clip's read and PCM buffers, each one malloc. */
constexpr uint32_t decodeBufferBytes(uint32_t readBytes, uint32_t pcmBytes) {
  return readBytes + kHeapBlockOverhead + pcmBytes + kHeapBlockOverhead;
}

/** True when starting the host now leaves room for a 48 kHz stereo speaker.
 * decodeReserve is the clip memory not yet allocated, or 0 while a clip is
 * decoding, so the host does not start only to fall back to piezo on every clip.
 */
inline bool canHostPlay(DmaSnapshot heap, uint32_t ringBytes, uint32_t decodeReserve) {
  SpeakerFootprint worst{};
  if (!speakerFootprint(48000, 16, 2, worst))
    return false;
  return heap.largestBlock >= ringBytes + kHeapBlockOverhead &&
         heap.largestBlock >= worst.urbBytes &&
         heap.freeBytes >=
             hostConnectedBytes(ringBytes) + worst.totalBytes + decodeReserve + kAdmissionHeadroom;
}

/** DMA allocation hooks for trialSpeakerUrbs(); heap_caps_calloc/free with MALLOC_CAP_DMA on
 * device. */
struct DmaAllocator {
  void *(*calloc)(size_t count, size_t size);
  void (*free)(void *block);
};

/** True when every speaker URB block can be allocated from the heap as it is now.
 * Makes the same calls in the same order as _usb_urb_list_alloc() and frees them
 * all, so issue the resume right after with no other heap use.
 */
inline bool trialSpeakerUrbs(const SpeakerFootprint &speaker, const DmaAllocator &heap) {
  constexpr size_t kBlockCount = 1 + 2 * CONFIG_NUM_ISOC_SPK_URBS;
  constexpr size_t kHeaderBytes =
      kUrbHeaderBytes + CONFIG_UAC_SPK_ST_MAX_MS_DEFAULT * kIsocDescriptorBytes;
  void *blocks[kBlockCount] = {};
  size_t count = 0;
  bool allocated = true;
  blocks[count] = heap.calloc(CONFIG_NUM_ISOC_SPK_URBS, sizeof(uint32_t));
  allocated = blocks[count] != nullptr;
  if (allocated)
    ++count;
  for (size_t urb = 0; allocated && urb < CONFIG_NUM_ISOC_SPK_URBS; ++urb) {
    blocks[count] = heap.calloc(1, kHeaderBytes);
    allocated = blocks[count] != nullptr;
    if (!allocated)
      break;
    ++count;
    blocks[count] = heap.calloc(CONFIG_UAC_SPK_ST_MAX_MS_DEFAULT, speaker.packetBytes);
    allocated = blocks[count] != nullptr;
    if (allocated)
      ++count;
  }
  while (count > 0)
    heap.free(blocks[--count]);
  return allocated;
}

} // namespace pm_audio
