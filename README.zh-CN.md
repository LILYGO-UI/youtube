# LILYGO YouTube

[English](README.md) | **简体中文**

LILYGO YouTube 是面向 LILYGO UI 的原生 C++17 YouTube 客户端，
支持 CM0 ARM64 Linux 设备，并提供用于开发和测试的 SDL2 桌面模拟器。
应用直接实现搜索、推荐、缩略图和播放功能，无需配套计算机、Node.js 解析服务、
`yt-dlp` 或 Shell 管道。

## 预览

<p>
  <img src="assets/previews/preview1.png" alt="LILYGO YouTube 视频播放界面" width="280">
</p>

## 功能

- **首页推荐：** 通过公开主题搜索提供最多 48 条去重推荐；
  刷新时轮换搜索主题，不依赖账号或观看历史。
- **视频搜索：** 按关键词搜索最多 12 条视频，缩略图在后台加载。
  首页还提供刷新和加载更多控件。
- **播放控制：** 支持播放、暂停、重试、进度跳转、前后跳转 10 秒、音量调整和展开播放器；
  关闭播放器后保留当前结果列表。
- **原生媒体处理：** 通过 HTTPS 解析公开视频的 H.264/AAC 流，并使用 libmpv 播放。
  默认清晰度上限为 720p；有兼容流时可选择 240p、360p、480p、720p 和 1080p。
- **CM0 渲染：** 支持 V4L2 M2M H.264 解码、OpenGL ES 视频纹理，
  以及带横竖屏旋转的 LVGL DRM/GBM/EGL 合成。
- **响应式界面：** 提供 LVGL 搜索键盘、视频卡片、播放控件和适配屏幕方向的布局，
  并通过主机渲染测试验证。
- **受限的后台任务：** HTTPS 请求、缩略图转换、取消机制和响应大小限制，
  确保网络处理不占用 LVGL 线程。

客户端使用 YouTube 内部 HTTPS 接口，接口可能随时变更。
目前支持能够提供直接 H.264/AAC 流的公开视频。
登录、年龄受限或付费内容、DRM、直播清单以及动态签名挑战不在支持范围内。
请求端点、超时、隐私和失败处理详见[服务与网络指南](docs/03-services-and-network.md)。

## 开发环境搭建

项目标识和软件包元数据由 [`lpm.toml`](lpm.toml) 提供，
[`CMakePresets.json`](CMakePresets.json) 定义了稳定的 `host-simulator`
和 `cm0-cross` 配置。所有命令均在仓库根目录执行。

### 获取源码

已有仓库可使用以下命令初始化锁定版本的 AppKit 依赖：

```sh
git submodule update --init --recursive
```

模拟器直接使用 `third_party/cm0-appkit`。子模块初始化后，无需单独安装 AppKit，
也无需从网络下载 LVGL。

### 主机依赖

需要 CMake 3.21 或更高版本、CTest 和 CPack、Python 3.11 或更高版本、
C++17 编译器、`pkg-config`、`patch`、SDL2、FreeType、json-c、libcurl、
libmpv 和 FFmpeg。运行主机模拟器还需要图形桌面会话。
在 Debian 12 或 Ubuntu 24.04 上安装依赖：

```sh
sudo apt update
sudo apt install build-essential cmake git pkg-config patch libsdl2-dev \
  libfreetype6-dev libjson-c-dev libcurl4-openssl-dev libmpv-dev ffmpeg \
  ca-certificates python3 python3-venv python3-pip
```

在 macOS 上，安装 Xcode Command Line Tools 和对应的 Homebrew 软件包：

```sh
xcode-select --install
brew install git cmake pkg-config patch sdl2 freetype json-c curl mpv ffmpeg python
```

如果 `PATH` 中尚无 LPM，可在项目本地虚拟环境中安装：

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install "git+https://github.com/LILYGO-UI/lpm.git"
lpm --version
```

每次打开新的 Shell 后需要重新激活 `.venv`。
也可通过 `-DLPM_EXECUTABLE=/absolute/path/to/lpm` 为 CMake 指定已有的 LPM。

## 构建与测试

### 桌面模拟器

日常开发可直接使用 LPM：

```sh
lpm start --foreground
```

如需复用已有构建，执行 `lpm start --no-build --foreground`。
对应的底层 CMake 命令为：

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
```

Debug 构建产物位于 `build/host-simulator`，渲染快照文件为
`youtube-portrait.ppm` 和 `youtube-landscape.ppm`。
也可执行 `lpm test`，一次完成配置、构建和同一套测试。

| 测试 | 覆盖内容 |
| --- | --- |
| `youtube-core` | 视频 ID 校验、JSON 解析、媒体流选择、限制和时长格式化 |
| `youtube-view-model` | 搜索及推荐状态、取消、缩略图、控件和播放命令 |
| `youtube-render-portrait` | `568 x 1232` 下的渲染和交互 |
| `youtube-render-landscape` | `1232 x 568` 下的渲染和交互 |

修改 UI 后应检查两张快照；断言通过并不能验证视觉间距、字体回退或内容裁切。

### 可选播放检查

以下命令会访问 YouTube 或打开本地媒体，不属于默认的离线测试套件：

```sh
cmake --build --preset host-simulator --target youtube-player-smoke
build/host-simulator/youtube-player-smoke /absolute/path/to/video.mp4
build/host-simulator/youtube-player-smoke TFWhoT9B1XU
build/host-simulator/youtube-player-smoke /absolute/path/to/video.mp4 --audio
build/host-simulator/youtube-player-smoke /absolute/path/to/video.mp4 \
  --audio-file /absolute/path/to/audio.m4a --audio
build/host-simulator/youtube-render-ui 568 1232 /tmp/youtube-live.ppm live
build/host-simulator/youtube-render-ui 568 1232 /tmp/youtube-home.ppm recommendations
```

播放冒烟测试默认静音，传入 `--audio` 后才启用声音。
`live` 模式检查搜索、缩略图加载、媒体流解析和内嵌播放；
`recommendations` 模式检查首页推荐，不打开播放器。

### CM0 设备构建

设备构建需要 AArch64 交叉编译器和 CM0 BSP。
在 Debian 或 Ubuntu 上安装 `gcc-aarch64-linux-gnu` 和 `g++-aarch64-linux-gnu`；
在 Apple Silicon 上执行 `brew install aarch64-unknown-linux-gnu`，然后运行：

```sh
lpm pack
```

对应的 CMake 和 CPack 命令为：

```sh
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -B dist
```

首次配置交叉编译时会将锁定版本的 BSP 下载到 `.cache`。
较旧的 CM0 BSP 若缺少 EGL、GLES2 和 GBM 开发文件，可使用项目本地 GPU SDK 补充目录：

```sh
sh tools/sync-gpu-sdk.sh pi@192.168.8.206
cmake --preset cm0-cross
```

通过 `-DYOUTUBE_GPU_SDK_ROOT=/absolute/path/to/overlay` 可指定其他补充目录，
也可通过 `-DCM0_DRM_OPENGL=OFF` 配置软件显示后端。
CPack 输出 `dist/lilygo-ui-youtube_0.1.0_arm64.deb`；在设备上使用
`apt install` 安装，让 Debian 自动解析声明的运行时依赖。

目标软件包需要 `libmpv2 >= 0.38`、`libcurl4t64`、json-c、FFmpeg、
DRM/EGL/GLES2/GBM、FreeType、CA 证书和 `lilygo-ui-appkit-dev >= 0.1.0`。
AppKit 提供设备共享运行时字体。GPU SDK 同步脚本只下载并解压开发软件包，
不会将它们安装到设备上。

`lpm.toml` 中的源码和主页 URL 仍指向模板仓库，发布前需改为最终仓库地址。

### 运行时诊断与问题复现

| 环境变量 | 作用 |
| --- | --- |
| `LILYGO_YOUTUBE_DEBUG=1` | 输出渲染器、解码器、帧数及丢帧数、缓冲、缓存、视频尺寸及帧率和音画偏移日志。 |
| `LILYGO_YOUTUBE_TEST_MEDIA=/path/to/video.mp4` | 在完整应用中打开本地媒体，用于离线播放检查。 |
| `LILYGO_YOUTUBE_TEST_VIDEO=<video-id>` | 直接启动正常的媒体流解析与播放流程，无需搜索或触摸操作。 |
| `LILYGO_YOUTUBE_TEST_QUALITY=240\|360\|480\|720\|1080` | 为 `TEST_VIDEO` 指定播放清晰度上限；无效值沿用默认设置。 |

同时设置两个测试输入时，`LILYGO_YOUTUBE_TEST_MEDIA` 优先。
网络播放需要能够访问 YouTube、`i.ytimg.com` 和 `*.googlevideo.com`，
并安装系统 CA 证书。代理支持取决于处理各类请求的原生库，具体请求路径见服务指南。

在 CM0 上，播放使用默认 ALSA 设备，音频格式为 48 kHz、有符号 16 位双声道 PCM。
`LILYGO_YOUTUBE_DEBUG=1` 会增加 `[youtube-audio]` 诊断日志，显示实际后端与输出格式。
720p/1080p 的实测限制、缓冲和显示交接行为见
[CM0 图形与播放验证](docs/02-cm0-performance.md)。

## 常见问题

| 现象 | 处理方法 |
| --- | --- |
| 找不到 `lpm` 或项目元数据 | 激活 `.venv`、安装 LPM，或设置 `LPM_EXECUTABLE`。 |
| 找不到 `LilyGoUI` 或 LVGL | 执行 `git submodule update --init --recursive`。 |
| 缺少 SDL2、FreeType、json-c、libcurl 或 libmpv | 安装主机开发依赖；交叉编译时检查目标 sysroot。 |
| 找不到 Homebrew 库 | 使用 `-DCMAKE_PREFIX_PATH="$(brew --prefix)"` 配置。 |
| 播放器提示视频不受支持 | 尝试提供 H.264/AAC 流的公开视频及受支持的清晰度上限；暂不支持年龄限制、DRM 和直播。 |
| 播放卡顿或缩略图加载失败 | 检查 DNS、代理设置、CA 证书，以及 YouTube、`i.ytimg.com` 和 `googlevideo.com` 的可访问性。 |
| 缺少设备 GPU 头文件 | 使用 `tools/sync-gpu-sdk.sh` 补全 `.cache/gpu-sdk`，或使用软件显示后端。 |

## 文档

| 指南 | 内容 |
| --- | --- |
| [架构总览](docs/00-overview.md) | 数据流、工作线程归属、播放生命周期、取消和失败处理 |
| [UI 开发规范](docs/01-user-interface.md) | LVGL 布局、字体排版、所有权、屏幕方向和渲染测试要求 |
| [服务与网络](docs/03-services-and-network.md) | HTTPS 端点、媒体流和缩略图请求、限制、隐私及离线行为 |
| [CM0 图形与播放验证](docs/02-cm0-performance.md) | 设备处理链路、实测性能、缓冲和验证依据 |

## 源码结构与许可

- `src/pages/youtube/`：搜索、推荐和播放页面的 Model、ViewModel 与 LVGL View。
- `src/services/http.*`：通过系统 libcurl 发起有边界限制的 HTTPS 请求。
- `src/services/youtube_service.*`：工作线程中的网络请求和缩略图任务。
- `src/services/player.*`：libmpv 播放、硬件解码、OpenGL 纹理和模拟器渲染。
- `src/services/process.*`：有边界限制且不经过 Shell 的 FFmpeg 调用，用于缩略图转换。
- `tests/`：核心逻辑、ViewModel、渲染、音频和 GPU 冒烟检查。

应用不内置或重新分发完整媒体栈，运行时库由操作系统提供。
公开头文件的上游声明和共享 AppKit 字体许可证会随 Debian 软件包安装到
`/usr/share/doc/lilygo-ui-youtube`。
