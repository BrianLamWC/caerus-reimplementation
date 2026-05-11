# Codex Tasks — MERGED_HASH two-phase verification

**Do NOT start until `.omc/plans/handoff.md` exists.** All naming, proto shape, hash
algorithm, and fallback behavior decisions are documented there. Apply them exactly.

## Context
Spec: `.omc/specs/deep-interview-merger-snapshot.md`
Decisions: `.omc/plans/handoff.md`

---

## Task 1: Update protos

File: `Caerus/proto/request.proto`
- Add the new enum value (name from handoff.md) to `Request.Recipient`

File: `Caerus/proto/graph_snapshot.proto` (or wherever handoff.md says)
- Add the new hash response message (exact layout from handoff.md)

Then regenerate:
```
protoc --cpp_out=proto proto/request.proto proto/graph_snapshot.proto
```

---

## Task 2: Add `sendMergedHashOnFd` to Merger

Files: `Caerus/Server/merger.h`, `Caerus/Server/merger.cpp`

- Declare the method in `merger.h` (same style as `sendMergedOrdersOnFd`)
- Implement in `merger.cpp`:
  - Call `graph.merged.snapshot()` to get a non-destructive copy of the merged order
  - Hash the ordered transaction IDs using the algorithm from handoff.md
  - Serialize the hash response message (from handoff.md)
  - Send length-prefix + payload using `writeNBytes` (same pattern as `sendMergedOrdersOnFd`)

---

## Task 3: Add `MERGED_HASH` branch in client handler

File: `Caerus/Server/client.cpp`

- In `handleClient`, add an `else if (req_proto.recipient() == request::Request::<ENUM_VALUE>)` branch
- Branch body: call `merger->sendMergedHashOnFd(connfd)` (same pattern as the `MERGED` branch)

---

## Task 4: Add hash request + comparison to test client

File: `Caerus/test/main.cpp`

- Add `requestMergedHashFromHost(server_id)` (mirror of `requestMergedOrderFromHost` — sends the new request type, receives the hash response, stores hash per server_id)
- Add `compareHashes()` (compares stored hashes across all servers, applies fallback behavior from handoff.md)
- Add the new menu option (label from handoff.md) that calls `requestMergedHashFromHost` for all servers then `compareHashes()`

---

## Out of scope
- Do NOT modify `sendMergedOrdersOnFd` or the existing `MERGED` path
- Do NOT change `compareSnapshots` or `verifyMergedOrderFromHost`
- Do NOT add any other features or refactors
