---
title: "Audio and media"
description: "Shared playback, PCM timing, persistent preferences, streamed tags, and USB ownership."
---

# Audio and media

Native apps use `P_AUDIO` and `P_MEDIA`. ELF apps include `pm_app_api.h` and use
generated wrappers over the same services. The OS owns decoding, output drivers,
SD access, metadata parsing, and preferences.

## Playback

Call `pm_audio_begin()` before playback, then `pm_audio_play(path)`. Save
`pm_audio_get_playback_id()` for each request and query
`pm_audio_get_playback_result(id)` for `PENDING`, `ENDED`, `FAILED`, or `CANCELLED`.
Playback state is queried separately with `pm_audio_get_state()`. Natural EOF
and manual stop are different results; apps should advance playlists only once
for an ended request.

Every `play` call creates a new ID and resets PCM time, including rejected calls.
Null, empty, or overlong paths and initialization, shutdown, task, or queue
failures report `FAILED`; `pm_audio_get_last_error()` describes the rejection.
The previous clip is superseded, and its queued work or late decoder completion
cannot turn the rejected request into `ENDED`.

`pm_audio_pause()`, `pm_audio_resume()`, and `pm_audio_stop_playback()` control
playback within the session. `pm_audio_stop_checked()` ends the service and
returns false if shutdown is incomplete. It retains live resources so the
control task can retry; app code must remain resident until callbacks and
workers are finished. The ELF runner handles this on normal app return.

`pm_audio_get_position()` is the encoded byte position.
`pm_audio_get_position_ms()` reports elapsed decoded PCM frames, accounting for
sample rate and channels. It avoids estimating elapsed time from compressed
bytes. Gain is applied to PCM samples before either sink.

## Output and preferences

`pm_audio_set_output()` selects `PM_AUDIO_OUTPUT_PIEZO`, `PM_AUDIO_OUTPUT_USB`,
or `PM_AUDIO_OUTPUT_AUTO`. `pm_audio_get_output()` is the preference;
`pm_audio_get_active_output()` is the currently available sink. USB falls back
to piezo when a compatible speaker is unavailable.

`pm_audio_set_volume_level()` accepts 0 through 255 and maps to the shared
33-step gain range. Read back `pm_audio_get_volume_level()` for the quantized
level. The older signed gain API remains available.

`pm_audio_load_settings()` and `pm_audio_save_settings()` use the NVS namespace
`PMAudio`, key `settings`, with versioned gain/output data. `begin()` loads it
once per service instance. Invalid data or I/O failure returns false and leaves
the current settings available. Apps share these preferences rather than saving
separate audio configuration on SD.

## Streamed metadata and artwork

`pm_media_probe(path, &info)` fills `pm_media_info` on success and leaves the
caller output unchanged on failure. It uses bounded reads through the SD
gateway rather than loading the encoded file into RAM. Fields include UTF-8
title, artist, album, duration, MPEG audio offset, and an embedded JPEG/PNG
region. Zero duration or cover length means unavailable.

The parser handles ID3v2.3/v2.4 tags and MPEG duration, including Xing/VBRI
headers where available. Unsupported tag transforms leave the affected fields
empty. Malformed tags, invalid paths, and I/O errors fail the probe. Pass the
cover offset and length to the image-region decoder; the app need not implement
ID3 scanning or cache host-owned metadata code.

## Connector lifecycle

`pm_usb` arbitrates the S3 internal PHY between DebugSerial, Audio, Keyboard,
and Storage. Role changes are serialized; incomplete shutdown retains the
current owner.

- Debug serial is the unclaimed default. Opening Piezo or unattached Auto audio
  does not install a USB host.
- An attached accessory receives a five-second audio enumeration window. A
  failed or unsupported probe is suppressed until detach or output selection
  changes, restoring serial and allowing keyboard polling.
- The OS checks pending HID teardown before audio probing. HID shutdown stops
  new connections, joins its connection worker, disables the root port, drains
  device disconnects, uninstalls HID, and joins the USB library worker. Only then
  may the control task perform final library drainage and release the PHY.
- Starting Auto/USB audio or selecting USB takes over an already attached
  accessory from HID through that same checked shutdown. A failed handoff is
  retried on subsequent connector polls; it does not suppress audio probing.
- Transfer mode stops audio and HID before reserving Storage and unmounting
  FAT. Deep sleep also requires checked audio/HID shutdown.

Call connector lifecycle functions from the OS control task, never from a USB
callback. Apps normally select output and let the OS poll attachment. Physical
enumeration, serial re-enumeration, audio output, and sleep must also be tested
on the target board; native lifecycle tests cannot prove electrical behavior.
