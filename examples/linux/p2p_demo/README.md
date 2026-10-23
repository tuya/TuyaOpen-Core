# p2p_demo

English | [中文](README_zh.md)

## Overview

A Linux demo that pushes device video to the Tuya App over a P2P channel (the
App watches the live stream), with optional local recording and calendar
playback. There is no camera on Linux, so the video source is the sample shipped
with the demo, `assets/demo_video.264` (2304x1296@15fps, 36-second loop); the
virtual camera `demo_vcam` feeds it to the media chain cyclically at the
configured frame rate. Audio is not implemented.

## Build and Run

```bash
cd tuyaopen-core/examples/linux/p2p_demo
cmake -B build
cmake --build build -j$(nproc)
cd build && ./p2p_demo
```

> The demo must be built inside this repository tree: p2p_demo reuses the shared
> examples component `../components/qrcode` (no local copy). Copying this
> directory elsewhere makes configure fail because qrcode cannot be found (CMake
> reports this explicitly).

Credentials are optional: the PID/UUID/AuthKey default to `src/tuya_config.h`
and can be overridden with
`-DTUYA_PRODUCT_ID=.. -DTUYA_OPENSDK_UUID=.. -DTUYA_OPENSDK_AUTHKEY=..`, set in
`src/tuya_config_secrets.h` (see the `.example` template), or supplied through
environment variables of the same names.
Credential priority: **`-D` / environment variables > `tuya_config_secrets.h` >
the defaults in `tuya_config.h`** (the `#define`s in the secrets template are
guarded by `#ifndef`, so they do not override command-line injection). The video
source is copied to `build/demo_video.264` at build time; running from `build/`
works directly (when run elsewhere, the path is resolved relative to the
executable directory).

Runtime behavior:

1. Initialize logging/KV/time service/work queue and connect to the cloud over
   the host network;
2. When the device is not activated, enter binding mode: print the QR code after
   the direct MQTT connection succeeds;
3. Scan the QR code with the Tuya App to bind and activate the device;
4. After activation, the `MQTT_CONNECTED` event starts the media thread
   (`TUYA_APP_Start` -> P2P listen), and the App can open the live stream (an
   IPC-category product is required, see "Known Limitations").

## Kconfig Options

| Symbol | Meaning | Default |
|---|---|---|
| `P2P_DEMO` | Build this example; selects `ENABLE_TUYA_P2P`, `ENABLE_IPC_RING_BUFFER` | y (no prompt) |
| `P2P_DEMO_LOCAL_STORE` | Local recording and calendar playback; selects `ENABLE_LOCAL_STORE` | n |
| `P2P_DEMO_WIDTH` | Video width (pixels) reported to the App; must match the video source | 2304 |
| `P2P_DEMO_HEIGHT` | Video height (pixels) reported to the App; must match the video source | 1296 |
| `P2P_DEMO_FPS` | Virtual camera frame feed rate, also reported to the App | 15 |
| `P2P_DEMO_KBPS` | Target bitrate reported to the App (the source does not take part in rate control) | 960 |
| `P2P_DEMO_GOP` | GOP length reported to the App (the bundled video has a GOP of 60) | 60 |
| `P2P_DEMO_H264_FILE` | Live video source path; empty uses the bundled video | "" |
| `P2P_DEMO_LIVE_RECORD` | Continuously write local_store during live streaming (split into segments) | n |
| `P2P_DEMO_RECORD_MAX_SEC` | Maximum duration of one recording segment (seconds, 30-3600) | 120 |
| `P2P_DEMO_PB_SEED` | Seed today's index with the live source file at startup to try playback without recording | n |

menuconfig usage (`P2P_DEMO` has no prompt; simply do not build this example
instead of disabling it):

```bash
pip3 install --user kconfiglib          # one-time
cd tuyaopen-core/examples/linux/p2p_demo
KCONFIG_CONFIG=./.config menuconfig     # interactive configuration
KCONFIG_CONFIG=./.config olddefconfig   # generate from defaults
```

Priority: **`-D` command line > `.config` > built-in defaults**. `-D` can also
be used directly to override:

```bash
cmake -B build -DCONFIG_P2P_DEMO_FPS=15
cmake -B build -DCONFIG_P2P_DEMO_H264_FILE=/path/to/your.h264
```

Note that this demo's own `.config` (in its directory) is read by its CMake;
once it exists it takes precedence. Delete it to return to the built-in
defaults.

## Selftests

Two standalone executables that need neither the cloud nor the App (run from the
example directory):

```bash
./build/vcam_selftest
./build/media_chain_selftest
```

- `vcam_selftest`: opens the video source and verifies the AU index and frame
  feed timing; expects `VCAM SELFTEST PASSED` (80 frames, timestamps stepping by
  66ms, 2 I frames).
- `media_chain_selftest`: exercises the full media chain (vcam -> frame
  validation/queue/ring buffer -> P2P frame-fetch callback) and verifies
  "request I frame"; expects `MEDIA CHAIN SELFTEST PASSED` (60 frames in 4
  seconds, monotonically increasing timestamps, at most 8 frames between the
  request and the next I frame -- normally 1~2 frames, while an ignored request
  waits ~59 P frames). When the video source is missing it reports
  `sample not found: run from build/ ...`.

## Video Source

The bundled video `assets/demo_video.264` (copied to `build/demo_video.264` at
build time):

- **Format**: Annex-B H.264, High profile, 2304x1296@15fps, 540 frames
  (36-second loop), 9 IDRs (GOP=60), no AUD (AU boundaries are split when a VCL
  NAL follows an existing VCL), maximum frame about 56KB, whole file 4.3MB.
- `demo_vcam` parses it into an AU index on open and feeds frames cyclically at
  the configured frame rate; `request_i_frame` jumps to the next IDR.

To use your own H.264:

```bash
cmake -B build -DCONFIG_P2P_DEMO_H264_FILE=/path/to/your.h264
```

Alternatively set `P2P_DEMO_H264_FILE` in menuconfig (or edit `.config`
directly). Annex-B format is required; when resolution/frame rate/GOP change,
`P2P_DEMO_WIDTH/HEIGHT/FPS/GOP` must be updated accordingly (and `KBPS` is
recommended), otherwise the AV capabilities reported to the App do not match
the actual stream. A single AU must be smaller than 256KB (the demo frame queue
slot limit; larger frames are dropped with a warning).

## Recording and Playback (off by default)

By default only P2P live streaming is enabled. To enable local recording and
calendar playback:

```bash
cmake -B build-ls -DCONFIG_P2P_DEMO_LOCAL_STORE=ON -DCONFIG_ENABLE_LOCAL_STORE=ON
cmake --build build-ls -j$(nproc)
```

- `P2P_DEMO_LOCAL_STORE` selects the core `ENABLE_LOCAL_STORE`; in menuconfig
  only the former needs to be checked, while with `-D` both must be given.
- Recordings are written to `./media` (relative to the **run directory**; when
  running from `build/` this is `build/media`), with segment files under
  `media/rec/<YYYY-MM-DD>/` (`live_N.media` plus an index); the date uses the
  device local time, which is `1970-01-01` before cloud time synchronization
  (just started, not yet activated).
- Continuous recording during live streaming additionally requires
  `P2P_DEMO_LIVE_RECORD` (default n); the segment duration is controlled by
  `P2P_DEMO_RECORD_MAX_SEC` (default 120 seconds).
- `P2P_DEMO_PB_SEED` (default n) seeds today's index with the live source file at
  startup, so the App can try calendar playback without any recording.

## Known Limitations

- **An IPC-category product is required**: the default PID is an IPC product
  (`sjzw8nclwoitx0rv`). With a generic
  product, the device can still connect to the cloud, be bound by the App, and
  report skills successfully, but the cloud
  `tuya.device.ipc.p2p.config.get` returns `IP Camera Not exist` -- a
  non-IPC-category product has no P2P configuration, `TUYA_APP_Start` cannot
  obtain the p2p auth, and the P2P service does not start. For production, use
  your own IPC product and license (`-D` or `src/tuya_config_secrets.h`).
- **The default UUID/AuthKey is the test license in this repository** (same as
  iot_demo): the same license cannot be online in two places at once; running it
  alongside iot_demo kicks each other offline.
- **Activation data is stored in `tuyadb/` under the run directory**: the first
  run requires scanning the QR code with the Tuya App; running from another
  directory means a different device identity and requires scanning again (or
  copy the activated `tuyadb/` along).
- **Three consecutive quick resets clear the network configuration**: the
  `reset_netcfg` counter accumulates when not cleared within 5 seconds, and
  reaching 3 triggers `tuya_iot_reset` (factory reset, followed by a
  `core_system_reset` restart). Three quick restarts while debugging drop the
  activation data and require re-scanning to bind.
- **An unactivated device stays in binding wait state**: MQTT is connected and
  the QR code is printed, waiting for the App to scan; the P2P service does not
  start before activation.
- **Calling `TUYA_APP_Start` on an unactivated device blocks**: when `p2p_init`
  cannot obtain the p2p auth, it retries forever at 1-second intervals
  (`while(1)`, continuously printing `get p2p auth failed`). The normal flow
  starts it from the `MQTT_CONNECTED` event after activation, so this is not hit;
  it only happens if called while unactivated.
- App bitrate adjustment has no effect (the video source has no encoder and the
  callback returns not-supported); in a real session it may log one bitrate
  adjustment line per second.
- **Long-run note**: the pjproject TLS row table has a fixed size of 24 rows, is
  allocated per thread, and is never reclaimed (`os_core_tuyaos.c` in the
  p2p-core stack); each P2P session adds a session thread (`rtc_wrk`). When the
  table is full, `pj_thread_local_set` returns `PJ_ENOMEM` and `get` returns NULL
  (graceful degradation, no crash), so part of the P2P context for that session
  becomes unavailable. Only extremely frequent connect/disconnect cycles (on the
  order of tens) reach this; a full fix requires changing p2p-core.

## Directory Layout

```sh
+- p2p_demo
    +- assets                       # demo_video.264 (the bundled sample video)
    +- src
    |   -- tuya_main.c              # Entry: init, cloud connection, event handling, media thread
    |   -- reset_netcfg.c/.h        # Clear network config after 3 consecutive resets
    |   -- tuya_ipc_demo.c/.h       # Media chain (frame fetch/queue/ring buffer/recording) and callback contract
    |   -- demo_vcam.c/.h           # Virtual camera: sample AU index + cyclic frame feed at the frame rate
    |   -- demo_media_event.c/.h    # local_store playback (calendar query/playback frame sending)
    |   +- adapt/                    # Platform adaptation layer (replacement point on real hardware)
    |   |   -- fs_card.c            # File system card (core_fs_ops; used by local_store)
    |   -- tuya_config.h            # PID/UUID/AuthKey (overridable by secrets or -D)
    |   -- tuya_config_secrets.h.example  # Credential override template
    +- selftest                      # Offline selftests (standalone targets, no cloud; see "Selftests")
    |   -- demo_vcam_selftest.c     # vcam selftest (target vcam_selftest)
    |   -- media_chain_selftest.c   # Media chain selftest (target media_chain_selftest)
    -- CMakeLists.txt
    -- Kconfig                      # P2P_DEMO* options
```

> `fs_card.c` has a copy in p2p_demo and agent_demo and **must be kept in sync**:
> after modifying either copy, run
> `diff tuyaopen-core/examples/linux/p2p_demo/src/adapt/fs_card.c tuyaopen-core/examples/linux/agent_demo/src/adapt/fs_card.c`
> to check (sync the change to the other copy if they differ).
