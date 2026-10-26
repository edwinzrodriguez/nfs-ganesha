# Socket TX zerocopy (SO_ZEROCOPY / MSG_ZEROCOPY)

## What it is

Linux TCP can avoid copying userspace payload into skbuffs when sending with
`MSG_ZEROCOPY` after `setsockopt(SO_ZEROCOPY)`. Completions are **not** ordinary
readable/writable epoll events: they are queued on the socket **error queue**
and surface as **`EPOLLERR` / `POLLERR`**. Drain with:

```c
recvmsg(fd, &msg, MSG_ERRQUEUE);
```

Parse `sock_extended_err` (`IP_RECVERR` / `IPV6_RECVERR`) with
`ee_origin == SO_EE_ORIGIN_ZEROCOPY`. See
[kernel MSG_ZEROCOPY docs](https://docs.kernel.org/networking/msg_zerocopy.html).

ntirpc implements this for TCP RPC replies: large `sendmsg` calls may use
`MSG_ZEROCOPY`, and `XDR_DESTROY` / `uio_release` (including FSAL gifted buffers)
are deferred until ERRQUEUE completion.

## Two independent “zerocopy” layers

| Knob | Layer | What it avoids |
|------|--------|----------------|
| FSAL_CEPH `zerocopy=true` | Application | Ganesha ↔ libcephfs userspace copy (`ceph_buffer` / `to_iovec`) |
| `NFS_Core_Param { Enable_Socket_Zerocopy = true; }` | Kernel TX | Copy into TCP skbuffs on send |

Use both for end-to-end userspace→wire savings on large NFS READ replies. Socket
TX may still copy (`SO_EE_CODE_ZEROCOPY_COPIED`) depending on NIC/path.

## Ganesha config (`NFS_Core_Param`)

```
Enable_Socket_Zerocopy = true;       # default false
Socket_Zerocopy_Threshold = 16384;   # bytes; 0 = ntirpc default 16KiB
Socket_Zerocopy_Max_Pending = 0;     # 0 = ntirpc default 64MiB per xprt
```

Requires a kernel/glibc with `SO_ZEROCOPY` / `MSG_ZEROCOPY`. Soft-fails to classic
send if `setsockopt` fails.

## Man page

ntirpc documents the API in
[`libntirpc/man/rpc_svc_zerocopy.3t`](../libntirpc/man/rpc_svc_zerocopy.3t)
(`rpc_svc_zerocopy(3)`): `svc_init_params`, `svc_set_socket_zerocopy`,
and buffer-lifetime rules.

## Implementation notes

- Enable at TCP accept in ntirpc (`svc_vc.c`).
- Send path: `svc_zc_sendmsg` may OR `MSG_ZEROCOPY`; `ENOBUFS` falls back.
  The predicted kernel send ID is installed on `zcq` *before* `sendmsg` (lock
  released for the syscall) so ERRQUEUE drain cannot miss the id — without
  holding `writeq.qmutex` across `sendmsg`, which stalled drains and hung.
- Completions: `svc_zc_drain` empties ERRQUEUE from EPOLLERR and opportunistic
  post-send drains. EPOLLERR-only still consumes `EPOLLONESHOT` on the fd that
  fired (`xp_fd` RECV vs dup `xp_fd_send`); rearm **only that** direction
  (SEND bit in epoll user-data). Clearing the sibling arm drops a still-live
  oneshot and hangs under remote ZC. `write_resume` after ERR must not run
  when a SEND task owns `xioq_send` (UAF / NULL `ioq_wpe.fun`), but *must* run
  whenever ERR did not hand off a SEND task. Unmatched IDs are kept briefly as
  orphans. `XDR_DESTROY` runs only *after* releasing `writeq.qmutex`.
- Use a dedicated `zc_s` list node — never `ioq_s` (still on `writeq` during
  send).
- The RPC record-mark header must live in `xioq->frag_hdr_be` for ZC sends —
  a `flushv` stack local is reused after return while loopback deferred copy
  may still read it (corrupt RM → client slots stick, fio hangs).
- Partial ZC then `EAGAIN` on the remainder arms edge-triggered `EPOLLOUT`;
  resume via one post-arm retry and `svc_ioq_write_resume` after ERRQUEUE
  drain (completions free TX space without a reliable EPOLLOUT edge).
- Teardown: `svc_zc_clear` destroys parked replies on xprt unregister.

## Stats

`ganesha_stats.py socket_zc` (DBus `GetSocketZerocopyStats`) exposes ntirpc
`zc_stats`:

| Field | Meaning |
|-------|---------|
| `sends` | Successful `MSG_ZEROCOPY` sendmsg calls |
| `completions` | ERRQUEUE ZC completion IDs applied |
| `copied` | Completions with `SO_EE_CODE_ZEROCOPY_COPIED` |
| `enobufs` | `ENOBUFS` on ZC send |
| `fallback` | Fell back to classic send after ZC failure |
| `threshold` / `max_pending` | Effective ntirpc knobs |

`ganesha_stats.py reset` clears these counters. High `copied/sends` means the
kernel still copied; pin budget / threshold experiments should watch this ratio.

## Validation checklist

1. Build with `HAVE_SO_ZEROCOPY=ON` (CMake status line).
2. Set `Enable_Socket_Zerocopy = true;` (optionally with CEPH `zerocopy = true;`).
3. Large NFSv4 TCP READ; confirm no crash/UAF under load.
4. `perf` / folded stacks: expect lower `__memmove` / skbuff copy share on TX vs off.
5. `ganesha_stats.py json socket_zc` — `sends` and `completions` rising under load.
6. Connection close under outstanding ZC sends: no hang (zcq cleared on unregister).
7. Negative: old kernel / `setsockopt` fail → classic send path.
