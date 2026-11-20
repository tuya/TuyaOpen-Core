# T5AI P2P 内存与栈分析

本文记录 `TUYA_T5AI_BOARD` 上 `ipc_p2p_demo`（libre ICE 后端）的内存/栈基线、
已经确认的大对象，以及后续按顺序在真机上验证的优化项。

日期：2026-11-03
固件基线：`TuyaOpen-Core 82ed87a`（mbuf payload 已改 PSRAM），
父工程 `tuyaopen-submodule 5a8b1f10`。

## 1. 结论

- 没有发现运行期“持续长大”的栈；P2P 线程栈是固定申请，但数值明显偏大，
  合计约 **360 KB**，并且全部落在 PSRAM（`CONFIG_FREERTOS_SMP=y`，
  `tkl_thread_create()` 走 `xTaskCreateInPsram()`）。
- 栈上最大的临时数组是 `apply_pending_remote()` 里的
  `char cands[16][256]`（4 KB），加 `rufrag/rpwd` 约 4.6 KB，跑在
  24 KB 的 `rtc_wrk` 上；不危险，但可以优化。
- SRAM 的大头不是栈，而是每个会话的堆结构：
  `tuya_p2p_rtc_session_t` 14,824 B + `pj_ice_session_t` 11,440 B +
  mbuf 描述符。mbuf payload 已在 `82ed87a` 移到 PSRAM，修复后日志里
  不再出现 `tkl_system_malloc failed`。
- PSRAM 大块：媒体帧 300 KB、ring 1.5 MB、KCP payload 最高 512 KB、
  摄像头帧、线程栈约 360 KB；预览时 PSRAM 仍剩约 5.4 MB。

## 2. 线程栈

`CONFIG_FREERTOS_SMP=y` 时，T5AI 的 `tkl_thread_create()` 使用
`xTaskCreateInPsram()`，所以这些栈来自 `PSRAM_STACK_HEAP`（1 MB 区域），
不占 SRAM。

| 线程 | 申请栈 | 位置 | 说明 |
|---|---:|---|---|
| `tuya_p2p_listen_task` | 128 KB | `p2p-core/svc_streaming_p2p/src/tuya_ipc_p2p.c:256` | 循环只做 listen / deal，偏大 |
| `p2p_media_send` | 96 KB | `tuya_ipc_p2p.c:93`、`:2703` | 帧数据在堆/PSRAM，栈不需要这么大 |
| `p2p_cmd_recv` | 64 KB | `tuya_ipc_p2p.c:96`、`:2695` | 命令解析 |
| `ctx_sig` | 48 KB | `p2p-core/base_ice/src/tuya_media_service_rtc.c:122`、`:961` | 信令/SDP/cJSON |
| `rtc_wrk` | 24 KB | `tuya_media_service_rtc.c:750` | libre ICE/KCP 事件循环 |
| libre pthread shim | 8 KB/线程 | `p2p-core/third_party/port/re/src/re_pthread.c:33` | `RE_TUYAOPEN_THREAD_STACK` |
| `encoded_flow_task` | 8 KB | app `tuya_ipc_demo.c` | 编码帧回调 |

`STACK_SIZE_P2P_MEDIA_RECV` / `CMD_SEND` / `DETECT` 三个 64 KB 常量当前
没有创建对应线程，是死常量。

## 3. 栈上临时大数组

| 位置 | 大小 | 说明 |
|---|---:|---|
| `p2p-core/third_party/port/re/src/libre_ice.c:989` | 4,096 B | `cands[ICE_MAX_PENDING_CANDS][256]`，`rtc_wrk` 上约 4.6 KB（含 rufrag/rpwd） |
| `p2p-core/base_ice/src/tuya_misc.c:418` | 1,024 B | `md[1024]` + `mbedtls_x509_crt`，证书指纹函数（DTLS 路径，当前未用） |
| `libre_ice.c` `tmp[512]`、`line[256]` | ≤512 B | 局部字符串 |
| `tuya_sdp.c` | ≤256 B | SDP 解析局部数组 |

`RTP_PAYLOAD_MAX_SIZE`（10 MB）是上限，不是预分配；RTP payload 按实际包
`realloc`。App/camera 未发现大栈数组，帧缓冲走 PSRAM
（`ENABLE_EXT_RAM=1`）。

## 4. 堆上的大对象

target ELF 通过 gdb `sizeof` 实测：

| 对象 | 大小 | 说明 |
|---|---:|---|
| `tuya_p2p_rtc_session_t` | 14,824 B/会话 | cfg 7,280 B（3×2048 token）+ local/remote SDP 各 1,624 B + cmd recv_buf 4,096 B |
| `pj_ice_session_t` | 11,440 B/会话 | pending candidates 4,096 B 等 |
| `P2P_SESSION_T` | 4,696 B | `read_buff[4096]` |
| `rtc_channel_t` | 136 B × 7 | 约 1 KB |
| `ikcpcb` | 224 B × 7 | 约 1.6 KB |
| `tuya_mbuf_t` 描述符 | 36 B/个 | 视频队列 512 KB 预算最多 320 个，约 11.5 KB；所有队列最坏 20–30 KB |

单会话约 27 KB SRAM 堆，两个会话约 55 KB。`ctx_session_create()` /
`pj_ice_session_create()` 使用 newlib `malloc/calloc`，mbuf 使用
`core_malloc`（Beken 堆），存在两套分配器混用的问题。

其他静态/堆对象：

- `p2p-core/base_ice/src/tuya_misc.c:91`：`static char print_buf[8092]`，
  8 KB BSS。
- `rtc_dtls_t` 内的 `cert[8K] + pkey[8K] + fingerprint[1K]` 只是类型定义，
  当前未实例化（DTLS 未用），不计入实际占用。

## 5. PSRAM 大缓冲

| 缓冲 | 大小 | 说明 |
|---|---:|---|
| 媒体帧 `media_frame` | 300 KB | `d036943`，`core_p2p_malloc`（PSRAM） |
| ring buffer slots | 8 × 192 KB = 1.5 MB | `31f49cb` |
| KCP mbuf payload | 视频队列最高 512 KB | `82ed87a` |
| 摄像头帧 | 由 `ENABLE_EXT_RAM=1` 走 PSRAM | platform camera |
| 线程栈 | 约 360 KB | `PSRAM_STACK_HEAP`（1 MB） |

内存映射（`app_memory.txt`）：SRAM RAM 区 334 KB，静态占用约 109 KB；
`CONFIG_CUSTOMIZE_HEAP_SIZE=163840`；PSRAM 侧有 1 MB stack heap、
8.8 MB heap、1.4 MB encode slab、4.5 MB display slab。

## 6. 真机水位

`82ed87a` 之后从 16:04 开始没有再出现 `tkl_system_malloc failed`；预览时
TuyaOpen 侧 SRAM 约 68–73 KB free，PSRAM 约 5.4 MB free。

### 6.1 栈 high-water mark（2026-11-03 真机）

在 `92fbdcd` 上打开 App 实时预览约 1 分钟，`tkl_thread_get_watermark`
每 10 秒打印一次，取每线程峰值：

| 线程 | 申请栈 | 实测峰值 used | 占比 |
|---|---:|---:|---:|
| `tuya_p2p_listen` | 128 KB | 1,244 B | 0.9% |
| `p2p_cmd_recv` | 64 KB | 1,348 B | 2.1% |
| `p2p_media_send` | 96 KB | 1,164 B | 1.2% |
| `ctx_sig` | 48 KB | 5,648 B | 11.5% |
| `rtc_wrk` | 24 KB | 6,280 B | 25.6% |

按峰值预留约 3–10 倍余量，T5AI 的最终栈大小：

| 线程 | 新栈 |
|---|---:|
| `tuya_p2p_listen` | 4 KB |
| `p2p_cmd_recv` | 4 KB |
| `p2p_media_send` | 4 KB |
| `ctx_sig` | 10 KB |
| `rtc_wrk` | 10 KB |

合计从约 360 KB 降到 **32 KB**（比第一轮 72 KB 再减 40 KB）。栈大小已做成
Kconfig 参数（`P2P_LISTEN_STACK_SIZE`、`P2P_CMD_RECV_STACK_SIZE`、
`P2P_MEDIA_SEND_STACK_SIZE`、`P2P_CTX_SIG_STACK_SIZE`、
`P2P_RTC_WORKER_STACK_SIZE`，默认 4/4/4/10/10 KB），Linux 和其他平台走
`#ifndef` 回退，也可以在 app config/menuconfig 里覆盖。

新栈最终真机复测（4/4/4/10/10，T5AI 与 Linux demo 同时在线预览）：
`p2p_cmd_recv` 峰值 1,720 / 4,096 B（free 2,376），`p2p_listen`
1,244 / 4,096 B（free 2,852），`p2p_media_send` 1,280 / 4,096 B
（free 2,816），`ctx_sig` 5,648 / 10,240 B（free 4,592），`rtc_wrk`
3,524 / 10,240 B（free 6,716；候选逐条 drain 后峰值从 6,280 降到 3,524）。
`ICE negotiation success`、`LIVE video start` 正常，无 HardFault / malloc 失败。

Linux 冒烟（完整链路）：用默认 Kconfig 小栈构建 `p2p_demo`，App
扫码绑定后 `ctx_sig`/`rtc_wrk` 10 KB、其余 4 KB 线程全部创建，ICE 成功、
`LIVE video start` 正常；vcam 文件结束后会话正常销毁，无栈溢出/崩溃。
Linux demo 与 T5AI 使用不同 UUID，可以同时在线预览。

> 约束：core 后续不再新增 PSRAM 专用代码。本文已有的 media frame /
> ring slot / mbuf payload PSRAM 改动暂时保留，不再扩展；会话/ICE
> 结构迁移 PSRAM 一项取消，后续由平台层统一处理。

## 7. 后续真机验证顺序

| 序号 | 项目 | 方法 | 状态 |
|---:|---|---|---|
| 1 | 栈水位打点 | 在 5 个 P2P 线程里周期打印 `tkl_thread_get_watermark`，预览+重连后取峰值 | 已完成（真机数据见 6.1） |
| 2 | 收紧 P2P 线程栈 | 已做成 Kconfig 参数，默认 4/4/4/10/10 KB | 已完成（T5AI/Linux 真机验证） |
| 3 | 会话/ICE 结构迁移 PSRAM | ~~`tuya_p2p_rtc_session_t` / `pj_ice_session_t` 改 PSRAM~~ | 取消：core 不再写 PSRAM，后续由平台层处理 |
| 4 | `apply_pending_remote()` 4 KB 临时数组 | 已改为逐条 drain 到单个 256 B slot，`rtc_wrk` 栈上占用从 ~4.6 KB 降到 ~0.8 KB | 已改，待真机复测 |
| 5 | 统一大对象分配器 | 明确 newlib / `core_malloc` / `core_p2p_malloc` 的边界，避免双堆碎片 | 暂缓（涉及 core 分配策略） |
| 6 | display slab 预留 | `PSRAM_SLAB_DISPLAY` 4.5 MB 在 `LCD_NONE` 下是否必要（平台层） | 待做 |

每一步都在真实 `TUYA_T5AI_BOARD` 上烧录验证，结果回填到本表。
