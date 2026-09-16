# libcurl Headers

libcurl 8.7.1 public API headers, copied from the macOS command line SDK.
Each file retains its upstream copyright and permission notice.
Upstream: https://curl.se/libcurl/

The application links the system libcurl. These portable headers let the
CM0 BSP link its existing libcurl.so.4 without requiring additional SDK files.
TLS certificate verification uses libcurl's defaults and is never disabled.
