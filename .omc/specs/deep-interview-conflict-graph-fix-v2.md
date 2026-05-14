# Deep Interview Spec: Conflict Graph Replication Bug — Fix v2

## Metadata
- Interview ID: conflict-graph-fix-v2-2026-05-13
- Rounds: 0 (brownfield diagnosis via codebase analysis)
- Final Ambiguity Score: ~9%
- Type: brownfield / debugging
- Generated: 2026-05-13
- Threshold: 20%
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.97 | 35% | 0.3395 |
| Constraint Clarity | 0.92 | 25% | 0.230 |
| Success Criteria | 0.95 | 25% | 0.2375 |
| Context Clarity | 0.88 | 15% | 0.132 |
| **Total Clarity** | | | **0.939** |
| **Ambiguity** | | | **~9%** |

---

## Goal
Fix the conflict graph replication bug where nodes produce non-identical snapshots.
After the previous fix attempt (v1), the failure direction has **inverted**: host 1 now
has MORE edges than host 3 (previously host 3 had more). Both are manifestations of the
same root cause, which the v1 fix did not address.

---

## Root Cause

### One sentence
`removeMostRecentWriter()` erases the MRW map entry when a transaction is promoted, so
`getMostRecentWriterID()` returns `""` afterward — making the `!mrw` fallback branch
**unreachable** and all subsequent conflict detection for that data item incorrect.

### Detailed causal chain

1. **`most_recent_writer` stores `Transaction*`** (`graph.h:22`).
   `getMostRecentWriterID()` returns `it->second->getID()` and checks `!= nullptr`.

2. **`removeTransaction()` calls `removeMostRecentWriter(data_item)`** (`graph.cpp:175`).
   This **erases** the map entry entirely. After promotion:
   - `getMostRecentWriterID(item)` → `""` (key gone)
   - `mrw_id.empty()` is `true`
   - Code falls into the "no previous writer" branch — no edge created, no MRR update.

3. **The `else if (!mrw)` branch is unreachable** (`merger.cpp:215`).
   It requires `mrw_id` non-empty but `mrw == nullptr`. Because step 2 erases the entry,
   `mrw_id` is always `""` after promotion — this branch can never fire.
   The v1 fix commented it out, but even uncommented it would never run.

4. **Same erasure problem for MRR** (`graph.cpp:173`).
   `removeMostRecentReader()` is also called on promotion, removing reader IDs from
   tracking. Future writers miss edges to promoted readers.

5. **Why the direction flipped between test runs:**
   The original test involved SID=3 transactions (tx 26796, 26831). Host 3 received those
   in one large local batch — edges formed before promotion. Hosts 1/2 got them in small
   network chunks — promotions happened between chunks — edges missed on 1/2.
   The new test involves SID=1 transactions (tx 9626, etc.). Now host 1 is the local
   server — big local batch — edges formed. Host 3 gets SID=1 over network in chunks —
   promotions happen — edges missed on 3. Same race, different "home" server.

6. **v1 fix side-effect** (`merger.cpp:213`): the active-MRW path now calls BOTH
   `addNeighborOut()` (which already writes to `nodes_static`) AND `addNeighborOutStatic()`
   again. Because `neighbors_out` is `unordered_set<Transaction*>`, the duplicate insert
   is a no-op — no extra edges — but it is dead/redundant code.

### Bug taxonomy
| Dimension | Assessment |
|-----------|------------|
| Logic bug | Yes — design stores pointers that are invalidated on promotion |
| Implementation bug | Yes — v1 fix commented out the fallback without fixing the root cause |

---

## Constraints
- Fix must not corrupt the SCC/topological-sort algorithm running on `nodes` (active graph).
- Fix must not alter conflict-detection semantics (ps(R) filter stays intact).
- Edges to promoted transactions must appear in `nodes_static` snapshot.
- No changes to the partial-sequence gossip protocol.

## Non-Goals
- Changing what counts as a conflict.
- Changing promotion timing / deferring `getMergedOrders()`.

---

## Acceptance Criteria
- [ ] `get merged` reports **"All snapshots are identical"** for every host pair (1==2, 1==3, 2==3).
- [ ] `compare hashes` returns **"All hashes match"**.
- [ ] `Verification completed` still passes on all three servers (merged-order vs snapshot).
- [ ] No regression: merged-order queue still terminates.

---

## Required Code Changes

### 1. `graph.h` — change MRW storage type
**Line 22:** `std::unordered_map<DataItem, Transaction *> most_recent_writer`
→ `std::unordered_map<DataItem, std::string> most_recent_writer`

Rationale: strings don't go dangling when the Transaction object is moved into `merged`.

### 2. `graph.cpp` — `addMostRecentWriter`
Store the ID string, not the pointer:
```cpp
void Graph::addMostRecentWriter(DataItem item, Transaction* txn)
{
    most_recent_writer[item] = txn->getID();
}
```

### 3. `graph.cpp` — `getMostRecentWriterID`
Return string directly; drop the `!= nullptr` guard (strings can't be null):
```cpp
std::string Graph::getMostRecentWriterID(DataItem item)
{
    auto it = most_recent_writer.find(item);
    if (it != most_recent_writer.end())
        return it->second;
    return "";
}
```

### 4. `graph.cpp` — `removeTransaction` — remove MRW and MRR cleanup
Delete the calls to `removeMostRecentWriter` and `removeMostRecentReader` inside the
operations loop. IDs are now strings; they persist safely after promotion. Future writers
will overwrite the MRW entry via `addMostRecentWriter`. Future writes will clear stale
MRR entries via `clearMRRIds`.

### 5. `merger.cpp` — read-set `!mrw` branch — uncomment the fallback
Lines 215–219: uncomment both `addNeighborOutStatic` and `addMostRecentReader`:
```cpp
else if (!mrw)
{ // previous writer was promoted — record edge in static snapshot
    graph.addNeighborOutStatic(curr_txn->getID(), mrw_id);
    graph.addMostRecentReader(data_item, curr_txn->getID());
}
```
This now works because `mrw_id` is non-empty (string stored from step 2).

### 6. `merger.cpp` — remove duplicate `addNeighborOutStatic` in active-MRW path
Line 213: remove `graph.addNeighborOutStatic(curr_txn->getID(), mrw_id);`
`addNeighborOut()` already mirrors the edge into `nodes_static` internally.

---

## Why This Is Fully Correct

After changes 1–4, the MRW lifecycle becomes:
| Event | `most_recent_writer[X]` |
|-------|------------------------|
| T1 writes X | `"T1"` |
| T1 promoted | `"T1"` (not erased — string is safe) |
| T2 reads X | `mrw_id="T1"`, `mrw=nullptr` → `addNeighborOutStatic(T2,T1)` + MRR updated |
| T4 writes X | `mrw_id="T1"`, `mrw=nullptr`, MRR={T2} → edges via static/active |
| T4 updates MRW | `"T4"` (overwrites "T1") |

MRR lifecycle similarly: promoted reader IDs persist until the next `clearMRRIds` call
(triggered by the next write), so future writers still detect the conflict.

---

## Technical Context (Relevant Files)
| File | Role |
|------|------|
| `Caerus/Server/merger.cpp:190–263` | `insertAlgorithm()` — read/write-set conflict detection |
| `Caerus/Server/graph.cpp:142–190` | `removeTransaction()` — the MRW/MRR cleanup that must be removed |
| `Caerus/Server/graph.cpp:369–391` | `addMostRecentWriter` / `getMostRecentWriterID` — need updating |
| `Caerus/Server/graph.h:22` | MRW map type declaration |
| `Caerus/Server/transaction.h:30` | `neighbors_out` is `unordered_set<Transaction*>` — idempotent inserts |

---

## Interview Transcript
N/A — spec derived from direct codebase analysis (brownfield fast-path).
Previous interview transcript: `.omc/specs/deep-interview-conflict-replication-bug.md`
