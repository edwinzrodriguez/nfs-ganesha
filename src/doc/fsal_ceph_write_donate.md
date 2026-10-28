# FSAL_CEPH WRITE donate / claim (fallback)

Prefer upper-layer alloc when available (`USE_FSAL_CEPH_FS_ALLOC_IO`, see
[`upper_layer_write_alloc.md`](upper_layer_write_alloc.md)): Ceph pages are
allocated before recv, so `write2` consumes an owned `bufferlist` with no
donate/claim.

Donate/claim remains the fallback when fill_payload is unset or fails.
Do not reach for Ceph `unique_leakable_ptr<raw>` here — it is C++-internal
transfer typing only (see
[`upper_layer_write_alloc.md`](upper_layer_write_alloc.md#ceph-unique_leakable_ptr-not-for-ganesha)).
ObjectCacher needs to own WRITE payload bytes past the NFS write callback.
Non-owning `ceph_bufferlist_wrap_iov` / `create_static` cannot do that: recv UV
buffers are freed in `XDR_DESTROY` when the compound resumes after the CB.

## Ownership model

| Stage | Owner |
|-------|--------|
| TCP recv | ntirpc `xdr_ioq_uv` (`UIO_FLAG_FREE`) |
| `xdr_ioq_donate_iovs` | Mark UV `UIO_FLAG_DONATED`; clear `FREE` |
| `ceph_bufferlist_claim_iov` | Ceph `buffer::raw` + custom deleter |
| `XDR_DESTROY` | Frees UV metadata only (skips `free_buffer`) |
| Last BH / raw drop | `xdr_ioq_donate_deleter` frees **allocation base** |

WRITE iov bases are often mid-UV (`vio_head` after RPC/COMPOUND headers). The
deleter must never `free(iov_base)`; it maps each claimed slice to the UV
`vio_base` / `ioquv_size` and frees that once (atomic per-alloc refcount).

## Code

- ntirpc: `UIO_FLAG_DONATED`, `xdr_ioq_donate_iovs` / `_cancel` / `_deleter`
  in [`xdr_ioq.c`](../libntirpc/src/xdr_ioq.c)
- FSAL: `ceph_fsal_write2` prefers claim when `USE_FSAL_CEPH_FS_CLAIM_IO`;
  falls back to `wrap_iov` if donate cannot map every iov (copy-decode path)
- Build: CMake detects `ceph_bufferlist_claim_iov`

## Config

`client_oc` + `zerocopy` remains rejected until Phase 2 routes ZeroCopyClient
writes through ObjectCacher. Donate/claim is already exercised on the
Objecter-only ZC write path when claim_iov is available.

## Failure / cancel

- Donate failure → `wrap_iov` (unchanged lifetime: valid until write CB)
- `claim_iov` failure after donate → `xdr_ioq_donate_cancel` restores `FREE`
- `ceph_ll_async_write` consumes the bufferlist even on submit error → do
  **not** cancel; Ceph destroys the bl and runs the deleter
