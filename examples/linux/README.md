# tuyaopen-core linux target

English | [中文](README_zh.md)

## Build

The examples live in the core repository (`tuyaopen-core/examples/linux/`).

```bash
cd iot_demo
cmake -B build
cmake --build build -j$(nproc)
./build/iot_demo
```

Credentials are optional: `-DTUYA_PRODUCT_ID=.. -DTUYA_OPENSDK_UUID=.. -DTUYA_OPENSDK_AUTHKEY=..`.
When omitted, the defaults from the `#ifndef` guards in `src/tuya_config.h` or
`tuya_config_secrets.h` are used.

## Kconfig / menuconfig configuration (optional)

```bash
pip3 install --user kconfiglib        # one-time
cd iot_demo                           # any of the three demos

KCONFIG_CONFIG=./.config menuconfig   # interactive configuration
KCONFIG_CONFIG=./.config olddefconfig # generate with defaults
```

Each demo owns its `Kconfig` (an entry that rsources `../../../Kconfig`, the
core entry, plus the shared qrcode switches) and its own `.config`; symbols
match the CMake options one-to-one with the same defaults. The generated
`.config` is read automatically by that demo's CMake (`CONFIG_xxx=y` is
normalized to ON). The chain is:

```
menuconfig -> .config -> demo CMake -> tuyaopen-core.cmake macro injection
```

Priority: **`-D` command line > `.config` > built-in project defaults**.
Note: once a `.config` exists, it takes precedence (the built-in defaults are
no longer used as a fallback). Delete `.config` to return to the built-in
defaults. `.config` is not committed (.gitignore).

Manual equivalent without menuconfig: `-DCONFIG_ENABLE_WIRED=ON`, or edit the
commented switches in
`../../port/linux/include/common/tuya_kconfig.h` (`-D` takes priority).

## Updating the core version

The examples are part of the core repository, so they are versioned together
with it: update the whole checkout as usual. The linux port lives in
`../../port/linux` and is built by each demo's CMake (no separate copy).
