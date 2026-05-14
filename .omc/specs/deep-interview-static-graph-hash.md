# Deep Interview Spec: Static Graph Hash Command

## Metadata
- Interview ID: di-graph-hash-2026-05-14
- Rounds: 4
- Final Ambiguity Score: 17.8%
- Type: brownfield
- Generated: 2026-05-14
- Threshold: 20%
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.90 | 35% | 0.315 |
| Constraint Clarity | 0.80 | 25% | 0.200 |
| Success Criteria | 0.75 | 25% | 0.188 |
| Context Clarity | 0.80 | 15% | 0.120 |
| **Total Clarity** | | | **0.823** |
| **Ambiguity** | | | **17.8%** |

## Goal
Add a new non-destructive "compare static" command that requests a hash of each server's static graph adjacency list (`nodes_static`) and compares them across all nodes — without touching any existing `get merged` or `compare hashes` logic.

## Constraints
- **Non-destructive**: existing commands (`get merged`, `compare hashes`, `send`, `test`) must be unchanged
- **Additive only**: new protobuf message type, new server handler, new client command
- **Scope**: hash only the static graph adjacency (`nodes_static`), NOT the merged order (that's already covered by `compare hashes`)
- **Deterministic hash**: `nodes_static` is an `unordered_map` so iteration order is undefined — must sort keys before hashing
- **Reuse existing hash utility**: use `hashCombine()` from `utils.h` (boost-style FNV combine), same as `sendMergedHashOnFd()`

## Non-Goals
- Replacing or modifying `MERGED` / `MERGED_HASH` / `compareSnapshots()` / `verfiyMergedOrderFromHost()`
- Hashing merged_order (already done by `compare hashes`)
- Combining adj hash + merged_order hash into one value (out of scope for now)
- Caching or incremental hash updates

## Acceptance Criteria
- [ ] New `Request::RecipientType::STATIC_GRAPH_HASH` enum value in `request.proto`
- [ ] New `StaticGraphHash` protobuf message (or reuse `MergedOrderHash`) with `node_id` (string) and `hash` (uint64) fields
- [ ] New `sendStaticGraphHashOnFd(int server_id, int fd)` in `merger.cpp` that: sorts `nodes_static` keys, hashes each (tx_id + sorted out-neighbors) via `hashCombine()`, sends the result framed
- [ ] `client.cpp` routes `STATIC_GRAPH_HASH` requests to the new handler
- [ ] `main.cpp`: new `compare static` command that connects to all servers, calls the new request, and prints match/mismatch (same pattern as `compare hashes`)
- [ ] Running `compare static` when all servers agree prints "All static graph hashes match."
- [ ] Running `compare static` when servers differ prints which server IDs disagree and their hash values
- [ ] Existing `get merged`, `compare hashes`, `send <file>`, `test <n>` commands still work identically

## Assumptions Exposed & Resolved
| Assumption | Challenge | Resolution |
|------------|-----------|------------|
| "We need the full adj for verification" | Does compareSnapshots() diff actually help? | No — at scale the diff is too noisy to be actionable. Hash is sufficient. |
| "Replacing get merged with a hash" | Would that break verfiyMergedOrderFromHost? | Resolved by making this additive — get merged stays intact |
| "Hash merged_order + adj together" | What scope should the new hash cover? | adj-only; merged_order is already covered by existing compare hashes |

## Technical Context

### Relevant files
- `Caerus/Server/merger.cpp:338` — `sendMergedOrdersOnFd()` (full snapshot, unchanged)
- `Caerus/Server/merger.cpp:370` — `sendMergedHashOnFd()` (merged_order hash, unchanged)
- `Caerus/Server/graph.cpp:452` — `buildSnapshotProto()` (builds full snapshot; NOT needed for adj hash)
- `Caerus/Server/graph.h:22` — `nodes_static` is `unordered_map<string, unique_ptr<Transaction>>`
- `Caerus/Server/utils.h:83` — `hashCombine()` FNV-style combine (reuse this)
- `Caerus/Server/client.cpp:130` — `handleClient()` dispatch (add new case here)
- `Caerus/proto/request.proto:34` — `RecipientType` enum (add `STATIC_GRAPH_HASH`)
- `Caerus/proto/graph_snapshot.proto:17` — `MergedOrderHash` (can reuse or create parallel message)
- `Caerus/test/main.cpp:367` — `compareHashes()` (model the new command on this pattern)
- `Caerus/test/main.cpp:575` — `requestMergedHashFromHost()` (model the new request function on this)

### Key implementation note
`nodes_static` is an `unordered_map` — iteration order is non-deterministic. To get a stable hash:
1. Collect all keys into a `std::vector<std::string>`, sort it
2. For each key in sorted order, hash `tx_id`, then hash each out-neighbor ID (sorted)
3. Combine with `hashCombine()` throughout

`Transaction::getOutNeighborIDs()` (or equivalent) should also be iterated in sorted order.

## Ontology (Key Entities)
| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| StaticGraph | core domain | nodes_static (unordered_map) | snapshots MergedOrder state |
| MergedOrder | core domain | merged queue (QueueTS<Transaction>) | produced from StaticGraph after SCC |
| GraphSnapshot | supporting | adj + merged_order (proto) | serializes both StaticGraph and MergedOrder |
| ConsistencyCheck | supporting | host_hash_map, host_txn_neighbors_map | compares outputs across servers |
| StaticGraphHashCommand | new feature | new proto message, new request type | sends hash of StaticGraph adj only |

## Ontology Convergence
| Round | Entity Count | New | Changed | Stable | Stability Ratio |
|-------|-------------|-----|---------|--------|----------------|
| 1 | 3 | 3 | - | - | N/A |
| 2 | 4 | 1 | 0 | 3 | 75% |
| 3 | 4 | 1 | 0 | 3 | 75% |
| 4 | 5 | 1 | 0 | 4 | 80% |

## Interview Transcript
<details>
<summary>Full Q&A (4 rounds)</summary>

### Round 1
**Q:** What specific problem or limitation in the current approach prompted this idea?
**A:** Design principle — sending the full graph feels like a leaky abstraction; the client shouldn't need all raw adjacency data just to check consistency.
**Ambiguity:** 56.5% (Goal: 0.60, Constraints: 0.25, Criteria: 0.20, Context: 0.75)

### Round 2
**Q:** When two servers return different hashes, what should the system be able to do next?
**A:** The detailed diff isn't actionable — when mismatches happen (usually with many transactions) the diff shows too many differences to be meaningful. Questioned whether full diff capability is worth preserving.
**Ambiguity:** 45.5% (Goal: 0.70, Constraints: 0.40, Criteria: 0.35, Context: 0.75)

### Round 3
**Q:** How important is `verfiyMergedOrderFromHost()` (the structural cross-check between merged order and static graph)?
**A:** Rather than remove it, make this a separate NEW command so existing code is untouched.
**Ambiguity:** 26.5% (Goal: 0.85, Constraints: 0.75, Criteria: 0.55, Context: 0.75)

### Round 4
**Q:** Should the new command hash adj only, combined adj+merged_order, or just adj for now?
**A:** Static graph (adj) only.
**Ambiguity:** 17.8% (Goal: 0.90, Constraints: 0.80, Criteria: 0.75, Context: 0.80)

</details>
