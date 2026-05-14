# Claude Tasks — static-graph-hash

These tasks require judgment: API design decisions, choosing what to hash, lock strategy.
Complete these before Codex starts its mechanical tasks.

---

## Task C1: Add `Graph::computeStaticGraphHash()` to graph.h and graph.cpp

**File:** `Caerus/Server/graph.h`
**File:** `Caerus/Server/graph.cpp`

### Decision rationale (already made)
- Method lives on `Graph`, not `Merger` — keeps `nodes_static` private (no getter needed)
- Reuses `snapshot_mtx` — same lock used by `buildSnapshotProto()`, correct pattern
- Hash covers: sorted node keys → each node's tx_id → each node's sorted out-neighbor IDs
- Sorting is mandatory: `nodes_static` and `getOutNeighbors()` both use unordered containers
- Return type: `uint64_t` — same as `MergedOrderHash.hash` field
- Reuse `hashCombine()` from `utils.h` (already included in graph.h)

### graph.h change
Add to the public section:
```cpp
uint64_t computeStaticGraphHash() const;
```

### graph.cpp change
Add after `buildSnapshotProto()`:
```cpp
uint64_t Graph::computeStaticGraphHash() const
{
    std::lock_guard<std::mutex> lock(snapshot_mtx);

    std::vector<std::string> keys;
    keys.reserve(nodes_static.size());
    for (const auto &kv : nodes_static)
        keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());

    std::size_t seed = 0;
    for (const auto &key : keys)
    {
        const Transaction *tx = nodes_static.at(key).get();
        hashCombine(seed, tx->getID());

        std::vector<std::string> nbr_ids;
        for (const Transaction *nbr : tx->getOutNeighbors())
            nbr_ids.push_back(nbr->getID());
        std::sort(nbr_ids.begin(), nbr_ids.end());
        for (const auto &nbr_id : nbr_ids)
            hashCombine(seed, nbr_id);
    }

    return static_cast<uint64_t>(seed);
}
```

**Status:** [x] complete
