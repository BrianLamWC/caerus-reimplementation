# Deep Interview Spec: Efficient Graph Consistency Verification

## Metadata
- Interview ID: caerus-merger-snapshot
- Rounds: 4
- Final Ambiguity Score: 17%
- Type: Brownfield
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|---|---|---|---|
| Goal Clarity | 0.85 | 35% | 0.30 |
| Constraint Clarity | 0.75 | 25% | 0.19 |
| Success Criteria | 0.85 | 25% | 0.21 |
| Context Clarity | 0.85 | 15% | 0.13 |
| **Total Clarity** | | | **0.83** |
| **Ambiguity** | | | **17%** |

---

## Goal
Replace the single always-full-graph endpoint with a **two-phase verification protocol**:
1. **Fast path (normal case):** test client requests a hash of the merged order from each server and compares hashes — tiny payload, O(1) comparison
2. **Slow path (on failure or debugging):** test client requests the full GraphSnapshot as today — full adj + merged_order for structural diff

---

## Constraints
- Must work for current test/debug usage (manually triggered from `test/main.cpp`)
- Must not degrade at scale (thousands of transactions — hash stays O(1), full graph stays on-demand)
- Both adj consistency and merged_order consistency need to be verifiable, but separately
- The existing `MERGED` full-graph endpoint should stay untouched (used as fallback)

## Non-Goals
- Automatic continuous consistency monitoring (not needed yet)
- Removing the existing `sendMergedOrdersOnFd` / `MERGED` path (keep it as fallback)
- Persisting hashes across runs

---

## Acceptance Criteria
- [ ] A new request type (`MERGED_HASH`) exists in `request.proto`
- [ ] Server computes a deterministic hash of its `merged_order` sequence (ordered transaction IDs) and returns it
- [ ] `test/main.cpp` has a `compareHashes()` flow: request `MERGED_HASH` from all servers, compare — report pass/fail
- [ ] If hashes differ, `test/main.cpp` falls back to requesting full `MERGED` snapshot from diverging servers for debugging
- [ ] Hash comparison is correct: same merged order → identical hash on all servers (hash is order-sensitive)
- [ ] Existing `MERGED` full-graph request still works unchanged

---

## Technical Context

### Current flow (merger.cpp:332-362)
`sendMergedOrdersOnFd(fd)` → `graph.buildSnapshotProto(snap)` → serializes full `GraphSnapshot` (adj + merged_order) → length-prefixed write

### Current test flow (test/main.cpp)
`requestMergedOrderFromHost(server_id)` → sends `MERGED` request → receives full `GraphSnapshot` → populates `host_txn_neighbors_map` (adj) and `host_merged_order_map` (merged_order) → `compareSnapshots()` diffs adj across servers

### What to add

**Proto (`request.proto`):**
- Add `MERGED_HASH` to the `Request.Recipient` enum

**Proto (`graph_snapshot.proto` or inline in `request.proto`):**
- Add a `MergedOrderHash` message: `{ string hash = 1; int32 server_id = 2; }`

**Server (`merger.cpp` + `merger.h`):**
- Add `sendMergedHashOnFd(int fd)`: compute SHA-256 (or simpler FNV/xxHash) over the ordered sequence of transaction IDs in `merged` (the linearized output), serialize as `MergedOrderHash`, send length-prefixed

**Server (`client.cpp`):**
- In `handleClient`, add `MERGED_HASH` branch → calls `merger->sendMergedHashOnFd(connfd)`

**Test (`test/main.cpp`):**
- Add `requestMergedHashFromHost(server_id)` → sends `MERGED_HASH`, receives and stores hash
- Add `compareHashes()` → compares hashes across all servers, prints pass/fail
- Update the interactive menu to offer "compare hashes" (fast) and "compare full graph" (slow/debug) as separate commands

### Hash computation note
The merged order in `graph.cpp` is stored in the `merged` deque/vector (the linearized output). The hash should be computed over transaction IDs **in order** — e.g. concatenate IDs as a string then SHA-256, or use an incremental hash. The hash must be deterministic and order-sensitive.

---

## Assumptions Exposed & Resolved
| Assumption | Challenge | Resolution |
|---|---|---|
| "Efficient" = smaller payload | What specifically is inefficient? | All three: payload size, redundant resends, and comparison accuracy |
| Always need full graph | What if you only need full graph on failure? | Two-phase is fine: hash normally, full graph only on divergence |
| Adj comparison is the right check | Does the full graph actually prove correctness? | Both adj and merged_order should be verifiable, but separately |

---

## Interview Transcript
<details>
<summary>Full Q&A (4 rounds)</summary>

### Round 1
**Q:** Is this graph comparison purely a testing/debugging tool, or does it need to scale?
**A:** Both eventually — test-only now but should not break down at scale
**Ambiguity:** 47%

### Round 2
**Q:** Which property bothers you most — payload size, redundant resends, or comparison accuracy?
**A:** All of the above
**Ambiguity:** 36%

### Round 3
**Q:** What do you fundamentally need to prove is identical across servers — merged order only, full adj, or both separately?
**A:** Both, but separately
**Ambiguity:** 28%

### Round 4 (Contrarian)
**Q:** Would a two-phase approach satisfy you — hash for the normal fast path, full graph only on failure?
**A:** Yes, that works
**Ambiguity:** 17% ✓

</details>
