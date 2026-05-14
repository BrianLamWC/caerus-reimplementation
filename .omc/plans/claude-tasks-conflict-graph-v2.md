# Claude Tasks — conflict-graph-v2

These tasks require understanding the full type system and semantics. Complete these
before Codex begins. All decisions become input to the handoff doc.

## Task 1: graph.h — change MRW storage type

File: `Caerus/Server/graph.h`

**Decision required:** `most_recent_writer` stores `Transaction*` today. This pointer
becomes dangling after promotion because the `Transaction` is moved from `nodes` into
the `merged` queue (value copy). Change to `std::string` to store the ID instead.

**Change:** Line 22
```
// Before:
std::unordered_map<DataItem, Transaction *> most_recent_writer;
// After:
std::unordered_map<DataItem, std::string> most_recent_writer;
```

**Judgment call:** Keep `removeMostRecentWriter` function signature as-is or remove it?
→ Decide and document in handoff.

---

## Task 2: graph.cpp — update MRW accessor implementations

File: `Caerus/Server/graph.cpp`

**2a. `addMostRecentWriter`:** Store the ID string, not the pointer.
```cpp
void Graph::addMostRecentWriter(DataItem item, Transaction* txn)
{
    most_recent_writer[item] = txn->getID();
}
```
Function signature stays the same (callers still pass `Transaction*`).

**2b. `getMostRecentWriterID`:** Drop the `!= nullptr` guard — strings can't be null.
```cpp
std::string Graph::getMostRecentWriterID(DataItem item)
{
    auto it = most_recent_writer.find(item);
    if (it != most_recent_writer.end())
        return it->second;
    return "";
}
```

---

## Task 3: graph.cpp — removeTransaction: remove MRW/MRR cleanup loop

File: `Caerus/Server/graph.cpp`

**Decision required:** The loop labelled "remove from mrw or mrr maps" in
`removeTransaction` calls `removeMostRecentWriter` (WRITE ops) and
`removeMostRecentReader` (READ ops). Both calls must be removed — if we erase entries
on promotion, the fallback branch can never fire (this was the exact bug).

**Decision:** Remove the entire comment + loop block (lines ~163–176 of the current
file). The loop has no other useful side effects once the two calls are gone.

Specifically, delete this entire block from `removeTransaction()`:
```cpp
// 4) remove from mrw or mrr maps
for (const auto &op : removed->getOperations())
{
    auto db_it = mock_db.find(op.key);
    if (db_it == mock_db.end())
    {
        std::cout << "REMOVE::ReadWriteSet: key " << op.key << " not found" << std::endl;
        continue;
    }
    auto data_item = db_it->second;
    if (op.type == OperationType::READ)
    {
        removeMostRecentReader(data_item, removed->getID());
    }
    else if (op.type == OperationType::WRITE)
    {
        removeMostRecentWriter(data_item);
    }
}
```

---

## Completion Gate

All three tasks complete → write `handoff-conflict-graph-v2.md` with decisions made,
then Codex picks up `codex-tasks-conflict-graph-v2.md`.
