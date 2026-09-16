# Services and Network

[Back to README](../README.md)

This guide describes the services used for search, recommendations, thumbnails,
and stream resolution. The application has no account login or persistent viewing
history and does not use Node.js, yt-dlp, or another external stream resolver.

## Online Services

| Purpose | Request | Client |
| --- | --- | --- |
| Search and recommendations | `POST https://www.youtube.com/youtubei/v1/search?prettyPrint=false` | libcurl + json-c |
| Resolve a video stream | `POST https://www.youtube.com/youtubei/v1/player?prettyPrint=false` | libcurl + json-c |
| Video thumbnails | `GET https://i.ytimg.com/vi/<video-id>/mqdefault.jpg` | ffmpeg child process |
| Media playback | Resolved `https://*.googlevideo.com/...` URL | libmpv |

Search and player requests send JSON with the Android or Web client context used by
the parser. The internal YouTube API is not a stable public API; response changes can
cause a visible error. A new search or home refresh clears the previous list; a failed
load-more request retains the recommendations already shown. The final media
URL is checked before it reaches libmpv and must be HTTPS on a `googlevideo.com`
subdomain.

Recommendations are public topic searches. The current topic set includes technology,
science, nature, music, travel, cooking, space, DIY electronics, art, history,
architecture and short films. A refresh chooses a topic different from the preceding
one when possible; no account or personalized history is sent.

## Request Limits and Cancellation

| Operation | Limits |
| --- | --- |
| YouTube JSON request | 10 s connection timeout, 30 s total timeout, 4 MiB response body |
| Thumbnail request | 8 s ffmpeg read timeout, 12 s process timeout, exactly 160x90 BGRA output |
| Search response | At most 12 valid video cards per response |
| Recommendation list | At most 48 unique video IDs retained in the ViewModel |
| Search text | Trimmed and limited to 512 bytes |
| Playback stream | H.264/AVC, no more than 1920x1080 and 30 fps; quality ceiling 240/360/480/720/1080 |

The service owns one worker thread and one current cancellation token. Starting a new
operation cancels the old operation, clears its pending updates, and prevents stale
results from being posted. `cancel()` is used when the user closes the search or leaves
the player. The destructor cancels and joins the worker before releasing service state.

Search and player API requests use libcurl's normal certificate verification and
proxy environment handling; redirect following is not enabled for those requests.
Thumbnails and media use FFmpeg and libmpv's own network backends, whose proxy and
TLS behavior depends on the system build. The application does not concatenate shell
input. The thumbnail command uses a validated 11-character video ID and a bounded
output buffer. Failed thumbnail conversions leave placeholders in place.

## Playback Buffering

libmpv receives the resolved stream with the external downloader path disabled
(`ytdl=no`). The player is configured with:

- 30 seconds of read-ahead (`cache-secs=30`);
- 16 MiB forward and 4 MiB backward demuxer byte limits;
- a 15 second network timeout;
- initial and recovery cache pause, waiting for about 3 seconds;
- software rendering in the simulator;
- CM0 `v4l2m2m` H.264 decoding and OpenGL ES texture rendering when EGL is active.

Separate video and audio streams may each allocate cache. The debug cache line describes
the main demuxer and cannot by itself prove that a separate audio request is healthy.
A sustained connection below the selected stream bitrate can still underrun after the
initial buffer fills.

Search results and thumbnails are held in memory for the current session. There is
no persistent video download or thumbnail cache, and no offline search or home feed.
The local-media test below verifies playback without downloading a YouTube stream.

## Runtime Diagnostics and Offline Checks

Set `LILYGO_YOUTUBE_DEBUG=1` to print five-second player reports. The reports include
renderer, active hardware decoder, rendered and dropped frames, playback position,
buffering, cache duration and input rate, video dimensions/FPS, A/V offset, and audio
output parameters. A value of `0` disables the reports.

For an offline player check, set `LILYGO_YOUTUBE_TEST_MEDIA` to a local media path:

```sh
LILYGO_YOUTUBE_TEST_MEDIA=/path/to/video.mp4 \\
  lpm start --no-build --foreground
```

For an unattended online check, set a public 11 character video ID. The normal resolver
and embedded player are used:

```sh
LILYGO_YOUTUBE_TEST_VIDEO=jNQXAC9IVRw \\
  LILYGO_YOUTUBE_TEST_QUALITY=720 \\
  LILYGO_YOUTUBE_DEBUG=1 \\
  lpm start --no-build --foreground
```

`LILYGO_YOUTUBE_TEST_QUALITY` accepts 240, 360, 480, 720 or 1080; an invalid value
keeps the default quality. `LILYGO_YOUTUBE_TEST_MEDIA` takes precedence when both
variables are set. These checks require the normal display backend; online checks also
require DNS, network access and the system CA certificates. Diagnostic logs do not
include media URLs.

## Privacy and Compatibility

IP location, account state and personalized recommendations are outside this application.
YouTube receives the search text and the client context; `i.ytimg.com` receives the
video IDs whose thumbnails are requested; `googlevideo.com` receives media requests
made by libmpv. Follow the providers' terms and usage policies.

Supported playback is limited to public videos exposing direct H.264/AAC streams.
Login, age-restricted or paid content, DRM, dynamic signature challenges and live
manifests are not implemented. A failure is reported to the user and does not trigger
a fallback downloader.
