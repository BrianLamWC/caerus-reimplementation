# Handoff Summary — Server Restructure Conventions

All judgment tasks (CT-1 through CT-10) from `claude-tasks.md` are complete.
Codex may now begin `codex-tasks.md`. Every `[HANDOFF]` placeholder in that file resolves below.

---

## 1. File Naming Convention

**Rule:** All source file names are lowercase snake_case.

| Current | Renamed To |
|---------|-----------|
| `partialSequencer.cpp` | `partial_sequencer.cpp` |
| `partialSequencer.h` | `partial_sequencer.h` |
| `queueTS.cpp` | `queue_ts.cpp` |
| `queueTS.h` | `queue_ts.h` |

All other files are already single-word lowercase — no changes needed.

**Include guards** after rename:
- `PARTIALSEQUENCER_H` → `PARTIAL_SEQUENCER_H`
- `QUEUE_TS_H` stays as-is (already matches)

**Makefile:** Uses `$(wildcard *.cpp)` — no changes needed after file renames.

---

## 2. Class Naming Convention

**Rule:** All class names are PascalCase, no underscores, no snake segments.

| Current | Renamed To | Location |
|---------|-----------|----------|
| `Queue_TS` | `QueueTS` | `queue_ts.h`, `queue_ts.cpp`, `batcher.h`, `merger.h` |

All other class names (`Batcher`, `PartialSequencer`, `Merger`, `Server`, `Client`, `Graph`, `Logger`, `Pinger`, `Coordinator`, `PeerListener`, `CompareByRound`) are already compliant.

---

## 3. Struct Naming Convention

**Rule:** All struct names are PascalCase.

| Current | Renamed To | Rationale |
|---------|-----------|-----------|
| `struct server` | `struct ServerInfo` | Cannot use `Server` (class collision). `ServerInfo` accurately describes it as a metadata record (ip, port, id, isOnline, isLeader) for each node in the distributed system. |

**Files affected:** `utils.h`, `utils.cpp`, `batcher.h`, `partial_sequencer.h`, `partialSequencer.cpp` → `partial_sequencer.cpp`, and any other callers.

**Special case:** The loop variable `for (auto &server : servers)` in `partial_sequencer.cpp:171` is a *local variable name*, not the type. It does not need to be renamed (it's coincidental that it matched the old struct name).

All other struct names (`PingerThreadArgs`, `DataItem`, `Operation`, `PeerListenerThreadsArgs`, `ServerArgs`, `ClientListenerThreadsArgs`, `ClientArgs`, `PingerThreadArgs`, `CompareByRound`) are already compliant.

---

## 4. Member Variable Convention

**Rule:** All private member variables are snake_case with **no trailing underscore**.

| Current | Renamed To | Location |
|---------|-----------|----------|
| `next_round_` | `next_round` | `batcher.h:36`, `batcher.cpp` |
| `next_round_` | `next_round` | `partial_sequencer.h:15`, `partial_sequencer.cpp` |
| `partial_sequence_` | `partial_sequence_proto` | `partial_sequencer.h:19`, `partial_sequencer.cpp` |
| `ready_q_` | `ready_q` | `merger.h:49`, `merger.cpp` |
| `enqueued_sids_` | `enqueued_sids` | `merger.h:50`, `merger.cpp` |
| `primaryCopyID` | `primary_copy_id` | `utils.h:55` (`DataItem` struct), `utils.cpp` |

**Note on `partial_sequence_` rename:** The member `request::Request partial_sequence_` in `PartialSequencer` is renamed to `partial_sequence_proto` (not `partial_sequence`) because `partial_sequence` already exists as `std::vector<Transaction> partial_sequence` on the previous line. The proto member is the `request::Request` assembled each round for broadcast to the local merger queue and remote peer regions — "proto" suffix accurately distinguishes it.

**Note on `partial_sequence` (the vector):** This member (`std::vector<Transaction> partial_sequence` in `partialSequencer.h:17`) appears to be **unused** in `partialSequencer.cpp` — the partial sequence is built directly into `partial_sequence_proto`. This is flagged as a potential dead code bug (see Bugs section below).

---

## 5. Global Variable Convention

**Rule:**
- Mutable global variables: snake_case (no trailing underscore)
- Synchronization primitives (mutex, cv, atomic): UPPER_SNAKE_CASE (already consistent)
- Constants and epoch references: UPPER_SNAKE_CASE (already consistent)

### Extern Queue Names (trailing `_` removed)

| Current | Renamed To | Location |
|---------|-----------|----------|
| `request_queue_` | `request_queue` | `queue_ts.h`, `queue_ts.cpp`, all callers |
| `batcher_to_partial_sequencer_queue_` | `batcher_to_partial_sequencer_queue` | `queue_ts.h`, `queue_ts.cpp`, all callers |
| `partial_sequencer_to_merger_queue_` | `partial_sequencer_to_merger_queue` | `queue_ts.h`, `queue_ts.cpp`, all callers |

Companion mutex/cv (`partial_sequencer_to_merger_queue_mtx`, `partial_sequencer_to_merger_queue_cv`) have no trailing `_` — they are already consistent after the queue rename.

### camelCase Globals (renamed to snake_case)

| Current | Renamed To | Location |
|---------|-----------|----------|
| `mockDB` | `mock_db` | `utils.h:68`, `utils.cpp`, all callers |
| `mockDB_logging` | `mock_db_logging` | `utils.h:69`, `utils.cpp`, all callers |

**Unchanged mutable globals** (already snake_case, no action needed):
- `peer_port`, `my_id`, `servers` — correct snake_case for mutable globals

**Unchanged constant globals** (already UPPER_SNAKE_CASE, no action needed):
- `LEADER_IP`, `LEADER_PORT`, `LEADER_ID`, `LOGICAL_EPOCH`, `LOGICAL_EPOCH_READY`, `LEADER`, `READY_MTX`, `READY_CV`, `READY_SET`, `EXPECTED_SERVERS_COUNT`

---

## 6. Function Naming Convention

**Rule:** All functions and methods are camelCase, no underscores, no trailing `_`.

### `utils.h`

| Current | Renamed To | Location |
|---------|-----------|----------|
| `hash_combine` | `hashCombine` | `utils.h:81` (definition), `utils.h:100-101` (usages in `std::hash<DataItem>`) |

**Rationale:** Only snake_case function in the project. Boost/std precedent for this name is outweighed by project-wide camelCase consistency.

### `graph.h` / `graph.cpp` (CT-10 decision)

| Current | Renamed To | Location | Callers |
|---------|-----------|----------|---------|
| `add_MRW` | `addMostRecentWriter` | `graph.h:50`, `graph.cpp` | `merger.cpp:209,217` |
| `remove_MRW` | `removeMostRecentWriter` | `graph.h:51`, `graph.cpp` | `merger.cpp:240` |
| `add_MRR` | `addMostRecentReader` | `graph.h:53`, `graph.cpp` | `merger.cpp:260` |
| `remove_MRR` | `removeMostRecentReader` | `graph.h:54`, `graph.cpp` | `merger.cpp:285` |
| `getMergedOrders_` | `getMergedOrders` | `graph.h:68`, `graph.cpp` | `grep -rn "getMergedOrders_" Server/` |

**Rationale:** The companion getters (`getMostRecentWriterID`, `getMostRecentReadersIDs`) already establish `MostRecentWriter`/`MostRecentReader` as the naming pattern. Expanding the mutators to match creates a fully consistent method family. `getMergedOrders_` trailing `_` is removed for method name consistency.

All other functions are already camelCase and require no changes.

---

## 7. File Organization Pattern

**Rule:** Flat structure — all source files in a single `/Server` directory.

No subdirectory structure is introduced. The codebase has ~12 component pairs and is easy to navigate flat. The pipeline architecture (client → batcher → partial_sequencer → merger → logger) is reflected in naming, not folder structure.

**Layer reference (for orientation):**
| Layer | Files |
|-------|-------|
| Entry point | `main.cpp` |
| Client input | `client.cpp/h` |
| Batching | `batcher.cpp/h` |
| Local sequencing | `partial_sequencer.cpp/h` |
| Merging | `merger.cpp/h` |
| Graph/dependency | `graph.cpp/h` |
| Peer comms | `server.cpp/h` |
| Logging | `logger.cpp/h` |
| Infrastructure | `queue_ts.cpp/h`, `transaction.h`, `utils.cpp/h` |
| Logs output | `batcher_logs/`, `merger_logs/` |

---

## 8. Bugs Flagged — DO NOT FIX

These are identified during the naming/organization audit. They are documented here for awareness only. Codex must not fix any of these.

| # | Bug | Location | Severity |
|---|-----|----------|----------|
| B1 | **Typo:** `neigbors_in_ids` — misspelled member name; should be `neighbors_in_ids` | `transaction.h:32`, method bodies on lines 55 and 72, all callers | Medium — any code searching by name will fail |
| B2 | **Dead code (confirmed):** `std::vector<Transaction> partial_sequence` in `PartialSequencer` is never written or read in `partialSequencer.cpp` (verified: `grep -n "partial_sequence\b" partialSequencer.cpp` returns no results). The partial sequence is built directly into `partial_sequence_proto` each round. Likely a leftover from a prior design. Similarly, `transactions_received` (`partialSequencer.h:18`) also appears unused. | `partialSequencer.h:17-18` | Low — dead fields, no runtime impact |
| B3 | **Naming collision risk:** `struct server` (lowercase) at global namespace alongside `Server` class — resolved by CT-1 rename to `ServerInfo`, but the underlying design smell is noted | `utils.h:21` | Low — resolved by rename |
| B4 | **Naming collision (design):** `partial_sequence` and `partial_sequence_` in `PartialSequencer` were two different members with near-identical names and different types — the trailing `_` was used as a poor disambiguator | `partialSequencer.h:17,19` | Low — resolved by CT-2 rename to `partial_sequence_proto` |
| B5 | **Comment typo:** `//use move beccause` should be `//use move because` | `utils.h:59` | Cosmetic |

---

## 9. Convention Summary Table

| Category | Convention | Notes |
|----------|-----------|-------|
| Source file names | `snake_case.cpp/h` | Multi-word files renamed |
| Folder names | `snake_case/` | Already consistent |
| Class names | `PascalCase` | No underscores |
| Struct names | `PascalCase` | `struct server` → `ServerInfo` |
| Enum names | `PascalCase` | Already consistent |
| Enum values | `UPPER_SNAKE_CASE` | Already consistent |
| Method/function names | `camelCase` | `hash_combine` → `hashCombine`; `add_MRW`/`remove_MRW`/`add_MRR`/`remove_MRR` → full-word camelCase; `getMergedOrders_` trailing `_` removed |
| Private member variables | `snake_case` (no trailing `_`) | Trailing `_` removed from 5 members |
| Mutable global variables | `snake_case` | `mockDB` → `mock_db`, queues trailing `_` removed |
| Constant/sync globals | `UPPER_SNAKE_CASE` | Already consistent, no changes |
| Macros | `UPPER_SNAKE_CASE` | `SERVERLIST`, `MOCKDB` — no changes |
| Include guards | `UPPER_SNAKE_CASE_H` | Updated after file renames |

---

*Handoff written: 2026-04-27. All claude-tasks complete. Codex may now start `codex-tasks.md`.*
