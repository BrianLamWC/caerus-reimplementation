# Deep Interview Spec: Merged Order Hash Mismatch Root Cause

## Metadata
- Interview ID: di-hash-debug-001
- Rounds: 3
- Final Ambiguity Score: 13.1%
- Type: brownfield
- Generated: 2026-05-11
- Threshold: 20%
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.92 | 35% | 0.32 |
| Constraint Clarity | 0.75 | 25% | 0.19 |
| Success Criteria | 0.88 | 25% | 0.22 |
| Context Clarity | 0.93 | 15% | 0.14 |
| **Total Clarity** | | | **0.869** |
| **Ambiguity** | | | **13.1%** |

## Goal
Understand (no code changes) the exact root cause of why `sendMergedHashOnFd` returns different hash values across servers for the same set of transactions, delivered as a step-by-step call chain trace.

## Constraints
- Root cause explanation only — no fix required
- The mismatch is reproducible only when transactions arrive in the same order at all servers; different arrival orders produce different hashes
- Server 2 diverged from servers 1 & 3 (which agreed), implying server 2 received/inserted transactions in a different order

## Non-Goals
- Implementing a fix
- Adding diagnostic tooling
- Performance analysis

## Root Cause: Step-by-Step Call Chain Trace

### The Invariant That Must Hold (but Doesn't)

For all servers to produce the same hash, they must produce the same `merged_order` sequence. The hash in `sendMergedHashOnFd` (`merger.cpp:370-371`) is just `hashCombine` over `merged_order` tx_ids in sequence — it is fully deterministic *given* the same sequence. The non-determinism is in how `merged_order` is built.

### Step 1 — Transaction Insertion (`insertAlgorithm`, `merger.cpp:93`)

Each server's `Merger::insertAlgorithm()` processes partial sequences from all servers as they arrive. It calls `graph.addNode()` for each new transaction, which inserts into:

```cpp
// graph.h:22
std::unordered_map<std::string, std::unique_ptr<Transaction>> nodes;
```

**Key fact:** The bucket layout of `std::unordered_map` is determined by insertion order. If server 2 received the same transactions but in a different arrival order than servers 1 & 3, its `nodes` map has a different internal structure — even though it contains the same set of keys.

### Step 2 — `getMergedOrders()` is called after every insert (`merger.cpp:285`)

```cpp
int removed = graph.getMergedOrders();
```

Inside `getMergedOrders()`:

### Step 3 — `findSCCs()` iterates `nodes` in non-deterministic order (`graph.cpp:274-282`)

```cpp
for (auto &kv : nodes) {           // <-- unordered_map iteration: bucket-layout dependent
    Transaction *v = kv.second.get();
    if (index_map.find(v) == index_map.end()) {
        strongConnect(v);
    }
}
```

`std::unordered_map` iteration order is determined by bucket layout, which is determined by insertion order. Different servers → different iteration order here.

### Step 4 — `strongConnect()` (Tarjan's) assigns SCC indices based on visit order (`graph.cpp:221-261`)

Tarjan's algorithm is **correct** — it always finds the same SCCs regardless of starting order. But the **index assigned to each SCC** in the `sccs` vector depends entirely on which root node `strongConnect` finishes first.

If Tarjan visits tx_A's subtree before tx_B's on server 1, tx_A's SCC gets a lower index. On server 2, if it visits tx_B first, tx_B's SCC gets the lower index. The SCCs are the same; their indices are swapped.

### Step 5 — `getMergedOrders()` seeds its queue by iterating `sccs` in index order (`graph.cpp:395-401`)

```cpp
for (int c = 0; c < sccs_count; ++c) {
    if (out_degrees[c] == 0 && isSCCComplete(c)) {
        Q.push(c);         // sink SCCs enqueued in SCC index order
    }
}
```

**This is where the divergence becomes the output order.** When two SCCs are both sinks (no outgoing edges — meaning the transactions they contain are **independent, non-conflicting**), they both have `out_degrees[c] == 0`. They are enqueued in SCC index order. Since different servers assigned different indices to these same SCCs (step 4), they are enqueued in different order.

### Step 6 — `merged.push()` records the output sequence (`graph.cpp:436`)

```cpp
for (Transaction *T : comp) {
    if (auto up = removeTransaction(T)) {
        merged.push(*up);       // order is Q-dequeue order
        transaction_count++;
    }
}
```

Transactions are pushed into `merged` in the order Q dequeues SCCs. Independent transactions that happened to get different SCC indices end up in different positions in `merged` on different servers.

### Step 7 — `buildSnapshotProto()` copies `merged.snapshot()` directly (`graph.cpp:487-497`)

```cpp
for (const auto &kv : merged.snapshot()) {
    request::VertexAdj *va = snap.add_merged_order();
    va->set_tx_id(kv.getID());
    ...
}
```

The `merged_order` protobuf field is the `merged` queue contents in push-order — already divergent between servers.

### Step 8 — `sendMergedHashOnFd()` hashes the sequence (`merger.cpp:369-371`)

```cpp
std::size_t seed = 0;
for (int i = 0; i < snap.merged_order_size(); ++i)
    hashCombine(seed, snap.merged_order(i).tx_id());
```

Same transactions, different order → **different hash**.

---

## The Precise Trigger Condition

The mismatch only manifests for **independent concurrent transactions** — transactions with no conflicting read/write operations on the same data items. Conflicting transactions produce directed edges in the dependency graph, which forces a specific relative ordering in the topological sort regardless of SCC indices. Independent transactions have no such edges, so their relative order in `merged_order` is entirely determined by which SCC got which index from Tarjan — which is a function of `nodes` insertion order.

This is why: if all servers insert transactions in the same order → same `nodes` bucket layout → same Tarjan traversal → same SCC indices → same `merged_order` → same hash. The user's observation that "transactions have to arrive at each node at the same time" is exactly this condition.

Server 2 receiving transactions in a different order is sufficient to cause the mismatch, with no other difference in behavior.

---

## Ontology (Key Entities)

| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| Server | core domain | server_id, partial_sequences | has Merger |
| Merger | core domain | graph, ready_q, partial_sequences | owns Graph, runs insertAlgorithm |
| Graph | core domain | nodes (unordered_map), sccs, merged | contains Transactions, produces MergedOrder |
| Transaction | core domain | id, operations, order, server_id | belongs to Graph, member of SCC |
| SCC | supporting | index, members, out_degree | groups Transactions, ordered in condensation |
| MergedOrder | core domain | sequence of tx_ids | produced by Graph, hashed by Merger |
| Hash | supporting | uint64 seed | computed from MergedOrder sequence |

## Ontology Convergence
| Round | Entity Count | New | Changed | Stable | Stability Ratio |
|-------|-------------|-----|---------|--------|----------------|
| 1 | 4 | 4 | - | - | N/A |
| 2 | 6 | 2 | 0 | 4 | 67% |
| 3 | 7 | 1 | 0 | 6 | 86% |

## Interview Transcript
<details>
<summary>Full Q&A (3 rounds)</summary>

### Round 1
**Q:** What is your actual goal here?
**A:** Understand root cause only
**Ambiguity:** 51.5% (Goal: 0.75, Constraints: 0.20, Criteria: 0.30, Context: 0.65)

### Round 2
**Q:** Is the hash mismatch reproducible consistently, or does it happen intermittently?
**A:** I think it is reproducible but it would be very hard, transactions have to arrive at each node at the same time for each run, and they have to be ordered the same way. I am not sure though you should understand the code base first.
**Ambiguity:** 31.5% (Goal: 0.85, Constraints: 0.65, Criteria: 0.35, Context: 0.90)

### Round 3
**Q:** What does a satisfying root cause explanation look like to you?
**A:** Step-by-step trace through the call chain
**Ambiguity:** 13.1% (Goal: 0.92, Constraints: 0.75, Criteria: 0.88, Context: 0.93)

</details>
