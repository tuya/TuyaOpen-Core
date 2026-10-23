# iot_demo

[English](README.md) | 中文

## 概述

一个简单、跨平台、跨系统、多连接的开关类示例。配合涂鸦 App 与涂鸦云服务，
可以对该开关进行配网绑定、激活、升级、远程控制（不在同一局域网时）、
局域网控制（同一局域网内）以及蓝牙控制（无网络时）。

![](https://images.tuyacn.com/fe-static/docs/img/0e155d73-1042-4d9f-8886-024d89ad16b2.png)

## 目录结构

```sh
+- iot_demo
    +- libqrencode
    +- src
        -- cli_cmd.c
        -- qrencode_print.c
        -- tuya_main.c
        -- tuya_config.h
    -- CMakeLists.txt
    -- README.md
```

* libqrencode：用于二维码显示的开源库
* qrencode_print.c：在屏幕或串口工具上打印二维码
* cli_cmd.c：用于操作 switch_demo 的命令行指令
* tuya_main.c：iot_demo 的主函数
* tuya_config.h：涂鸦 PID 与 License；获取 License 需要在涂鸦 AI+IoT 平台
  创建产品，参考 [TuyaOS 快速入门](https://developer.tuya.com/en/docs/iot-device-dev/application-creation?id=Kbxw7ket3aujc)

## 支持的硬件

当前工程可以在所有已支持的芯片和开发板上运行。

## 说明

- **连续快速重启三次会清除配网信息**：`reset_netcfg` 计数器在 5 秒内未清零时
  会持续累加，达到 3 次即触发 `tuya_iot_reset`（恢复出厂设置，随后由
  `core_system_reset` 复位）。调试时连续快速重启 3 次会丢失激活数据，需要
  重新扫码绑定。
