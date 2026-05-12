# Deep Interview Spec: Conflict Graph Replication Bug

## Metadata
- Interview ID: conflict-replication-2026-05-12
- Rounds: 3 (+ pseudocode clarification)
- Final Ambiguity Score: ~9%
- Type: brownfield / debugging
- Generated: 2026-05-12
- Threshold: 20%
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.95 | 35% | 0.3325 |
| Constraint Clarity | 0.90 | 25% | 0.225 |
| Success Criteria | 0.85 | 25% | 0.2125 |
| Context Clarity | 0.95 | 15% | 0.1425 |
| **Total Clarity** | | | **0.9125** |
| **Ambiguity** | | | **~9%** |

---

## Goal
All servers must produce **identical conflict graph snapshots** (identical `adj` edge sets for every transaction). Currently host 3 consistently has more outgoing edges than hosts 1 and 2 for the same transaction IDs.

---

## Root Cause (Evidence-Based)

### The bug in one sentence
`getMergedOrders()` fires after every per-sid batch, promoting transactions **before** conflicting transactions from later batches arrive — the promoted transaction disappears from `nodes`, so `getNode()` returns `nullptr`, the `addNeighborOut` call is skipped, and the edge is never written to `nodes_static` (the snapshot).

### Detailed causal chain

1. **`getNode()` only searches `nodes`** (`graph.cpp:63-67`).  
   When `removeTransaction()` promotes a transaction to the merged order it is erased from `nodes` but kept in `nodes_static`.

2. **`addNeighborOut` is guarded by `mrw != nullptr`** (`merger.cpp:211, 247`).  
   If the MRW (or any MRR) has been promoted, the condition is false → the call never fires → no edge in `nodes_static`.

3. **`getMergedOrders()` fires after every single-sid batch** (`merger.cpp:285-290`).  
   A transaction with `expected_regions = {R}` (only touches one region's primary set) is immediately complete and becomes promotable the instant its SCC is a sink.

4. **Batch-size asymmetry between home server and remote servers.**  
   - Server 3 delivers its own transactions (`sid=3`) in one large local batch — tx 26796 and tx 26831 land in the same `pop()`, both are in `nodes` when 26831's conflict detection runs → edge created.  
   - Server 1 receives `sid=3` transactions over the network in smaller chunks; `getMergedOrders()` fires between chunks → 26796 is promoted (it is a sink, immediately complete) → when 26831 arrives in the next chunk, `getNode("26796")` returns `nullptr` → edge never created.

5. **The cascade in the output** — tx 26897 on host 3 has extra edges to 26831, 26862, 26871 precisely because those transactions themselves carry extra edges on host 3 (the graph stays "denser" locally, so the SCC/topological promotion sequence differs, compounding further missed edges on host 1).

### Same bug, three call sites
| Code path | File | Lines | Guard condition |
|-----------|------|-------|-----------------|
| ReadSet: MRW active check | merger.cpp | 211 | `mrw and mrw->getID() != curr_txn->getID()` |
| WriteSet: MRR active check | merger.cpp | 254–256 | `read_txn != nullptr` |
| WriteSet: MRW (no-reader path) | merger.cpp | 247–248 | `mrw != nullptr` |

All three skip edge creation when the conflicting transaction has been promoted.

---

## Constraints
- The fix must not corrupt the SCC/topological-sort algorithm that runs on `nodes` (active graph).
- The fix must not alter the semantics of what constitutes a conflict (the `ps(R)` filter must remain intact).
- Edges to promoted transactions must still appear in the `nodes_static` snapshot.

## Non-Goals
- Changing the conflict-detection logic (what counts as a conflict) — the algorithm is correct, only the timing/persistence is broken.
- Changing the partial-sequence gossip protocol.

---

## Acceptance Criteria
- [ ] Running `get merged` in the test client reports **"All snapshots are identical"** for every host pair (1==2, 1==3, 2==3).
- [ ] Running `compare hashes` returns **"All hashes match"**.
- [ ] `Verification completed` (the existing per-server merged-order vs snapshot cross-check) still passes on all three servers.
- [ ] No regression: the merged-order queue still terminates (no infinite loop / stall waiting for promotions).

---

## Proposed Fix

### Option A — Defer `getMergedOrders()` until ready queue is drained (simpler)

In `merger.cpp` `insertAlgorithm()`, replace the unconditional call:
```cpp
int removed = graph.getMergedOrders();
```
with a guarded call that only promotes when no other sid has queued work:
```cpp
{
    std::lock_guard<std::mutex> g(ready_mtx);
    if (!ready_q.empty()) {
        lk.lock();
        continue;   // skip getMergedOrders, re-enter wait loop
    }
}
int removed = graph.getMergedOrders();
lk.lock();
```
**Trade-off:** Increases promotion latency (promotions only happen in "quiet" moments). Does not fully eliminate the race if transactions arrive in separate network packets after the queue drains.

### Option B — Add edges to promoted transactions in `nodes_static` directly (more robust)

Add a method `Graph::addNeighborOutStatic(const string& from_id, const string& to_id)` that writes the edge only to `nodes_static` (not `nodes`), bypassing the active graph. Then in each of the three call sites above, when `getNode()` returns `nullptr` but `getMostRecentWriterID()`/MRR returns a non-empty ID, fall back to:
```cpp
graph.addNeighborOutStatic(curr_txn->getID(), mrw_id);
```
This ensures the snapshot always captures the edge regardless of promotion timing, without corrupting the SCC computation on the active graph.

**Trade-off:** More code changes, but fully correct regardless of batch-size or network chunking. This is the recommended approach.

---

## Assumptions Exposed & Resolved
| Assumption | Challenge | Resolution |
|------------|-----------|------------|
| Primary-set filter uses the current server's ID | Pseudocode shows ps(R) where R = source region, not current server | Implementation correctly uses `sid` (source region) — not the bug |
| All nodes have identical graphs by design | Design intent was confirmed | All nodes must produce identical graphs — host 3 is more correct, hosts 1/2 are missing edges |
| The fix requires a new replication mechanism | Option: remove primary-set filter | Actual fix: fix the MRW/MRR promotion-race; filter is correct |

---

## Technical Context (Relevant Files)
| File | Role |
|------|------|
| `Caerus/Server/merger.cpp:93-294` | `insertAlgorithm()` — conflict detection, the three buggy call sites |
| `Caerus/Server/graph.cpp:63-86` | `getNode()` (active only), `addNeighborOut()` (writes to both `nodes` and `nodes_static`) |
| `Caerus/Server/graph.cpp:378-464` | `getMergedOrders()` — SCC + topological sort + promotion |
| `Caerus/Server/graph.h:22-23` | `nodes` vs `nodes_static` distinction |
| `Caerus/Server/transaction.h:34-35` | `expected_regions` / `seen_regions` — completion gate |
| `Caerus/test/main.cpp:633-734` | `compareSnapshots()` — the test that surfaces the bug |

---

## Interview Transcript
<details>
<summary>Full Q&A (3 rounds + pseudocode)</summary>

### Round 1
**Q:** Should all nodes have identical conflict graphs, or is per-node-local-primary-only acceptable?
**A:** All nodes → identical graph
**Ambiguity:** ~39% (Goal: 0.80, Constraints: 0.40, Criteria: 0.55, Context: 0.70)

### Round 2
**Q:** Should the fix remove/relax the primary-copy filter, or add a separate broadcast mechanism?
**A:** Remove / relax the primary-copy filter (let each server compute full graph from gossip data)
**Ambiguity:** ~28% (Goal: 0.80, Constraints: 0.75, Criteria: 0.55, Context: 0.75)

### Round 3
**Q:** (Rejected / user provided pseudocode instead — see below)

### Pseudocode clarification
User shared `Algorithm 1` (image.png). Key finding: `ps(R)` in the pseudocode refers to the **source region R's primary set**, not the current server's. Implementation correctly uses `sid` for this. The real bug is not in the filter but in the MRW/MRR promotion race described above.

</details>
