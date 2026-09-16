# libmpv API Headers

Unmodified `client.h` and `render.h` from mpv v0.40.0:
https://github.com/mpv-player/mpv/tree/v0.40.0/include/mpv

Each header contains its upstream copyright and ISC permission notice.
Only API declarations are vendored. The application loads the system's
`libmpv.so.2` (Linux) or `libmpv.2.dylib` (macOS) at runtime. This lets the
CM0 SDK cross-compile the application without bundling a media stack.
The Debian package requires libmpv2; its codecs and license notices are
provided by the system package manager.
