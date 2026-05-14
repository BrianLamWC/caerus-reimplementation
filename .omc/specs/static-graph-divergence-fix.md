# Fix: Static Graph Divergence Across Nodes

## Root Cause

`addMostRecentWriter` in `graph.cpp` blindly overwrites the current entry with whoever was
processed last locally:

```cpp
most_recent_writer[item] = txn->getID();  // pure overwrite — no ordering guarantee
```

Each node processes partial sequences in network arrival order (own sequences first, then
peers' in whatever order they arrive). Two nodes seeing `txnA` and `txnB` (both writing
key X) in opposite orders end up with opposite dependency edges — structurally different
static graphs.

A secondary source: when a transaction is promoted out of the dynamic graph, its
`most_recent_writer` entry is **erased** (graph.cpp:~544). Whether that erasure has
happened before a later transaction looks up the key depends on per-node promotion timing,
dropping edges non-deterministically. The fix code for this already exists in
`merger.cpp:220-224` but is commented out.

---

## Why `(order, server_id)` is safe

Every transaction's `order` field (= `random_stamp`) is assigned **once** by the
originating server's batcher and travels in the protobuf. All nodes see the **same**
`order` value for a given transaction. Combined with `server_id` as a tiebreaker,
`(order, server_id)` is a globally consistent total order — the same comparator
`getMergedOrders()` already uses in `graph.cpp:403-411`.

---

## Changes

### 1. `graph.h:24` — store order metadata alongside the writer ID

```cpp
// Before
std::unordered_map<DataItem, std::string> most_recent_writer;

// After
struct WriterEntry { std::string id; int32_t order; int32_t server_id; };
std::unordered_map<DataItem, WriterEntry> most_recent_writer;
```

---

### 2. `graph.cpp:525` — only promote to most-recent-writer if globally higher rank

```cpp
// Before
void Graph::addMostRecentWriter(DataItem item, Transaction* txn)
{
    most_recent_writer[item] = txn->getID();
}

// After
void Graph::addMostRecentWriter(DataItem item, Transaction* txn)
{
    auto it = most_recent_writer.find(item);
    if (it == most_recent_writer.end() ||
        std::tie(txn->getOrder(), txn->getServerId()) >
        std::tie(it->second.order, it->second.server_id))
    {
        most_recent_writer[item] = {txn->getID(), txn->getOrder(), txn->getServerId()};
    }
}
```

---

### 3. `graph.cpp:551` — update `getMostRecentWriterID` return

```cpp
// Before
return it->second;

// After
return it->second.id;
```

---

### 4. `graph.cpp:~544` — remove erasure on promotion

Do **not** erase from `most_recent_writer` when a transaction is promoted out of the
dynamic graph. The deterministic comparator ensures the stored entry remains correct even
after the transaction leaves the live graph, so subsequent lookups can still find and edge
against it.

---

### 5. `merger.cpp:220-224` — uncomment the promoted-writer static edge path

```cpp
// Before (both lines commented out)
else if (!mrw)
{ // previous writer was promoted
    // graph.addNeighborOutStatic(curr_txn->getID(), mrw_id);
    // graph.addMostRecentReader(data_item, curr_txn->getID());
}

// After
else if (!mrw)
{
    graph.addNeighborOutStatic(curr_txn->getID(), mrw_id);
    graph.addMostRecentReader(data_item, curr_txn->getID());
}
```

This path fires when the most recent writer was already promoted (no longer in the dynamic
graph). Without Change 4 it was unreachable because the entry was erased before this
branch could trigger; with Change 4 it works correctly.

---

## Summary

| File | Location | Change |
|------|----------|--------|
| `graph.h:24` | `most_recent_writer` type | Add `WriterEntry` struct with `id`, `order`, `server_id` |
| `graph.cpp:525` | `addMostRecentWriter` | Only overwrite if `(order, server_id)` is higher |
| `graph.cpp:551` | `getMostRecentWriterID` | Return `it->second.id` |
| `graph.cpp:~544` | promotion path | Remove `most_recent_writer.erase(item)` |
| `merger.cpp:221-223` | promoted-writer branch | Uncomment `addNeighborOutStatic` + `addMostRecentReader` |
