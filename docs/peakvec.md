# PeakVec — local vector index

PeakVec is a Peak-authored, in-guest vector index for session/workspace context. It is **not** Qdrant/Pinecone; it is a system primitive beside VFS and peak-agent.

## Design

- **Local-only by default.** Embeddings are hashed n-grams (`peakvec_embed_text`) — no model weights, no network.
- **Slim kernel, large data on disk.** Index pages through the [blobstore](#blobstore) LRU cache (128 KiB). Total size scales with the block device.
- **Cosine search** over int16 vectors (`PEAKVEC_DIM=64`). Norms are cached; the query is normalized once. When a namespace has ≥64 live entries, an IVF-lite coarse bucket (argmax dimension) probes first, then falls back to the remainder. Namespaces are honored on upsert/query/delete/count (RAM tags; default `agent`). Last query duration is published to sysmon (`peakvec_us`).
- **Capability-gated:** `CAP_VEC` (shell default includes it); agent namespace also accepts `CAP_AGENT`.

## Blobstore

`kernel/blobstore.c` — block-backed object store starting at LBA 8192 (4 MiB), with:

- bump-allocated extents (grow-in-place only for the tip object)
- 48×4 KiB LRU page cache (override `BLOBSTORE_CACHE_PAGES` in host tests)
- sync on PeakDisk save

PeakFS persistence itself is **streamed** (no fixed 512 KiB snapshot cap); see `peakdisk_save` / `vfs_export_ramdisk_size`.

### VFS large files

When blobstore is available, `vfs_create_blob_file` / `vfs_bind_blob` store file bodies on disk (`vfs_node.blob_id`) instead of the heap. Ranged I/O goes through `vfs_read_at` / `vfs_write_at`; PeakFS export materializes blob bytes through the LRU cache (streaming export is future work).

## Limits (honest)

| limit | value | notes |
|-------|-------|-------|
| `PEAKVEC_DIM` | 64 | hashed n-gram embedder |
| `PEAKVEC_MAX_ENTRIES` | 4096 | per index; namespaces share the table |
| `PEAKVEC_TOPK_MAX` | 8 | query cap |
| IVF-lite probe | ≥64 live entries | 16 coarse buckets; remainder scanned |
| Blobstore objects | 256 | bump allocator; 256 MiB data cap |
| RAM index fallback | `/var/peak/vec` | used when blockdev absent |

PeakVec prefers blobstore on fresh init (`peakvec_init`); a tiny VFS pointer file (`blob:<id>`) remains for discoverability.

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
