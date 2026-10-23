# iot_demo

English | [中文](README_zh.md)

## Overview
A simple, cross-platform, cross-system, and multi-connection switch example. By using the Tuya App and Tuya Cloud Services, you can pair, activate, upgrade, remotely control (when away), local area network control (within the same LAN), and Bluetooth control (when no network is available) for this switch.

![](https://images.tuyacn.com/fe-static/docs/img/0e155d73-1042-4d9f-8886-024d89ad16b2.png)



## Directory
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
* libqrencode: a open souce libirary for QRCode display
* qrencode_print.c: print the QRCode in screen or serial tools
* cli_cmd.c: cli cmmand which used to operater the swith_demo
* tuya_main.c: the main function of the iot_demo
* tuya_config.h: the tuya PID and license, to get the license, you need create a product on Tuya AI+IoT Platfrom following [TuyaOS quickstart](https://developer.tuya.com/en/docs/iot-device-dev/application-creation?id=Kbxw7ket3aujc)


## Supported Hardware  
The current project can run on all currently supported chips and development boards.

## Notes

- **Three quick restarts clear the network config**: the `reset_netcfg` counter accumulates
  when not cleared within 5 seconds; reaching 3 triggers `tuya_iot_reset` (factory reset,
  followed by `core_system_reset`). Debugging with 3 quick restarts will drop the activation
  data and require re-scanning the QR code.
