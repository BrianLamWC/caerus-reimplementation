# Claude Tasks — MERGED_HASH two-phase verification

These tasks require judgment. Complete all of them before writing handoff.md.

## Context
Adding a fast hash-based consistency check as an alternative to the full graph snapshot.
Spec: `.omc/specs/deep-interview-merger-snapshot.md`

---

## Task 1: Decide hash algorithm and portability tradeoff

`utils.h` already has `hashCombine` (XOR-fold with `std::hash<std::string>`).
`std::hash<std::string>` is NOT portable across compilers or runs — hashes may differ between
server processes even on the same machine if ASLR or different stdlib versions are involved.

**Decision needed:**
- Is `std::hash` acceptable here (all servers are same binary, same run)?
- Or do we need a deterministic, portable hash (e.g. FNV-1a over raw bytes, no stdlib)?

Pick one and justify. Implement the chosen hash helper (or reuse `hashCombine` if acceptable).
Write the decision to `handoff.md`.

---

## Task 2: Decide proto shape for hash response

Options:
- A) New top-level message `MergedOrderHash { string hash = 1; int32 server_id = 2; }` in `graph_snapshot.proto`
- B) Add `string merged_hash = 3;` field to existing `GraphSnapshot` (server populates only the hash, leaves adj/merged_order empty)
- C) Reuse `request::Request` with a string payload field

**Decision needed:** Pick the cleanest option that minimizes proto churn and is consistent with existing patterns.
Inspect `graph_snapshot.proto` and `request.proto` for existing conventions, then decide.
Write the chosen message name and field layout to `handoff.md`.

---

## Task 3: Decide fallback behavior in test/main.cpp

When `compareHashes()` detects a mismatch, what happens next?

Options:
- A) Automatically fetch full graph from diverging servers and print the diff
- B) Print "hashes differ on servers X, Y — run full compare manually"
- C) Prompt the user interactively: "Hashes differ. Fetch full graph? [y/n]"

Look at the existing interactive menu pattern in `test/main.cpp` and pick the option most consistent with how other commands work. Write the decision to `handoff.md`.

---

## Task 4: Finalize naming conventions

Decide the canonical names for all new symbols (these become the law for Codex):

| Symbol | Candidate names | Pick one |
|---|---|---|
| New proto enum value | `MERGED_HASH` | confirm or rename |
| New proto message | `MergedOrderHash` | confirm or rename |
| Merger method | `sendMergedHashOnFd` | confirm or rename |
| Test function (request) | `requestMergedHashFromHost` | confirm or rename |
| Test function (compare) | `compareHashes` | confirm or rename |
| Menu label | "compare hashes (fast)" | confirm or rename |

Check existing naming patterns in `merger.h`, `client.cpp`, `test/main.cpp` and confirm or adjust each.
Write the final decided names to `handoff.md`.

---

## Output
When all four tasks are done, write `.omc/plans/handoff.md` with:
- Hash algorithm decision + rationale
- Proto message layout (exact field names and numbers)
- Fallback behavior decision
- Final canonical names for all new symbols
- Any other decisions Codex needs to apply the pattern mechanically
