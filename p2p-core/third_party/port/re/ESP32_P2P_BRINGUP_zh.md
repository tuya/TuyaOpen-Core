# ESP32-S3 P2P 出流移植与内存调优记录

本文记录 TuyaOpen-Core 的 P2P（libre/re ICE）在 ESP32-S3 上从“能编译、能配网”到“第一次打开就能出画面”的完整过程，重点说明：

- 最终生效的配置项，以及每一项为什么需要；
- 为什么前后改了多轮才跑通；
- 每一类故障的日志特征、根因和修复方式，方便后续在别的 MCU/平台上复用经验。

适用代码：

- `TuyaOpen-Core`：`0677496`、`f34dac5`、`1209589`、`7133512`
- ESP32 示例工程：`f81b492`（`TuyaOpen-Core-esp32/p2p_demo`）
- 本文 3.7~3.9 及 4.9~4.12 的后续调优（MQTT 锁、私网候选过滤、ICE 启动条件、1500 ms 队列预算）已随本文档一并提交。

---

## 1. 目标与环境

| 项目 | 配置 |
| --- | --- |
| 芯片 | ESP32-S3（N16R8） |
| PSRAM | 8 MB Octal，80 MHz |
| Flash | 4 MB，QIO |
| SDK | ESP-IDF 5.4.4 |
| P2P 后端 | libre（`CONFIG_P2P_ICE_BACKEND_LIBRE=y`），不使用 pjproject |
| 网络栈 | ESP-IDF 自带 lwIP（与 TuyaOpen 的 lwIP 视图无关） |
| 视频源 | 内嵌 `demo_video.264`，320x240@10，250 kbps，GOP 10，4 秒循环 |
| 音频 | 无（回调为空实现） |
| 配网 | BLE NimBLE，NVS 保存 WiFi 凭据 |
| 云端 | Tuya MQTT + RTC 信令 + libre ICE/STUN/TURN |

目标：不接摄像头，用内嵌 H.264 样例流验证 ESP32-S3 的完整链路：

```
BLE 配网 -> WiFi -> MQTT -> RTC 信令 -> ICE/STUN/TURN
        -> LIVE video start -> vcam 取 AU -> P2P 媒体发送 -> App 出画面
```

---

## 2. 最终数据流与关键线程

```
App
 │  MQTT RTC 信令
 ▼
tuya_app_main / tuya_iot_yield
 │  TUYA_EVENT_RTC_REQ -> gw_p2p_mqtt_data_cb
 ▼
ctx_sig（信令 worker，PSRAM 栈）
 │  解析 offer/candidate，驱动 libre ICE
 ▼
rtc_wrk（ICE/媒体会话，PSRAM 栈）
 │  建立 TURN/STUN、选 pair、创建 KCP 通道
 ▼
p2p_media_send（PSRAM 栈）
 │  调用 OnGetVideoFrameCallback
 ▼
demo_vcam（PSRAM 栈）
   读取内嵌 AU -> __demo_encoded_frame_cb -> 2 槽帧队列
   -> P2P RTP/KCP -> TURN/直连 -> App
```

几条关键规则：

- **PSRAM 栈的任务不能调用 flash API**（NVS、OTA、文件系统、SPIFFS/FAT 等）。
- 线程的 **TCB 永远在内部 RAM**；只有栈可以放 PSRAM。
- P2P 的大缓冲区（300 KB 媒体帧、512 KB KCP 队列、2×256 KB 帧队列）放 PSRAM。
- WiFi/BLE/TLS 的内部 RAM 与 P2P 线程栈、AES DMA 是竞争关系。

---

## 3. 最终配置清单

以下配置在 `TuyaOpen-Core-esp32/p2p_demo/sdkconfig.defaults` 中。

### 3.1 PSRAM / heap

```ini
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
```

作用：

| 配置 | 作用 |
| --- | --- |
| `SPIRAM_USE_MALLOC` | 让普通 `malloc/calloc` 可以在内部 RAM 和 PSRAM 之间按大小选择 |
| `ALWAYSINTERNAL=1024` | ≤1 KB 的分配优先内部 RAM；更大的（KCP 1600 B slab、TLS/MQTT/媒体）走 PSRAM |
| `RESERVE_INTERNAL=32768` | 给 DMA/内部专用分配预留 32 KB，避免普通分配把最后一点内部 RAM 吃光 |
| `TRY_ALLOCATE_WIFI_LWIP` | WiFi/lwIP 的大缓冲优先 PSRAM |

注意：`ALWAYSINTERNAL` 必须小于 `TUYA_MBUF_HUGE_SIZE`（1600 B）。KCP 的每个 mbuf
payload 固定申请 1600 字节，只有阈值低于它时才会走 PSRAM；否则最多 500–800 KB
的发送队列会以 1600 B 小块的形式先吃光内部 SRAM。TLS 的 4 KB 收发缓冲同样会
落到 PSRAM，触发 mbedTLS 的“外部内存 AES”路径（见 4.6），需要内部 DMA 仍有约
1.6 KB 余量。

### 3.2 BLE

```ini
CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_MAX_BONDS=1
CONFIG_BT_CTRL_BLE_MAX_ACT=1
```

作用：

- NimBLE **host** 的堆改到 PSRAM；controller 仍使用内部 RAM。
- 设备只需要一条 BLE 连接（配网/DP），因此连接数、bond 数、controller 活动数都降到 1，减少 controller 内部缓冲。

这一项释放的内部 RAM 非常可观，是后续 AES DMA 能成功的关键之一。

### 3.3 WiFi

```ini
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=8
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16
CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=8
CONFIG_ESP_WIFI_CACHE_TX_BUFFER_NUM=16
```

默认是 16/32/16/32。P2P 样例流只有 250 kbps，8 个静态 RX/TX 和 16 个动态 RX 足够；减半后释放约 25 KB 内部 RAM。

### 3.4 TLS / mbedTLS

```ini
CONFIG_ENABLE_PLATFORM_MBEDTLS=y
CONFIG_MBEDTLS_THREADING_C=y
CONFIG_MBEDTLS_THREADING_ALT=y
CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=4096
# CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN is not set
```

说明：

- 使用 ESP-IDF 自带的 mbedTLS，不编译 core 自带的副本，避免符号冲突。
- 对称 4 KB in/out，与上游 ESP32 port 保持一致。此前关掉对称配置时，core 会向 MQTT 服务端声明 `max_fragment_length=1024`，服务端的约 2 KB 证书消息会被拆到多个 record，而 mbedTLS 3.x 没有跨 record 的 handshake 重组，会报 `-0x7080`。这个坑在配置注释里已有记录。
- TLS 缓冲最终落在 PSRAM，因此 AES 走外部内存 DMA 路径；必须保证内部 DMA 池有余量。

### 3.5 P2P / libre

```ini
CONFIG_ENABLE_TUYA_P2P=y
CONFIG_P2P_ICE_BACKEND_LIBRE=y
# CONFIG_ENABLE_LOCAL_STORE is not set
# CONFIG_ENABLE_IPC_RING_BUFFER is not set
```

内存放置（`p2p-core/CMakeLists.txt`）：

`P2P_MEM_USE_PSRAM` 已从所有平台移除，`core_p2p_malloc()` 直接走普通
`malloc()`；配合 ESP-IDF 的 `SPIRAM_MALLOC_ALWAYSINTERNAL=1024`，300 KB 媒体帧
和 1600 B 的 KCP mbuf payload 都会自动落到 PSRAM。T5AI 侧由
`tkl_memory.c` 的 `TKL_PSRAM_MALLOC_ALWAYS_INTERNAL=1024` 提供同样的策略。

P2P 内部的大块内存：

| 缓冲区 | 大小 | 位置 |
| --- | --- | --- |
| 媒体帧 `media_frame.data` | 300 KB | PSRAM |
| KCP 发送队列 | 512 KB | PSRAM |
| demo 帧队列 | 2 × 256 KB | PSRAM |
| 信令队列节点/负载 | 小对象 | 内部 RAM（当前积压为 0） |

### 3.6 线程栈放置策略

这是 ESP32 上最关键、也最容易踩坑的一张表。

| 线程 | 栈大小 | 位置 | 原因 |
| --- | --- | --- | --- |
| `tuya_app_main` | 24 KB | 内部 | 读 NVS WiFi 凭据、TLS 握手、OTA、事件循环 |
| `wq_system` / `wq_highpri` | 6 / 5 KB | 内部 | OTA/KV/云端回调，可能触 flash |
| `sys_timer` | 4 KB | 内部 | 核心定时器 |
| `lan_sock_loop` | 4 KB | 内部 | LAN socket，保持与云/配网一致 |
| `p2p_cmd_recv` | 4 KB | 内部 | 会回调 App 媒体接口，播放路径可能访问 flash 文件系统 |
| `tuya_ipc_demo_thread` | 8 KB | 内部 | `TUYA_APP_Start -> p2p_init` 内有 `core_kv_get/set` |
| `local_store_wr` | 12 KB | 内部 | 录音写文件（启用时） |
| `p2p_pb_send` | 12 KB | 内部 | 播放时读 local_store 文件 |
| `heap_mon` | 2.8 KB | 内部 | 诊断任务（可删除） |
| `ctx_sig` | 10 KB | **PSRAM** | 只做信令/ICE，不触 flash |
| `rtc_wrk` | 10 KB | **PSRAM** | ICE/媒体会话，不触 flash |
| `tuya_p2p_listen_task` | 4 KB | **PSRAM** | 监听/接入，不触 flash |
| `p2p_media_send` | 4 KB | **PSRAM** | KCP/媒体发送，不触 flash |
| `p2p_audio_dl` | 8 KB | **PSRAM** | 下行音频，不触 flash |
| libre `re` 线程 | 8 KB | **PSRAM** | socket/timer/ICE 内部工作线程 |
| `demo_vcam` | 4 KB | **PSRAM** | 读内嵌样例、喂帧队列，不触 flash |

判断标准只有一句话：

> **这个线程会不会直接或间接调用 NVS/OTA/文件系统？会，就必须内部栈；不会，才可以 PSRAM 栈。**

---

### 3.7 MQTT 与信令可靠性

| 参数 / 行为 | 位置 | 作用 |
| --- | --- | --- |
| `CORE_MQTT_BUFFER_SIZE` 2048 → 4096 | `iot-core/tuya_transport/config/core_mqtt_config.h` | coreMQTT 的 RX/TX 共用网络缓冲。RTC 的 SDP offer/answer 约 1.5~1.8 KB，2048 时容易在收包回调里回 answer 时失败 |
| coreMQTT 递归互斥锁 | `iot-core/tuya_transport/src/mqtt/mqtt_client_wrapper.c` | 串行化 `MQTT_ProcessLoop`、`MQTT_Publish`、`Subscribe`、`Connect`、`Disconnect`。coreMQTT 不是线程安全的：主线程收包和 `ctx_sig` 发 answer 并发时会 `MQTTSendFailed` 并断线 |
| 重复 offer 保留会话 | `p2p-core/base_ice/src/tuya_media_service_rtc.c` | 同一 session + 相同 SDP 哈希 + 创建 < 5 秒：不销毁重建，只重发 answer。App 在等 answer 时会约 1 秒重试一次，重建会让 ICE 永远来不及完成 |

`CORE_MQTT_BUFFER_SIZE` 现在允许应用覆盖：

```c
#ifndef CORE_MQTT_BUFFER_SIZE
#define CORE_MQTT_BUFFER_SIZE (4096U)
#endif
```

### 3.8 ICE 候选策略

ESP32 默认仍然是 **relay-only**：云端提供 TURN 时只用 TURN relay 候选。原因是实测中 App 的 host 候选经常是 VPN/私网地址（`10.x`、`100.100.x`、`192.0.0.x`），ESP32 从局域网不可达。

| 参数 / 行为 | 位置 | 作用 |
| --- | --- | --- |
| `P2P_ICE_ALLOW_DIRECT` | `p2p-core/third_party/port/re/src/libre_ice.c` | 实验开关。设为 1 时保留 host/srflx 候选；ESP32 实测反而变慢（不可达 host 候选优先级最高），默认关闭 |
| 私网 host 候选过滤 | `p2p-core/third_party/port/re/src/libre_ice.c` | relay-only 模式下丢弃远端 host 候选中的 `10/8`、`172.16/12`、`192.168/16`、`100.64/10`、`192.0.0/24`、`169.254/16`、回环/组播。避免 ICE 先花时间检查不可达 pair，错过 App 的首次超时 |
| ICE 启动条件 | `p2p-core/third_party/port/re/src/libre_ice.c` | 只有至少一个可用远端候选已经入队（或 icem 中已有）才置 `start_requested`。否则过滤掉前几个候选后会出现 `ice: form: ... no remote candidates` 的早启动失败 |
| ICE early-conclude 收紧 | `p2p-core/patches/re/0002-ice-early-conclude.patch` | 当前有效 pair 是 relay→relay 且还有非 relay pair 未检查时，不提前 conclude；等更好的 pair 或全部检查完 |
| 远端候选日志 | `tuya_media_service_rtc.c` | `remote candidate: ...` 打印候选类型/地址；relay-only 过滤时打印 `skip private host candidate ...` |

> 注意：`P2P_ICE_ALLOW_DIRECT=1` 只适合双方 host 候选在同一可达网络的场景。公网/移动网络下保留它会让 ICE 先检查不可达的 host 候选，建议保持关闭。

### 3.9 媒体发送队列预算

| 参数 | ESP32 | T5AI / Linux | 作用 |
| --- | --- | --- | --- |
| `P2P_TX_LATENCY_BUDGET_MS` | 700 | 700 | KCP 发送队列的延迟预算。超过预算会丢帧并请求新 I 帧。ESP32 在固定码率样例流阶段曾用 1500；250/150/100 梯子上线后回到 700 |

该值在 `tuya_ipc_p2p.c` 里默认为 700，可用编译宏覆盖（不再按平台在 CMake 里切换）。

注意：`__p2p_video_fill_pct()` 还会应用 keyframe floor——`budget = max(kbps*P2P_TX_LATENCY_BUDGET_MS/8, tx_max_frame*P2P_TX_KEYFRAME_ROOM)`，`ROOM=3`。250 档首个 I 帧约 17 KB 时，实际 shedding 阈值仍约 50–62 KB（1.6–2.0 s），700 ms 并不会把阈值压到 22 KB。要真正降低排队延迟，需要单独评估 `P2P_TX_KEYFRAME_ROOM`（3→1.5~2）或准入逻辑；shedding 日志现在会打印实际 budget（含 floor）。

### 3.10 媒体发送有界等待

| 参数 | 值 | 作用 |
| --- | --- | --- |
| `P2P_MEDIA_SEND_TIMEOUT_MS` | 200 | `p2p_send_rtp_data()` 的 KCP 队列等待上限；超时或部分发送都整帧丢弃，并请求新 I 帧 |

同一媒体线程同时发 VDATA/ADATA，过去使用 `timeout=-1` 的无界等待；队列满或 relay 停摆时，线程可能长时间无法回到取帧循环，表现为下一路预览 `first enqueue` 后没有 `first fetch`。现在：

- 200 ms 是**分片级**超时（4 片最坏约 800 ms），下一轮验证需要看 `vsend send slow` 的 elapsed 分布，再决定是否加帧级 deadline；
- 发送失败/部分发送后置 `video_need_iframe=TRUE` 并调用 `__p2p_request_i_frame()`（1.5 s 限频），避免断链后继续发 P 帧；
- 关键诊断：`vsend send start/done`（仅首帧窗口）、`vsend send slow`（≥100 ms）、`rtc send wait/still waiting/wait done`（队列等待）、`vcam tick late`（生产者调度延迟）。

### 3.11 ESP32 码率梯子

| 档位 | 文件 | 实际平均码率 | I 帧大小 |
| --- | --- | --- | --- |
| 250 kbps | `demo_video.264` | ≈235 kbps | 首个 ≈17.4 KB，最大 ≈20.8 KB |
| 150 kbps | `demo_video_150.264` | ≈143 kbps | 更小 |
| 100 kbps | `demo_video_100.264` | ≈93 kbps | 更小 |

三档统一从 Linux 2304×1296 源转码为 320×240@10、40 AU、4 I、GOP 10、Constrained Baseline、I/P only，SPS/PPS 逐字节一致（切换不重置解码器）。`demo_vcam_open_embedded()` 预索引三档并校验 SPS/PPS；`demo_vcam_set_bitrate()` 选“≤请求的最高兼容档”，pending/2 秒 hold-down 返回 `OPRT_RESOURCE_NOT_READY`，低于最低档返回 `OPRT_NOT_SUPPORTED`，下一 tick 从新档的 AU0（IDR）开始。

`DEMO_VCAM_FORCE_SWITCH_TEST=1` 是 bring-up 测试钩子：预览 10 s 强制 150、20 s 强制 100，用于验证切档与解码连续性；生产构建为 0。`vcam tick late` 在 vcam 线程晚于计划 >200 ms 时打印（每路上限 5 条），用于区分生产者调度延迟和消费者阻塞。

注意：core 日志里的 `rc_cur_kbps` 是**请求值**，不是实际档位。demo 选择“≤请求的最高兼容档”，例如请求 212 实际切到 150，但 core 仍把 `rc_cur` 记为 212（下一窗口日志从 212 继续，而不是 150）。实际档位以 `vcam switch: Nkbps` 为准；队列预算和 rate-control 步长按请求值计算，可能比实际流高一个梯级。另：`link` 估计在突发期不可信（例如 `link 679 kbps` 但队列到 64 KB），判断拥塞应以队列填充率、`full events` 和 shedding 为准。

---

## 4. 为什么改了这么多轮：故障链复盘

这些故障不是同时出现的，而是“前一个修好之后，后一个才暴露”。整体顺序如下：

```
构建/链接冲突
  -> 配网成功，但 P2P 初始化失败（内部 RAM 耗尽）
  -> 全量 PSRAM 栈，启动时 NVS flash 断言
  -> 改为按线程选择栈，P2P 初始化成功
  -> App 一发 RTC 信令，pthread_self 断言
  -> 修 pthread_self，出现 MQTT/TLS 重连风暴 + 计时器失效
  -> 修 CLOCK_MONOTONIC_RAW，出现 AES DMA 分配失败（SDP 解不开）
  -> 释放内部 RAM，SDP 能解开，ICE 能建立
  -> 第一次直播仍失败，重试才有画面（ICE 选了 relay→relay）
  -> 修 ICE 选路 + 首帧时序，第一次打开就能出画面
```

### 4.1 构建/链接期

| 现象 | 根因 | 修复 |
| --- | --- | --- |
| `sys_msleep` 重定义 | ESP-IDF lwIP 的 `arch/sys_arch.h` 已定义 `sys_msleep` 宏 | libre 补丁 `0005` 给 `re_sys.h` 加 `#ifndef` 保护，并 staging 一份 `re.h` 让 include 命中补丁头 |
| `gai_strerror` 未定义 | ESP-IDF lwIP 不提供 | `port/esp-idf/src/p2p_libc/libre_compat.c` 提供 weak 兜底 |
| `udp_send/udp_connect/tcp_accept/tcp_connect` 冲突 | libre 与 ESP-IDF lwIP 同名 | 编译定义把 libre 的副本重命名为 `re_udp_send` 等 |
| `hmac_sha1/hmac_sha256` 冲突 | wpa_supplicant 也导出同名符号 | 编译定义重命名为 `re_hmac_sha1/re_hmac_sha256` |
| `clock_gettime` 重复定义 | 端口 shim 与 ESP-IDF newlib 同名 | ESP-IDF 下不编译端口 shim，使用 libc 版本 |
| base_ice 没命中 staging 头 | include 顺序被上游 include 目录抢先 | `target_include_directories(BEFORE PRIVATE .../re-patched/include)` + 依赖 `re_patch` |
| `-include` 泄漏到汇编文件 | 平台选项放到了 interface target | 新增 `RE_PLATFORM_COMPILE_OPTIONS`，只加在 `libre_p2p`/`base_ice` 上 |

经验：**p2p-core/third_party/re 永远不改**，所有平台差异通过 `p2p-core/patches/re/*.patch` 在构建目录 staging 后再编译。

### 4.2 配网成功，P2P 初始化失败

现象：

```
create p2p_media_send task failed
tuya_ipc_p2p_init failed
esp-aes: Failed to allocate memory
```

根因：配网后 BLE + WiFi + TLS + 线程栈把内部 RAM 吃到只剩几 KB，连 4 KB 的 `p2p_media_send` 栈都分配不出来。

第一次尝试的修复是“把所有 core 线程栈都放 PSRAM”，但引入了 4.3 的启动断言。

### 4.3 PSRAM 栈 + flash 断言

现象：

```
assert failed: spi_flash_disable_interrupts_caches_and_other_cpu
(cache_utils.c:163 esp_task_stack_is_sane_cache_disabled())
Backtrace: ... nvs_get_blob <- __kv_get <- core_kv_get <- wifi_sta_load_cfg
```

根因：

- ESP-IDF 执行 flash 操作时会关闭 cache；
- 此时当前任务的栈必须在内部 DRAM 中，否则断言；
- `tuya_app_main` 启动时从 NVS 读取 WiFi 凭据，而它的栈被放到了 PSRAM。

修复：

- `THREAD_CFG_T` 不再提供把线程栈放到 PSRAM 的开关；
- 所有 core 线程（含 P2P/媒体线程）均由端口的 `xTaskCreate` 在内部 DRAM 创建。

> 说明：曾引入 `THREAD_CFG_T.psram_mode` 与 `core_api_ops.thread_create_in_psram`，现已移除；P2P 线程与其它线程一样走端口默认的线程创建路径。

相关代码：

- `iot-core/core_system/include/core_thread.h`
- `iot-core/core_system/src/core_thread.c`
- `port/esp-idf/src/port_api.c`
- `p2p-core/svc_streaming_p2p/src/tuya_ipc_p2p.c`
- `p2p-core/base_ice/src/tuya_media_service_rtc.c`
- `p2p-core/third_party/port/re/src/re_pthread_tuyaopen.c`

### 4.4 `pthread_self` 断言

现象：

```
assert failed: pthread_self pthread.c:583
(false && "Failed to find current thread ID!")
Backtrace: thrd_current -> re_alloc -> re_thread_init -> libre_init
           -> global_init -> pj_ice_session_create -> ctx_signaling_worker_thread
```

根因：

- ESP-IDF 的 `pthread_self()` 只对 `pthread_create()` 创建的任务有效；
- P2P 的 `ctx_sig` 是 core_thread 创建的原始 FreeRTOS 任务；
- libre 的 `thrd_current()` 直接调用 `pthread_self()`。

修复：libre 的 `pthread_self()` 现在由可移植 shim 提供
（`re_platform_config.h` 把它映射到 `re_pthread_self()`，后者返回
`core_thread_get_id()`；ESP-IDF 的该实现就是当前 FreeRTOS 任务句柄）。
因此 `thrd_current()` 保持上游写法即可，不再需要平台分支：

```c
thrd_t thrd_current(void)
{
    return pthread_self();
}
```

`pthread_equal()` 只比较值，因此用 task handle 作为线程标识，`re_thread_check()` 的语义不变。

### 4.5 `CLOCK_MONOTONIC_RAW` 导致计时器全部失效

现象：

```
tmr: jiffies: clock_gettime() failed (unknown error [22])
p2p signaling backlog qbytes=60384 ... 64894
mqtt client connected / disconnected 反复出现
esp-aes: Failed to allocate memory
```

根因：

- libre 的 `tmr.c` 优先使用 `CLOCK_MONOTONIC_RAW`；
- ESP-IDF 的 `clock_gettime()` 只支持 `CLOCK_REALTIME` 和 `CLOCK_MONOTONIC`，对 `CLOCK_MONOTONIC_RAW` 返回 `EINVAL`（errno 22）；
- 所有 timer jiffies 变成 0，ICE/信令 worker 的定时、重传、poll 超时全部异常，消息积压，MQTT/TLS 进入重连风暴。

修复：时钟选择收敛到 `p2p-core/third_party/port/re/include/re_platform_config.h`
（平台适配层），`tmr.c` 本身不再出现平台宏：

```c
#if defined(ESP_PLATFORM)          /* libc 声明 RAW 但运行期返回 EINVAL */
#define RE_TIMER_CLOCK_ID CLOCK_MONOTONIC
#endif
```

`tmr.c` 优先使用 `RE_TIMER_CLOCK_ID`，未定义时回退到上游的
`CLOCK_MONOTONIC_RAW`/`CLOCK_MONOTONIC` 逻辑。

### 4.6 AES DMA 分配失败（SDP 解不开）

现象：

```
recv message ... payload len:1690
E esp-aes: Failed to allocate memory
Cmd Parse Fail:-1
protocol message parse error:-1
```

小 candidate 消息能处理，但约 1.5~1.7 KB 的 SDP 每次解密失败，导致 ICE 永远拿不到远端参数，只能不停收 candidate。

根因：

- ESP-IDF 的 mbedTLS AES 在缓冲区位于 PSRAM（或非 DMA 可达）时，会走 `esp_aes_process_dma_ext_ram()`；
- 该路径需要内部 DMA 缓冲，ESP32-S3 上单块最大 `AES_MAX_CHUNK_WRITE_SIZE = 1600` 字节；
- 内部 RAM 被 BLE/WiFi/TLS/线程栈吃光后，连 1.6 KB 都分配不出来；
- 于是不是 JSON 解析问题，而是“解密前的 AES 分配失败”。

修复思路不是关掉硬件 AES，而是**把内部 RAM 腾出来**：

1. NimBLE host 堆迁到 PSRAM；
2. BLE 连接/bond/controller 活动数降到 1；
3. WiFi 静态 RX/TX、动态 RX 减半；
4. P2P 大缓冲和帧队列继续放 PSRAM；
5. 增加堆水位日志，确认内部 RAM 从“几乎为 0”恢复到空闲 85 KB 左右。

最终空闲水位（P2P ready，未出流）：

```
heap: int=84087 largest=31744 dma=76299 psram=8215912
```

出流时：

```
heap: int=55~64KB dma=47~56KB psram≈7.34MB
```

### 4.7 第一次直播失败，重试才有画面（ICE 选路）

现象：

- 第一次：ICE 成功、LIVE 启动、码率控制也在跑，但 App 约 6 秒后发 `CTRL VIDEO op[3]` 停止；
- 重试：出画面并持续正常。

日志对比：

第一次（失败）：

```
selected pair lcand=198.51.100.10:63356 rcand=198.51.100.10:60568
ICE negotiation success ...
LIVE video start ...
6 秒后 App 停止
```

重试（成功）：

```
selected pair lcand=198.51.100.20:49997 rcand=203.0.113.10:3397
ICE negotiation success ...
LIVE video start ... 持续
```

根因：

- libre 的 `0002-ice-early-conclude.patch` 会“每个 component 只要有一个有效 pair 就提前结束 ICE”；
- 第一次先验证通过的是 relay→relay（TURN 到 TURN）的最低优先级路径；
- 更好的非 relay pair 还没检查完，ICE 已经 conclude，媒体只能走两跳 TURN，App 首帧超时；
- 重试时候选顺序变化，先验证到 relay→direct，才正常。

修复：收紧 `0002` 补丁：

- 如果当前有效 pair 是 relay→relay，且 check list 里还有非 relay pair 未完成，就不提前结束；
- 等更好的 pair 出结果，再由 `concluding_ice()` 选最高优先级的有效 pair；
- 如果所有剩余候选都是 relay，才回退到 relay。

修复后第一次尝试的日志：

```
selected pair lcand=198.51.100.30:60200 rcand=203.0.113.10:3358
```

### 4.8 首帧时序：SPS/PPS 不能丢

除了选路，首帧本身也有时序问题：

- 原逻辑先打开 vcam，再把 `s_live_push_enable` 置 TRUE；
- vcam 线程可能在置 TRUE 之前就送出第一个 AU（带 SPS/PPS 的 IDR），被回调直接丢掉；
- App 收不到 SPS/PPS，解码器无法初始化，表现为黑屏/超时。

修复：

1. 直播启动时先清空帧队列、置 `s_live_push_enable = TRUE`，再打开 vcam；
2. 保证第一个入队帧是 AU0（AUD/SPS/PPS/SEI/IDR）；
3. 增加首帧日志：

```
live first enqueue: +55ms key=1 len=5023 pre_drops=0
live first fetch:   +58ms key=1 size=5023
vcam I-frame jump: au=10
```

现在第一次直播从 `LIVE video start` 到首帧取走约 58 ms。

---

### 4.9 MQTT 并发导致第一次 answer 发送失败

现象：

```
MQTT_ProcessLoop returned with status = MQTTSendFailed
mqtt client disconnected
```

第一次打开实时画面时，App 发出 RTC offer，设备在 `ctx_sig` 线程回 answer；与此同时 `tuya_app_main` 正在跑 `MQTT_ProcessLoop`。coreMQTT 不是线程安全的，两个线程同时操作同一个 MQTT context，发送失败后客户端主动断开。

修复：在 `mqtt_client_wrapper.c` 中给所有 coreMQTT 操作加递归互斥锁。使用递归锁是为了兼容“在收包回调内部 publish”的路径。修复后第一次 offer 的 `MQTTSendFailed` 消失。

### 4.10 App 重发 offer 导致 ICE 反复重建

现象：

```
process signaling offer
p2p session destroy reason=offer_same_session_ice_stuck
process signaling offer
p2p session destroy reason=offer_same_session_ice_stuck
...
```

App 在没收到 answer 时会约 1 秒重发一次相同 offer。旧逻辑只要同一 session 的 ICE 还没 ready，就销毁重建；于是形成“创建→销毁→创建”的循环，ICE 永远来不及完成。

修复：

- 会话记录 `create_ms` 和 `offer_hash`（FNV-1a）；
- 同一 session、SDP 哈希相同、创建 < 5 秒：保留会话，走现有路径重发 answer；
- SDP 不同或卡住超过 5 秒：才销毁重建。

### 4.11 远端私网 host 候选拖慢 ICE

现象：

```
remote candidate: ... 10.132.150.7 ... typ host
remote candidate: ... 100.100.15.113 ... typ host
remote candidate: ... 192.0.0.6 ... typ host
worker_pending: lcand=1 rcand=5 conncheck=1 nego=0/0
（约 7 秒后 App 超时，换新 session）
```

根因：

- 这些 host 候选来自 App 的 VPN/私网接口，ESP32 从 192.168.1.x 不可达；
- ICE 中 host 候选优先级最高，检查它们会消耗首次连接的时间窗口；
- 真正可达的 `srflx`/`relay` pair 排在后面，等轮到它们时 App 已经超时。

修复：relay-only 模式下过滤远端私网 host 候选（见 3.8）。ICE 直接检查 `relay → srflx` / `relay → relay`，不再被不可达 host 拖延。

过滤后还要注意一个启动时序问题：前几个候选（私网 host）被过滤时，远端 ufrag/pwd 已经拿到，旧逻辑会立刻 `start_requested = true`，libre 在零个远端候选时组建 checklist，报：

```
ice: form: 'tuya_p2p' no remote candidates
libre_ice: conncheck start failed
```

因此现在只有至少一个可用远端候选入队后才允许启动 ICE；`pj_ice_session_try_start_ice` 也加了同样的条件。

### 4.12 固定码率样例流 + TURN 中继的队列预算

现象：

```
video queue 22272 bytes is 101 pct of the 1500ms budget, shedding frames
rate control: 250 -> 212 kbps [decrease] (link 323 kbps, queue avg 100 pct, ...)
```

根因：

- 当时 ESP32 没有码率梯子，样例流码率固定，`demo_vcam_set_bitrate()` 返回 `NOT_SUPPORTED`；
- 自适应控制器可以“建议”降码率，但源端不会真的降；
- TURN 中继路径的首帧 I 帧突发较大，队列预算在链路预热阶段就可能被判定为拥塞，开始丢帧，App 看到卡顿后退出。

修复：固定码率阶段 ESP32 曾把 `P2P_TX_LATENCY_BUDGET_MS` 放宽到 1500 ms；250/150/100 梯子上线后回到 700 ms。700 ms 仍受 keyframe floor 限制（见 3.9），真正的延迟下降需要后续评估 `P2P_TX_KEYFRAME_ROOM`。

### 4.13 有界发送、码率梯子与剩余问题

**14:27 失败签名**：队列 1 秒内从 5% 跳到 96–100%，同时 `link` 估计从 252–278 kbps 升到 314 kbps，7 秒后 App 发 `op[3]`；`rate control` 每秒重复 `250 -> 212 kbps [decrease]`，但 `rc_cur_kbps` 始终是 250——`demo_vcam_set_bitrate()` 当时返回 `NOT_SUPPORTED`。这组形状是 relay/ACK 停摆（发送窗口里的数据没有确认），不是渐进拥塞；码率梯子只能减小停摆期间的积压，救不了通路本身。

**P0 有界发送**：`p2p_send_rtp_data()` 改为 200 ms 超时，超时/部分发送整帧丢弃并请求 I 帧；14:46 起的会话没有再出现 `rtc send wait` 或 `vsend send slow`。

**梯子验证**：15:14 会话强制 250→150（10 s）→100（20 s），两次 `ret=0`，无 `op[3]`/LIVE 停止、无 shedding、无 `vcam tick late`；设备侧未见重连，画面连续性**待用户目测确认**。4.5 分钟 250 档会话 0 shedding、0 次 rate control 变化。15:57 生产固件会话走了真实路径：队列 64,528 B 超过实际预算 62,484 B 后 shedding，`rate control: 250 -> 212` 触发 `vcam switch: 150kbps`，队列排空后 4 秒内 `246 -> 250` 切回 250，无 `op[3]`。

**剩余未验证项**：

1. 真实弱网下 rate control 触发 250→150/100 的路径（force switch 绕过了它）；最好用限速 AP（约 120 kbps）验证，否则留现场数据。
2. 原始“无首帧”签名在 P0 后尚未复现；`vcam tick late` 已就位，若再出现 511 ms 级生产者停顿可直接定性。
3. 14:27 的 ACK 饥饿/relay 断流应单独立项（例如 `ikcp_waitsnd` 连续 N 秒不降且无新 ACK 时触发会话重建/重新 offer），不要用梯子验证结果覆盖它。

---

## 5. 内存账本

ESP32-S3 启动时（日志）：

```
heap_init: 222 KiB + 21 KiB + 32 KiB 内部 DRAM（另有 7 KiB RTC）
esp_psram: 8192 KiB PSRAM
```

这些内部 RAM 同时要供：

- WiFi 静态/动态缓冲；
- BLE controller + NimBLE host；
- MQTT/TLS（mbedTLS 上下文、证书、4 KB in/out）；
- Tuya core 线程栈（`tuya_app_main` 24 KB、wq 线程等）；
- P2P 线程栈（可放 PSRAM）；
- AES DMA（1.6 KB chunk + descriptors）；
- lwIP pbuf、socket 等。

最终策略：

| 资源 | 放置 |
| --- | --- |
| P2P 大缓冲（300 KB/512 KB/2×256 KB） | PSRAM |
| P2P 信令/媒体线程栈 | PSRAM |
| NimBLE host 堆 | PSRAM |
| WiFi/lwIP 大缓冲 | PSRAM（`TRY_ALLOCATE_WIFI_LWIP`） |
| NVS/OTA/文件系统相关线程栈 | 内部 RAM |
| TLS/MQTT 缓冲 | 按大小阈值，最终大块在 PSRAM |
| TCB、DMA、WiFi 静态缓冲、BLE controller | 内部 RAM |

结论：**PSRAM 不是“内部 RAM 的透明替代品”**。栈、DMA、flash 操作各有约束，必须按用途分配。

---

## 6. 日志特征与排查手册

| 日志/断言 | 含义 | 处理 |
| --- | --- | --- |
| `esp_task_stack_is_sane_cache_disabled` | PSRAM 栈任务调用了 flash API | 把该线程改回内部栈；检查 NVS/OTA/文件系统调用链 |
| `pthread_self ... Failed to find current thread ID` | 原始 FreeRTOS 任务调用了 ESP-IDF `pthread_self()` | libre `thrd_current` 用 task handle（补丁 0006） |
| `tmr: jiffies: clock_gettime() failed ... [22]` | 不支持的 clock id | ESP-IDF 用 `CLOCK_MONOTONIC`（补丁 0007） |
| `esp-aes: Failed to allocate memory` | 内部 DMA 不足（常见于 SDP/大消息/TLS） | 释放内部 RAM；检查 BLE/WiFi/TLS/`ALWAYSINTERNAL` |
| `create p2p_media_send task failed` | 4 KB 内部栈都分配失败 | 给 P2P 线程 PSRAM 栈，或继续释放内部 RAM |
| `Cmd Parse Fail` + `protocol message parse error` | 多半是前面的 AES 解密失败，不是 JSON 语法错 | 先看同一时刻的 `esp-aes` |
| `p2p signaling backlog qbytes=...` | 信令 worker 处理不过来 | 查 timer/clock、CPU 占用、锁、消息风暴 |
| `mqtt client connected/disconnected` 反复 | TLS/AES 失败、timer 失效，或 coreMQTT 并发 | 查 AES、clock、内部 RAM；确认 MQTT 互斥锁生效 |
| `MQTT_ProcessLoop ... MQTTSendFailed` | coreMQTT 被多线程并发调用，或 MQTT 缓冲不足 | 串行化 coreMQTT 操作；必要时把 `CORE_MQTT_BUFFER_SIZE` 提到 4096 |
| `duplicate offer: keep session` | App 重发相同 offer，设备保留会话并重发 answer | 正常；说明去重逻辑生效 |
| `offer_same_session_ice_stuck` 连续出现 | 同一会话被反复销毁重建 | 检查 SDP 哈希/年龄判断；不要让重复 offer 重置 ICE |
| `skip private host candidate ... (relay-only)` | 远端 host 候选是私网/VPN 地址，已过滤 | 正常；避免 ICE 先检查不可达 pair |
| `ice: form: ... no remote candidates` / `conncheck start failed` | 过滤后还没有可用远端候选就启动了 ICE | 只在至少一个可用候选入队后才置 `start_requested` |
| `selected pair ...` 为 relay→relay 且 App 超时 | ICE 选了最低优先级路径 | 收紧 early-conclude（补丁 0002）；过滤私网 host 候选 |
| `shedding frames` / `queue ... pct of the ... budget` | KCP 发送队列超过延迟预算 | 查链路/RTT；ESP32 可把 `P2P_TX_LATENCY_BUDGET_MS` 放宽到 1500；固定码率源端无法真正降码率 |
| `live first enqueue/fetch` 没有出现 | 帧没入队/没被取走 | 查 `s_live_push_enable`、vcam 启动、帧队列 |
| `pre_drops` 很大 | 开流前 vcam 已经送帧，SPS/PPS 可能丢 | 先置 push enable 再开 vcam |

---

## 7. 学到的规则（Checklist）

1. **先区分“初始化成功”和“媒体链路成功”。** P2P listen 起来不代表 ICE/媒体能通。
2. **任何要触 flash 的线程，栈必须在内部 RAM。** 这是硬约束，不是性能建议。
3. **PSRAM 栈只给纯网络/媒体线程。** 拿不准就保持内部栈，先省别的地方。
4. **ESP-IDF 的 POSIX 兼容层不是完整 POSIX。** `pthread_self()`、`clock_gettime()` 的可用范围要按平台确认。
5. **计时器失效会伪装成网络问题。** 看到 MQTT 重连风暴、信令积压，先 grep `clock_gettime`。
6. **AES 分配失败不一定是密码学问题。** 在 MCU 上经常是内部 DMA 池不足。
7. **先加堆水位和阶段日志，再猜。** 没有日志时，“没有画面”可能来自十几个不同环节。
8. **libre 的平台差异用 patch 管理。** `third_party` 保持 pristine，构建时 staging。
9. **ICE 连通不等于路径好。** relay→relay 可能“能通但首帧超时”；候选优先级和 conclude 时机要一起看。
10. **首帧要带 SPS/PPS 的 IDR。** 开流时序不对，第一个 AU 会被丢掉。
11. **coreMQTT 不是线程安全的。** 收包线程和业务线程同时 publish 必须加锁，否则第一次 answer 就可能 `MQTTSendFailed`。
12. **重复 offer 不能重置 ICE。** App 的重试通常只是没收到 answer；相同 SDP 应保留会话并重发 answer。
13. **relay-only 时要过滤不可达的远端 host 候选。** 私网/VPN host 优先级最高却不可达，会吃掉首次连接的超时窗口。
14. **固定码率源端不能只靠自适应控制器。** 控制器建议降码率但源端不降时，需要真正的码率梯子；ESP32 已用 250/150/100 三档实现，而不是继续放宽队列预算。

---

## 8. 当前限制与后续工作

- ESP32 使用**内嵌 4 秒样例流**，不是摄像头实时画面；画面每 4 秒循环。
- 无音频。
- ESP32 已接入 250/150/100 三档内嵌码率梯子，`demo_vcam_set_bitrate()` 会按请求选“≤请求的最高兼容档”并从 IDR 切换；`DEMO_VCAM_FORCE_SWITCH_TEST=1` 只用于 bring-up 强制切档验证，生产构建为 0。真实弱网下由 rate control 触发的切档路径仍需限速 AP 或现场数据验证。
- 700 ms 队列预算仍受 keyframe floor 限制（`tx_max_frame * P2P_TX_KEYFRAME_ROOM`，250 档约 50–62 KB）；要压低排队延迟，需要另评估 `P2P_TX_KEYFRAME_ROOM`。
- 14:27 那类 relay/ACK 停摆（队列瞬间满且 `link` 反升）不属于码率问题；后续应单独立项做 ACK 饥饿检测/会话重建。
- ICE 是否成功仍取决于 App 侧的候选和 TURN/NAT 路径。设备侧已做：relay-only、私网 host 候选过滤、early-conclude 收紧、重复 offer 保留会话；如果仍间歇失败，先看 `remote candidate` 和 `selected pair`，再判断是 TURN 路径还是 App 侧超时。
- `P2P_ICE_ALLOW_DIRECT=1` 是实验开关，ESP32 默认关闭；只有双方 host 候选在同一可达网络时才建议打开。
- `CORE_MQTT_BUFFER_SIZE=4096` 是按当前 Tuya MQTT/SDP 大小选的；换成其他云或更大的信令消息时需要重新评估。
- `heap_mon`、首帧日志、`remote candidate` 日志是诊断代码，量产时可以裁剪或降低日志等级。
- 如果后续接真实摄像头/编码器，建议保留 PSRAM 栈策略，并重新评估编码器线程是否需要内部栈（编码器可能使用 DMA/flash）。

---

## 9. 复现与关键文件

构建：

```bash
source <esp-idf>/export.sh
cd TuyaOpen-Core-esp32/p2p_demo
idf.py -B build -D CONFIG_ENABLE_TUYA_P2P=ON build
idf.py -B build -p /dev/ttyUSB0 flash
```

注意：

- `components/tuyaopen-core` 指向本地 `TuyaOpen-Core`；
- `p2p_demo/main/tuya_config_secrets.h` 保存真实 UUID/AuthKey，已被 `.gitignore` 忽略；
- 不要提交 `tuya_config_secrets.h`、`sdkconfig`、真实 `tuya_config.h`。

关键代码：

| 文件 | 作用 |
| --- | --- |
| `p2p-core/patches/re/0002-ice-early-conclude.patch` | ICE 选路（避免 relay→relay 提前 conclude） |
| `p2p-core/patches/re/0005-re-sys-msleep.patch` | lwIP `sys_msleep` 冲突 |
| `p2p-core/patches/re/0006-re-thrd-current-esp-idf.patch` | ESP-IDF 线程标识 |
| `p2p-core/patches/re/0007-re-tmr-monotonic-esp-idf.patch` | ESP-IDF clock id |
| `p2p-core/third_party/port/re/re.cmake` | libre 构建、patch staging、HEADER 顺序 |
| `p2p-core/third_party/port/re/platform/esp-idf.cmake` | ESP-IDF 平台源/宏/兼容选项 |
| `port/esp-idf/src/port_api.c` | 线程创建（内部 RAM） |
| `p2p-core/svc_streaming_p2p/src/tuya_ipc_p2p.c` | P2P 线程创建与媒体发送 |
| `TuyaOpen-Core-esp32/p2p_demo/main/tuya_ipc_demo.c` | 帧队列/首帧时序/诊断日志 |
| `TuyaOpen-Core-esp32/p2p_demo/main/demo_vcam.c` | 内嵌样例 AU 索引与循环 |
| `TuyaOpen-Core-esp32/p2p_demo/main/tuya_main.c` | BLE/MQTT/RTC 事件、堆水位 |

---

## 10. 一句话总结

ESP32 上跑通 P2P，难的从来不是“把代码编译过”，而是三件事同时成立：

1. **内部 RAM 留够**给 WiFi/BLE/TLS 和 AES DMA；
2. **PSRAM 栈只给不碰 flash 的媒体线程**；
3. **libre 的定时器、线程标识、ICE 选路都按 ESP-IDF 的真实行为修正**。

这三层任何一层没对齐，现象都只是“没有画面”；日志里的 assert、`esp-aes`、`clock_gettime`、`selected pair` 才是把问题一层层剥开的关键。
