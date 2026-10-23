# tuyaopen-core Linux 目标

[English](README.md) | 中文

## 构建

示例位于 core 仓库中（`tuyaopen-core/examples/linux/`）。

```bash
cd iot_demo
cmake -B build
cmake --build build -j$(nproc)
./build/iot_demo
```

凭据可选：`-DTUYA_PRODUCT_ID=.. -DTUYA_OPENSDK_UUID=.. -DTUYA_OPENSDK_AUTHKEY=..`。
不指定时，使用 `src/tuya_config.h` 或 `tuya_config_secrets.h` 中带 `#ifndef`
保护的默认值。

## Kconfig / menuconfig 配置（可选）

```bash
pip3 install --user kconfiglib        # 一次性
cd iot_demo                           # 三个 demo 任选其一

KCONFIG_CONFIG=./.config menuconfig   # 交互式配置
KCONFIG_CONFIG=./.config olddefconfig # 按默认值生成
```

每个 demo 自带 `Kconfig`（入口 rsource `../../../Kconfig`（core 入口）以及公共
qrcode 开关）和自己的 `.config`；符号与 CMake 选项一一对应、默认值一致。生成的
`.config` 会被该 demo 的 CMake 自动读取（`CONFIG_xxx=y` 会归一化为 ON）。链路为：

```
menuconfig -> .config -> demo CMake -> tuyaopen-core.cmake 宏注入
```

优先级：**`-D` 命令行 > `.config` > 工程内置默认值**。
注意：`.config` 一旦存在即优先使用（内置默认值不再作为回退）。删除 `.config`
即可回到内置默认值。`.config` 不提交（.gitignore）。

不用 menuconfig 的等价做法：`-DCONFIG_ENABLE_WIRED=ON`，或修改
`../../port/linux/include/common/tuya_kconfig.h` 中注释掉的开关（`-D` 优先）。

## 更新 core 版本

示例属于 core 仓库，与 core 一起做版本管理：按通常方式更新整个检出即可。
Linux port 位于 `../../port/linux`，由各 demo 的 CMake 构建（没有单独的副本）。
