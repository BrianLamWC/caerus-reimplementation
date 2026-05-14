# Handoff: static-graph-hash

Claude tasks are complete. This document gives Codex full context before starting its tasks.

---

## What was built (Claude's work)

### `Caerus/Server/graph.h` — new public method declared
```cpp
uint64_t computeStaticGraphHash() const;
```

### `Caerus/Server/graph.cpp` — new method implemented (after `buildSnapshotProto`)
- Acquires `snapshot_mtx` (same lock as `buildSnapshotProto`)
- Collects all keys from `nodes_static`, sorts them alphabetically
- For each node: hashes `tx_id`, then hashes each out-neighbor ID (also sorted)
- Uses `hashCombine()` from `utils.h` throughout
- Returns `uint64_t`

---

## Conventions decided

| Decision | Choice | Why |
|---|---|---|
| Where hash logic lives | `Graph::computeStaticGraphHash()` | Keeps `nodes_static` private; no getter needed |
| Lock | `snapshot_mtx` | Same lock `buildSnapshotProto` uses — correct for `nodes_static` access |
| Key sort order | `std::sort` on string keys | `unordered_map` iteration is non-deterministic |
| Neighbor sort order | `std::sort` on neighbor ID strings | `unordered_set<Transaction*>` is non-deterministic |
| Response proto message | Reuse `request::MergedOrderHash` | Has `node_id` (string) + `hash` (uint64) — exact shape needed, no new message required |
| `node_id` value | `std::to_string(my_id)` | `my_id` is the global `extern int32_t` from `utils.h`/`utils.cpp` |
| New request type number | `STATIC_GRAPH_HASH = 9` | Last used was `MERGED_HASH = 8` |
| New command name | `compare static` | Parallel to `compare hashes`; short and unambiguous |
| New global in main.cpp | `host_static_hash_map` | Parallel to `host_hash_map` |

---

## What Codex needs to do

See `codex-tasks-static-graph-hash.md` for the full task list with exact code to insert.

**Files to touch:**
1. `Caerus/proto/request.proto` — add `STATIC_GRAPH_HASH = 9` + regenerate
2. `Caerus/Server/merger.h` — declare `sendStaticGraphHashOnFd(int fd)`
3. `Caerus/Server/merger.cpp` — implement `sendStaticGraphHashOnFd()` (calls `graph.computeStaticGraphHash()`)
4. `Caerus/Server/client.cpp` — add `STATIC_GRAPH_HASH` dispatch in `handleClient()`
5. `Caerus/test/main.cpp` — add global, forward decls, `requestStaticGraphHashFromHost()`, `compareStaticHashes()`, `compare static` command

**Do NOT touch:**
- `graph.h`, `graph.cpp` (Claude already modified these)
- Any existing `MERGED` / `MERGED_HASH` / `get merged` / `compare hashes` code
- `proto/graph_snapshot.proto` (no new message needed — reuse `MergedOrderHash`)

---

## Key file locations

| Symbol | File | Line |
|---|---|---|
| `computeStaticGraphHash()` declaration | `Server/graph.h` | end of public section |
| `computeStaticGraphHash()` implementation | `Server/graph.cpp` | after `buildSnapshotProto` (~line 486) |
| `sendMergedHashOnFd()` (model for D3) | `Server/merger.cpp` | ~line 370 |
| `handleClient()` dispatch (model for D4) | `Server/client.cpp` | ~line 130 |
| `requestMergedHashFromHost()` (model for D5d) | `test/main.cpp` | ~line 575 |
| `compareHashes()` (model for D5e) | `test/main.cpp` | ~line 604 |
| `host_hash_map` global (model for D5a) | `test/main.cpp` | line 79 |
| `my_id` global | `Server/utils.h` | line 76 (extern), `utils.cpp` line 17 (definition) |
| `hashCombine()` | `Server/utils.h` | line 82 |
| `MergedOrderHash` proto | `proto/graph_snapshot.proto` | line 17 |
| `RequestRecipient` enum | `proto/request.proto` | line 27 |

---

## Build notes

After editing `request.proto`, regenerate with:
```bash
cd Caerus/proto
protoc --cpp_out=. request.proto
```

The project uses a Makefile in `Caerus/Server/`. Build with:
```bash
cd Caerus/Server && make
```
and test client with:
```bash
cd Caerus/test && make
```
(check the Makefiles for exact targets if these differ)
