# agent_demo

English | [中文](README_zh.md)

## Introduction

A Linux demo that runs the **whole link to the cloud AI agent**: it uploads a
preset voice sample as the trigger, the cloud performs ASR and the agent
replies, and the downlink **prints text only** by default (downlink audio can be
played through ALSA when the AI audio module is enabled). There is no UI, no
buttons, no wake word and no microphone.

- **Uplink**: the bundled voice sample `assets/voice_16k.pcm` (16kHz/16bit/mono,
  synthesized with edge-tts, 2.592s) is uploaded once automatically after
  startup (simulating "the user said something");
- **Downlink**: `[AGENT][ASR]` prints the recognition result and `[AGENT][NLG]`
  prints the agent reply text; audio packets delivered by the cloud are played
  through the ALSA consumer when the AI audio module is enabled, and are dropped
  without decoding or playback when it is disabled;
- **Audio adaptation**: microphone capture is abstracted behind a minimal
  application-side contract (`play_local_in_*`, src/); the Linux implementation
  is **PCM file playback** — no real audio hardware adaptation, only enough to
  make the link run. Replace this implementation when integrating a real device.

## Build and run

```bash
cd tuyaopen-core/examples/linux/agent_demo
cmake -B build
cmake --build build -j$(nproc)
cd build && env -u DISPLAY ./agent_demo
```

> The build must be done inside this repository tree: agent_demo reuses the
> common examples component `../components/qrcode` (no private copy), so copying
> this directory elsewhere makes configure fail because qrcode cannot be found
> (CMake gives an explicit error).

> **Runtime note: start it with `env -u DISPLAY`** (Linux desktop/virtual
> machine environments). This demo opens the ALSA audio output at startup
> (`play_local_out_install`); the ALSA pulse plugin reads `DISPLAY` during
> initialization to connect to the X server — if `DISPLAY` points to an
> **unreachable X** (e.g. a VM/SSH environment pointing at the host while the
> host X has no TCP listener), that connection **hangs forever** (`ss` shows the
> process in `SYN-SENT` to `<host>:6000`; 6000 is the X11 port) and the main
> thread blocks in the ALSA open → **the cloud connection never starts and the
> binding QR code is never printed**. Removing `DISPLAY` (ALSA does not need X)
> restores it: `env -u DISPLAY ./agent_demo`.
> The selftest targets (agent_chain/nlg/http_selftest) do not open audio devices
> and are unaffected; iot_demo / p2p_demo have no audio output and are likewise
> unaffected.

Credentials are optional: by default the PID/UUID/AuthKey from
`include/tuya_config.h` are used, and they can be overridden with
`-DTUYA_PRODUCT_ID=.. -DTUYA_OPENSDK_UUID=.. -DTUYA_OPENSDK_AUTHKEY=..`, or
configured in `include/tuya_config_secrets.h` (see the `.example` template);
same-named environment variables are supported as well.
Credential priority: **`-D` / environment variables > `tuya_config_secrets.h` >
defaults in `tuya_config.h`** (the `#define`s in the secrets template carry
`#ifndef` guards and do not override command-line injection; removing the
guards would make secrets silently override `-D`). The voice sample is copied to
`build/voice_16k.pcm` at build time; run from the `build/` directory.

Runtime behavior:

1. Initializes logging/KV/time service/work queue and connects to the cloud
   using the host network;
2. If the device is not activated, it enters binding mode: after the direct MQTT
   connection succeeds it prints the binding QR code;
3. Scan the QR code with the Tuya App to bind and activate the device (**the PID
   must have the AI service enabled**);
4. After activation the `MQTT_CONNECTED` event starts the AI link:
   `ai_agent_init()` → wait for the AI client to become ready → automatically
   upload one utterance (log `[AGENT] trigger: upload one utterance from ...`);
5. The cloud returns: `[AGENT][ASR] ...` (recognition result) →
   `[AGENT][NLG] ...` (agent reply text, the final criterion that the whole link
   works).

## Kconfig switches

| Symbol | Meaning | Default |
|---|---|---|
| `AGENT_DEMO` | Build this example; selects `ENABLE_AI_COMPONENTS`, `ENABLE_QRCODE` | y (no prompt) |
| `AGENT_DEMO_AUDIO_FILE` | Uplink voice sample path (16k/16bit/mono PCM); empty uses the bundled sample | "" |
| `ENABLE_COMP_AI_MCP` (defined in core, see `agent-core/svc_ai_mcp/Kconfig`) | MCP link switch: the core server (`svc_ai_mcp`) + the application-side tool set (`src/components/ai_mcp`, see "MCP support"); the demo defaults it to ON in `CMakeLists.txt` | ON (built-in default) |

menuconfig usage (`AGENT_DEMO` has no prompt; simply do not build this example,
there is no need to disable it):

```bash
pip3 install --user kconfiglib          # one-time
cd tuyaopen-core/examples/linux/agent_demo
KCONFIG_CONFIG=./.config menuconfig     # interactive configuration
KCONFIG_CONFIG=./.config olddefconfig   # generate from defaults
```

Priority: **`-D` command line > `.config` > project built-in defaults**. `-D`
can also be used directly:

```bash
cmake -B build -DCONFIG_AGENT_DEMO_AUDIO_FILE=/path/to/your_16k.pcm
```

Note that this demo's own `.config` (in its directory) is read by its CMake;
once it exists it takes precedence. Delete it to return to the built-in
defaults (AI components / QRCODE / WIRED on).

## MCP support (cloud tool calls)

**Switch**: the core `CONFIG_ENABLE_COMP_AI_MCP` (this demo's `.config` may set
`=y`, and the demo's `CMakeLists.txt` has a
built-in default ON; `-D` can override). When
disabled the `ai_mcp` module is not compiled and the wiring in `tuya_main.c` is
guarded by the `ENABLE_COMP_AI_MCP` macro (the same style as `ENABLE_QRCODE`);
the demo still builds and runs, just without MCP tools.

**Wiring** (`src/tuya_main.c`): `user_main()` calls `ai_mcp_init()` before
`tuya_iot_start()`. That function only subscribes:
`core_event_subscribe(EVENT_MQTT_CONNECTED, "ai_mcp_init", ...,
SUBSCRIBE_TYPE_ONETIME)`. The real initialization happens in the callback after
MQTT connects: `ai_mcp_server_init` (sets the server name/version) → register
tools → `tuya_ai_agent_mcp_set_cb(ai_mcp_server_parse_message)` attaches the MCP
message callback to the AI agent (and sets `enable_mcp`, reporting the
`supportCustomMCP` capability to the cloud when the session is established).

The cloud then sends JSON-RPC (MCP protocol) messages inside the AI session:
`__mcp_handle` in `tuya_ai_agent.c` → `ai_mcp_server_parse_message` (handles
`initialize` / `tools/list` / `tools/call`) → execute the tool →
`tuya_ai_agent_mcp_response` replies.

**Current tool list** (`src/components/ai_mcp/ai_mcp_tools.c`):

| Tool | Capability | Parameters | Returns |
|---|---|---|---|
| `device_info_get` | Report device information: `model` (= `PROJECT_NAME`, `agent_demo` for this demo), `serialNumber` (= the IoT client's real devid, `unknown` when not activated), `firmwareVersion` (= `PROJECT_VERSION`) | none | JSON |

Only `device_info_get` is registered, so the cloud `tools/list` matches the
actual capabilities and never exposes placeholders whose calls would always
fail.

## Selftests

Three standalone executables that need no cloud connection, no App and no
credentials (run from the build directory — the `voice_16k.pcm` sample is copied
there by CMake; running elsewhere prints a `run from build/` hint):

```bash
cd build && ./agent_chain_selftest
cd build && ./agent_nlg_selftest
cd build && ./agent_http_selftest
```

> Verification discipline: use `cmake --build build` (**all targets**) as the
> criterion — running a single selftest is not complete verification: the
> selftests can link while the agent_demo main target fails to link.

- `agent_chain_selftest`: uplink audio source contract and sample frame
  splitting — total bytes 82944, split into 80ms frames (2560B) = 33 frames
  (32 full frames + a 1024B last frame), EOF semantics, close/reopen, bad path
  rejection, **I/O error vs. EOF distinction** (fopen on a directory path
  succeeds but read must return <0 rather than 0/EOF). The sample path uses the
  same source as the app (`CONFIG_AGENT_DEMO_AUDIO_FILE` non-empty means that
  file is tested). Expected output `AGENT CHAIN SELFTEST PASSED` (exit 0).
- `agent_nlg_selftest`: downlink consumption contract — the final segment
  (eof=1) of a multi-segment NLG stream carried non-empty by
  `AI_USER_EVT_TEXT_STREAM_STOP` must be printed, while an empty packet must not
  print anything. Events are built through the public API (independent of the
  cloud packetization). Expected output `AGENT NLG SELFTEST PASSED` (exit 0).
- `agent_http_selftest`: http_manager URL parsing boundary — out-of-range ports
  (wraparound samples) and an explicit `:0` must be rejected by the parser;
  in-range ports must not be rejected; empty-host regression (no network
  needed). Expected output `AGENT HTTP SELFTEST PASSED` (exit 0).

## Voice sample

The bundled sample `assets/voice_16k.pcm` (copied to `build/voice_16k.pcm` at
build time):

- **Content**: a Chinese utterance ("what time is it now?") synthesized with edge-tts (zh-CN-XiaoxiaoNeural)
- **Format**: 16kHz / 16bit / mono / little-endian PCM (raw), 2.592 seconds,
  82944 bytes
- **How to generate it** (needs network):
  ```bash
  pip3 install --user edge-tts
  edge-tts --voice zh-CN-XiaoxiaoNeural --text "<your text>" --write-media voice.mp3
  ffmpeg -y -i voice.mp3 -ar 16000 -ac 1 -f s16le voice_16k.pcm
  ```

To use your own voice: generate 16k/16bit/mono PCM in the same format and
specify it with `-DCONFIG_AGENT_DEMO_AUDIO_FILE`, or replace
`assets/voice_16k.pcm`. The sample length determines the upload duration
(frames = duration × 12.5 frames/second).

## Capability boundaries (what this demo deliberately does not do)

> **Positioning**: agent_demo is a **verification vehicle** for the whole cloud
> AI agent link, not a feature-complete product. The capabilities below are
> **intentionally omitted**: the goal is to make the link run and the criteria
> real.

**UI**: none — no screen / emotion / LVGL / chat interface.

**Hardware-related**: none — no buttons, wake word, VAD, camera, printer,
battery or low power, and **no real microphone** (the audio source is PCM file
playback through the application-side contract `play_local_in_*`; replace that
implementation for a real device, the contract header documents the adaptation
point). Note that "replacing only the capture implementation" does not add four
things: half-duplex gating (no upload while playing), AEC, local VAD endpointing
and push capture semantics — those decisions belong to a conversation state
machine that this demo does not implement.

**Audio/video capabilities not implemented**:
- **Uplink Opus encoding**: uplink audio is always PCM 16kHz/16bit/mono in this
  demo; there is no Opus encoder path in this tree — adding one is
  application-layer work;
- **Downlink TTS playback**: when the AI audio module is disabled, downlink
  audio packets are dropped; when `ENABLE_COMP_AI_AUDIO` is enabled they are
  played through the application-side ALSA consumer;
- **Media URL streaming download**: not implemented (media download fails
  safely);
- **Image / file / video downlink**: not implemented (no UI / camera / player).

**Conversation interaction**: single trigger (one preset utterance uploaded
after startup); multi-turn conversation, voice interruption and server-side VAD
interaction are not enabled.

## Known limitations

- **The PID needs the AI service enabled**: the default PID
  `alon7qgyjj8yus74` is a test PID; the authorization code is the test license
  from iot_demo. For production, use your own AI product + license (`-D` or
  `include/tuya_config_secrets.h`).
- **The default UUID/AuthKey are the repository's test license** (the same as
  iot_demo/p2p_demo): one license cannot be online in several places at the same
  time, so running it in parallel with other demos makes them kick each other
  offline.
- **Three consecutive resets clear the network configuration**: the
  `reset_netcfg` counter accumulates when it is not cleared within 5 seconds and,
  once it reaches 3, triggers `tuya_iot_reset` (factory reset, followed by a
  `core_system_reset` reboot). Debugging with three quick restarts in a row loses
  the activation data and requires scanning the QR code again.
- **Activation data lives in `tuyadb/` in the run directory**: the first run
  needs QR-code activation with the Tuya App; running from another directory
  means another device identity and requires scanning again (or copying the
  activated `tuyadb/` along).
- **Uplink audio is PCM 16kHz/16bit/mono**: Opus encoding is not implemented.
  If the cloud requires an encoded uplink, integrate an encoder in the
  application and change the audio attribute branch in `ai_agent.c`.
- **One upload only**: the trigger is one-shot; for multiple rounds, rerun or
  extend the trigger thread.
- **Downlink audio is dropped** when `ENABLE_COMP_AI_AUDIO` is disabled; the
  cloud still delivers the TTS audio stream (it consumes traffic but is not
  processed).
- **NLG final-segment printing boundary**: the final segment (eof=1) of a
  multi-segment NLG stream is printed when carried by STOP; this consumption
  contract is covered by `agent_nlg_selftest`, but whether the cloud ever sends
  a non-empty final segment depends on its packetization at runtime.
  Contract-level success != end-to-end verification.
- **URL parser boundaries (media download related)**: IPv6 literals (`[::1]`)
  and URLs containing userinfo (`user:pass@host`) are not supported — both fail
  safely via the explicit `:0` invalid check (the host segment is truncated at
  the first `:` and the port segment hits a non-number); supporting them would
  require explicit bracket/userinfo handling in the host segment. Media URLs
  longer than 1023 characters are silently truncated by `fill_url_head`
  (`DEF_URL_LEN=1024`). Media download is not implemented, so this has no
  practical effect today; handle all three points if download is implemented
  later.
- **Media URL download not implemented**: if the cloud sends download URLs for
  images/files/audio, the agent-core side fails safely (error log
  `http media download not implemented`) and nothing is downloaded. The text
  link (ASR/NLG/emoji) does not trigger this path.
- **AI monitor (TCP 5055)**: core's `ai_monitor` listens on local port 5055 and
  outputs the AI protocol stream for debugging (`ENABLE_AI_MONITOR`, on by
  default). When the port is in use the monitor itself retries in a degraded
  manner (log `create server socket failed`) and **does not block the AI link**.

## Directory structure

```sh
+- agent_demo
    +- assets
    |   -- voice_16k.pcm            # uplink voice sample (edge-tts, 16k/16bit/mono)
    +- include                       # application headers
    |   -- agent.h / agent_audio.h / play_local_in.h / play_local_out.h
    |   -- reset_netcfg.h / tuya_config.h / tuya_config_secrets.h.example
    +- src
    |   -- tuya_main.c              # entry: initialization, cloud connection, event handling and dispatch
    |   -- agent.c                  # AI link startup orchestration (after MQTT connects: agent init + one voice trigger)
    |   -- agent_audio.c            # audio link glue: uplink feeding (PCM frame upload) + downlink ASR/NLG printing
    |   -- reset_netcfg.c           # consecutive-reset network configuration clearing
    |   +- adapt/                    # platform adaptation layer (real-device replacement points)
    |   |   -- play_local_in.c      # audio input contract implementation (PCM file playback)
    |   |   -- play_local_out.c     # ALSA output endpoint (compiled in under ENABLE_AI_PLAYER)
    |   |   -- fs_card.c            # filesystem card (standby; not linked with the narrowed DATASINK)
    |   +- components/               # application-side AI components
    |       -- utility/             # event bus (ai_user_event)
    |       -- audio_player/        # player core (svc_ai_player + consumer/datasink/decoder/mixer/resample)
    |       -- ai_audio/            # TTS/music playback orchestration
    |       -- ai_agent/            # application wrapper (output cbs registration, text/file/image uplink)
    |       -- ai_skills/           # downlink text parsing (ASR/NLG/skill)
    |       -- ai_mcp/              # MCP tool set
    +- selftest                      # offline selftests (standalone targets, no cloud; see "Selftests")
    |   -- agent_chain_selftest.c   # link selftest (target agent_chain_selftest)
    |   -- agent_nlg_selftest.c     # downlink consumption selftest (target agent_nlg_selftest)
    |   -- agent_http_selftest.c    # URL parsing boundary selftest (target agent_http_selftest)
    -- CMakeLists.txt
    -- Kconfig                      # AGENT_DEMO* switches + component switches (audio_player/ai_audio)
```

The design document lives in the docs tree:
`docs/tuyaopen-core-dev/linux/agent-demo-design.md`
(under `/home/share/samba/open-components`).

Platform adaptation layer (application side, under `src/adapt/`):

```sh
+- agent_demo/src/adapt
    +- play_local_in.h/.c            # audio input contract + Linux implementation (PCM file playback, real-device replacement point)
    +- play_local_out.h/.c           # ALSA output endpoint (compiled in under ENABLE_AI_PLAYER)
    +- fs_card.c                     # filesystem card (core_fs_ops; copy for standby —
                                     #   not linked while DATASINK is narrowed to MEM|URL, enabled when restoring the FILE source)
+- p2p_demo/src/adapt/fs_card.c      # the same original (used by local_store)
```

> `fs_card.c` is kept in sync with the p2p_demo copy (same implementation); if
> you change the filesystem card, run
> `diff tuyaopen-core/examples/linux/agent_demo/src/adapt/fs_card.c tuyaopen-core/examples/linux/p2p_demo/src/adapt/fs_card.c`
> after syncing to confirm.
