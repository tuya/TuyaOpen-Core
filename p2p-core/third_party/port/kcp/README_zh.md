# KCP 移植层（TuyaOpen 适配）

上游 KCP 以源码形式内置于 `p2p-core/third_party/kcp`，TuyaOpen 的 mbuf/CUBIC/pacing
修改直接落在内置的 `ikcp.c`/`ikcp.h` 中：没有 submodule、没有构建期 staging、
也没有 patch 文件。

| 文件 | 用途 |
|---|---|
| `p2p-core/third_party/kcp/ikcp.c/.h` | 内置 KCP 1.7 + TuyaOpen mbuf/CUBIC/pacing 修改 |
| `ikcp_cong.c/.h` | CUBIC 拥塞控制 |
| `ikcp_minmax.c/.h` | min/max 辅助 |
| `ikcp_pacing.c/.h` | 发送 pacing |
| `kcp.cmake` | 给 `base_ice` 提供源码与头文件路径 |

`kcp.cmake` 由 `p2p-core/base_ice/CMakeLists.txt` include；`base_ice` 编译
`${TUYAOPEN_THIRD_PARTY_DIR}/kcp/ikcp.c` 以及本目录下的 port 源码。

上游参考：KCP 1.7（`https://github.com/skywind3000/kcp`，commit `38e0c93`）。
升级时用新的上游源码替换 `p2p-core/third_party/kcp`，重新把 TuyaOpen 修改落到
`ikcp.c`/`ikcp.h`，并与本目录 port 源码的差异一起评审。
