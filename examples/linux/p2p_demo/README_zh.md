# p2p_demo

[English](README.md) | 中文

## 概述

一个 Linux 示例：通过 P2P 通道把设备视频推送到涂鸦 App（App 端观看实时
画面），并可选择本地录像与日历回放。Linux 上没有摄像头，所以视频源使用
示例自带的样片 `assets/demo_video.264`（2304x1296@15fps，36 秒循环）；
虚拟摄像头 `demo_vcam` 按配置的帧率循环喂给媒体链路。音频未实现。

## 构建与运行

```bash
cd tuyaopen-core/examples/linux/p2p_demo
cmake -B build
cmake --build build -j$(nproc)
cd build && ./p2p_demo
```

> 该示例必须在仓库树内构建：p2p_demo 复用公共示例组件 `../components/qrcode`
> （没有本地副本）。把本目录复制到别处会导致 configure 失败（找不到 qrcode，
> CMake 会明确报错）。

凭据可选：PID/UUID/AuthKey 默认取自 `src/tuya_config.h`，可以通过
`-DTUYA_PRODUCT_ID=.. -DTUYA_OPENSDK_UUID=.. -DTUYA_OPENSDK_AUTHKEY=..` 覆盖，
也可以写入 `src/tuya_config_secrets.h`（参考 `.example` 模板），或通过同名
环境变量提供。
凭据优先级：**`-D` / 环境变量 > `tuya_config_secrets.h` > `tuya_config.h`
中的默认值**（secrets 模板里的 `#define` 带 `#ifndef` 保护，不会覆盖命令行
注入）。视频源在构建时复制到 `build/demo_video.264`；从 `build/` 目录运行
可直接使用（在别处运行时，路径相对可执行文件所在目录解析）。

运行时行为：

1. 初始化日志/KV/时间服务/工作队列，并通过主机网络连接云端；
2. 设备未激活时进入绑定模式：直连 MQTT 成功后打印二维码；
3. 用涂鸦 App 扫描二维码绑定并激活设备；
4. 激活后由 `MQTT_CONNECTED` 事件启动媒体线程
   （`TUYA_APP_Start` -> P2P 监听），App 即可打开实时画面（需要 IPC 类目
   产品，见“已知限制”）。

## Kconfig 选项

| 符号 | 含义 | 默认值 |
|---|---|---|
| `P2P_DEMO` | 构建本示例；选中 `ENABLE_TUYA_P2P`、`ENABLE_IPC_RING_BUFFER` | y（无提示项） |
| `P2P_DEMO_LOCAL_STORE` | 本地录像与日历回放；选中 `ENABLE_LOCAL_STORE` | n |
| `P2P_DEMO_WIDTH` | 上报给 App 的视频宽度（像素）；必须与视频源一致 | 2304 |
| `P2P_DEMO_HEIGHT` | 上报给 App 的视频高度（像素）；必须与视频源一致 | 1296 |
| `P2P_DEMO_FPS` | 虚拟摄像头喂帧帧率，同时上报给 App | 15 |
| `P2P_DEMO_KBPS` | 上报给 App 的目标码率（视频源不参与码率控制） | 960 |
| `P2P_DEMO_GOP` | 上报给 App 的 GOP 长度（自带视频的 GOP 为 60） | 60 |
| `P2P_DEMO_H264_FILE` | 实时视频源路径；为空时使用自带视频 | "" |
| `P2P_DEMO_LIVE_RECORD` | 直播过程中持续写入 local_store（按分段切分） | n |
| `P2P_DEMO_RECORD_MAX_SEC` | 单个录像分段的最大时长（秒，30-3600） | 120 |
| `P2P_DEMO_PB_SEED` | 启动时用实时视频源播种当天的索引，便于在没有录像时尝试回放 | n |

menuconfig 用法（`P2P_DEMO` 没有提示项；不想构建本示例时不要勾选它即可，
无需把它关掉）：

```bash
pip3 install --user kconfiglib          # 一次性
cd tuyaopen-core/examples/linux/p2p_demo
KCONFIG_CONFIG=./.config menuconfig     # 交互式配置
KCONFIG_CONFIG=./.config olddefconfig   # 按默认值生成
```

优先级：**`-D` 命令行 > `.config` > 内置默认值**。也可以直接用 `-D` 覆盖：

```bash
cmake -B build -DCONFIG_P2P_DEMO_FPS=15
cmake -B build -DCONFIG_P2P_DEMO_H264_FILE=/path/to/your.h264
```

注意：本 demo 自己目录下的 `.config` 会被其 CMake 读取；一旦存在即优先使用，
删除后回到内置默认值。

## 自测

两个独立可执行文件，不需要云端，也不需要 App（在示例目录下运行）：

```bash
./build/vcam_selftest
./build/media_chain_selftest
```

- `vcam_selftest`：打开视频源并校验 AU 索引与喂帧时序；期望输出
  `VCAM SELFTEST PASSED`（80 帧、时间戳步进 66ms、2 个 I 帧）。
- `media_chain_selftest`：跑通整条媒体链路（vcam -> 帧校验/队列/环形缓冲区
  -> P2P 取帧回调）并验证“请求 I 帧”；期望输出
  `MEDIA CHAIN SELFTEST PASSED`（4 秒内 60 帧、时间戳单调递增、请求与下一个
  I 帧之间最多 8 帧——通常 1~2 帧，而被忽略的请求要等约 59 个 P 帧）。视频源
  缺失时会提示 `sample not found: run from build/ ...`。

## 视频源

自带视频 `assets/demo_video.264`（构建时复制到 `build/demo_video.264`）：

- **格式**：Annex-B H.264，High profile，2304x1296@15fps，540 帧
  （36 秒循环），9 个 IDR（GOP=60），无 AUD（当 VCL NAL 跟在已有 VCL 之后
  时切分 AU 边界），最大帧约 56KB，整个文件 4.3MB。
- `demo_vcam` 打开时将其解析为 AU 索引，并按配置帧率循环喂帧；
  `request_i_frame` 会跳到下一个 IDR。

使用自己的 H.264：

```bash
cmake -B build -DCONFIG_P2P_DEMO_H264_FILE=/path/to/your.h264
```

也可以在 menuconfig 中设置 `P2P_DEMO_H264_FILE`（或直接编辑 `.config`）。
必须是 Annex-B 格式；分辨率/帧率/GOP 变化时，`P2P_DEMO_WIDTH/HEIGHT/FPS/GOP`
必须同步修改（`KBPS` 建议一起修改），否则上报给 App 的音视频能力与实际流
不一致。单个 AU 必须小于 256KB（示例帧队列槽位上限；更大的帧会被丢弃并打印
告警）。

## 录像与回放（默认关闭）

默认只启用 P2P 实时画面。要启用本地录像与日历回放：

```bash
cmake -B build-ls -DCONFIG_P2P_DEMO_LOCAL_STORE=ON -DCONFIG_ENABLE_LOCAL_STORE=ON
cmake --build build-ls -j$(nproc)
```

- `P2P_DEMO_LOCAL_STORE` 会选中 core 的 `ENABLE_LOCAL_STORE`；在 menuconfig
  中只需勾选前者，用 `-D` 时两者都要给。
- 录像写入 `./media`（相对**运行目录**；从 `build/` 运行时即 `build/media`），
  分段文件位于 `media/rec/<YYYY-MM-DD>/`（`live_N.media` 加索引）；日期使用
  设备本地时间，云端时间同步前（刚启动、尚未激活）为 `1970-01-01`。
- 直播过程中持续录像还需要打开 `P2P_DEMO_LIVE_RECORD`（默认 n）；分段时长由
  `P2P_DEMO_RECORD_MAX_SEC` 控制（默认 120 秒）。
- `P2P_DEMO_PB_SEED`（默认 n）会在启动时用实时视频源播种当天的索引，让 App
  在没有录像的情况下也能尝试日历回放。

## 已知限制

- **必须使用 IPC 类目产品**：默认 PID 是 IPC 产品
  （`sjzw8nclwoitx0rv`）。使用普通产品时，设备仍能连接云端、被 App 绑定、
  成功上报技能，但云端 `tuya.device.ipc.p2p.config.get` 会返回
  `IP Camera Not exist`——非 IPC 类目产品没有 P2P 配置，`TUYA_APP_Start`
  拿不到 p2p auth，P2P 服务不会启动。量产请使用自己的 IPC 产品和 License
  （`-D` 或 `src/tuya_config_secrets.h`）。
- **默认 UUID/AuthKey 是本仓库的测试 License**（与 iot_demo 相同）：同一个
  License 不能同时在线两处；与 iot_demo 并行运行会互相把对方踢下线。
- **激活数据存放在运行目录的 `tuyadb/`**：首次运行需要用涂鸦 App 扫码；
  换目录运行意味着不同的设备身份，需要重新扫码（或把已激活的 `tuyadb/`
  一并复制过去）。
- **连续快速复位三次会清除配网信息**：`reset_netcfg` 计数器在 5 秒内未清零时
  会累加，达到 3 次即触发 `tuya_iot_reset`（恢复出厂设置，随后由
  `core_system_reset` 重启）。调试时连续快速重启 3 次会丢失激活数据，需要
  重新扫码绑定。
- **未激活设备停留在等待绑定状态**：MQTT 已连接、二维码已打印，等待 App
  扫码；激活之前 P2P 服务不会启动。
- **在未激活设备上调用 `TUYA_APP_Start` 会阻塞**：`p2p_init` 拿不到 p2p auth
  时会以 1 秒间隔无限重试（`while(1)`，持续打印 `get p2p auth failed`）。
  正常流程是激活后由 `MQTT_CONNECTED` 事件启动，因此不会遇到；只有在未激活
  时调用才会触发。
- App 调节码率无效（视频源没有编码器，回调返回不支持）；真实会话中可能每秒
  打印一条码率调整日志。
- **长时间运行注意**：pjproject 的 TLS row 表固定为 24 行，按线程分配且从不
  回收（p2p-core 栈中的 `os_core_tuyaos.c`）；每个 P2P 会话会增加一个会话
  线程（`rtc_wrk`）。表满后 `pj_thread_local_set` 返回 `PJ_ENOMEM`、`get`
  返回 NULL（优雅降级，不会崩溃），该会话的部分 P2P 上下文会不可用。只有
  极频繁的连接/断开（数十次量级）才会触发；彻底修复需要修改 p2p-core。

## 目录结构

```sh
+- p2p_demo
    +- assets                       # demo_video.264（自带样片视频）
    +- src
    |   -- tuya_main.c              # 入口：初始化、云连接、事件处理、媒体线程
    |   -- reset_netcfg.c/.h        # 连续复位 3 次后清除配网信息
    |   -- tuya_ipc_demo.c/.h       # 媒体链路（取帧/队列/环形缓冲区/录像）与回调契约
    |   -- demo_vcam.c/.h           # 虚拟摄像头：样片 AU 索引 + 按帧率循环喂帧
    |   -- demo_media_event.c/.h    # local_store 回放（日历查询/回放帧发送）
    |   +- adapt/                    # 平台适配层（真实硬件上的替换点）
    |   |   -- fs_card.c            # 文件系统卡（core_fs_ops；local_store 使用）
    |   -- tuya_config.h            # PID/UUID/AuthKey（可被 secrets 或 -D 覆盖）
    |   -- tuya_config_secrets.h.example  # 凭据覆盖模板
    +- selftest                      # 离线自测（独立目标，不依赖云端；见“自测”）
    |   -- demo_vcam_selftest.c     # vcam 自测（目标 vcam_selftest）
    |   -- media_chain_selftest.c   # 媒体链路自测（目标 media_chain_selftest）
    -- CMakeLists.txt
    -- Kconfig                      # P2P_DEMO* 选项
```

> `fs_card.c` 在 p2p_demo 与 agent_demo 各有一份，**必须保持同步**：
> 修改任意一份后，运行
> `diff tuyaopen-core/examples/linux/p2p_demo/src/adapt/fs_card.c tuyaopen-core/examples/linux/agent_demo/src/adapt/fs_card.c`
> 检查（如有差异，把改动同步到另一份）。
