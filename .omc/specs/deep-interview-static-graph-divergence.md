# Deep Interview Spec: Static Graph Divergence Across Nodes

## Metadata
- Interview ID: di-static-graph-divergence
- Rounds: 4
- Final Ambiguity Score: 15%
- Type: brownfield
- Generated: 2026-05-15
- Threshold: 20%
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.92 | 35% | 0.322 |
| Constraint Clarity | 0.78 | 25% | 0.195 |
| Success Criteria | 0.78 | 25% | 0.195 |
| Context Clarity | 0.92 | 15% | 0.138 |
| **Total Clarity** | | | **0.850** |
| **Ambiguity** | | | **15%** |

## Goal
Determine why `computeStaticGraphHash()` returns different values on different nodes after all transactions for a test run (1M txns) have finished processing and `getMergedOrders()` has been called on each node.

## Root Cause (Confirmed)

**Primary: `most_recent_writer` is order-dependent, not globally consistent.**

In `merger.cpp:insertAlgorithm()`, dependency edges are added based on who the "most recent writer" of a data item is at the moment a transaction is processed. This state lives in `Graph::most_recent_writer` (graph.h:24), an `unordered_map<DataItem, std::string>` updated by `addMostRecentWriter()` (graph.cpp:533):

```cpp
most_recent_writer[item] = txn->getID();  // simply overwrites with latest processed
```

Each node processes partial sequences from peer nodes in **network arrival order** — its own sequences first, then peers' in whatever order they arrive. Two nodes that receive the same transactions in a different order build different `most_recent_writer` states, which means:

- Node A processes txnA (writes key=X), then txnB (writes key=X):
  `most_recent_writer[X] = txnA` → then `= txnB` → edge: txnB depends on txnA
- Node B processes txnB first, then txnA:
  `most_recent_writer[X] = txnB` → then `= txnA` → edge: txnA depends on txnB

These are **opposite dependency edges** for the same pair of transactions.

**Secondary: `getMergedOrders()` is called INSIDE the batch loop (merger.cpp:291).**

After each server's batch is processed, promoted transactions are removed from the dynamic graph and their `most_recent_writer` entries are erased (graph.cpp:544). Whether a prior writer has already been promoted — and thus its entry erased — when a later transaction looks it up depends on inter-node promotion timing, creating further non-determinism.

## Constraints
- All nodes receive all partial sequences (broadcast by `partial_sequencer.cpp`)
- The static graph (`nodes_static` in graph.h) is built incrementally as transactions are added — it is not recomputed at the end
- `computeStaticGraphHash()` (graph.cpp:487) sorts neighbor lists before hashing, so ordering within adjacency lists is not the issue — the edges themselves differ
- Test scenario: 1M transactions sent to cluster, check hash after full processing

## Non-Goals
- Changing how partial sequences are broadcast
- Changing the SCC / Tarjan algorithm in `getMergedOrders()`
- Modifying the external interface of `computeStaticGraphHash()`

## Acceptance Criteria
- [ ] `computeStaticGraphHash()` returns identical values on all N nodes after all transactions for a given epoch finish processing
- [ ] The fix passes the 1M-transaction cluster test with hash agreement
- [ ] No new data races introduced (existing mutex coverage in graph.cpp must be maintained)
- [ ] `insertAlgorithm()` still produces a valid topological order via `getMergedOrders()`

## Assumptions Exposed & Resolved
| Assumption | Challenge | Resolution |
|------------|-----------|------------|
| Graph structure difference = edge difference | Could have been same edges, different serialization order | Confirmed: structurally different adjacency lists |
| All nodes receive same transactions | Could have been a delivery issue | Confirmed: same txn set, processed in different order |
| most_recent_writer is globally consistent | Contrarian: could be intentionally local | Confirmed bug: should be globally consistent for identical graphs |

## Technical Context

### Key Files
| File | Role |
|------|------|
| `Caerus/Server/graph.h:24-25` | `most_recent_writer` and `most_recent_readers` maps |
| `Caerus/Server/graph.cpp:525-544` | `addMostRecentWriter()` / `promoteNode()` erases entry |
| `Caerus/Server/graph.cpp:551-565` | `getMostRecentWriterID()` — pure map lookup |
| `Caerus/Server/merger.cpp:93-300` | `insertAlgorithm()` — the non-deterministic insertion loop |
| `Caerus/Server/merger.cpp:194-225` | Read Set ∩ Primary Set edge detection |
| `Caerus/Server/merger.cpp:228-268` | Write Set ∩ Primary Set edge detection |
| `Caerus/Server/merger.cpp:291` | `getMergedOrders()` called mid-loop — promotion interleaving |
| `Caerus/Server/graph.cpp:487-512` | `computeStaticGraphHash()` — symptom observation point |

### Fix Directions (in order of invasiveness)

**Option 1 — Deterministic "most recent" by total transaction order (least invasive)**
Replace the naive "last processed wins" with a deterministic comparator. When two transactions compete to be the "most recent writer" for a data item, always keep the one with the globally higher rank (e.g., compare by `partial_sequence_epoch`, then `server_id`, then sequence number within batch). Change `addMostRecentWriter` to only update if the new transaction ranks higher than the current entry.

**Option 2 — Sort all batches before insertAlgorithm() runs**
Collect all partial sequences for a full epoch across all servers, sort them by a globally consistent key (epoch + server_id + intra-batch index), then run `insertAlgorithm()` over the sorted list. All nodes apply the same sort → same processing order → same `most_recent_writer` state.

**Option 3 — Two-pass graph construction**
Pass 1: ingest all transactions, build read/write sets. Pass 2: sort by deterministic key, detect dependencies in sorted order. This decouples ingestion from edge detection entirely.

Option 1 is the minimal change; Option 2 is the clearest correctness argument.

## Ontology (Key Entities)
| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| Node (server) | core domain | server_id, partial_sequences | processes Transactions, builds StaticGraph |
| Transaction | core domain | id, read_set, write_set, order, random_stamp | processed by Node, becomes vertex in StaticGraph |
| StaticGraph (nodes_static) | core domain | adjacency list per txn_id | built by Node during insertAlgorithm |
| most_recent_writer | supporting state | unordered_map<DataItem, txn_id> | maintained per-Node, source of divergence |
| PartialSequence | supporting | server_id, epoch, transactions[] | broadcast by partial_sequencer to all Nodes |
| DataItem | external concept | key, primary_copy_id | accessed by Transactions |

## Ontology Convergence
| Round | Entity Count | New | Changed | Stable | Stability Ratio |
|-------|-------------|-----|---------|--------|----------------|
| 1 | 4 | 4 | - | - | N/A |
| 2 | 5 | 1 | 0 | 4 | 80% |
| 3 | 5 | 0 | 0 | 5 | 100% |
| 4 | 6 | 1 | 0 | 5 | 83% (most_recent_writer added as named entity) |

## Interview Transcript
<details>
<summary>Full Q&A (4 rounds)</summary>

### Round 1
**Q:** How are you detecting that nodes produce different static graphs?
**A:** computeStaticGraphHash() returns different values on different nodes
**Ambiguity:** 45% (Goal: 0.65, Constraints: 0.30, Criteria: 0.55, Context: 0.75)

### Round 2
**Q:** At what point in the algorithm are all nodes expected to produce the same static graph hash?
**A:** All nodes should eventually build the same static graph; tested by sending 1M txns to cluster, then running getMergedOrders() after they all finish processing
**Ambiguity:** 38% (Goal: 0.70, Constraints: 0.55, Criteria: 0.65, Context: 0.75)

### Round 3
**Q:** When two nodes produce different hashes, is it because they have different edges in nodes_static, or the same edges in a different order?
**A:** Different actual edges — the adjacency lists contain different dependencies
**Ambiguity:** 28% (Goal: 0.80, Constraints: 0.60, Criteria: 0.70, Context: 0.80)

### Round 4 (Contrarian Mode)
**Q:** In merger.cpp's insertAlgorithm(), when it finds the 'most recent writer' for a data dependency — how is that determined?
**A:** It looks up the most_recent_writer unordered map for the most recent writer of a data item
**Ambiguity:** 15% (Goal: 0.92, Constraints: 0.78, Criteria: 0.78, Context: 0.92)
*(Code confirmed: most_recent_writer[item] = txn->getID() — pure overwrite, no ordering guarantee)*

</details>
