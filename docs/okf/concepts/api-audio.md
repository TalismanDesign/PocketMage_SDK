---
type: api
title: "Audio and media"
description: "Shared playback, PCM timing, persistent preferences, streamed tags, and USB ownership."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/api/audio/"
path: /api/audio/
updated: 2026-11-26
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-11-26T04:24:34.149Z"
---
---
title: "Audio and media"
description: "Shared playback, PCM timing, persistent preferences, streamed tags, and USB ownership."
---

# Audio and media

Native apps use `P_AUDIO` and `P_MEDIA`. ELF apps include `pm_app_api.h` and use generated wrappers over the same services. The OS owns decoding, output drivers, SD access, metadata parsing, and preferences.

## Playback

Call `pm_audio_begin()` before playback, then `pm_audio_play(path)`. Save `pm_audio_get_playback_id()` for each request and query `pm_audio_get_playback_result(id)` for `PENDING`, `ENDED`, `FAILED`, or `CANCELLED`. Playback state is queried separately with `pm_audio_get_state()`. Natural EOF and manual stop are different results; apps should advance playlists only once for an ended request.

Every `play` call creates a new ID and resets PCM time, including rejected calls. Null, empty, or overlong paths and initialization, shutdown, task, or queue failures report `FAILED`; `pm_audio_get_last_error()` describes the rejection. The previous clip is superseded, and its queued work or late decoder completion cannot turn the rejected request into `ENDED`.

`pm_audio_play()` accepts `.mp3`, `.wav`, and `.ogg` paths. Ogg files can contain Vorbis or Opus audio, and unsupported extensions fail before the decoder opens. The decode task is recreated with a format-sized stack, so switching formats does not keep a larger stack resident than the current clip needs.

`pm_audio_pause()`, `pm_audio_resume()`, and `pm_audio_stop_playback()` control playback within the session. `pm_audio_stop_playback()` returns right away, before the decoder has freed its memory. If you need that memory back, for example to load artwork for the next song, call `pm_audio_stop_playback_checked(timeout_ms)` instead. It waits for cleanup and returns false if it times out; new playback is refused until cleanup finishes. `pm_audio_stop_checked()` ends the service and returns false if shutdown is incomplete. It retains live resources so the control task can retry; app code must remain resident until callbacks and workers are finished. The ELF runner handles this on normal app return.

`pm_audio_get_position()` is the encoded byte position. `pm_audio_get_position_ms()` reports elapsed decoded PCM frames, accounting for sample rate and channels. It avoids estimating elapsed time from compressed bytes. Gain is applied to PCM samples before either sink.

## Output and preferences

`pm_audio_set_output()` selects `PM_AUDIO_OUTPUT_PIEZO`, `PM_AUDIO_OUTPUT_USB`, or `PM_AUDIO_OUTPUT_AUTO`. `pm_audio_get_output()` is the preference; `pm_audio_get_active_output()` is the currently available sink. USB falls back to piezo when a compatible speaker is unavailable.

`pm_audio_set_volume_level()` sets the volume from 0 to 255 and `pm_audio_get_volume_level()` reads it back. Zero mutes, and 255 plays the audio at its original level (about -48 dB at 1, never louder than the source). USB and piezo keep separate volumes, since a speaker needs much less than the piezo. The piezo starts at 160 (about -18 dB) and USB at 51 (about -36 dB, shown as 20%).

These two calls change the output that will play next: USB if a speaker is playing, or connected with the output set to USB or Auto; otherwise the piezo. To change one output directly, use `pm_audio_set_volume_level_for(output, level)` and `pm_audio_get_volume_level_for(output)` with `PM_AUDIO_OUTPUT_PIEZO` or `PM_AUDIO_OUTPUT_USB`. Setting `PM_AUDIO_OUTPUT_AUTO` does nothing, and reading it returns the piezo volume.

USB plays the audio as decoded, in stereo and without filtering; only the volume is applied. The piezo mixes to mono and filters out low frequencies it cannot play.

`pm_audio_load_settings()` and `pm_audio_save_settings()` use the NVS namespace `PMAudio`, key `settings`, and store both volumes and the output. `begin()` loads them once. If the data is invalid or cannot be read, the call returns false and the current settings stay in use. Apps share these preferences rather than saving their own audio settings on SD.

Settings saved by older firmware, which had one shared volume, are converted on load: that volume becomes the piezo volume and USB starts at its default.

## Streamed metadata and artwork

`pm_media_probe(path, &info)` fills `pm_media_info` on success and leaves the caller output unchanged on failure. It uses bounded reads through the SD gateway rather than loading the encoded file into RAM. Fields include UTF-8 title, artist, album, duration, MPEG audio offset, and an embedded JPEG/PNG region. Zero duration or cover length means unavailable.

MP3 uses ID3v2.3/v2.4 tags and MPEG duration. Duration comes from the Xing/Info or VBRI header when the file has one; otherwise it is estimated from the average bitrate of the first 32 frames.

WAV reads RIFF fmt and data chunks, LIST/INFO tags, and an embedded ID3 chunk.

Ogg reads Vorbis or Opus identification and comment packets, and uses the final page granule for duration.

Unsupported tag transforms leave the affected fields empty. Malformed tags, invalid paths, and I/O errors fail the probe.

Ogg `METADATA_BLOCK_PICTURE` covers are exposed as a base64 byte region. The image decoder detects this region and decodes it while reading the file, so artwork is never cached in RAM. Pass the cover offset and length to the image-region decoder; the app need not implement tag scanning or cache host-owned metadata code.

## Connector lifecycle

`pm_usb` arbitrates the S3 internal PHY between DebugSerial, Audio, Keyboard, and Storage. Role changes are serialized; incomplete shutdown retains the current owner.

- Debug serial is the unclaimed default. Opening Piezo or unattached Auto audio does not install a USB host.
- An attached accessory receives a five-second audio enumeration window. A failed or unsupported probe is suppressed until detach or output selection is requested, restoring serial and allowing keyboard polling. A speaker that has worked once is detected again when replugged, even if the USB adapter stays attached.
- The OS checks pending HID teardown before audio probing. HID shutdown stops new connections, joins its connection worker, disables the root port, drains device disconnects, uninstalls HID, and joins the USB library worker. Only then may the control task perform final library drainage and release the PHY.
- Starting Auto/USB audio or selecting USB takes over an already attached accessory from HID through that same checked shutdown. A failed handoff is retried on subsequent connector polls; it does not suppress audio probing.
- Transfer mode stops audio and HID before reserving Storage and unmounting FAT. Deep sleep also requires checked audio/HID shutdown.

Call connector lifecycle functions from the OS control task, never from a USB callback. Apps normally select output and let the OS poll attachment. Physical enumeration, serial re-enumeration, audio output, and sleep must also be tested on the target board; native lifecycle tests cannot prove electrical behavior.

## USB speakers

USB speakers play 16-bit mono or stereo audio at up to 48 kHz. If the format is not supported, or there is not enough free memory to start the speaker, playback falls back to the piezo. Selecting an output again retries the speaker.
