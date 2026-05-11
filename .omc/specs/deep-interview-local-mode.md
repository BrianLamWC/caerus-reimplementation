# Deep Interview Spec: Local Mode Flag

## Metadata
- Interview ID: local-mode-2026-05-04
- Rounds: 5
- Final Ambiguity Score: 10%
- Type: Brownfield
- Generated: 2026-05-04
- Threshold: 20%
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.92 | 0.35 | 0.322 |
| Constraint Clarity | 0.90 | 0.25 | 0.225 |
| Success Criteria | 0.88 | 0.25 | 0.220 |
| Context Clarity | 0.88 | 0.15 | 0.132 |
| **Total Clarity** | | | **0.899** |
| **Ambiguity** | | | **10%** |

---

## Goal
Add an optional `local` argument to the server binary so that any node can be run
locally on the same machine as the other nodes, connecting over localhost instead of
the configured EC2 IPs. All nodes run simultaneously in separate terminals using a
`local_servers.json` config file that specifies localhost IPs and unique ports per node.

---

## Constraints
- CLI stays positional: `./build/main <id> [local]`
- `local` is an optional second argument (argv[2]); omitting it runs in normal distributed mode (backward compatible)
- When `local` is specified, load `local_servers.json` instead of `servers.json`
- If `local` is specified but `local_servers.json` does not exist: print a clear error message and `exit(1)`
- `local_servers.json` is manually maintained by the developer (not auto-generated)
- `local_servers.json` schema extends `servers.json` with a `client_port` field:
  ```json
  {
    "servers": [
      { "ip": "127.0.0.1", "port": 8001, "client_port": 7001, "id": 1, "leader": true },
      { "ip": "127.0.0.1", "port": 8002, "client_port": 7002, "id": 2, "leader": false },
      { "ip": "127.0.0.1", "port": 8003, "client_port": 7003, "id": 3, "leader": false }
    ]
  }
  ```
- Each node looks up its own entry in `local_servers.json` by `my_id` and uses that
  entry's `port` (for peer listening) and `client_port` (for client listening)
- Peer-to-peer connections use each target's `port` from `local_servers.json` (already
  how batcher.cpp and partial_sequencer.cpp work — they read `server.port` from the
  global `servers` vector)
- `LEADER_IP` and `LEADER_PORT` are set from the `leader: true` entry in the loaded config
- No changes to algorithm, protocol, or log output

---

## Non-Goals
- Auto-generating `local_servers.json` from `servers.json`
- Supporting mixed-mode (some nodes local, some on EC2 in one run)
- Changing the peer-to-peer protocol or message format
- Fixing or touching the protobuf regeneration issue

---

## Acceptance Criteria
- [ ] `./build/main 1` (no second arg) works identically to today — no regression
- [ ] `./build/main 1 local` loads `local_servers.json`, not `servers.json`
- [ ] `./build/main 1 local` when `local_servers.json` is missing: prints `Error: local_servers.json not found. Create it to run in local mode.` and exits with code 1
- [ ] Running `./build/main 1 local`, `./build/main 2 local`, `./build/main 3 local` in three terminals:
  - Each node binds to the correct peer port (8001, 8002, 8003 respectively)
  - Each node binds to the correct client port (7001, 7002, 7003 respectively)
  - Non-leader nodes (2, 3) send READY to `127.0.0.1:8001` (local leader)
  - Leader (node 1) receives READY from both peers and broadcasts START
  - All 3 nodes reach operational state without crashing
- [ ] `local_servers.json` template committed to the repo

---

## Technical Context (Brownfield Findings)

### Files and lines affected

| File | Current state | Change needed |
|------|--------------|---------------|
| `Server/main.cpp:17-21` | Enforces `argc == 2`, exits otherwise | Allow `argc == 3`; parse `argv[2] == "local"` |
| `Server/main.cpp:25-26` | `peer_port = 8001; client_port = 7001;` hardcoded | In local mode, override from own entry in `servers` vector after `getServers()` |
| `Server/utils.h:18` | `#define SERVERLIST "servers.json"` | Add `#define LOCAL_SERVERLIST "local_servers.json"` |
| `Server/utils.h:21-28` | `ServerInfo` struct: `ip, port, id, isOnline, isLeader` | Add `int client_port` field (0 when not in local mode) |
| `Server/utils.cpp:176-214` | `getServers()` reads SERVERLIST unconditionally | Accept `bool local_mode` param; load LOCAL_SERVERLIST when true; parse `client_port` JSON field |
| `Server/utils.cpp:~176` | No missing-file error for servers.json | When `local_mode && file not found`: print error + `exit(1)` |

### Files with NO changes needed
- `batcher.cpp` / `partial_sequencer.cpp` — already use `server.port` from the global `servers`
  vector; will automatically use the localhost ports once `getServers()` loads them correctly
- `server.cpp`, `client.cpp` — use global `peer_port` / `client_port` which main.cpp will set
- `merger.cpp`, `graph.cpp`, `logger.cpp`, `queue_ts.*` — no IP/port awareness

### Key globals involved
- `extern int peer_port` — set in main.cpp, used by server.cpp to bind listener
- `extern int client_port` — set in main.cpp, used by client.cpp to bind listener
- `extern std::vector<ServerInfo> servers` — populated by getServers(), used by batcher + partial_sequencer for peer discovery
- `extern std::string LEADER_IP; extern int LEADER_PORT; extern int LEADER_ID` — set by getServers() from the leader entry

---

## Assumptions Exposed & Resolved

| Assumption | Challenge | Resolution |
|------------|-----------|------------|
| Port 8001 works for all nodes locally | Ports collide when 3 nodes run simultaneously on same machine | Use unique ports per node in local_servers.json |
| Only one config file needed | Local config may drift from distributed config | User accepts manual maintenance of local_servers.json |
| Auto-generation would be convenient | What if the user wants different local topology? | Manual control preferred |
| Only peer ports matter | client_port = 7001 is also hardcoded, same collision | Add client_port to local_servers.json schema |
| All nodes run simultaneously locally | Maybe only one at a time for smoke testing | Full multi-node simultaneous confirmed |

---

## Ontology (Key Entities)

| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| Node | core domain | id, peer_port, client_port, is_leader | runs as one process; connects to Peers |
| ServerInfo | supporting | ip, port, client_port, id, isOnline, isLeader | stored in `servers` vector; one per node |
| servers.json | external config | ip, port, id, leader | loaded in normal mode; source of ServerInfo |
| local_servers.json | external config | ip, port, client_port, id, leader | loaded in local mode; same schema + client_port |
| local mode flag | supporting | argv[2] == "local" | triggers alternate config loading |
| peer_port | supporting | int | global; set in main; used by server.cpp to bind |
| client_port | supporting | int | global; set in main; used by client.cpp to bind |

---

## Interview Transcript

<details>
<summary>Full Q&A (5 rounds)</summary>

### Round 1
**Q:** When running locally, do you want all nodes running simultaneously, or just one node at a time?
**A:** All nodes simultaneously
**Ambiguity:** 40% (Goal: 0.70, Constraints: 0.45, Criteria: 0.50, Context: 0.80)

### Round 2
**Q:** In local mode, how should each node's peer port be determined?
**A:** Keep port 8001, use local_servers.json with explicit localhost IPs and different ports per node
**Ambiguity:** 29% (Goal: 0.80, Constraints: 0.65, Criteria: 0.55, Context: 0.85)

### Round 3
**Q:** In local mode, each node also needs a unique client-facing port (7001 hardcoded). How should this be handled?
**A:** Add client_port to local_servers.json
**Ambiguity:** 23% (Goal: 0.85, Constraints: 0.77, Criteria: 0.60, Context: 0.85)

### Round 4 (Contrarian)
**Q:** Should local_servers.json be manually maintained, or auto-generated from servers.json?
**A:** Manually maintained — full control over local topology
**Ambiguity:** 20.3% (Goal: 0.87, Constraints: 0.80, Criteria: 0.65, Context: 0.87)

### Round 5
**Q:** What's the minimum bar for local mode to be 'working'?
**A:** Nodes start + coordinator handshake (READY→START completes)
**Q:** What if local_servers.json is missing?
**A:** Hard error with helpful message
**Ambiguity:** 10% (Goal: 0.92, Constraints: 0.90, Criteria: 0.88, Context: 0.88)

</details>
