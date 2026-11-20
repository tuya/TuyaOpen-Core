# ESP32 SRAM/PSRAM 内存管理机制（ESP-IDF 5.4）

本文说明 ESP-IDF 在 ESP32/ESP32-S3 上如何自动管理内部 SRAM 与外部 PSRAM，
以及哪些事情 IDF **不会**自动做。目的是解释：为什么 P2P/媒体线程的栈默认在内部
RAM、为什么大缓冲区会自动落在 PSRAM、以及本仓库移除 `THREAD_CFG_T.psram_mode`
之后内存行为会回到什么状态。

适用环境：

- ESP-IDF：v5.4.4（本文代码引用均基于该版本，行号可能随版本变化）
- 示例工程：`TuyaOpen-Core-esp32/p2p_demo`（ESP32-S3 N16R8，8 MB Octal PSRAM）
- 本仓库：`TuyaOpen-Core`，`p2p-core` libre ICE 后端

---

## 1. 结论速览

| 内存对象 | IDF 是否自动放 PSRAM | 机制 |
|---|---|---|
| 普通 `malloc()` 分配的大块内存 | **是** | `CONFIG_SPIRAM_USE_MALLOC` + 大小阈值 |
| WiFi/lwIP 缓冲 | **是**（可配置） | `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` |
| `xTaskCreate()` 的 TCB 和任务栈 | **否，永远内部 RAM** | FreeRTOS 的 `pvPortMalloc` 写死 `MALLOC_CAP_INTERNAL` |
| `xTaskCreateStatic()` + PSRAM 栈 | **需要显式指定** | `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM` 只放宽校验 |
| `.bss` / `.noinit` 段 | **否，默认内部** | 需要 `EXT_RAM_BSS_ATTR` 等属性 + 对应 Kconfig |
| BLE 主机内存 | **否，需显式配置** | `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`（示例工程已开） |
| DMA 缓冲 | **否，必须内部** | `MALLOC_CAP_DMA` / `MALLOC_CAP_INTERNAL` |

一句话：**IDF 自动管理的是堆上的普通分配，不是任务栈。**

---

## 2. 堆：`malloc` 如何自动选择 SRAM/PSRAM

### 2.1 PSRAM 接入默认堆

`CONFIG_SPIRAM_USE_MALLOC=y` 会把 PSRAM 注册进 IDF 的默认堆
（`components/esp_psram/Kconfig.spiram.common:51-67`）。
三种模式的区别：

| 模式 | 含义 |
|---|---|
| `SPIRAM_USE_MEMMAP` | PSRAM 只映射进地址空间，由应用自己管理 |
| `SPIRAM_USE_CAPS_ALLOC` | 只有 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` 能用 |
| `SPIRAM_USE_MALLOC` | `malloc()` 可以直接返回 PSRAM 指针（本仓库示例使用的模式） |

### 2.2 自动分配策略

`malloc()` 最终进入 `heap_caps_malloc_default()`（`components/heap/heap_caps.c:107-134`）：

```c
if (size <= malloc_alwaysinternal_limit)          // 小块：先内部 RAM
    r = heap_caps_malloc_base(size, MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
else                                              // 大块：先 PSRAM
    r = heap_caps_malloc_base(size, MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM);

if (r == NULL)                                    // 首选区域失败，放宽 caps 重试
    r = heap_caps_malloc_base(size, MALLOC_CAP_DEFAULT);
```

因此：

- **小于等于阈值**的分配优先内部 SRAM；
- **大于阈值**的分配优先 PSRAM；
- 任何一边满了，都会回退到另一边，不会因为“选错区域”直接失败。

`realloc()` 使用同样的策略（`heap_caps.c:140-167`）。

### 2.3 关键 Kconfig

| 配置 | 默认值 | 本文示例值 | 作用 |
|---|---:|---:|---|
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` | 16384 | **1024** | ≤该值的 `malloc` 优先内部 RAM；必须低于 KCP mbuf slab（1600 B） |
| `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` | 32768 | 32768 | 预留内部池，普通 `malloc` 不能用；DMA/任务栈等强制内部的请求使用 |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` | n | **y** | WiFi/lwIP 缓冲优先 PSRAM，失败再内部 |

定义位置：`components/esp_psram/Kconfig.spiram.common:77-112`。

`SPIRAM_MALLOC_RESERVE_INTERNAL` 的 Kconfig help 特意说明：
**FreeRTOS 栈被强制在内部内存，也会消耗这个预留池**
（`Kconfig.spiram.common:111-112`）。调小它会让高负载时更难创建新线程。

示例工程的实际生效值见
`TuyaOpen-Core-esp32/p2p_demo/sdkconfig.defaults:69-76`，编译产物中为
`build/config/sdkconfig.h:725-729`。

### 2.4 需要确定性时的显式接口

自动策略只保证“优先”，不保证“一定”。需要确定落点时用 caps API：

```c
heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);   // 只从 PSRAM 取
heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // 只从内部 SRAM 取
heap_caps_malloc(size, MALLOC_CAP_DMA);                        // DMA 可见（内部）
heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT); // 优先顺序
```

本仓库曾用 `port/esp-idf/src/port_memory.c` 的 `tal_psram_malloc()`
（“优先 PSRAM、失败回退普通 `malloc`”）承接 ESP32 的 `core_p2p_malloc()`。
该文件已移除：ESP32 改为依赖上面的 `malloc` 自动策略，并把
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` 调到 **1024**，低于 KCP mbuf slab 的
1600 B。`P2P_MEM_USE_PSRAM` 宏也已从 `p2p-core/CMakeLists.txt` 和
`core_memory.h` 中全部删除；T5AI 改由 `tkl_memory.c` 的
`TKL_PSRAM_MALLOC_ALWAYS_INTERNAL=1024` 在 `tkl_system_malloc` 内做同样的
按大小分派。

---

## 3. 任务栈：IDF 不会自动放到 PSRAM

### 3.1 FreeRTOS 的动态分配被写死为内部 RAM

ESP-IDF 的 FreeRTOS 把堆操作映射到 `heap_caps`，但 caps 是固定的：

```c
#define portFREERTOS_HEAP_CAPS  (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

void *pvPortMalloc(size_t xWantedSize)
{
    return heap_caps_malloc(xWantedSize, portFREERTOS_HEAP_CAPS);
}
```

— `components/freertos/heap_idf.c:43-58`

`xTaskCreate()` 的 TCB 和栈都走 `pvPortMalloc()`
（`components/freertos/FreeRTOS-Kernel/tasks.c:952` 等），所以：

- **即使开启了 `CONFIG_SPIRAM_USE_MALLOC`，`xTaskCreate()` 的栈也永远在内部 SRAM；**
- `xPortGetFreeHeapSize()` 统计的也只是内部 RAM 的 FreeRTOS 堆。

### 3.2 允许 PSRAM 栈的开关

`CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM`
（旧名 `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY`，见
`components/freertos/sdkconfig.rename:8`）：

- 定义：`components/freertos/Kconfig:576-592`；
- 依赖 `SPIRAM`，ESP32 目标默认 `n`，其它目标默认 `y`；
- 作用只是**允许把外部内存传给 `xTaskCreateStatic`**，不会移动
  `xTaskCreate` 的栈；
- 关闭时，`xTaskCreateStaticPinnedToCore` 里的
  `configASSERT(portVALID_STACK_MEM(...))` 会拒绝 PSRAM 指针
  （`components/freertos/esp_additions/freertos_tasks_c_additions.h:299`，
  校验实现见 `components/freertos/heap_idf.c:97-108`）。

### 3.3 `xTaskCreateWithCaps` 做了什么

官方封装 `xTaskCreateWithCaps()`（`components/freertos/esp_additions/idf_additions.c:31-77`）：

```c
pxTaskBuffer = pvPortMalloc(sizeof(StaticTask_t));              // TCB：内部 RAM
pxStack      = heap_caps_malloc(usStackDepth, uxMemoryCaps);    // 栈：调用者指定
xHandle      = xTaskCreateStaticPinnedToCore(..., pxStack, pxTaskBuffer, ...);
```

因此：

- TCB 永远在内部 RAM，只有栈可以来自 PSRAM；
- 用 `xTaskCreateWithCaps()` 创建的任务必须用 `vTaskDeleteWithCaps()` 删除
  （`idf_additions.c:84-163`），否则释放路径不匹配。

`CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION` 在 v5.4 中始终为 `y`
（`components/freertos/Kconfig:616-621`），所以该 API 总是可用。

### 3.4 为什么 PSRAM 栈不能碰 flash

ESP-IDF 在执行 flash 操作前会关闭 cache，并断言当前栈指针在内部 DRAM：

```c
static inline bool esp_task_stack_is_sane_cache_disabled(void)
{
    const void *sp = (const void *)esp_cpu_get_sp();
    return esp_ptr_in_dram(sp) /* || RTC fast mem */;
}

void spi_flash_disable_interrupts_caches_and_other_cpu(void)
{
    assert(esp_task_stack_is_sane_cache_disabled());
    ...
}
```

— `components/spi_flash/cache_utils.c:82-91,161-163`

所以 PSRAM 栈的线程不能调用 NVS、OTA、文件系统等任何 flash API。
这一约束 IDF 无法替应用检查或规避，必须由创建线程的一方保证。

---

## 4. 其它内存放置

| 对象 | 机制 | 说明 |
|---|---|---|
| WiFi/lwIP | `SPIRAM_TRY_ALLOCATE_WIFI_LWIP` | 缓冲优先 PSRAM；`SPIRAM_IGNORE_NOTFOUND` 时行为会退化 |
| BLE/NimBLE host | `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y` | 示例工程显式开启；controller 仍在内部 RAM |
| `.bss` | `SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` + `EXT_RAM_BSS_ATTR` | 默认 `n`，不自动 |
| `.noinit` | `SPIRAM_ALLOW_NOINIT_SEG_EXTERNAL_MEMORY` + `EXT_RAM_NOINIT_ATTR` | 默认 `n`，不自动 |
| DMA | `MALLOC_CAP_DMA` | PSRAM 不是 DMA-capable（ESP32-S3 AES DMA 除外，仍需内部池） |

定义位置：`components/esp_psram/Kconfig.spiram.common:117-138`。

---

## 5. P2P 内存放置：移除 `psram_mode` 与 ESP32 `tal_psram_malloc` 后

### 5.1 被移除的机制

`0677496`（2026-11-04）曾引入：

- `THREAD_CFG_T.psram_mode`；
- `core_api_ops.thread_create_in_psram`；
- ESP-IDF 端口用 `xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM)` 创建
  P2P/媒体线程栈，并用 `vTaskDeleteWithCaps()` 释放。

随后又移除了 ESP-IDF 专用的 `tal_psram_malloc()` 与
`port/esp-idf/src/port_memory.c`；`P2P_MEM_USE_PSRAM` 也已从
`p2p-core/CMakeLists.txt` 和 `core_memory.h` 中完全删除，T5AI 改由
`tkl_system_malloc` 的按大小分派处理。

该机制已移除，核心线程创建恢复为与上游一致的
`ops->thread_create` → `xTaskCreate`。

### 5.2 移除后的行为

- **所有线程栈回到内部 SRAM**，这是 IDF 的默认语义，不是 IDF “不管理”了；
- **P2P 大缓冲仍然自动使用 PSRAM**：示例的阈值是 1 KB，
  300 KB 媒体帧、1600 B KCP mbuf slab、256 KB 帧队列都会走
  `heap_caps_malloc_default` 的 PSRAM 分支；
- 注意 1600 B 的 KCP slab 只比阈值大 576 B，不能再把阈值调回 2048 或更高；
- 内部 RAM 的紧张程度重新由以下参数决定：
  - `SPIRAM_MALLOC_ALWAYSINTERNAL`（调小 → 更多小分配去 PSRAM）；
  - `SPIRAM_MALLOC_RESERVE_INTERNAL`（调大 → 给任务栈/DMA 留更多余量）；
  - `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`；
  - WiFi 静态/动态 buffer 数量；
  - `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`。

### 5.3 调参顺序建议

若再次出现 `create p2p_media_send task failed` 或 `esp-aes: Failed to allocate memory`：

1. 看 `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` 与
   `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` 的差值，
   判断是总量不足还是碎片；
2. 优先调 `SPIRAM_MALLOC_ALWAYSINTERNAL` 和 `RESERVE_INTERNAL`；
   `ALWAYSINTERNAL` 不要升到 1600 及以上，否则 KCP mbuf 会回流内部 SRAM；
3. 其次压缩 WiFi/BLE 的常驻内部缓冲；
4. 最后才考虑重新引入“把不碰 flash 的线程栈放 PSRAM”；
   若重新引入，必须同时处理 `vTaskDeleteWithCaps` 和 flash 约束。

### 5.4 关于阈值 1024 的选择

`TUYA_MBUF_HUGE_SIZE` 是 1600 B（`p2p-core/base_ice/include/tuya_mbuf.h:18-20`），
每个 KCP mbuf payload 都按这个大小分配（`tuya_mbuf.c:196`）。去掉显式
PSRAM 分配器之后，只有 `SPIRAM_MALLOC_ALWAYSINTERNAL < 1600` 时这些 payload
才会优先落到 PSRAM。取 **1024** 留出了 576 B 余量，同时让 1280 B 的
`media_audio_frame` 也进入 PSRAM（该缓冲不参与 DMA，可以直接放在外部 RAM）。

代价与约束：

- 所有 >1 KB 的普通 `malloc` 都会优先 PSRAM，影响范围大于 P2P；
- DMA/必须内部的分配必须使用 `heap_caps_*` 或 `MALLOC_CAP_DMA` 显式指定；
- T5AI 已改为在 `tkl_system_malloc` 内做同样的按大小分派，阈值由
  `TKL_PSRAM_MALLOC_ALWAYS_INTERNAL` 控制（默认 1024），不再有 P2P 专用路径。

---

## 6. 代码位置速查

| 主题 | 位置（ESP-IDF v5.4.4） |
|---|---|
| PSRAM 接入堆的模式 | `components/esp_psram/Kconfig.spiram.common:51-67` |
| `malloc` 自动选择策略 | `components/heap/heap_caps.c:107-134` |
| 小块优先内部阈值 | `components/esp_psram/Kconfig.spiram.common:77-86` |
| 内部预留池 | `components/esp_psram/Kconfig.spiram.common:97-112` |
| WiFi/lwIP 优先 PSRAM | `components/esp_psram/Kconfig.spiram.common:89-96` |
| FreeRTOS 堆写死内部 | `components/freertos/heap_idf.c:43-58` |
| PSRAM 栈许可开关 | `components/freertos/Kconfig:576-592` |
| 旧配置名映射 | `components/freertos/sdkconfig.rename:8` |
| 静态任务栈校验 | `components/freertos/esp_additions/freertos_tasks_c_additions.h:299`，`heap_idf.c:97-108` |
| `xTaskCreateWithCaps` | `components/freertos/esp_additions/idf_additions.c:31-77` |
| `vTaskDeleteWithCaps` | `components/freertos/esp_additions/idf_additions.c:84-163` |
| flash 操作的栈断言 | `components/spi_flash/cache_utils.c:82-91,161-163` |
| 本仓库端口 `malloc` | `port/esp-idf/src/port_api.c:114-117` |
| 本仓库 mbuf slab 大小 | `p2p-core/base_ice/include/tuya_mbuf.h:18-20`，`p2p-core/base_ice/src/tuya_mbuf.c:196` |
| 本仓库 P2P 大缓冲放置 | ESP-IDF：`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`；T5AI：`tkl_memory.c` 的 `TKL_PSRAM_MALLOC_ALWAYS_INTERNAL` |
