# Handoff: Claude Tasks → Codex Tasks

## Hash Algorithm

**Decision: `std::hash<string>` is acceptable. Do NOT use FNV-1a.**

Rationale: All servers are the same compiled binary, running on the same machine, in the same session. `std::hash<string>` is implementation-defined but produces identical results within a single binary run on a single platform. The hash only needs to be consistent across processes in the same session — it does not need to survive restarts or work cross-platform. The existing `hashCombine` utility in `utils.h` already uses `std::hash<T>` with boost-style mixing; the merged hash should follow the same approach.

Implementation: Iterate over the merged order vector (sequence of transaction ID strings), feeding each `tx_id` string into `hashCombine` with a running seed of 0. Return the final `size_t` seed as a `uint64_t` in the proto.

## Proto Shape

**Decision: Option A — new `MergedOrderHash` message in `graph_snapshot.proto`.**

Rationale: `graph_snapshot.proto` already owns all snapshot/state messages (`GraphSnapshot`, `VertexAdj`). Mixing hash data into `request.proto` would conflate transport/routing concerns with state data. A standalone message is cleaner than a field on `GraphSnapshot` because it is a separate request/response pair with its own recipient enum value.

Exact proto definition to add to `/Users/brianlam/SocketAdventures/Caerus/proto/graph_snapshot.proto`:

```protobuf
message MergedOrderHash {
  required string node_id = 1;   // server ID that produced the hash
  required uint64 hash = 2;      // std::hash result over merged order tx_ids
}
```

Also add to `request.proto` in the `RequestRecipient` enum:

```protobuf
MERGED_HASH = 8;
```

(Next available value after `MERGED = 7`.)

## Fallback Behavior

**Decision: Option B — print a message telling the user to run full compare manually.**

Rationale: The existing command loop in `handleCommand()` in `test/main.cpp` uses simple string-match dispatch. All commands run to completion, print results inline to stdout, and return — there are no interactive [y/n] prompts anywhere in the codebase. `compareSnapshots()` follows this same pattern: it prints diff details and returns. The new `compareHashes()` function should follow the same pattern: print which servers have mismatching hashes and print a line like `"Hashes differ — run 'get merged' for full comparison."`, then return.

## Canonical Names

| Concept | Final Name | Notes |
|---|---|---|
| Enum value (RequestRecipient) | `MERGED_HASH` | Mirrors `MERGED = 7`, value `= 8` |
| Proto message | `MergedOrderHash` | In `graph_snapshot.proto` |
| Merger method | `sendMergedHashOnFd` | Mirrors `sendMergedOrdersOnFd` — note plural "Orders" vs singular for hash |
| Test request function | `requestMergedHashFromHost` | Mirrors `requestMergedOrderFromHost` |
| Test compare function | `compareHashes` | Mirrors `compareSnapshots` pattern |
| Menu command string | `"compare hashes"` | Follows `"get merged"` style — lowercase, no parens |
| Menu label (display) | `compare hashes (fast)` | For any help/usage text |

**Naming rationale:** `sendMergedOrdersOnFd` uses plural "Orders" — the new hash method is singular because it returns one scalar hash, so `sendMergedHashOnFd` (no plural). All other names follow the existing `*MergedOrder*` → `*MergedHash*` substitution pattern.

## Notes for Codex

1. **Server side (merger.h / merger.cpp):**
   - Add `void sendMergedHashOnFd(int fd);` to the `Merger` class in `merger.h`.
   - Implement it in `merger.cpp`: compute hash by iterating `graph.getMergedOrder()` (or equivalent accessor), calling `hashCombine(seed, tx.id)` for each transaction in order. Serialize a `MergedOrderHash` proto and send framed (same `writeNBytes` + 4-byte big-endian length prefix as `sendMergedOrdersOnFd`).

2. **Server side (client.cpp):**
   - In `handleClient()`, add an `else if` branch for `request::Request::MERGED_HASH` that calls `merger->sendMergedHashOnFd(connfd)` and `continue`s, mirroring the existing `MERGED` branch exactly.

3. **Test side (test/main.cpp):**
   - Add `void requestMergedHashFromHost(int server_id, int fd)` — open connection, send `Request{recipient=MERGED_HASH}`, recv `MergedOrderHash` proto, store in a `std::map<int32_t, uint64_t> host_hash_map`.
   - Add `void compareHashes()` — iterate `host_hash_map`, compare all values to the first. On mismatch, print which server IDs differ and print `"Hashes differ — run 'get merged' for full comparison."`. On match, print `"All hashes match."`.
   - Add `"compare hashes"` branch in `handleCommand()`: connect to all servers, call `requestMergedHashFromHost` for each, then call `compareHashes()`. Follows the `"get merged"` block structure exactly.

4. **Proto files:**
   - Edit `graph_snapshot.proto`: add `MergedOrderHash` message (see above).
   - Edit `request.proto`: add `MERGED_HASH = 8` to `RequestRecipient` enum.
   - Regenerate proto bindings (`make proto` or equivalent) before compiling.

5. **Hash seed:** Start seed at `0`, not at the merged order size or any other value, to keep it simple and reproducible within a session.
