# agent_demo

[English](README.md) | 中文

## 简介

一个运行**完整云端 AI Agent 链路**的 Linux 示例：上传一段预置语音作为触发，
云端完成 ASR，Agent 回复，下行默认**只打印文本**（启用 AI 音频模块时可通过
ALSA 播放下行音频）。没有 UI、没有按键、没有唤醒词、没有麦克风。

- **上行**：示例自带语音样本 `assets/voice_16k.pcm`（16kHz/16bit/单声道，
  由 edge-tts 合成，2.592 秒），启动后自动上传一次（模拟“用户说了一句话”）；
- **下行**：`[AGENT][ASR]` 打印识别结果，`[AGENT][NLG]` 打印 Agent 回复文本；
  云端下发的音频包在启用 AI 音频模块时通过 ALSA 消费者播放，关闭时不解码、
  不播放，直接丢弃；
- **音频适配**：麦克风采集被抽象在一个最小化的应用侧契约后面
  （`play_local_in_*`，src/）；Linux 实现是 **PCM 文件播放**——没有真实音频
  硬件适配，只够让链路跑起来。接真实设备时替换该实现。

## 构建与运行

```bash
cd tuyaopen-core/examples/linux/agent_demo
cmake -B build
cmake --build build -j$(nproc)
cd build && env -u DISPLAY ./agent_demo
```

> 必须在仓库树内构建：agent_demo 复用公共示例组件 `../components/qrcode`
> （没有私有副本），把本目录复制到别处会导致 configure 失败（找不到 qrcode，
> CMake 会明确报错）。

> **运行注意：请用 `env -u DISPLAY` 启动**（Linux 桌面/虚拟机环境）。本示例
> 启动时会打开 ALSA 音频输出（`play_local_out_install`）；ALSA pulse 插件在
> 初始化时会读取 `DISPLAY` 去连接 X server——如果 `DISPLAY` 指向**不可达的
> X**（例如虚拟机/SSH 环境指向宿主机，而宿主机 X 没有监听 TCP），该连接会
> **永久挂起**（`ss` 中可见进程处于到 `<host>:6000` 的 `SYN-SENT`；6000 是
> X11 端口），主线程阻塞在 ALSA open → **云连接永远不会开始，绑定二维码也
> 永远不会打印**。去掉 `DISPLAY`（ALSA 不需要 X）即可恢复：
> `env -u DISPLAY ./agent_demo`。
> 自测目标（agent_chain/nlg/http_selftest）不打开音频设备，不受影响；
> iot_demo / p2p_demo 没有音频输出，同样不受影响。

凭据可选：默认使用 `include/tuya_config.h` 中的 PID/UUID/AuthKey，可以通过
`-DTUYA_PRODUCT_ID=.. -DTUYA_OPENSDK_UUID=.. -DTUYA_OPENSDK_AUTHKEY=..` 覆盖，
或在 `include/tuya_config_secrets.h` 中配置（参考 `.example` 模板）；也支持
同名环境变量。
凭据优先级：**`-D` / 环境变量 > `tuya_config_secrets.h` > `tuya_config.h`
中的默认值**（secrets 模板里的 `#define` 带 `#ifndef` 保护，不会覆盖命令行
注入；去掉保护会让 secrets 静默覆盖 `-D`）。语音样本在构建时复制到
`build/voice_16k.pcm`；请在 `build/` 目录下运行。

运行时行为：

1. 初始化日志/KV/时间服务/工作队列，并使用主机网络连接云端；
2. 设备未激活时进入绑定模式：直连 MQTT 成功后打印绑定二维码；
3. 用涂鸦 App 扫描二维码绑定并激活设备（**PID 必须开通 AI 服务**）；
4. 激活后由 `MQTT_CONNECTED` 事件启动 AI 链路：
   `ai_agent_init()` → 等待 AI client ready → 自动上传一句话
   （日志 `[AGENT] trigger: upload one utterance from ...`）；
5. 云端返回：`[AGENT][ASR] ...`（识别结果）→ `[AGENT][NLG] ...`
   （Agent 回复文本，整条链路打通的最终判据）。

## Kconfig 开关

| 符号 | 含义 | 默认值 |
|---|---|---|
| `AGENT_DEMO` | 构建本示例；选中 `ENABLE_AI_COMPONENTS`、`ENABLE_QRCODE` | y（无提示项） |
| `AGENT_DEMO_AUDIO_FILE` | 上行语音样本路径（16k/16bit/单声道 PCM）；为空时使用自带样本 | "" |
| `ENABLE_COMP_AI_MCP`（定义在 core，见 `agent-core/svc_ai_mcp/Kconfig`） | MCP 链路开关：core 服务端（`svc_ai_mcp`）+ 应用侧工具集（`src/components/ai_mcp`，见“MCP 支持”）；示例在 `CMakeLists.txt` 中默认打开 | ON（内置默认） |

menuconfig 用法（`AGENT_DEMO` 没有提示项；不想构建本示例时不要勾选它即可，
无需把它关掉）：

```bash
pip3 install --user kconfiglib          # 一次性
cd tuyaopen-core/examples/linux/agent_demo
KCONFIG_CONFIG=./.config menuconfig     # 交互式配置
KCONFIG_CONFIG=./.config olddefconfig   # 按默认值生成
```

优先级：**`-D` 命令行 > `.config` > 工程内置默认值**。也可以直接用 `-D`：

```bash
cmake -B build -DCONFIG_AGENT_DEMO_AUDIO_FILE=/path/to/your_16k.pcm
```

注意：本 demo 自己目录下的 `.config` 会被其 CMake 读取；一旦存在即优先使用，
删除后回到内置默认值（AI components / QRCODE / WIRED 打开）。

## MCP 支持（云端工具调用）

**开关**：core 的 `CONFIG_ENABLE_COMP_AI_MCP`（本 demo 的 `.config` 可设为
`=y`，demo 的 `CMakeLists.txt` 也有一个内置默认 ON；`-D` 可覆盖）。关闭时
`ai_mcp` 模块不参与编译，`tuya_main.c` 中的接线由 `ENABLE_COMP_AI_MCP` 宏
保护（与 `ENABLE_QRCODE` 同样的写法）；示例仍能构建和运行，只是没有 MCP
工具。

**接线**（`src/tuya_main.c`）：`user_main()` 在 `tuya_iot_start()` 之前调用
`ai_mcp_init()`。该函数只做订阅：
`core_event_subscribe(EVENT_MQTT_CONNECTED, "ai_mcp_init", ...,
SUBSCRIBE_TYPE_ONETIME)`。真正的初始化发生在 MQTT 连接后的回调中：
`ai_mcp_server_init`（设置服务器名称/版本）→ 注册工具 →
`tuya_ai_agent_mcp_set_cb(ai_mcp_server_parse_message)` 把 MCP 消息回调挂到
AI agent 上（并设置 `enable_mcp`，在会话建立时向云端上报
`supportCustomMCP` 能力）。

随后云端在 AI 会话内发送 JSON-RPC（MCP 协议）消息：
`tuya_ai_agent.c` 中的 `__mcp_handle` → `ai_mcp_server_parse_message`
（处理 `initialize` / `tools/list` / `tools/call`）→ 执行工具 →
`tuya_ai_agent_mcp_response` 回复。

**当前工具列表**（`src/components/ai_mcp/ai_mcp_tools.c`）：

| 工具 | 能力 | 参数 | 返回 |
|---|---|---|---|
| `device_info_get` | 上报设备信息：`model`（= `PROJECT_NAME`，本 demo 为 `agent_demo`）、`serialNumber`（= IoT client 的真实 devid，未激活时为 `unknown`）、`firmwareVersion`（= `PROJECT_VERSION`） | 无 | JSON |

只注册了 `device_info_get`，因此云端 `tools/list` 与实际能力一致，不会暴露
调用必然失败的占位工具。

## 自测

三个独立可执行文件，不需要云端、App 或凭据（在 build 目录运行——CMake 会把
`voice_16k.pcm` 样本复制过去；在别处运行会提示 `run from build/`）：

```bash
cd build && ./agent_chain_selftest
cd build && ./agent_nlg_selftest
cd build && ./agent_http_selftest
```

> 验证纪律：以 `cmake --build build`（**全部目标**）为准——只跑单个自测不算
> 完整验证：自测能链接，agent_demo 主目标仍可能链接失败。

- `agent_chain_selftest`：上行音频源契约与样本帧切分——总字节 82944，按
  80ms 帧（2560B）切成 33 帧（32 个整帧 + 最后一个 1024B 帧）、EOF 语义、
  关闭/重开、坏路径拒绝、**I/O 错误与 EOF 的区分**（对目录路径 fopen 会成功，
  但 read 必须返回 <0 而不是 0/EOF）。样本路径与应用一致
  （`CONFIG_AGENT_DEMO_AUDIO_FILE` 非空时测试该文件）。期望输出
  `AGENT CHAIN SELFTEST PASSED`（退出码 0）。
- `agent_nlg_selftest`：下行消费契约——多段 NLG 流中由
  `AI_USER_EVT_TEXT_STREAM_STOP` 携带的最后一段（eof=1）即使非空也必须被
  打印，而空包不得打印任何内容。事件通过公开 API 构造（不依赖云端分包）。
  期望输出 `AGENT NLG SELFTEST PASSED`（退出码 0）。
- `agent_http_selftest`：http_manager URL 解析边界——越界端口（回绕样本）和
  显式 `:0` 必须被解析器拒绝；范围内端口不得被拒绝；空 host 回归（不需要
  网络）。期望输出 `AGENT HTTP SELFTEST PASSED`（退出码 0）。

## 语音样本

自带样本 `assets/voice_16k.pcm`（构建时复制到 `build/voice_16k.pcm`）：

- **内容**：用 edge-tts（zh-CN-XiaoxiaoNeural）合成的一句中文
  （“请问现在是几点钟？”）
- **格式**：16kHz / 16bit / 单声道 / 小端 PCM（裸流），2.592 秒，82944 字节
- **生成方法**（需要网络）：
  ```bash
  pip3 install --user edge-tts
  edge-tts --voice zh-CN-XiaoxiaoNeural --text "<你的文本>" --write-media voice.mp3
  ffmpeg -y -i voice.mp3 -ar 16000 -ac 1 -f s16le voice_16k.pcm
  ```

使用自己的语音：按相同格式生成 16k/16bit/单声道 PCM，用
`-DCONFIG_AGENT_DEMO_AUDIO_FILE` 指定，或替换 `assets/voice_16k.pcm`。样本
长度决定上传时长（帧数 = 时长 × 12.5 帧/秒）。

## 能力边界（本示例刻意不做的事情）

> **定位**：agent_demo 是云端 AI Agent 整条链路的**验证载体**，不是功能完整
> 的产品。下面的能力是**刻意省略**的：目标是让链路跑通、判据真实。

**UI**：无——没有屏幕 / 表情 / LVGL / 聊天界面。

**硬件相关**：无——没有按键、唤醒词、VAD、摄像头、打印机、电池或低功耗，
也**没有真实麦克风**（音频源是通过应用侧契约 `play_local_in_*` 的 PCM 文件
播放；接真实设备时替换该实现，契约头文件里写明了适配点）。注意“只替换采集
实现”并不能带来四件事：半双工门控（播放时不上传）、AEC、本地 VAD 断句和
push 采集语义——这些决策属于本示例没有实现的会话状态机。

**未实现的音视频能力**：

- **上行 Opus 编码**：本示例上行固定为 PCM（16kHz/16bit/单声道）；本树没有
  Opus 编码路径，需要时在应用层自行接入；
- **下行 TTS 播放**：AI 音频模块关闭时，下行音频包被丢弃；启用
  `ENABLE_COMP_AI_AUDIO` 时通过应用侧 ALSA 消费者播放；
- **媒体 URL 流式下载**：未实现（媒体下载会安全失败）；
- **图片 / 文件 / 视频下行**：未实现（没有 UI / 摄像头 / 播放器）。

**对话交互**：单次触发（启动后自动上传一句预置语音）；多轮对话、语音打断和
服务端 VAD 交互未启用。

## 已知限制

- **PID 需要开通 AI 服务**：默认 PID `alon7qgyjj8yus74` 是测试 PID；授权码
  沿用 iot_demo 的测试 License。量产请使用自己的 AI 产品 + License（`-D` 或
  `include/tuya_config_secrets.h`）。
- **默认 UUID/AuthKey 是本仓库的测试 License**（与 iot_demo/p2p_demo 相同）：
  同一个 License 不能同时在多处在线，与其他 demo 并行运行会互相把对方踢
  下线。
- **连续快速复位三次会清除配网信息**：`reset_netcfg` 计数器在 5 秒内未清零时
  会累加，达到 3 次即触发 `tuya_iot_reset`（恢复出厂设置，随后由
  `core_system_reset` 重启）。调试时连续快速重启 3 次会丢失激活数据，需要
  重新扫码。
- **激活数据存放在运行目录的 `tuyadb/`**：首次运行需要用涂鸦 App 扫码激活；
  换目录运行意味着不同的设备身份，需要重新扫码（或把已激活的 `tuyadb/`
  一并复制过去）。
- **上行音频是 PCM（16kHz/16bit/单声道）直传**：未实现 Opus 编码。如果云端
  要求编码上行，需要在应用层接入编码器并修改 `ai_agent.c` 中的音频属性分支。
- **只上传一次**：触发是一次性的；要多轮请重跑或扩展触发线程。
- **下行音频被丢弃**：`ENABLE_COMP_AI_AUDIO` 关闭时，云端仍会下发 TTS 音频
  流（消耗流量但不处理）。
- **NLG 末段打印边界**：多段 NLG 流中由 STOP 携带的最后一段（eof=1）会被
  打印；该消费契约由 `agent_nlg_selftest` 覆盖，但云端运行时是否真的发送
  非空末段取决于其分包。契约级成功 != 端到端验证。
- **URL 解析器边界（媒体下载相关）**：不支持 IPv6 字面量（`[::1]`）和带
  userinfo 的 URL（`user:pass@host`）——两者都会通过显式的 `:0` 非法检查
  安全失败（host 段在第一个 `:` 处截断，port 段命中非数字）；要支持它们需要
  在 host 段显式处理方括号/userinfo。超过 1023 字符的媒体 URL 会被
  `fill_url_head` 静默截断（`DEF_URL_LEN=1024`）。媒体下载未实现，所以目前
  没有实际影响；以后实现下载时需处理这三点。
- **媒体 URL 下载未实现**：如果云端下发图片/文件/音频的下载 URL，
  agent-core 侧会安全失败（错误日志 `http media download not implemented`），
  不会下载任何内容。文本链路（ASR/NLG/表情）不会触发该路径。
- **AI monitor（TCP 5055）**：core 的 `ai_monitor` 监听本地 5055 端口，输出
  AI 协议流用于调试（`ENABLE_AI_MONITOR`，默认开启）。端口被占用时 monitor
  自身以降级方式重试（日志 `create server socket failed`），**不会阻塞 AI
  链路**。

## 目录结构

```sh
+- agent_demo
    +- assets
    |   -- voice_16k.pcm            # 上行语音样本（edge-tts，16k/16bit/单声道）
    +- include                       # 应用头文件
    |   -- agent.h / agent_audio.h / play_local_in.h / play_local_out.h
    |   -- reset_netcfg.h / tuya_config.h / tuya_config_secrets.h.example
    +- src
    |   -- tuya_main.c              # 入口：初始化、云连接、事件处理与分发
    |   -- agent.c                  # AI 链路启动编排（MQTT 连接后：agent 初始化 + 一次语音触发）
    |   -- agent_audio.c            # 音频链路胶水：上行喂帧（PCM 帧上传）+ 下行 ASR/NLG 打印
    |   -- reset_netcfg.c           # 连续复位后清除配网信息
    |   +- adapt/                    # 平台适配层（真实设备上的替换点）
    |   |   -- play_local_in.c      # 音频输入契约实现（PCM 文件播放）
    |   |   -- play_local_out.c     # ALSA 输出端点（ENABLE_AI_PLAYER 时参与编译）
    |   |   -- fs_card.c            # 文件系统卡（备用；DATASINK 收窄后不链接）
    |   +- components/               # 应用侧 AI 组件
    |       -- utility/             # 事件总线（ai_user_event）
    |       -- audio_player/        # 播放器核心（svc_ai_player + consumer/datasink/decoder/mixer/resample）
    |       -- ai_audio/            # TTS/音乐播放编排
    |       -- ai_agent/            # 应用封装（输出回调注册、文本/文件/图片上行）
    |       -- ai_skills/           # 下行文本解析（ASR/NLG/skill）
    |       -- ai_mcp/              # MCP 工具集
    +- selftest                      # 离线自测（独立目标，不依赖云端；见“自测”）
    |   -- agent_chain_selftest.c   # 链路自测（目标 agent_chain_selftest）
    |   -- agent_nlg_selftest.c     # 下行消费自测（目标 agent_nlg_selftest）
    |   -- agent_http_selftest.c    # URL 解析边界自测（目标 agent_http_selftest）
    -- CMakeLists.txt
    -- Kconfig                      # AGENT_DEMO* 开关 + 组件开关（audio_player/ai_audio）
```

设计文档位于 docs 树：
`docs/tuyaopen-core-dev/linux/agent-demo-design.md`
（在 `/home/share/samba/open-components` 下）。

平台适配层（应用侧，位于 `src/adapt/`）：

```sh
+- agent_demo/src/adapt
    +- play_local_in.h/.c            # 音频输入契约 + Linux 实现（PCM 文件播放，真实设备替换点）
    +- play_local_out.h/.c           # ALSA 输出端点（ENABLE_AI_PLAYER 时参与编译）
    +- fs_card.c                     # 文件系统卡（core_fs_ops；备用副本——
                                     #   DATASINK 收窄为 MEM|URL 时不链接，恢复 FILE 源时启用）
+- p2p_demo/src/adapt/fs_card.c      # 同一份原始实现（local_store 使用）
```

> `fs_card.c` 与 p2p_demo 中的副本保持同步（同一实现）；修改文件系统卡后，
> 同步后运行
> `diff tuyaopen-core/examples/linux/agent_demo/src/adapt/fs_card.c tuyaopen-core/examples/linux/p2p_demo/src/adapt/fs_card.c`
> 确认。
