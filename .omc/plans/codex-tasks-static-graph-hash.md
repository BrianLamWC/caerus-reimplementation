# Codex Tasks — static-graph-hash

All conventions and decisions are documented in handoff-static-graph-hash.md.
Start ONLY after Claude tasks are marked complete.

---

## Task D1: Add `STATIC_GRAPH_HASH = 9` to request.proto

**File:** `Caerus/proto/request.proto`

Find the `RequestRecipient` enum (currently ends at `MERGED_HASH = 8`) and append:
```proto
STATIC_GRAPH_HASH = 9;
```

After editing, regenerate the protobuf C++ files:
```bash
cd Caerus/proto
protoc --cpp_out=. request.proto graph_snapshot.proto
```

**Status:** [ ] pending

---

## Task D2: Declare `sendStaticGraphHashOnFd()` in merger.h

**File:** `Caerus/Server/merger.h`

In the public section, after line 70 (`void sendMergedHashOnFd(int fd);`), add:
```cpp
void sendStaticGraphHashOnFd(int fd);
```

**Status:** [ ] pending

---

## Task D3: Implement `sendStaticGraphHashOnFd()` in merger.cpp

**File:** `Caerus/Server/merger.cpp`

Add after `sendMergedHashOnFd()` (currently ends around line 404). Pattern is identical to `sendMergedHashOnFd()` except it calls `graph.computeStaticGraphHash()` instead of building the full snapshot:

```cpp
void Merger::sendStaticGraphHashOnFd(int fd)
{
    uint64_t hash_val = graph.computeStaticGraphHash();

    request::MergedOrderHash hash_msg;
    hash_msg.set_node_id(std::to_string(my_id));
    hash_msg.set_hash(hash_val);

    std::string payload;
    if (!hash_msg.SerializeToString(&payload))
    {
        std::cerr << "MERGER: failed to serialize StaticGraphHash\n";
        return;
    }

    uint32_t len = static_cast<uint32_t>(payload.size());
    uint32_t netlen = htonl(len);
    if (!writeNBytes(fd, &netlen, sizeof(netlen)))
    {
        std::cerr << "MERGER: failed to write static hash length to fd " << fd << "\n";
        return;
    }
    if (!writeNBytes(fd, payload.data(), payload.size()))
    {
        std::cerr << "MERGER: failed to write static hash payload to fd " << fd << "\n";
        return;
    }
}
```

**Status:** [ ] pending

---

## Task D4: Add `STATIC_GRAPH_HASH` dispatch in client.cpp

**File:** `Caerus/Server/client.cpp`

In `handleClient()`, after the `MERGED_HASH` else-if block (around line 158), add:
```cpp
else if (req_proto.recipient() == request::Request::STATIC_GRAPH_HASH)
{
    if (merger)
    {
        merger->sendStaticGraphHashOnFd(connfd);
        continue;
    }
    else
    {
        fprintf(stderr, "CLIENT_HANDLER: no merger available to serve STATIC_GRAPH_HASH\n");
        break;
    }
}
```

**Status:** [ ] pending

---

## Task D5: Add `compare static` command to test/main.cpp

**File:** `Caerus/test/main.cpp`

### 5a — Add global (after `host_hash_map` on line 79):
```cpp
std::map<int32_t, uint64_t> host_static_hash_map;
```

### 5b — Add forward declarations (after line 312, with the other forward decls):
```cpp
void requestStaticGraphHashFromHost(const int server_id, const int fd);
void compareStaticHashes();
```

### 5c — Add `compare static` branch in `handleCommand()` (after the `compare hashes` block, before `get merged`):
```cpp
if (command == "compare static")
{
    host_static_hash_map.clear();

    std::map<int,int> server_id_to_fd;
    connectToAllServers(server_id_to_fd);

    for (const auto &p : server_id_to_fd)
    {
        if (p.second <= 0)
        {
            std::cerr << "Skipping server_id " << p.first << " due to invalid fd.\n";
            continue;
        }
        requestStaticGraphHashFromHost(p.first, p.second);
    }

    compareStaticHashes();
    return;
}
```

### 5d — Add `requestStaticGraphHashFromHost()` (after `requestMergedHashFromHost()`, around line 602):
```cpp
void requestStaticGraphHashFromHost(const int server_id, const int fd)
{
    request::Request hash_req;
    hash_req.set_client_id(getpid());
    hash_req.set_recipient(request::Request::STATIC_GRAPH_HASH);

    if (!sendProtoFramed(fd, hash_req))
    {
        std::cerr << "Failed to send STATIC_GRAPH_HASH to " << server_id << "\n";
        close(fd);
        return;
    }

    request::MergedOrderHash hash_proto;
    if (!recvProtoFramed(fd, hash_proto))
    {
        std::cerr << "Failed to receive StaticGraphHash from " << server_id << "\n";
        close(fd);
        return;
    }

    std::cout << "Static graph hash from server_id=" << server_id << ": " << hash_proto.hash() << "\n";

    if (server_id != -1)
        host_static_hash_map[server_id] = hash_proto.hash();

    close(fd);
}
```

### 5e — Add `compareStaticHashes()` (after `compareHashes()`):
```cpp
void compareStaticHashes()
{
    if (host_static_hash_map.size() < 2)
    {
        std::cout << "Need at least 2 servers to compare static graph hashes.\n";
        return;
    }

    auto it = host_static_hash_map.begin();
    uint64_t ref_hash = it->second;
    int32_t ref_id = it->first;
    bool all_match = true;

    for (++it; it != host_static_hash_map.end(); ++it)
    {
        if (it->second != ref_hash)
        {
            std::cout << "STATIC GRAPH HASH MISMATCH: server " << ref_id << " hash=" << ref_hash
                      << " vs server " << it->first << " hash=" << it->second << "\n";
            all_match = false;
        }
    }

    if (all_match)
        std::cout << "All static graph hashes match.\n";
    else
        std::cout << "Static graphs differ — run 'get merged' for full comparison.\n";
}
```

**Status:** [ ] pending
