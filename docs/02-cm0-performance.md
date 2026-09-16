# CM0 Graphics and Playback Verification

[Back to README](../README.md) · [架构总览](00-overview.md)

Measured on 2026-09-08 using the CM0 at 192.168.8.206, BCM2837 / 512 MB,
VC4 V3D 2.1 and OpenGL ES 2.0 (Mesa 26.2.0). The CM0 datasheet lists
OpenGL ES 1.1/2.0 and H.264 1080p30 codec capability:
https://edatec.cn/docs/assets/zh/cm0/cm0+cm0-dev-board-datasheet-zh.pdf

## Active Pipeline

H.264 -> FFmpeg V4L2 M2M (`bcm2835-codec`) -> libmpv OpenGL FBO ->
LVGL external texture -> DRM/GBM/EGL -> display.

The runtime reports `hwdec=v4l2m2m` and `VC4 V3D 2.1`, confirming hardware
decoding and GPU rendering. libmpv provides the media pipeline; GStreamer is
not required for these hardware features. UI primitives are software-rasterized
and cached as textures; OpenGL handles composition and display rotation.
YUV planes are still uploaded to GPU textures, so this is not DMA-BUF zero-copy.
Encoding is not part of video playback and was not added or benchmarked.

## Initial Offline Measurements

Offline samples use a moving H.264 High-profile test pattern with AAC audio,
30 fps, 30 seconds long. Both 720p and 1080p contain the same visual sequence.
The table shows stable five-second intervals after startup. FPS is the number
of new frames rendered by libmpv, not a measurement of panel scanout.

| Full Application, Portrait | Rendered FPS | New Drops in Stable Playback |
| --- | ---: | ---: |
| 1280x720 at 30 fps | 30.0-30.2 | 0 |
| 1920x1080 at 30 fps | about 23 | about 7 per second |

720p is the default for this reason. Initial startup can drop frames while
libraries, shaders and decoder buffers are initialized. Network playback can
also be limited by bandwidth or remote buffering; these offline tests exclude
that variable.

An independent GPU player reaches approximately 30 fps in its stable interval
at 1080p, but the complete application's measured 1080p performance is lower.
Disabling frame dropping in the complete application makes its media clock
fall behind wall time; that workaround is not enabled.

Initial profiling at 1080p found approximately 10 ms per video render and 2 ms per LVGL
refresh, with no significant EGL swap, GBM lock or DRM wait on the UI thread.
Those CPU timings did not detect the presentation loss described below.
They also do not isolate the remaining 1080p supply/timing limit.
Hardware codec capability alone does not guarantee
1080p30 presentation through every decode, upload and display configuration.

## Online Playback and Display Handoff

Reproduced with the user's video:
https://www.youtube.com/watch?v=TFWhoT9B1XU

Although the requested quality ceiling was 720p, the available direct stream
selected for this response was H.264 640x360 at 30 fps with audio in the same
stream. The baseline ran for approximately 55 seconds. Its main cache stayed
at 9.98-10.01 seconds, with no underrun or cache pause. After startup, video
rendering remained at 30 fps and the drop counter did not increase. A/V offset
was approximately 0.2-0.4 ms. This run did not reproduce a network shortage.

The display is configured as 568x1232 at 35 Hz. The original DRM/EGL backend
could retain a new buffer while the previous page flip was in flight, then
release that pending buffer on the next refresh without submitting it. Thus
normal render counters did not imply normal screen presentation.

| Same YouTube Video, Stable Interval | Video Rendered | DRM Presentation |
| --- | ---: | ---: |
| Original display handoff, 30-second cache | 30 fps | 25.0-25.2 submissions/s |
| Fixed display handoff, 30-second cache | 30 fps | about 31 completed flips/s |

The approximately one extra flip per second is a UI update. With the corrected
handoff, each candidate buffer is submitted after the previous flip completes;
the measured submission and completion counts agree, with no failed flips.
Waiting is bounded, and errors/interruption preserve buffers still owned by
DRM. Stable kernel flip intervals range from about 28.6 to 57.2 ms, as expected
when displaying a 30 fps video with additional UI updates on a 35 Hz panel.
This is not equivalent to perfectly uniform 30 fps on a 60 Hz panel.

Network resilience was also improved: up to 30 seconds of read-ahead and about
3 seconds of initial/recovery buffering, with the existing 16 MiB forward and
4 MiB backward byte limits retained per demuxer. In the same video, the new
cache remains near 30 seconds using about 2.6 MiB. This adds startup buffering;
it does not fix sustained bandwidth below the stream's bitrate. For separate
audio/video downloads, main-demuxer cache telemetry alone cannot exclude an
audio download stall.

Repeat the complete application's online test using
`LILYGO_YOUTUBE_TEST_VIDEO=TFWhoT9B1XU`, optionally setting
`LILYGO_YOUTUBE_TEST_QUALITY=720`, and `LILYGO_YOUTUBE_DEBUG=1`.

## Reproduce on the Device

The following commands assume the cross-build environment described in the
[README](../README.md#cm0-device-build). Build the opt-in GPU executable:

```sh
cmake --build --preset cm0-cross --target youtube-player-gpu-smoke
```

Copy the ARM64 executable from `build/cm0-cross/` to the device and use a local
moving H.264 clip longer than 15 seconds:

```sh
LILYGO_YOUTUBE_DEBUG=1 ./youtube-player-gpu-smoke /path/to/1080p30.mp4 /tmp/gpu-test
```

The GPU test uses an offscreen EGL surface and does not take over the display.
It checks nonblack pixels, motion, color and orientation, pause, resizing while
paused, seek/resume, and decoder/texture destruction and recreation. It is a
functional check; it does not establish full-application 30 fps performance.

Both GPU and ordinary player smoke tests use silent audio by default. For an
audible check, build `youtube-player-smoke` with `cm0-cross`, copy it to the
board, and run:

```sh
LILYGO_YOUTUBE_DEBUG=1 ./youtube-player-smoke /path/to/video.mp4 --audio
```

During CM0 playback, `[youtube-audio]` should report `current-ao=alsa`,
`audio-out-params/samplerate=48000`, `audio-out-params/channel-count=2`, and
`audio-out-params/format=s16`. The ES8389's 19.2 MHz clock does not support
44.1 kHz playback, so libmpv resamples source audio to this output format.
The simulator uses the host's automatic audio backend.

## Validation

- Host core, ViewModel and portrait/landscape render tests.
- ARM64 cross compilation and reproducible LVGL patch application.
- Page-flip wait fault checks under ASan/UBSan: delayed events, timeout,
  interruption, errors, tick wraparound and retained buffer ownership.
- Real GPU pixel checks: nonblack picture, moving content, correct color and
  orientation, pause stability, paused resize, seek/resume, destruction and
  recreation.
- Full application on the board with DRM/EGL, in portrait and landscape.
- Installed online playback verified in both orientations. Two paused video
  crops are pixel-identical; the expanded paused view remains visible, and
  its pixels change after playback resumes.
- Installed DEB payload checksum matches the board executable. Launching from
  the real launcher icon selects VC4/EGL; the exit gesture returns code 0 and
  restores the active launcher service.

Enable `LILYGO_YOUTUBE_DEBUG=1` for driver, active decoder, frame/drop counts,
render duration, playback position and buffering diagnostics. Use
`LILYGO_YOUTUBE_TEST_MEDIA=/path/to/local.mp4` to repeat an offline run.
Five-second frame reports include redraws and should only be interpreted as
playback FPS while the video is playing at a fixed size.
