# KCP port (TuyaOpen adaptation layer)

Upstream KCP is vendored in `p2p-core/third_party/kcp` and modified in-tree: the
TuyaOpen mbuf/CUBIC/pacing changes are part of the vendored `ikcp.c`/`ikcp.h`,
so there is no submodule, no build-time staging and no patch files.

| File | Purpose |
|---|---|
| `p2p-core/third_party/kcp/ikcp.c/.h` | vendored KCP 1.7 with the TuyaOpen mbuf/CUBIC/pacing changes |
| `ikcp_cong.c/.h` | CUBIC congestion control |
| `ikcp_minmax.c/.h` | min/max helpers |
| `ikcp_pacing.c/.h` | send pacing |
| `kcp.cmake` | lists the sources and include dirs for `base_ice` |

`kcp.cmake` is included by `p2p-core/base_ice/CMakeLists.txt`; `base_ice`
compiles `${TUYAOPEN_THIRD_PARTY_DIR}/kcp/ikcp.c` plus the port sources from
this directory.

Upstream reference: KCP 1.7 (`https://github.com/skywind3000/kcp`, commit
`38e0c93`). To update, replace `p2p-core/third_party/kcp` with the new upstream tree,
re-apply the TuyaOpen changes to `ikcp.c`/`ikcp.h`, and review the diff
against this directory's port files.
