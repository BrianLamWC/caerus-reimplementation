# Codex Tasks — conflict-graph-v2

Mechanical tasks. All structural decisions have been made by Claude (see handoff doc).
Read `handoff-conflict-graph-v2.md` before starting.

## Prerequisites

Claude tasks must be fully applied to `graph.h` and `graph.cpp` before these run.
The code will not compile until BOTH sets of changes are in place.

---

## Task 1: merger.cpp — uncomment the `!mrw` fallback branch

File: `Caerus/Server/merger.cpp`

In `insertAlgorithm()`, read-set section, find this block:
```cpp
else if (!mrw)
{ // previous writer was promoted — record edge in static snapshot
    // graph.addNeighborOutStatic(curr_txn->getID(), mrw_id);
    // graph.addMostRecentReader(data_item, curr_txn->getID());
}
```

Remove the `//` comment markers from both lines:
```cpp
else if (!mrw)
{ // previous writer was promoted — record edge in static snapshot
    graph.addNeighborOutStatic(curr_txn->getID(), mrw_id);
    graph.addMostRecentReader(data_item, curr_txn->getID());
}
```

**Why:** After the graph.h/graph.cpp changes, `mrw_id` is now non-empty even when the
writer was promoted (string persists; pointer was what got erased). This branch will
now fire correctly.

---

## Task 2: merger.cpp — remove duplicate `addNeighborOutStatic` in active-MRW path

File: `Caerus/Server/merger.cpp`

In `insertAlgorithm()`, read-set section, find the active-MRW branch:
```cpp
else if (mrw and mrw->getID() != curr_txn->getID())
{ // previous writer in graph
    graph.addNeighborOut(curr_txn, mrw);
    graph.addNeighborOutStatic(curr_txn->getID(), mrw_id);   // <-- DELETE THIS LINE
    graph.addMostRecentReader(data_item, curr_txn->getID());
}
```

Delete the `graph.addNeighborOutStatic(...)` line entirely.

**Why:** `Graph::addNeighborOut` already mirrors every edge into `nodes_static`
internally (confirmed in graph.cpp). The second call is a no-op (unordered_set
deduplicates) but is dead/misleading code.

---

## Task 3: build and verify

```bash
cd /home/brian/SocketAdventures/Caerus/Server
make
```

Fix any compile errors before proceeding. Expected: clean build.

---

## Task 4: run acceptance tests

Start all three servers and the test client, then run:

```
get merged      # expected: "All snapshots are identical" for every host pair
compare hashes  # expected: "All hashes match"
```

Also confirm `Verification completed` still prints for server_id=1, 2, 3.

If any test fails, report the exact snapshot diff (same format as the original bug
report) and do NOT mark tasks complete.
