# Codex Tasks — Local Mode (Mechanical Implementation)

**PREREQUISITE:** Do not start until `claude-tasks.md` is complete and `.omc/plans/handoff.md` exists.
Read `handoff.md` first — every `[HANDOFF]` placeholder below is resolved there.

**Scope:** Implement only. No changes outside what the spec requires.
**Test:** After all changes, run: `./build/main 1 local` (with local_servers.json present) and confirm it starts; run without local_servers.json and confirm hard error.

Work directory: `/Users/brianlam/SocketAdventures/Caerus/Server`

---

## CDX-1: Add `client_port` to `ServerInfo` struct

- `Server/utils.h`: add `int client_port;` to `struct ServerInfo`
- Default value: `[HANDOFF: CT-2 decision]`

---

## CDX-2: Add `LOCAL_SERVERLIST` macro

- `Server/utils.h`: add `#define LOCAL_SERVERLIST "local_servers.json"` below `#define SERVERLIST`

---

## CDX-3: Update `getServers()` signature and implementation

Per `[HANDOFF: CT-1 decision]`, update `getServers()` in `utils.cpp` and its declaration in `utils.h`:
- Accept mode parameter (or filename — see handoff)
- When local mode: open `LOCAL_SERVERLIST`; if file not found: print `[HANDOFF: CT-4 error message]` to stderr and `exit(1)`
- Parse `client_port` from JSON when present; use default from `[HANDOFF: CT-2]` when absent
- All other logic (populating `servers` vector, setting LEADER_IP/LEADER_PORT/LEADER_ID) unchanged

---

## CDX-4: Update `main.cpp` argument parsing

- Change `argc` validation: accept `argc == 2` OR `argc == 3`
- If `argc == 3`: check `argv[2] == "local"` (or `std::string(argv[2]) == "local"`); if anything else, print usage and `exit(1)`
- Update usage/error message to show: `Usage: ./build/main <id> [local]`
- Pass the local flag to `getServers()` per `[HANDOFF: CT-1]`

---

## CDX-5: Set `peer_port` and `client_port` from config in local mode

In `main.cpp`, after `getServers()` returns in local mode:
- Iterate the `servers` vector to find the entry where `s.id == my_id`
- Set global `peer_port = s.port`
- Set global `client_port = s.client_port`
- If no matching entry found: print `"Error: no entry for id <my_id> in local_servers.json"` and `exit(1)`

---

## CDX-6: Create `local_servers.json` (or `.example`)

Per `[HANDOFF: CT-5 decision]`, create the file at `Server/local_servers.json` (or `.example`):

```json
{
    "servers": [
        { "ip": "127.0.0.1", "port": 8001, "client_port": 7001, "id": 1, "leader": true },
        { "ip": "127.0.0.1", "port": 8002, "client_port": 7002, "id": 2, "leader": false },
        { "ip": "127.0.0.1", "port": 8003, "client_port": 7003, "id": 3, "leader": false }
    ]
}
```

---

## CDX-7: Update `.gitignore` if needed

Per `[HANDOFF: CT-5 decision]`, add `local_servers.json` to `Server/.gitignore` (or `Caerus/.gitignore`) if the live file should not be committed.

---

## Final Verification

```bash
# Normal mode unchanged
./build/main 1          # should work as before (reads servers.json)

# Local mode — missing file
rm -f local_servers.json
./build/main 1 local    # must print error + exit 1

# Local mode — file present  
cp local_servers.json.example local_servers.json   # (or it already exists)
./build/main 1 local    # must start, bind to 127.0.0.1:8001 peer, 127.0.0.1:7001 client

# Bad flag
./build/main 1 badarg   # must print usage + exit 1
```
