# Claude Tasks — Server Restructure (Judgment Decisions)

These tasks require judgment. Complete them in order, then write `handoff.md` with every decision documented before Codex begins.

**Reference:** `.omc/specs/deep-interview-server-restructure.md`
**Output:** `.omc/plans/handoff.md` (write after all tasks below are done)

---

## CT-1: Decide `struct server` rename

**Why judgment:** Cannot rename to `Server` — `Server` class already exists in `server.h`. Need a semantically accurate, non-conflicting PascalCase name.

**Context:**
- `struct server` is defined in `utils.h:21` with fields: `ip`, `port`, `id`, `isOnline`, `isLeader`
- Used as `std::unordered_map<int, server> target_peers` in `batcher.h:33` and `partialSequencer.h:22`
- Used as `std::vector<server> servers` in `utils.h:76` (global extern)
- Used inside `PingerThreadArgs` and `Pinger` class in `utils.h`

**Candidates to evaluate:**
- `ServerInfo` — neutral, describes it as a record of server metadata
- `PeerInfo` — emphasizes role (these are peer servers, not the local server)
- `ServerEntry` — emphasizes it's an entry in a list
- `ServerConfig` — implies it's configuration (may be too narrow)

**Decision criteria:** Read all usages in `utils.cpp`, `batcher.cpp`, `partialSequencer.cpp`, `server.cpp`, `client.cpp`, `main.cpp` to see if the struct represents peer nodes, config, or both. Pick the name that best matches actual usage semantics.

**Action:** Choose the name, document decision in `handoff.md` under "Struct Rename Decisions."

---

## CT-2: Decide `partial_sequence_` rename in `PartialSequencer`

**Why judgment:** Simple trailing `_` removal creates a naming collision with an existing sibling member.

**Context (partialSequencer.h:15-20):**
```cpp
std::vector<Transaction> partial_sequence;   // line 17 — the local sequence
request::Request         partial_sequence_;  // line 19 — the proto/serialized form
```

These are two different things with nearly identical names. The trailing `_` was used as a poor disambiguator. The proto version needs a distinct, semantically accurate name.

**Candidates to evaluate:**
- `partial_sequence_proto` — explicit proto suffix
- `serialized_sequence` — emphasizes it's the serialized output form
- `outgoing_sequence` — emphasizes it's the network-ready form
- `sequence_payload` — generic payload naming

**Decision criteria:** Read `partialSequencer.cpp` to understand how `partial_sequence_` is populated and used (is it built from `partial_sequence`? sent over network? both?). Name it to reflect what it actually IS.

**Action:** Choose the name, document decision in `handoff.md` under "Member Rename Decisions."

---

## CT-3: Decide trailing underscore convention for private members

**Why judgment:** The trailing `_` appears on some members but not most — need to pick a consistent rule and apply it uniformly.

**Context:**
Members WITH trailing `_`:
- `batcher.h:36` — `int32_t next_round_`
- `partialSequencer.h:15` — `int32_t next_round_`
- `partialSequencer.h:19` — `request::Request partial_sequence_` (see CT-2 for collision)
- `merger.h:49` — `std::deque<int> ready_q_`
- `merger.h:50` — `std::unordered_set<int> enqueued_sids_`

Members WITHOUT trailing `_` (representative sample):
- `batcher.h`: `current_window`, `batch`, `batcher_thread`, `sender_thread`, `outbound_queue`, `batch_mutex`, `batch_cv`, `target_peers`, `partial_sequencer_fds`
- `merger.h`: `merger_thread`, `popper`, `insert_thread`, `dump_thread`, `partial_sequences`, `ready_mtx`, `ready_cv`, `expected_server_ids`

**Options:**
1. **Remove `_` from all** — majority convention wins; simpler. Result: all private members are plain snake_case.
2. **Add `_` to all** — Google C++ Style Guide convention; requires renaming ~25+ members.

**Recommendation:** Option 1 (remove `_` from all) — the majority of the codebase already uses no trailing underscore. Adding it everywhere is a much larger change with no consistency precedent in this codebase.

**Action:** Choose option, document in `handoff.md` under "Member Variable Conventions." If option 2 chosen, enumerate ALL private members that need `_` added (batcher.h, partialSequencer.h, merger.h, client.h, server.h, graph.h, logger.h).

---

## CT-4: Decide `hash_combine` naming

**Why judgment:** It's the only snake_case function in the codebase, but it may be intentionally styled to mirror `std::` naming.

**Context (`utils.h:81`):**
```cpp
template <class T>
inline void hash_combine(std::size_t& seed, const T& v) { ... }
```
Used in `std::hash<DataItem>::operator()` in the same file. All other functions in utils.h are camelCase: `setupListenfd`, `setNonBlocking`, `threadError`, `setupConnection`, `setupMockDB`, `getServers`, `readNBytes`, `writeNBytes`.

**Options:**
1. **Rename to `hashCombine`** — consistent with all other project functions
2. **Keep `hash_combine`** — intentional std:: mirror; boost::hash_combine is the standard reference implementation and uses this name

**Recommendation:** Rename to `hashCombine`. This is a private utility function, not a std:: extension. Consistency with the project outweighs the boost precedent.

**Action:** Choose option, document in `handoff.md` under "Function Naming Decisions."

---

## CT-5: Decide `mockDB` / `mockDB_logging` naming

**Why judgment:** These camelCase globals break the snake_case pattern for mutable globals, but renaming them touches database logic used throughout.

**Context (`utils.h:68-69`):**
```cpp
extern std::unordered_map<std::string, DataItem> mockDB;
extern std::unordered_map<std::string, DataItem> mockDB_logging;
```
Other mutable globals: `peer_port`, `my_id`, `servers` — all snake_case.
Constants/synchronization globals: `LEADER_IP`, `LOGICAL_EPOCH`, `READY_MTX` — all UPPER_SNAKE_CASE.

**Options:**
1. **Rename to `mock_db` / `mock_db_logging`** — consistent with snake_case for mutable globals
2. **Keep as-is** — minimize scope; treat as an acceptable exception

**Recommendation:** Option 1. `mockDB` is a clear snake_case violation that should be corrected for consistency.

**Action:** Choose option, document in `handoff.md` under "Global Variable Conventions."

---

## CT-6: Decide extern queue trailing underscore

**Why judgment:** These are extern globals (not private members), so the private-member trailing `_` convention doesn't apply. The inconsistency with companion mutex/cv is clear, but the right direction needs to be decided.

**Context (`queueTS.h:35-43`):**
```cpp
extern Queue_TS<request::Request> request_queue_;                     // trailing _
extern Queue_TS<request::Request> batcher_to_partial_sequencer_queue_; // trailing _
extern Queue_TS<request::Request> partial_sequencer_to_merger_queue_;  // trailing _
extern std::mutex partial_sequencer_to_merger_queue_mtx;               // no _
extern std::condition_variable partial_sequencer_to_merger_queue_cv;   // no _
```

**Options:**
1. **Remove `_` from queue names** — makes queues consistent with their companion mutex/cv
2. **Add `_` to mutex/cv** — less natural; mutex/cv naming convention doesn't typically use trailing `_`

**Recommendation:** Option 1 — remove trailing `_` from extern queue names.

**Action:** Choose option, document in `handoff.md` under "Global Variable Conventions."

---

## CT-7: Document code organization pattern

**Why judgment:** The flat file structure could be organized into subdirectories by layer, but this needs an explicit decision to avoid implicit restructuring.

**Current structure:** All 25 source files flat in `/Server`

**Pipeline layers identified:**
- Input: `client.cpp/h`
- Processing: `batcher.cpp/h`, `partial_sequencer.cpp/h`, `merger.cpp/h`, `graph.cpp/h`
- Output: `logger.cpp/h`
- Peer comms: `server.cpp/h`
- Infrastructure: `queue_ts.cpp/h`, `transaction.h`, `utils.cpp/h`, `main.cpp`
- Logs: `batcher_logs/`, `merger_logs/`

**Options:**
1. **Keep flat** — appropriate for this codebase size; no structural changes in scope
2. **Introduce subdirectories** — pipeline/, network/, utils/ groupings

**Recommendation:** Option 1. The file count (~12 component pairs) is small enough that flat is clean. Introducing subdirectory structure is out of scope per the original request.

**Action:** Document the flat structure as the intentional convention in `handoff.md` under "File Organization Pattern." Note which layer each file belongs to for reader orientation.

---

## CT-8: Flag identified bugs (do not fix)

Document each of the following in `handoff.md` under "Bugs Flagged — DO NOT FIX":

1. **`neigbors_in_ids` typo** (`transaction.h:32`) — Member name misspelled; should be `neighbors_in_ids`. Affects member declaration, `addNeighborInID()` body, `getIncomingNeighborIDs()` return, and any callers (grep for `neigbors_in_ids`).

2. **`partial_sequence` / `partial_sequence_` naming collision** (`partialSequencer.h:17,19`) — Two members of `PartialSequencer` with near-identical names but different types (`std::vector<Transaction>` vs `request::Request`). The trailing `_` was used as a poor disambiguator. This is confusing and a maintenance hazard.

3. **`struct server` global scope collision risk** (`utils.h:21`) — Lowercase struct `server` in the same namespace as `Server` class. Not a compile error today but a readability hazard and potential confusion for future maintainers.

4. **`DataItem` constructor comment typo** (`utils.h:59`) — `//use move beccause` should be `//use move because`. Not a code bug but worth noting.

---

## CT-10: Decide `graph.h` method renames

**Why judgment:** `add_MRW`, `remove_MRW`, `add_MRR`, `remove_MRR` use snake_case with uppercase abbreviations — not camelCase. The naming convention choice depends on whether the `MRW`/`MRR` abbreviations should be preserved or expanded.

**Context (`graph.h:50-54`):**
```cpp
void add_MRW(DataItem item, Transaction* txn);
void remove_MRW(DataItem item);
void add_MRR(DataItem item, const std::string& txn_id);
void remove_MRR(DataItem item, const std::string& txn_id);
```

The companion getter methods already use full words:
```cpp
std::string getMostRecentWriterID(DataItem item);       // full words
std::unordered_set<std::string> getMostRecentReadersIDs(DataItem item);  // full words
```

`getMergedOrders_` (`graph.h:68`) has a trailing `_` on a **method** name — a unique violation.

**Callers in `merger.cpp`:** lines 209, 217, 240, 260, 285.

**Options:**
1. **Keep abbreviation, fix case:** `addMRW`, `removeMRW`, `addMRR`, `removeMRR` — short, matches the `MRW`/`MRR` abbreviation already in use
2. **Expand to full words:** `addMostRecentWriter`, `removeMostRecentWriter`, `addMostRecentReader`, `removeMostRecentReader` — consistent with `getMostRecentWriterID` and `getMostRecentReadersIDs`

**Recommendation:** Option 2 (expand to full words) — the getters already establish `MostRecentWriter`/`MostRecentReader` as the naming pattern. Expanding the mutators to match creates a fully consistent family.

For `getMergedOrders_`: simply remove the trailing underscore → `getMergedOrders`.

**Action:** Choose option, add the decided renames to `handoff.md` under "Function Naming Decisions," and confirm Codex tasks CDX-E2–CDX-E6 are unblocked.

---

## CT-9: Write `handoff.md`

After completing CT-1 through CT-8, write `.omc/plans/handoff.md` with:

- All naming convention decisions (one section per category)
- All specific rename mappings (old → new, with file locations)
- Bugs flagged (do not fix list)
- Confirmation that Codex tasks in `codex-tasks.md` are now unblocked

**Only write `handoff.md` after all decisions above are finalized.**
