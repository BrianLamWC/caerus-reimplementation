# Claude Tasks — Local Mode (Judgment Decisions)

**Source spec:** `.omc/specs/deep-interview-local-mode.md`

Complete ALL of these, then write `.omc/plans/handoff.md` before Codex starts.

---

## CT-1: `getServers()` interface — how to pass the mode

Read `Server/utils.cpp` getServers() and all its call sites (`grep -rn "getServers" Server/`).

Choose the cleanest way to tell getServers() which file to load:
- A) `bool local_mode` parameter
- B) `const std::string& filename` parameter  
- C) Global `bool local_mode` read inside getServers()
- D) Two functions: `getServers()` and `getLocalServers()`

Decide and document in handoff.md.

---

## CT-2: `ServerInfo.client_port` default in normal mode

In normal mode, `servers.json` has no `client_port` field. What value should `ServerInfo.client_port` hold?
- A) `0` — unset sentinel
- B) `-1` — unset sentinel
- C) Always parse from JSON if present, default to `7001` if absent — field always valid

Check `grep -rn "client_port" Server/` to see if anything outside main.cpp reads it. Decide and document.

---

## CT-3: `local_servers.json` load path

Check the exact ifstream/fopen call in `getServers()`. Is `SERVERLIST` ("servers.json") opened relative to cwd or with any path prefix?

Decide whether `local_servers.json` uses the same convention (cwd-relative `#define LOCAL_SERVERLIST "local_servers.json"`) or something different. Document in handoff.md.

---

## CT-4: Error message text for missing `local_servers.json`

Write the exact `stderr` message string Codex will hardcode when the file is missing in local mode. Make it actionable (what to do to fix it).

---

## CT-5: Template file strategy

Decide:
1. Commit as `local_servers.json` (ready to use) or `local_servers.json.example` (safe template)?
2. Should `local_servers.json` (the live file) be added to `.gitignore`?
3. Confirm exact JSON content (IPs 127.0.0.1, ports 8001/8002/8003, client_ports 7001/7002/7003, ids 1/2/3, leader on id 1).
