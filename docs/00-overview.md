# LILYGO YouTube 架构总览

[返回 README](../README.md)

本文说明 LILYGO YouTube 的模块边界、数据流和构建约束，方便维护页面、网络服务
和播放器。项目是可独立构建的 C++17 应用，不是 AppKit 或 Launcher 的源码副本。
在线服务、请求限制和诊断环境变量见[服务与网络](03-services-and-network.md)。
应用直接访问 YouTube 的内部 HTTPS 接口，使用 libcurl 取回搜索和播放信息，再由
libmpv 播放 H.264/AAC 流。运行时无需 Node.js、Python 解析器或 yt-dlp；Python
仅用于安装开发工具 LPM 等辅助工作。

## 平台约束

- UI 使用 AppKit 提供的 LVGL 9.5；版本和补丁由 AppKit 管理，应用不自行引入另一份 LVGL。
- 桌面预览使用 SDL2，设备默认使用 DRM/KMS、GBM、EGL 和 OpenGL ES 2.0。
- 源码和测试使用 C++17。AppKit 生命周期边界保留 C ABI（`cm0_app_*`）。
- 稳定的 CMake preset 为 `host-simulator` 和 `cm0-cross`，构建目录分别是
  `build/host-simulator` 和 `build/cm0-cross`。
- UI 以父容器的实际尺寸使用 LVGL Flex/Grid 布局，支持 `568x1232` 竖屏和
  `1232x568` 横屏；页面不能依赖某一种设备型号或展示模式。
- 应用只能通过 `find_package(LilyGoUI CONFIG REQUIRED)` 使用平台 SDK，不得引用
  Launcher 源码或 AppKit 的私有文件。`/usr/share/launcher/apps` 中的 manifest
  只用于运行时发现。

## 模块边界

```text
src/
├── main.cpp                         # cm0_app_run 入口
├── app.cpp/.hpp                     # AppKit 生命周期和页面装配
├── components/
│   ├── components.cpp/.hpp          # 语义颜色、页面/卡片/按钮公共组件
│   └── lv_subject.hpp               # Subject 的 RAII 封装
├── pages/youtube/
│   ├── youtube_model.*              # 视频、搜索和播放状态；不依赖 LVGL
│   ├── youtube_view_model.*         # 用户命令、状态发布和服务编排
│   └── youtube_view.*               # 搜索键盘、结果卡片和播放器 UI
└── services/
    ├── youtube_service.*            # 工作线程、请求排队和异步更新
    ├── http.*                       # 有界 HTTPS 请求和 TLS 校验
    ├── player.*                     # libmpv 播放、音视频同步和纹理输出
    └── process.*                    # 缩略图解码等受限的 FFmpeg 进程
```

依赖方向保持为 `View -> ViewModel -> Model`。`youtube_model` 不初始化 LVGL，方便
在主机上测试解析、时长格式化和状态规则。ViewModel 不持有具体 widget；View 只
负责创建对象、布局、绑定 Subject 和转发事件。公共组件只有在多个页面复用或明确
承担资源生命周期时才放入 `src/components/`。

## 数据流和线程

```text
LVGL 事件
   │
   ▼
YoutubeView ──命令──> YoutubeViewModel ──更新──> YoutubeModel
   ▲                         │                    │
   │      poll()/revision    │                    └─ 搜索结果、缩略图、播放状态
   │                         ▼
   └────────────── UI 线程 <── SearchService 工作线程
                                  │
             libcurl + YouTube JSON 请求/缩略图/播放信息解析
```

`YoutubeService` 只有一个后台线程。`search()`、`recommend()`、`thumbnails()` 和
`resolve()` 写入只保留最新请求的待处理槽位；工作线程完成后把 `SearchUpdate` 放入受互斥锁保护的
更新队列。ViewModel 在 LVGL 线程调用 `poll()`，更新 Model 并递增 `revision`
Subject，避免工作线程直接触碰 LVGL。新请求会取消仍在进行的请求；析构服务前会
停止线程并唤醒条件变量。

网络请求通过 `youtube_request()` 使用系统 CA 和代理环境，连接、总时长和响应体
都有上限，且不拼接 shell 命令。缩略图先显示占位内容，再按结果批次逐项加载；
搜索和推荐响应最多保留 12 项一批，推荐列表最多保留 48 个唯一视频。新搜索或推荐
刷新会先清空旧列表；加载更多推荐则保留已有结果。请求失败时显示错误，加载更多
失败不会清除已有推荐。单个缩略图失败时保留占位图并继续处理其余视频。

## 播放管线

```text
YouTube 视频 ID
      │ resolve(height)
      ▼
MediaStream（视频 URL + 可选音频 URL）
      │
      ▼
libmpv ── v4l2m2m（CM0）/软件解码（模拟器）
      │
      ├── 音频：ALSA，48 kHz、s16、双声道（设备）
      └── 视频：OpenGL FBO → LVGL 外部纹理（设备）
```

CM0 设备默认请求 `v4l2m2m` H.264 解码。当前驱动会把解码后的 YUV 平面上传到
GPU 纹理，因此不是 DMA-BUF 零拷贝管线；GPU 负责颜色转换、缩放和旋转。设备不具备
OpenGL 条件时可使用软件显示后端，模拟器始终使用软件像素缓冲。播放器状态通过
`Playback` 轮询回 ViewModel，UI 不直接访问 libmpv 的线程或句柄。

播放器只接受公开、可取得直接 H.264/AAC 流的视频。登录、年龄限制、付费内容、DRM、
动态签名挑战和直播清单不在支持范围；失败时显示错误，不回退到外部解析器。

## 元数据、构建和产物

`lpm.toml` 是应用标识、版本、权限、图标、preset 和部署目标的唯一维护入口。
CMake 配置时由 LPM 生成身份头文件、Launcher manifest、desktop 和 AppStream
元数据。不要手工编辑生成文件，也不要提交 `.cache/`、`.lpm/`、`.venv/`、`build/`
或 `dist/`。

```sh
# 主机模拟器、测试和渲染快照
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator

# CM0 AArch64 构建和 Debian 包
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -B dist
```

修改 UI 后必须检查竖屏和横屏 `youtube-portrait.ppm`、`youtube-landscape.ppm`。
修改工具链、安装或元数据后还要完成 `cm0-cross` 构建和 CPack 打包。提交前至少应
通过 core、ViewModel 及两种方向的渲染测试；网络和真实硬件播放属于显式 opt-in
验证，命令见 [README](../README.md) 和 [CM0 播放验证](02-cm0-performance.md)。

## 目录和命名约定

应用公开名称使用 `lilygo-ui-youtube`，App ID 使用 `cc.lilygo.ui.YouTube`。新增
页面放在 `src/pages/<page>/`，新增跨页面业务对象才放入 `src/domain/`；不要创建
空目录或复制 AppKit 的字体、图标和 LVGL 源码。应用专用资源放在 `assets/`，并在
`lpm.toml` 和 CMake 安装规则中同时登记。
