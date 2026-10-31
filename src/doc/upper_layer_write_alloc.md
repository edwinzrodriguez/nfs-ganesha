# Upper-layer WRITE allocation (libntirpc Phase 1)

Ceph-native WRITE pages need to be allocated by the FSAL/Ceph before payload
bytes land, so ObjectCacher can own them without `claim_iov` mid-UV juggling.

## API

| Symbol | Role |
|--------|------|
| `UIO_FLAG_UPPER` | Recv UV whose `vio_base` is upper-owned; destroy skips `free_buffer` |
| `xdr_ioq_set_payload_alloc` | Register allocator on the decode stream |
| `xdr_ioq_has_payload_alloc` | Probe before choosing fill vs FILLBUFS |
| `xdr_ioq_fill_payload` | Alloc → copy buffered body → `readv` deferred remainder |
| `svc_vc_set_payload_defer` | Stage only `hdr_stage_max` of a large LAST_FRAG |
| `svc_vc_body_remain` / `svc_vc_readv_body` | Deferred body still on the socket |
| `svc_vc_drain_body_remain` | Discard leftover body if decode did not fill |

## Flow

1. Optional: `svc_vc_set_payload_defer(xprt, true, 0)` (default stage 256KiB).
2. Large LAST_FRAG: recv stages headers (+ payload prefix) into a FREE UV;
   remainder stays as `sx_body_remain` (EPOLLIN not rearmed until consumed).
3. Ganesha (Phase 2): `xdr_ioq_set_payload_alloc` then, after WRITE `data_len`,
   `xdr_ioq_fill_payload` instead of `XDR_FILLBUFS`.
4. Callback returns Ceph pages + cookie; fill copies prefix, `readv`s the rest,
   appends `UIO_FLAG_UPPER` UVs for deferred bytes so `SETPOS` works.
5. FSAL passes the cookie/`bufferlist` to `ceph_ll_async_write` (no donate/claim).

Without defer, fill still works: full body is copied from FREE UVs into upper
pages (Ceph constraints satisfied; one memcpy). Donate/claim remains the
fallback when alloc is unset or fill fails.

## Phase 2 (Ganesha wiring)

| Piece | Role |
|-------|------|
| `fso_allocate_own_write_buffer` | FSAL capability |
| `USE_FSAL_CEPH_FS_ALLOC_IO` | CMake detect `ceph_bufferlist_alloc` |
| `nfs_write_payload_setup` | Hook before `SVCAUTH_CHECKSUM` |
| `xdr_io_data_decode` | Prefers `xdr_ioq_fill_payload` when alloc set |
| `nfs4_op_write` / `nfs3_write` | Forward `release`/`release_data` into `write_arg` |
| `ceph_fsal_write2` | Consume cookie bl; skip donate/claim on happy path |

Donate/claim remains the fallback when fill_payload is unset or fails.

## Ceph `unique_leakable_ptr` (not for Ganesha)

Ceph’s `ceph::unique_leakable_ptr<buffer::raw>` is an internal C++ move-only
tag used when constructors such as `create_page_aligned` / `claim_buffer`
hand a `raw*` into `buffer::ptr` / `bufferlist::push_back`. It uses a nop
deleter: it does **not** free on destruction and will leak if never adopted.
`ZeroCopyClient::alloc_buffers` / `claim_iov` already consume those APIs
behind the C ABI.

Ganesha must not depend on it. Stay on
`ceph_bufferlist_{alloc,claim_iov,to_iovec,put}`. The type does not replace
ntirpc `UIO_FLAG_*`, fill_payload cookies, or donate deleters, and exposing
it would break the opaque C contract without fewer release paths.

## ObjectCacher (`client_oc` + `zerocopy`)

When both are enabled, ZeroCopyClient may dirty ObjectCacher with Allocated
(upper-layer) plaintext, or with ciphertext after FSCrypt encrypt. Ganesha
requires fill_payload / `ceph_bufferlist_alloc` on the typical WRITE path;
claim/wrap are not used when `client_oc` is on. Config refuses the combo if
this build lacks `USE_FSAL_CEPH_FS_ALLOC_IO`.

## Non-goals (later)

- Multi-fragment defer beyond a single large LAST_FRAG
- RDMA upper alloc
- Adopt Ceph `unique_leakable_ptr` in Ganesha (see above)
