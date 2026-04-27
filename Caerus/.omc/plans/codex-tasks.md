# Codex Tasks — Server Restructure (Mechanical Renames)

**PREREQUISITE:** Do not start until `claude-tasks.md` is complete and `.omc/plans/handoff.md` exists.
Read `handoff.md` first — all decisions marked `[HANDOFF]` below are resolved there.

**Scope:** Rename only. No logic changes. No bug fixes. No new code.
**Test:** After each group, run `make -C /Users/brianlam/SocketAdventures/Caerus/Server` to verify the build still passes.

---

## Group A — File Renames + Include Updates

### CDX-A1: Rename `partialSequencer.h` → `partial_sequencer.h`
- Rename the file
- Update include guard: `#ifndef PARTIALSEQUENCER_H` → `#ifndef PARTIAL_SEQUENCER_H`, `#define PARTIALSEQUENCER_H` → `#define PARTIAL_SEQUENCER_H`
- Update `#include "partialSequencer.h"` in every file that references it (full list):
  - `Server/server.h` (line 9)
  - `Server/main.cpp` (line 9)
  - Search for any remaining: `grep -r "partialSequencer.h" Server/`

### CDX-A2: Rename `partialSequencer.cpp` → `partial_sequencer.cpp`
- Rename the file
- Update the `#include "partialSequencer.h"` inside the file itself → `#include "partial_sequencer.h"`
- **Safety note:** Do NOT rename the runtime log filename strings inside this file: `"partial_sequence_log_"`, `"partial_sequence_sent_"`, `"partial_sequencer_received_"` (lines 54, 91, 141, 157-159). These are filesystem path strings, not identifiers.

### CDX-A3: Rename `queueTS.h` → `queue_ts.h`
- Rename the file
- Include guard `#ifndef QUEUE_TS_H` / `#define QUEUE_TS_H` — already correct spelling; verify it matches exactly
- Update `#include "queueTS.h"` in every file that references it (full list):
  - `Server/batcher.h` (line 17)
  - `Server/merger.h` (line 15)
  - `Server/partial_sequencer.h` (after CDX-A1, was line 7)
  - `Server/graph.h` (line 15)
  - `Server/logger.h` (line 10) — verify with `grep -n "queueTS" Server/logger.h`
  - Search for any remaining: `grep -r "queueTS.h" Server/`

### CDX-A4: Rename `queueTS.cpp` → `queue_ts.cpp`
- Rename the file
- Update any `#include "queueTS.h"` inside the file itself → `#include "queue_ts.h"`

**Build check after Group A.**

---

## Group B — Class and Struct Renames

### CDX-B1: Rename class `Queue_TS` → `QueueTS`
Replace all occurrences of `Queue_TS` with `QueueTS`:
- `Server/queue_ts.h` (after CDX-A3): class definition (`class Queue_TS`)
- `Server/queue_ts.cpp` (after CDX-A4): all template instantiations
- `Server/batcher.h`: `Queue_TS<request::Request> outbound_queue`
- `Server/merger.h`: `std::unique_ptr<Queue_TS<std::vector<Transaction>>>`
- `Server/queue_ts.h` extern declarations: `Queue_TS<...> request_queue_` etc.
- Search for any remaining: `grep -r "Queue_TS" Server/`

### CDX-B2: Rename `struct server` → `struct ServerInfo` (per handoff.md)
Replace all **8** known occurrences of the lowercase struct type `server`. Do NOT use grep to find sites — use this explicit enumeration to avoid false matches against `Server` class, `servers` variable, and loop variable names:

| File | Line(s) | What to change |
|------|---------|---------------|
| `Server/utils.h` | 21 | `struct server {` → `struct ServerInfo {` |
| `Server/utils.h` | 34 | `std::vector<server>*` in `PingerThreadArgs` → `std::vector<ServerInfo>*` |
| `Server/utils.h` | 44 | `Pinger(std::vector<server>*` → `Pinger(std::vector<ServerInfo>*` |
| `Server/utils.h` | 76 | `extern std::vector<server> servers` → `extern std::vector<ServerInfo> servers` |
| `Server/batcher.h` | 33 | `std::unordered_map<int, server>` → `std::unordered_map<int, ServerInfo>` |
| `Server/partial_sequencer.h` | 22 | `std::unordered_map<int, server>` → `std::unordered_map<int, ServerInfo>` |
| `Server/utils.cpp` | 18, 237 | type usages of `server` → `ServerInfo` |
| `Server/partial_sequencer.cpp` | — | Local var `server target = ...` on the reconnect path → `ServerInfo target = ...` |

After applying all 8 sites, run: `grep -rn "struct server\b\|\bserver\b" Server/ --include="*.h" --include="*.cpp"` to catch any remaining occurrences, then manually inspect each match (loop variables and `server.h`/`server.cpp` file references are NOT the type and should be left alone).

### CDX-B3: Rename `DataItem::primaryCopyID` → `primary_copy_id`
Replace all occurrences of `primaryCopyID` with `primary_copy_id`:
- `Server/utils.h`: struct field definition, `operator==`, `std::hash<DataItem>::operator()`
- `Server/utils.cpp`: any access via `.primaryCopyID`
- Search for all callers: `grep -rn "primaryCopyID" Server/`

**Build check after Group B.**

---

## Group C — Private Member Variable Renames

Apply per `[HANDOFF: trailing underscore decision]`. If decision is "remove trailing `_`":

### CDX-C1: `next_round_` → `next_round` in Batcher
- `Server/batcher.h`: member declaration (line 36)
- `Server/batcher.cpp`: all usages of `next_round_`
- Search: `grep -n "next_round_" Server/batcher.h Server/batcher.cpp`

### CDX-C2: `next_round_` → `next_round` in PartialSequencer
- `Server/partial_sequencer.h` (after CDX-A1): member declaration (line 15)
- `Server/partial_sequencer.cpp` (after CDX-A2): all usages of `next_round_`
- Search: `grep -n "next_round_" Server/partial_sequencer.h Server/partial_sequencer.cpp`

### CDX-C3: `partial_sequence_` → `[HANDOFF: decided name from CT-2]`
- `Server/partial_sequencer.h` (after CDX-A1): member declaration (line 19)
- `Server/partial_sequencer.cpp` (after CDX-A2): all usages
- **Caution:** Do not confuse with `partial_sequence` (the vector on line 17)
- Search: `grep -n "partial_sequence_" Server/partial_sequencer.h Server/partial_sequencer.cpp`

### CDX-C4: `ready_q_` → `ready_q` in Merger
- `Server/merger.h`: member declaration (line 49)
- `Server/merger.cpp`: all usages of `ready_q_`
- Search: `grep -n "ready_q_" Server/merger.h Server/merger.cpp`

### CDX-C5: `enqueued_sids_` → `enqueued_sids` in Merger
- `Server/merger.h`: member declaration (line 50)
- `Server/merger.cpp`: all usages of `enqueued_sids_`
- Search: `grep -n "enqueued_sids_" Server/merger.h Server/merger.cpp`

**Build check after Group C.**

---

## Group D — Global Variable Renames

### CDX-D1: Rename extern queue `request_queue_` → `request_queue`
Apply per `[HANDOFF: extern queue trailing underscore decision]`:
- `Server/queue_ts.h` (after CDX-A3): extern declaration
- `Server/queue_ts.cpp` (after CDX-A4): definition
- All callers: `grep -rn "request_queue_" Server/`

### CDX-D2: Rename extern queue `batcher_to_partial_sequencer_queue_` → `batcher_to_partial_sequencer_queue`
- `Server/queue_ts.h`: extern declaration
- `Server/queue_ts.cpp`: definition
- All callers: `grep -rn "batcher_to_partial_sequencer_queue_" Server/`

### CDX-D3: Rename extern queue `partial_sequencer_to_merger_queue_` → `partial_sequencer_to_merger_queue`
- `Server/queue_ts.h`: extern declaration + companion mutex/cv (which already have no `_`)
- `Server/queue_ts.cpp`: definition
- All callers: `grep -rn "partial_sequencer_to_merger_queue_" Server/`

### CDX-D4: Rename `mockDB` → `mock_db` (apply per `[HANDOFF: decision from CT-5]`)
- `Server/utils.h`: extern declaration (line 68)
- `Server/utils.cpp`: definition and all usages
- All callers: `grep -rn "mockDB\b" Server/`

### CDX-D5: Rename `mockDB_logging` → `mock_db_logging` (apply per `[HANDOFF: decision from CT-5]`)
- `Server/utils.h`: extern declaration (line 69)
- `Server/utils.cpp`: definition and all usages
- All callers: `grep -rn "mockDB_logging" Server/`

**Build check after Group D.**

---

## Group E — Function Renames

### CDX-E1: Rename `hash_combine` → `hashCombine` (apply per `[HANDOFF: decision from CT-4]`)
- `Server/utils.h`: template function definition (line 81)
- `Server/utils.h`: usages inside `std::hash<DataItem>::operator()` (lines 100, 101)
- Search for any other callers: `grep -rn "hash_combine" Server/`

### CDX-E2: Rename `add_MRW` → `[HANDOFF: decision from CT-10]` (e.g., `addMostRecentWriter`)
- `Server/graph.h`: declaration (line 50)
- `Server/graph.cpp`: definition
- `Server/merger.cpp`: callers — `grep -n "add_MRW" Server/graph.cpp Server/merger.cpp`

### CDX-E3: Rename `remove_MRW` → `[HANDOFF: decision from CT-10]` (e.g., `removeMostRecentWriter`)
- `Server/graph.h`: declaration (line 51)
- `Server/graph.cpp`: definition
- `Server/merger.cpp`: callers — `grep -n "remove_MRW" Server/graph.cpp Server/merger.cpp`

### CDX-E4: Rename `add_MRR` → `[HANDOFF: decision from CT-10]` (e.g., `addMostRecentReader`)
- `Server/graph.h`: declaration (line 53)
- `Server/graph.cpp`: definition
- `Server/merger.cpp`: callers — `grep -n "add_MRR" Server/graph.cpp Server/merger.cpp`

### CDX-E5: Rename `remove_MRR` → `[HANDOFF: decision from CT-10]` (e.g., `removeMostRecentReader`)
- `Server/graph.h`: declaration (line 54)
- `Server/graph.cpp`: definition
- `Server/merger.cpp`: callers — `grep -n "remove_MRR" Server/graph.cpp Server/merger.cpp`

### CDX-E6: Rename `getMergedOrders_` → `getMergedOrders` (remove trailing `_` from method)
- `Server/graph.h`: declaration (line 68)
- `Server/graph.cpp`: definition
- All callers: `grep -rn "getMergedOrders_" Server/`

**Build check after Group E.**

---

## Group F — Typo Fixes (Naming Bugs)

### CDX-F1: Fix typo `neigbors_in_ids` → `neighbors_in_ids`
- `Server/transaction.h:32`: member declaration
- `Server/transaction.h:55`: `addNeighborInID()` method body — uses `neigbors_in_ids.insert(...)`
- `Server/transaction.h:72`: `getIncomingNeighborIDs()` return statement — returns `neigbors_in_ids`
- All callers: `grep -rn "neigbors_in_ids" Server/`

**Build check after Group F.**

---

## Final Verification

After all groups complete:

1. `make -C /Users/brianlam/SocketAdventures/Caerus/Server` — must succeed with no errors
2. `grep -rn "Queue_TS\b" Server/` — must return zero results
3. `grep -rn "\bserver\b" Server/ --include="*.h" --include="*.cpp"` — must return zero results for the struct type (Server class and variable names `servers` are fine)
4. `grep -rn "partialSequencer\|queueTS" Server/` — must return zero results
5. `grep -rn "neigbors_in_ids" Server/` — must return zero results
6. `grep -rn "primaryCopyID" Server/` — must return zero results
7. `grep -rn "hash_combine\b" Server/` — must return zero results (if CT-4 decision was to rename)
8. `grep -rn "mockDB\b" Server/` — must return zero results (if CT-5 decision was to rename)
9. `grep -rn "\badd_MRW\|remove_MRW\|add_MRR\|remove_MRR\|getMergedOrders_" Server/` — must return zero results after CDX-E2–E6
