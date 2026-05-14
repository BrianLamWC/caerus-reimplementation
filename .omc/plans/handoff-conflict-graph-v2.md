# Handoff: conflict-graph-v2

**For:** Codex  
**Status:** Claude tasks COMPLETE. Two mechanical changes remain in merger.cpp.

---

## What was done (Claude)

All structural changes to `graph.h` and `graph.cpp` are applied and verified.

### Decision 1: MRW storage type change
`most_recent_writer` was `std::unordered_map<DataItem, Transaction*>`.
Changed to `std::unordered_map<DataItem, std::string>` (graph.h:24).

**Why:** `Transaction*` pointers become dangling after promotion — the `Transaction`
object is moved into the `merged` value queue. Storing the string ID is safe forever.

### Decision 2: `addMostRecentWriter` updated
`most_recent_writer[item] = txn->getID();` (was `= txn`). Function signature unchanged —
callers still pass `Transaction*` and the function extracts the ID. No merger.cpp changes
needed at call sites.

### Decision 3: `getMostRecentWriterID` updated
Dropped `&& it->second != nullptr` guard (strings have no null state).
Now returns `it->second` directly (was `it->second->getID()`).

### Decision 4: MRW/MRR cleanup loop removed from `removeTransaction`
The entire block labelled "4) remove from mrw or mrr maps" was deleted from
`Graph::removeTransaction()`. This is the root fix: keeping string IDs in the maps
after promotion lets `insertAlgorithm` find them and call `addNeighborOutStatic`.

`removeMostRecentWriter` and `removeMostRecentReader` are now dead code (no callers)
but left in place — no harm, no compile error.

---

## What Codex must do

### Task 1 — merger.cpp: uncomment `!mrw` fallback (2 lines)

File: `Caerus/Server/merger.cpp`

Find:
```cpp
else if (!mrw)
{ // previous writer was promoted — record edge in static snapshot
    // graph.addNeighborOutStatic(curr_txn->getID(), mrw_id);
    // graph.addMostRecentReader(data_item, curr_txn->getID());
}
```

Replace with:
```cpp
else if (!mrw)
{ // previous writer was promoted — record edge in static snapshot
    graph.addNeighborOutStatic(curr_txn->getID(), mrw_id);
    graph.addMostRecentReader(data_item, curr_txn->getID());
}
```

**Why this now works:** `mrw_id` is non-empty even after promotion (string persists in
map). `mrw = graph.getNode(mrw_id)` returns `nullptr` (promoted txn not in `nodes`).
So `!mrw` is true and the branch fires. `addNeighborOutStatic` writes the edge directly
to `nodes_static`, bypassing the active graph.

### Task 2 — merger.cpp: remove duplicate static edge write (1 line)

In the same function, find the active-MRW branch:
```cpp
else if (mrw and mrw->getID() != curr_txn->getID())
{ // previous writer in graph
    graph.addNeighborOut(curr_txn, mrw);
    graph.addNeighborOutStatic(curr_txn->getID(), mrw_id);
    graph.addMostRecentReader(data_item, curr_txn->getID());
}
```

Delete the `graph.addNeighborOutStatic(...)` line:
```cpp
else if (mrw and mrw->getID() != curr_txn->getID())
{ // previous writer in graph
    graph.addNeighborOut(curr_txn, mrw);
    graph.addMostRecentReader(data_item, curr_txn->getID());
}
```

**Why:** `Graph::addNeighborOut` (graph.cpp:69–82) already mirrors every edge into
`nodes_static` under `snapshot_mtx`. The second call is a no-op (unordered_set
deduplicates pointers) but is dead/misleading.

### Task 3 — build

```bash
cd /home/brian/SocketAdventures/Caerus/Server
make
```

Expected: clean build. If compile errors appear — the only likely cause is a lingering
reference to `it->second->getID()` or `Transaction*` in the MRW map; grep for
`most_recent_writer` and fix any remaining pointer dereferences.

### Task 4 — acceptance test

Run the distributed test. Success criteria:
- `get merged` → `Snapshots identical: host 1 == host 2`, `host 1 == host 3`, `host 2 == host 3`
- `compare hashes` → `All hashes match`
- `Verification completed` for server_id=1, 2, 3

If any snapshot diff appears, paste the full output — do NOT mark complete.

---

## Files touched so far
| File | Change |
|------|--------|
| `Caerus/Server/graph.h:24` | MRW type: `Transaction*` → `std::string` |
| `Caerus/Server/graph.cpp:~506` | `addMostRecentWriter`: store `txn->getID()` |
| `Caerus/Server/graph.cpp:~525-533` | `getMostRecentWriterID`: return string directly |
| `Caerus/Server/graph.cpp:~189-210` | `removeTransaction`: MRW/MRR loop deleted |

## Files Codex must touch
| File | Change |
|------|--------|
| `Caerus/Server/merger.cpp` | Uncomment 2 lines in `!mrw` branch |
| `Caerus/Server/merger.cpp` | Delete 1 line (duplicate `addNeighborOutStatic`) |
