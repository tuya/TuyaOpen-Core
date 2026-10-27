# TuyaOpen Core

English | [简体中文](README_zh.md)

TuyaOpen Core is a C SDK library for adding Tuya Cloud capabilities to an existing device project while keeping its OS and vendor SDK, connected through a thin platform adapter. The [full TuyaOpen framework](https://github.com/tuya/TuyaOpen) provides a broader ready-to-build environment for new products.

## Capabilities

| API | What it provides |
| --- | --- |
| [IoT](https://tuyaopen.ai/docs/tuyaopen-core/iot-api) | Device activation, authentication, data points, cloud control and OTA. |
| [AI Agent](https://tuyaopen.ai/docs/tuyaopen-core/agent-api) | Cloud voice and text conversations, with optional MCP capabilities. |
| [P2P](https://tuyaopen.ai/docs/tuyaopen-core/p2p-api) | Peer-to-peer audio and video, with optional local storage. |

## Architecture

The application uses the capabilities it needs. Tuya Cloud connectivity and platform services are provided through the core and a platform adapter.

~~~mermaid
flowchart TB
    App[Device application] --> Core[TuyaOpen Core]
    Core --> IoT[IoT]
    Core --> AI[AI Agent]
    Core --> P2P[P2P]
    IoT --> Cloud[Tuya Cloud]
    AI --> Cloud
    P2P --> Cloud
    Core --> Port[Platform adapter]
    Port --> OS[Existing OS / vendor SDK]
~~~

## Platform status

| Platform | Status in this repository |
| --- | --- |
| Linux / POSIX | Port implementation in [port/linux](port/linux). |
| ESP-IDF | Port implementation in [port/esp-idf](port/esp-idf). |
| TuyaOpen / TuyaOS | Shared contracts and an operations adapter in [port/tuyaopen](port/tuyaopen). |
| Other platforms | Add a port by implementing the platform contracts. |

## Resource usage summary

Measured on ESP32-S3 with `iot_demo` (BLE provisioning + MQTT + DP; P2P/AI
disabled) and with per-allocation accounting on Linux:

| Metric | Value | Basis |
|---|---:|---|
| Flash: core+port (excluding mbedtls/cJSON) | 140,272 B (≈137 KiB) | ESP32 ELF/map |
| Flash: core+port + platform mbedtls/cJSON | 265,026 B (≈259 KiB) | ESP32 upper bound |
| Static RAM: core+port (`.data+.bss`) | 2,043 B | ESP32 |
| Dynamic: core itself (steady / peak) | 21,582 / 49,577 B (≈21 / 48 KiB) | Linux per-allocation, Linux-specific KV/littlefs excluded |
| Dynamic: ESP32 total heap (steady / peak) | 237,448 / 250,748 B (≈232 / 245 KiB) | includes WiFi/BLE/platform mbedtls |

- ESP32 total heap breakdown: WiFi driver ≈49 KB, BLE stack ≈72 KB, core cloud path ≈49 KB, thread stacks 27.6 KB.
- Flash / static RAM figures come from the ESP32 build; Linux (x86-64) is only used for module attribution and its absolute values are not comparable.
- Dynamic usage depends on the scenario (OTA, reconnect, P2P/AI will be higher).

## Get started

Clone the repository and initialize its submodules:

~~~sh
git clone https://github.com/tuya/tuyaopen-core.git tuyaopen-core
cd tuyaopen-core
git submodule update --init --recursive
~~~

Then follow the guide for your platform or integration:

- [Quick start on Linux](https://tuyaopen.ai/docs/tuyaopen-core/linux-quick-start)
- [Quick start on ESP-IDF](https://tuyaopen.ai/docs/tuyaopen-core/esp-idf-quick-start)
- [Add Core to an existing project](https://tuyaopen.ai/docs/tuyaopen-core/add-to-project)
- [Port to another platform](https://tuyaopen.ai/docs/tuyaopen-core/port-to-platform)

Explore the Linux examples and their documentation:

[Overview](examples/linux/README.md) · [IoT](examples/linux/iot_demo/README.md) · [AI Agent](examples/linux/agent_demo/README.md) · [P2P](examples/linux/p2p_demo/README.md)

## Repository

~~~text
tuyaopen-core/
├── agent-core/          # AI Agent conversation services and MCP support.
├── iot-core/            # Cloud connectivity, device management and IoT services.
├── p2p-core/            # P2P audio/video and media services.
├── examples/            # Example applications.
│   └── linux/           # Linux example projects.
│       ├── iot_demo/    # IoT device connectivity to Tuya Cloud.
│       ├── agent_demo/  # AI conversations.
│       ├── p2p_demo/    # P2P audio and video.
│       └── components/  # Shared example components.
├── port/                # Platform adapters and operation contracts.
│   ├── linux/           # Linux/POSIX platform adapter.
│   ├── esp-idf/         # ESP-IDF/FreeRTOS platform adapter.
│   └── tuyaopen/        # TuyaOpen operations adapter and shared contracts.
├── third_party/         # External library submodules.
├── cmake/               # CMake build options.
├── CMakeLists.txt       # ESP-IDF component entry point.
├── tuyaopen-core.cmake  # Plain CMake integration entry point.
├── Kconfig              # Unified feature configuration entry point.
└── Kconfig.projbuild    # ESP-IDF menu configuration entry point.
~~~

## Documentation and community

Browse the [TuyaOpen Core documentation](https://tuyaopen.ai/docs/tuyaopen-core/overview), ask questions on the [TuyaOpen forum](https://forum-tuyaopen.discourse.group/), or join the [TuyaOpen Discord](https://discord.com/invite/yPPShSTttG).

Report issues and contribute through the [GitHub repository](https://github.com/tuya/tuyaopen-core).
