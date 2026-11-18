# P2P 资源占用（T5AI / libre）

本文汇总 P2P（libre ICE 后端 + KCP + IPC ring buffer）在
`TUYA_T5AI_BOARD` 上的内存、线程、队列和实测水位，作为容量规划和
MCU 移植的基线。

日期：2026-11-03
固件基线：`TuyaOpen-Core 92d51e3` 之后（含栈收缩和候选队列逐条 drain），
父工程 `tuyaopen-submodule` 对应 gitlink。
数据来源：target ELF 的 DWARF `sizeof`、`tkl_thread_get_watermark` 真机日志、
`app_memory.txt`、启动/预览日志。

## 1. 总览

| 项目 | T5AI 数据 | 说明 |
|---|---:|---|
| SRAM RAM 区 | 334 KB | `app_memory.txt` |
| SRAM 静态占用 | 约 109 KB | 代码/数据/BSS |
| 自定义堆 | 160 KB | `CONFIG_CUSTOMIZE_HEAP_SIZE` |
| PSRAM heap | 8.8 MB | `PSRAM_HEAP` |
| PSRAM stack heap | 1 MB | `PSRAM_STACK_HEAP`，所有 TuyaOpen 线程栈 |
| PSRAM encode slab | 1.4 MB | 编码器 |
| PSRAM display slab | 4.5 MB | 当前 `LCD_NONE` 仍预留（平台层待确认） |
| QIO 固件 | 2.5 MB | `ipc_p2p_demo_QIO_1.0.0.bin` |
| AP 分区 | 约 1.29 MB / 3.5 MB | 构建日志 |
| OTA 分区 | 约 1.31 MB / 2.94 MB | 构建日志 |

预览时 TuyaOpen 侧空闲：SRAM 约 **68–73 KB**，PSRAM 约 **5.4 MB**
（`82ed87a` 之后无 `tkl_system_malloc failed`）。

## 2. P2P 内存对象

### 2.1 一次性/全局缓冲

| 对象 | 大小 | 分配器/位置 | 生命周期 |
|---|---:|---|---|
| 视频媒体帧 `media_frame` | 300 KiB | PSRAM（`core_p2p_malloc`） | P2P init 到进程结束 |
| IPC ring buffer slots | 8 × 192 KiB = 1.5 MiB | PSRAM（`core_p2p_malloc`） | 首次 ring init 到进程结束 |
| P2P session 结构 | 4,696 B | `core_malloc`（SRAM） | P2P init 到进程结束 |
| 视频/音频 RTP 打包缓冲 | 2 × 1,228 B | `Malloc`（SRAM） | 每次会话建立 |
| 音频媒体帧 | 1,280 B | `Malloc`（SRAM） | P2P init 到进程结束 |

### 2.2 每会话对象

| 对象 | 大小 | 分配器/位置 | 说明 |
|---|---:|---|---|
| `tuya_p2p_rtc_session_t` | 14,824 B | newlib `malloc`（SRAM） | cfg 7,280 B（3×2048 token）+ 2×SDP 1,624 B + cmd recv_buf 4,096 B |
| `pj_ice_session_t` | 11,440 B | newlib `calloc`（SRAM） | pending candidates 4,096 B 等 |
| `rtc_channel_t` × 4 | 136 B × 4 = 544 B | newlib `malloc`（SRAM） | 3 个媒体通道 + 1 个 RTC CMD |
| `ikcpcb` × 4 | 224 B × 4 = 896 B | KCP | 每通道一个 |
| KCP mbuf 描述符 | 36 B/个 | `core_calloc`（SRAM） | 随发送队列按需分配 |
| KCP mbuf payload | 1,600 B/个 | PSRAM（`core_p2p_malloc`） | 按需分配，计入队列预算 |

单会话静态结构约 **27 KB SRAM**；mbuf 描述符在视频队列满时最多
512 KiB / 1,600 × 36 ≈ **11.5 KB**，所有队列最坏约 20–30 KB。
当前 app 只开 1 个会话（`stream_var.max_client_num = 1`），双会话约 55 KB。

### 2.3 App/平台侧（不计入 P2P core，但占同一预算）

- 摄像头帧：`ENABLE_EXT_RAM=1`，走 PSRAM（`tal_psram_malloc`）。
- demo 帧队列：`DEMO_FRAME_BUF_SIZE` 256 KiB × 2 = 512 KiB PSRAM。
- 编码器/显示 slab：见第 1 节。

## 3. KCP 通道与队列预算

`tuya_p2p_rtc_init()` 配置的发送/接收预算（`send_buf_size`/`recv_buf_size`）：

| 通道 | send 预算 | recv 预算 | 备注 |
|---|---:|---:|---|
| CMD（0） | 4 KiB | 4 KiB | 信令/控制 |
| VDATA（1） | 512 KiB | 1 KiB | 1,024 kbps × 4 s，夹在 500–800 KiB |
| ADATA（2） | 2×128 KiB+1,350 ≈ 263 KiB | 64 KiB | 音频关闭时基本不用 |
| RTC CMD（3） | 100 KiB | 100 KiB | `cfg.channel_number=3` 的额外通道 |
| TRANS（3/5） | 128 KiB+1,350 | 1 KiB / 1 MiB | SDK 配置了，但当前 `channel_number=3` 未实际分配 |

预算只是上限，mbuf 按实际发送按需分配；视频队列满时最多约 320 个
1,600 B mbuf（payload 在 PSRAM，描述符在 SRAM）。

## 4. 线程

T5AI 上所有 TuyaOpen 线程栈都在 `PSRAM_STACK_HEAP`
（`CONFIG_FREERTOS_SMP=y` → `xTaskCreateInPsram`）。

| 线程 | T5AI 新栈 | 实测峰值 | 优先级 | 用途 |
|---|---:|---:|---:|---|
| `tuya_p2p_listen_task` | 4 KiB | 1,244 B | 2 | 接受 P2P 会话 |
| `p2p_cmd_recv` | 4 KiB | 1,720 B | 2 | 命令/控制通道 |
| `p2p_media_send` | 4 KiB | 1,280 B | 2 | 视频/音频发送 |
| `ctx_sig` | 10 KiB | 5,648 B | 3 | MQTT/LAN 信令、SDP/cJSON |
| `rtc_wrk` | 10 KiB | 3,524 B（候选逐条 drain 后） | 3 | libre ICE/KCP 事件循环 |

实测为最终 4/4/4/10/10 配置下、T5AI 与 Linux demo 同时预览时的水位。
`rtc_wrk` 在候选 drain 前峰值为 6,280 B。

P2P 线程栈合计从原来的约 **360 KB** 降到 **32 KB**。栈大小由 Kconfig 配置，
默认值如下（可在 app config/menuconfig 覆盖，Linux 走 `#ifndef` 回退）：

| Kconfig | 默认值 |
|---|---:|
| `P2P_LISTEN_STACK_SIZE` | 4 KiB |
| `P2P_CMD_RECV_STACK_SIZE` | 4 KiB |
| `P2P_MEDIA_SEND_STACK_SIZE` | 4 KiB |
| `P2P_CTX_SIG_STACK_SIZE` | 10 KiB |
| `P2P_RTC_WORKER_STACK_SIZE` | 10 KiB |

其他相关线程（非 P2P core）：`encoded_flow_task` 8 KiB、Linux demo 的
`tuya_ipc_demo_thread` 20 KiB、平台 Wi-Fi/MQTT/TLS 线程。

## 5. 真机实测

- `ICE negotiation success`、`auth success`、`LIVE video start` 正常；
  480×480@20 H.264，实测上行约 800 kbps，rate control 在 1024 →
  650–870 kbps 自适应。
- 栈水位（`tkl_thread_get_watermark`，每 10 s 打印，最终 4/4/4/10/10 配置）：
  - `p2p_listen` 1,244 / 4,096 B
  - `p2p_cmd_recv` 1,720 / 4,096 B
  - `p2p_media_send` 1,280 / 4,096 B
  - `ctx_sig` 5,648 / 10,240 B
  - `rtc_wrk` 3,524 / 10,240 B（候选 drain 后；drain 前 6,280 / 24,576 B）
- `82ed87a` 之后预览期间无 `tkl_system_malloc failed`。
- Linux 小栈完整链路：`p2p_demo` 以默认 4/4/4/10/10 KB 构建，App 扫码绑定后
  `ctx_sig`/`rtc_wrk` 10 KB、其余 4 KB 线程全部创建，ICE 成功、
  `LIVE video start` 正常；vcam 文件结束后会话正常销毁，无栈溢出/崩溃。
  Linux demo 与 T5AI 使用不同 UUID，可同时在线预览。

## 6. 容量建议

- 当前 1 会话：P2P 一次性 PSRAM 约 1.8 MB（媒体帧 300 KB + ring 1.5 MB），
  mbuf payload 最高 512 KB，线程栈 32 KB；SRAM 结构约 27 KB + 描述符。
- 每增加 1 个会话：SRAM 增加约 27 KB（结构）+ 最多 20–30 KB（mbuf 描述符），
  PSRAM 增加 mbuf payload（按队列预算）。
- 若后续把 `max_client_num` 提到 2，建议同步评估 SRAM 余额和 ring buffer
  是否要按会话拆分；当前 ring 是全局 8 slot。
- `PSRAM_SLAB_DISPLAY` 4.5 MB 在 `LCD_NONE` 下仍是预留，平台层可确认是否
  可以缩小或复用。

## 7. 优化记录与约束

| 提交 | 内容 |
|---|---|
| `da9c1e4` | lwIP `O_NONBLOCK` 位 |
| `44df086` | getaddrinfo 端口 0 |
| `d036943` | 媒体帧 PSRAM |
| `31f49cb` | ring slot PSRAM |
| `82ed87a` | KCP mbuf payload PSRAM |
| `92fbdcd` | PSRAM 开关与 ICE 后端解耦、栈水位打点 |
| `17d2e0e` | 第一轮栈收缩 16/12/12/16/16 KB |
| `92d51e3` | `apply_pending_remote()` 逐条 drain |
| 当前 | 最终栈 4/4/4/10/10 KB；T5AI 与 Linux 均已真机验证 |

约束：**core 后续不再新增 PSRAM 专用代码**。已有的媒体帧/ring slot/
mbuf payload PSRAM 改动暂时保留；会话/ICE 结构迁移 PSRAM 取消，后续由
平台层统一处理。

## 8. 测量方法

- `tkl_thread_get_watermark()`：T5AI 上每 10 s 打印每个 P2P 线程的
  requested/free/used，取峰值。
- `arm-none-eabi-gdb` + DWARF：`p sizeof(...)` 获取 target 结构尺寸。
- `app_memory.txt`：SRAM/PSRAM 区域和静态占用。
- 预览日志：`Device Free heap SRAM/PSRAM`、`tkl_system_malloc failed`、
  `ICE negotiation success`、`LIVE video start`。
