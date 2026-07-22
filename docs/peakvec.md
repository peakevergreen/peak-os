# PeakVec — local vector index

PeakVec is a Peak-authored, in-guest vector index for session/workspace context. It is **not** Qdrant/Pinecone; it is a system primitive beside VFS and peak-agent.

## Design

- **Local-only by default.** Embeddings are hashed n-grams (`peakvec_embed_text`) — no model weights, no network.
- **Slim kernel, large data on disk.** Index pages through the [blobstore](#blobstore) LRU cache (128 KiB). Total size scales with the block device.
- **Brute-force cosine** over int16 vectors (`PEAKVEC_DIM=64`). ANN (HNSW/IVF) is deferred until corpora need it — revisit when a namespace regularly exceeds ~512 live entries or query latency shows up in Monitor/sysmon.
- **Capability-gated:** `CAP_VEC` (shell default includes it); agent namespace also accepts `CAP_AGENT`.

## Blobstore

`kernel/blobstore.c` — block-backed object store starting at LBA 8192 (4 MiB), with:

- bump-allocated extents
- 32×4 KiB LRU page cache
- sync on PeakDisk save

PeakFS persistence itself is **streamed** (no fixed 512 KiB snapshot cap); see `peakdisk_save` / `vfs_export_ramdisk_size`.

## API

```
peakvec_embed_text(text, vec)
peakvec_upsert(ns, key, vec, meta)
peakvec_query(ns, query, topk, hits)
peakvec_delete(ns, key)
peakvec_count(ns)
```

Syscall `SYS_peakvec`:

| op | meaning |
|----|---------|
| 1 | upsert text (a1=key, a2=text) |
| 2 | query text (a1=text, a2=hits buf, a3=topk) |
| 3 | count |
| 4 | delete (a1=key) |

## Privacy

Cloud / remote embedders are **not** wired. Any future remote path must use `CAP_NET_CLIENT` + consent + audit and must never be the default.
