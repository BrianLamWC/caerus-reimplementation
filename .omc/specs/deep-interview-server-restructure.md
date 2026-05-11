# Deep Interview Spec: Server Directory Restructure for Consistency

## Metadata
- Interview ID: di-server-restructure-2026-04-27
- Rounds: 1 (codebase exploration; initial ambiguity already below 20% threshold)
- Final Ambiguity Score: ~13%
- Type: brownfield
- Generated: 2026-04-27
- Threshold: 0.20
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.92 | 35% | 0.322 |
| Constraint Clarity | 0.92 | 25% | 0.230 |
| Success Criteria | 0.82 | 25% | 0.205 |
| Context Clarity | 0.78 | 15% | 0.117 |
| **Total Clarity** | | | **0.874** |
| **Ambiguity** | | | **~13%** |

## Goal
Standardize naming conventions (files, classes, structs, members, globals, functions) and code organization patterns across the `/Server` C++ directory. No logic, behavior, or bug fixes — pure renaming and structural consistency.

## Constraints
- No behavior changes of any kind
- No logic changes
- No bug fixes
- No performance improvements
- No new features
- All renames must be applied everywhere the identifier is used (no partial renames)
- The `Makefile` uses `$(wildcard *.cpp)` so file renames do not require Makefile changes, but `#include` directives do require updates
- After file renames, all `#include` guards must match the new filename

## Non-Goals
- Fixing the `neigbors_in_ids` logic behavior (flag only)
- Splitting `utils.h` into smaller headers
- Introducing subdirectory structure
- Changing protobuf schemas
- Modifying `json.hpp` (third-party library)
- Modifying generated protobuf files (`request.pb.cc`, `graph_snapshot.pb.cc`)

## Acceptance Criteria
- [ ] All source file names are snake_case (multi-word files renamed)
- [ ] All class names are PascalCase with no underscores (`Queue_TS` → `QueueTS`)
- [ ] All struct names are PascalCase (`struct server` renamed to non-conflicting PascalCase name)
- [ ] All private member variables are snake_case with no trailing underscore
- [ ] All mutable global variables are snake_case
- [ ] All function/method names are camelCase
- [ ] All `#include` directives updated to match renamed files
- [ ] All `#ifndef` include guards updated to match renamed files
- [ ] Typo `neigbors_in_ids` flagged in bug report (not fixed)
- [ ] `claude-tasks.md` complete with judgment decisions documented
- [ ] `codex-tasks.md` contains only mechanical tasks referencing decided conventions
- [ ] `handoff.md` written after claude-tasks complete, with full decision log

## Assumptions Exposed & Resolved
| Assumption | Challenge | Resolution |
|------------|-----------|------------|
| Majority file naming is lowercase | Confirmed — all single-word files are lowercase (`batcher.cpp`, `merger.cpp`, etc.) | snake_case for multi-word files |
| Trailing `_` on private members is inconsistent | Found in 5 of ~30+ private members | Decision deferred to claude-tasks |
| `hash_combine` snake_case is intentional std:: mirroring | Plausible — but inconsistent with all other project functions | Decision deferred to claude-tasks |
| `struct server` can't just become `Server` | `Server` class exists in `server.h` — direct collision | New semantic name needed |
| Makefile needs updating for file renames | Makefile uses `$(wildcard *.cpp)` | No Makefile changes needed; only `#include` updates |

## Technical Context

### Codebase Summary
- Language: C++11+, ~25 source files, flat structure in `/Server`
- Build: `g++ -Wall -g`, protobuf linked, `$(wildcard *.cpp)` — file renames don't break build
- Architecture: distributed transaction pipeline — `client → batcher → partial_sequencer → merger → logger`

### File Naming Violations Found
| Current | Convention Violated | Notes |
|---------|-------------------|-------|
| `partialSequencer.cpp/h` | camelCase multi-word | Other files are single-word lowercase |
| `queueTS.cpp/h` | camelCase + uppercase acronym | Inconsistent with snake_case folder names |

### Class/Struct Naming Violations Found
| Current | Location | Convention Violated |
|---------|----------|-------------------|
| `Queue_TS` | `queueTS.h:13` | Underscore in class name (all others PascalCase) |
| `struct server` | `utils.h:21` | Lowercase struct (all others PascalCase) |

### Member Variable Violations Found
| Current | Location | Convention Violated |
|---------|----------|-------------------|
| `next_round_` | `batcher.h:36`, `partialSequencer.h:15` | Trailing `_` inconsistent with siblings |
| `partial_sequence_` | `partialSequencer.h:19` | Trailing `_`; also naming collision with `partial_sequence` (line 17) |
| `ready_q_` | `merger.h:49` | Trailing `_` inconsistent with siblings |
| `enqueued_sids_` | `merger.h:50` | Trailing `_` inconsistent with siblings |
| `primaryCopyID` | `utils.h:55` | camelCase member (all others snake_case) |

### Global Variable Violations Found
| Current | Location | Convention Violated |
|---------|----------|-------------------|
| `mockDB` | `utils.h:68` | camelCase global (mutable globals use snake_case) |
| `mockDB_logging` | `utils.h:69` | camelCase global with mixed separator |
| `request_queue_` | `queueTS.h:35` | Trailing `_` on extern (companion mutex/cv have no `_`) |
| `batcher_to_partial_sequencer_queue_` | `queueTS.h:38` | Trailing `_` on extern |
| `partial_sequencer_to_merger_queue_` | `queueTS.h:41` | Trailing `_` on extern |

### Function Naming Violations Found
| Current | Location | Convention Violated |
|---------|----------|-------------------|
| `hash_combine` | `utils.h:81` | snake_case (all other functions are camelCase) |

### Bugs Flagged (Do Not Fix)
1. **Typo**: `neigbors_in_ids` (`transaction.h:32`) — should be `neighbors_in_ids`. Affects member declaration, `addNeighborInID()`, `getIncomingNeighborIDs()`, and callers.
2. **Naming collision**: `partial_sequence` (vector) and `partial_sequence_` (proto) in `PartialSequencer` — different types, similar names, trailing `_` used as poor disambiguator.
3. **Scope collision risk**: `struct server` (lowercase) in global namespace alongside `Server` class — confusion potential for future maintainers.

## Ontology (Key Entities)

| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| SourceFile | core domain | name, extension, includes | contains Classes, Structs, Functions, Globals |
| Class | core domain | name, members, methods | defined in SourceFile |
| Struct | core domain | name, fields | defined in SourceFile |
| MemberVariable | supporting | name, type, hasTrailingUnderscore | owned by Class/Struct |
| GlobalVariable | supporting | name, type, isMutable | declared in SourceFile |
| Function | supporting | name, caseStyle | defined in SourceFile |
| IncludeGuard | supporting | macroName | one per header file |

## Ontology Convergence

| Round | Entity Count | New | Changed | Stable | Stability Ratio |
|-------|-------------|-----|---------|--------|----------------|
| 0 (exploration) | 7 | 7 | - | - | N/A |

## Interview Transcript

<details>
<summary>Full Q&A (exploration-only; user request was pre-specified)</summary>

### Round 0 — Codebase Exploration
**Method:** Explore agent (haiku) mapped full directory tree, naming conventions, class/struct/function patterns, and inconsistencies.

**Findings summary:**
- 25 source files; flat directory structure
- Single-word files: all lowercase (consistent)
- Multi-word files: `partialSequencer`, `queueTS` (camelCase — inconsistent with lowercase siblings)
- Classes: PascalCase everywhere except `Queue_TS`
- Structs: PascalCase everywhere except `struct server`
- Members: snake_case except trailing `_` on 5 members
- Globals: mixed — UPPER_SNAKE_CASE for constants, snake_case for mutables, camelCase for `mockDB`/`mockDB_logging`
- Functions: camelCase everywhere except `hash_combine`

**Ambiguity score:** ~13% (below 20% threshold — request is well-specified)
</details>
