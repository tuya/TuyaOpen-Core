# esp32 port

This directory is the port adaptation for the ESP32 target, based on
**esp-idf / FreeRTOS**. It mirrors `port/linux` (**ops-card model** for the
OS/storage/link cards: core's `core_*.c` reference the `core_*_ops_platform`
cards via extern and complete dispatch, while the port assembles those cards).
Network is the exception: `core_network.c` calls the socket API directly and
this port only supplies the platform header it includes.

## Current status

| File | Content | Core contract |
|---|---|---|
| `src/port_api.c` | Main card: log/memory/system/semaphore/mutex/thread/queue (FreeRTOS/esp-idf primitives) | `core_api_ops.h` |
| `src/port_kv.c` | KV card (NVS; long keys folded into FNV-1a 56-bit hex) | `core_kv_ops.h` |
| `src/port_netif.c` | netif card (event-driven via esp_netif; link up = IP obtained) | `core_netif_ops.h` |
| `src/port_bluetooth.c` | BLE implementation on the esp-idf bundled NimBLE host + the CORE BLE card adapter (real binding with BLE on, empty card when off) | `core_bluetooth_ops.h` |
| `interface/core_network_platform.h` | Platform socket header included directly by `core_network.c` (lwIP headers + fd_set bridge; no ops card) | included by core |

> BLE (`src/port_bluetooth.c`, gated by `CONFIG_ENABLE_BLUETOOTH`): it
> implements the `port_ble_*` GAP/GATT entry points on top of the **esp-idf
> bundled NimBLE host** (`CONFIG_BT_NIMBLE_ENABLED`, controller + host in one
> image -- no external HCI transport and no vendored host stack).
> The same file also holds the mirror adapter that binds the 18-member
> `CORE_BLUETOOTH_OPS_T` card (layout asserts guard the mirror types);
> `include/bluetooth/port_bluetooth{,_def}.h` carry the port BLE interface.
> The card is **peripheral (slave) only**: the GATT client / central members
> (`gap_connect` and the six `gattc_*` entry points) were removed from the
> contract together with their implementations, so every remaining member is
> bound.
>
> The port CMakeLists is the single source of truth for BLE source selection:
> `src/port_bluetooth.c` is always compiled and its own `ENABLE_BLUETOOTH` guard
> selects the NimBLE implementation + real card, or the empty card (every member
> NULL, core degrades to `OPRT_NOT_SUPPORTED`). With `CONFIG_ENABLE_BLUETOOTH`
> on but `CONFIG_BT_NIMBLE_ENABLED` off the configure fails with an explicit
> error (the port is built against NimBLE only).

> OTA is not part of the port: core uses callback injection
> (`core_ota_register_cbs`, see `core_ota.h`), and the esp32 implementation is
> provided by the application layer (`examples/iot_demo/main/ota_app.c`). An
> application that does not register the callbacks does not support OTA
> (`core_ota_*` returns `OPRT_NOT_SUPPORTED`).

`include/` is the platform interface domain header root and contains:

- `common/`: 4 cross-target common type headers + `port_log.h` (this layer's
  `PORT_LOG_*` logging, which does not use core's `PR_*` macros);
- `system/`: `port_memory.h` (the port-internal `port_system_malloc` /
  `port_system_free`, i.e. the implementation body of the card's
  `.malloc/.free`; used across files by port_kv.c, while core goes through card
  dispatch only).

Layering: `interface/` is the contract layer (mirrors the linux side), and card
contracts are resolved from this layer; the port has **zero include / zero
symbol** dependency on core (core->port still goes through cards; the direction
is unchanged).

Not yet adapted (add when needed, with the same gating as the linux port):

- `port_fs` (used by p2p local_store; POSIX wrapper on linux);
- `media/port_ai_demo.h` (the audio input contract of agent_demo).

## Build integration

esp-idf derives a component's name from its directory name, so this directory
is the `esp-idf` component; the application adds `tuyaopen-core/` and
`tuyaopen-core/port/esp-idf` with `EXTRA_COMPONENT_DIRS` and depends on
`tuyaopen-core` and `esp-idf`.
Keeping the port a real esp-idf component is a hard requirement: `REQUIRES`
only resolves registered components, and esp-idf replays component
requirements in `script()` mode where `add_subdirectory` is not scriptable.
Registration also fixes the link order: this component REQUIRES
`tuyaopen-core` and `bt`, so esp-idf processes (and links) them in dependency
order -- core libraries, then the port archive, then bt/NimBLE and its
controller.

This `CMakeLists.txt`:

- registers with esp-idf (`REQUIRES tuyaopen-core esp_wifi esp_event esp_netif
  nvs_flash lwip esp_timer app_update bt`), ensuring core is processed first
  and the idf components are available and ordered after the port;
- builds `src/` as the plain CMake static library `tuyaopen-port` (plain
  targets do not automatically get idf component headers, so it injects them
  with `target_link_libraries(... idf::xxx)`);
- collects sources/headers (include/ + src/ + interface/), injects switch
  macros from `CONFIG_ENABLE_*` (core's macros do not propagate to the port
  target), and injects the port headers into `tuyaopen-headers` (core
  translation units get board macros through `tuya_kconfig.h` and resolve
  contract headers through `interface/`);
- bridges core's mbedtls integration under esp-idf (adds the esp-idf
  `esp_config.h` include directory to `tuyaopen-headers`). No GNU ld link group
  is used; core's references to the card instances are resolved by the
  component link order above.

**Deliberately does not link `core_system` and does not add core's header
paths**: any include of core fails at compile time (a compile-time
self-proving layering gate).
