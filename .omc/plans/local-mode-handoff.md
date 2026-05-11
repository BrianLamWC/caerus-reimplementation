# Handoff — Local Mode

All judgment tasks (CT-1 through CT-5) from `claude-tasks.md` are complete.
Codex may now begin `codex-tasks.md`. Every `[HANDOFF]` placeholder resolves below.

---

## CT-1: `getServers()` interface

**Decision: Add `bool local_mode` parameter.**

Signature changes to: `void getServers(bool local_mode)`

- Declaration in `utils.h:141`: `void getServers(bool local_mode);`
- Call site in `main.cpp:33`: `getServers(local_mode);` where `local_mode` is a bool set from argv[2]
- Inside `getServers()`: `if (local_mode)` opens `LOCAL_SERVERLIST`, else opens `SERVERLIST`

Rationale: single call site, no global state, no code duplication.

---

## CT-2: `ServerInfo.client_port` default

**Decision: Parse from JSON if present, default to `7001` if absent.**

In `getServers()`, when building each `ServerInfo`:
```cpp
int cp = server.contains("client_port") ? (int)server["client_port"] : 7001;
servers.push_back({server["ip"], server["port"], (int32_t)server["id"], false, (bool)server["leader"], cp});
```

`ServerInfo.client_port` is added as the last field in the struct. It always holds a valid port.
No other file reads `ServerInfo.client_port` except `main.cpp` in local mode.

---

## CT-3: `local_servers.json` load path

**Decision: cwd-relative, same convention as `servers.json`.**

Add to `utils.h` directly below `#define SERVERLIST`:
```cpp
#define LOCAL_SERVERLIST "local_servers.json"
```

`getServers(true)` opens `LOCAL_SERVERLIST` via `std::ifstream file(LOCAL_SERVERLIST)` —
identical pattern to how `SERVERLIST` is currently opened.

---

## CT-4: Error message for missing `local_servers.json`

**Decision: exact stderr message Codex must hardcode:**

```
Error: local_servers.json not found in current directory.
Create it for local mode. See local_servers.json.example for the template.
```

Print to `std::cerr`, then `exit(1)`. This replaces the generic `error()` call that would fire on missing file.

---

## CT-5: Template file strategy

**Decision:**
1. Commit the template as `Server/local_servers.json.example` (not `local_servers.json`) — safe to commit, won't be accidentally loaded since the code looks for exactly `local_servers.json`
2. Add `local_servers.json` (the live file) to `Caerus/.gitignore` — it's developer-local config
3. The `.example` file content:

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

## Summary Table

| Decision | Resolved To |
|----------|-------------|
| getServers() interface | `bool local_mode` parameter |
| ServerInfo.client_port default | Parse from JSON; default `7001` if absent |
| local_servers.json load path | cwd-relative (`#define LOCAL_SERVERLIST "local_servers.json"`) |
| Missing file error message | See CT-4 above |
| Template strategy | Commit `.example`; gitignore live `local_servers.json` |

*Handoff written: 2026-05-04. Claude tasks complete. Codex may now start `codex-tasks.md`.*
