# TuyaOpen Core

[English](README.md) | 简体中文

TuyaOpen Core 是一个 C SDK 库，可在保留现有 OS 和厂商 SDK 的基础上，通过轻量平台适配层为设备工程接入涂鸦云能力。[完整的 TuyaOpen 框架](https://github.com/tuya/TuyaOpen)则为新产品提供更全面、可直接构建的开发环境。

## 能力

| API | 能力简介 |
| --- | --- |
| [IoT](https://tuyaopen.ai/docs/tuyaopen-core/iot-api) | 设备激活、认证、数据点、云端控制和 OTA。 |
| [AI Agent](https://tuyaopen.ai/docs/tuyaopen-core/agent-api) | 云端语音和文本对话，可选 MCP 能力。 |
| [P2P](https://tuyaopen.ai/docs/tuyaopen-core/p2p-api) | 点对点音视频，可选本地存储。 |

## 架构

应用按需调用各项能力。Core 负责提供涂鸦云连接能力，并通过平台适配层使用现有系统服务。

~~~mermaid
flowchart TB
    App[设备应用] --> Core[TuyaOpen Core]
    Core --> IoT[IoT]
    Core --> AI[AI Agent]
    Core --> P2P[P2P]
    IoT --> Cloud[涂鸦云]
    AI --> Cloud
    P2P --> Cloud
    Core --> Port[平台适配层]
    Port --> OS[现有 OS / 厂商 SDK]
~~~

## 平台适配现状

| 平台 | 本仓库现状 |
| --- | --- |
| Linux / POSIX | 已有端口实现：[port/linux](port/linux)。 |
| ESP-IDF | 已有端口实现：[port/esp-idf](port/esp-idf)。 |
| TuyaOpen / TuyaOS | 提供共享契约和操作适配器：[port/tuyaopen](port/tuyaopen)。 |
| 其他平台 | 可通过实现平台契约新增端口。 |

## 快速开始

克隆仓库并初始化子模块：

~~~sh
git clone https://github.com/tuya/tuyaopen-core.git tuyaopen-core
cd tuyaopen-core
git submodule update --init --recursive
~~~

然后根据平台或集成方式阅读指南：

- [Linux 快速入门](https://tuyaopen.ai/docs/tuyaopen-core/linux-quick-start)
- [ESP-IDF 快速入门](https://tuyaopen.ai/docs/tuyaopen-core/esp-idf-quick-start)
- [集成到现有工程](https://tuyaopen.ai/docs/tuyaopen-core/add-to-project)
- [移植到其他平台](https://tuyaopen.ai/docs/tuyaopen-core/port-to-platform)

查看 Linux 示例及其使用文档：

[示例总览](examples/linux/README_zh.md) · [IoT](examples/linux/iot_demo/README_zh.md) · [AI Agent](examples/linux/agent_demo/README_zh.md) · [P2P](examples/linux/p2p_demo/README_zh.md)

## 仓库目录

~~~text
tuyaopen-core/
├── agent-core/          # AI Agent 对话服务与 MCP 支持。
├── iot-core/            # 云连接、设备管理和 IoT 服务。
├── p2p-core/            # P2P 音视频和媒体服务。
├── examples/            # 示例应用。
│   └── linux/           # Linux 示例工程。
│       ├── iot_demo/    # IoT 设备上云。
│       ├── agent_demo/  # AI 对话。
│       ├── p2p_demo/    # P2P 音视频。
│       └── components/  # 示例共享组件。
├── port/                # 平台适配与操作契约。
│   ├── linux/           # Linux/POSIX 平台适配。
│   ├── esp-idf/         # ESP-IDF/FreeRTOS 平台适配。
│   └── tuyaopen/        # TuyaOpen 操作适配与共享契约。
├── third_party/         # 外部库子模块。
├── cmake/               # CMake 构建选项。
├── CMakeLists.txt       # ESP-IDF 组件入口。
├── tuyaopen-core.cmake  # 普通 CMake 集成入口。
├── Kconfig              # 统一功能配置入口。
└── Kconfig.projbuild    # ESP-IDF 菜单配置入口。
~~~

## 文档与社区

查看 [TuyaOpen Core 文档](https://tuyaopen.ai/docs/tuyaopen-core/overview)，在 [TuyaOpen 论坛](https://forum-tuyaopen.discourse.group/)提问，或加入 [TuyaOpen Discord](https://discord.com/invite/yPPShSTttG)。

欢迎通过 [GitHub 仓库](https://github.com/tuya/tuyaopen-core)反馈问题和参与贡献。
