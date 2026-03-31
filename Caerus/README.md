# Caerus — Partial Sequencer Reimplementation

A partial reimplementation of the Caerus distributed database system, based on the paper:

> **Caerus: A Midtier Database Caching System**  
> Khuzaima Daudjee et al.  
> https://cs.uwaterloo.ca/~kdaudjee/Daudjee-Caerus.pdf

**Status: Work in progress.** This reimplementation is far from complete and currently covers only the replication/sequencing logic described in the paper.

---

## What is Caerus?

Caerus is a distributed system that achieves causal ordering of transactions across multiple servers or regions. The core idea is that rather than requiring global coordination before every transaction, each server independently computes a *partial sequence* — a local ordering of transactions touching data it holds. These partial sequences are then broadcast to all peers and merged into a single agreed-upon global order.

This approach allows the system to remain available and low-latency while still converging on a consistent causal ordering across all nodes.

---

## What This Reimplementation Covers

This implementation focuses on the **replication and sequencing logic** from the paper:

- **Batching** — Incoming client transactions are collected in 50ms windows and routed to the appropriate servers based on which server holds the primary copy of each data item.
- **Partial Sequencing** — Each server sequences the transactions it directly processes and broadcasts its partial sequence to all peers.
- **Merging** — Partial sequences from all servers are merged into a global order by building a transaction dependency graph (edges represent read-write conflicts) and extracting a causal ordering via Tarjan's SCC algorithm.

---

## Architecture

```
Client
  |
  v (port 7001)
Batcher  ──────────────────────────────────────────────►  Remote Batchers (port 8001)
  |
  v
PartialSequencer  ─────────────────────────────────────►  Remote PartialSequencers (port 8001)
  |
  v
Merger
  |
  v
Graph  (dependency DAG + Tarjan SCC → merged order)
  |
  v
Client (GraphSnapshot: adjacency list + merged order)
```

Each node in the cluster runs all components. Peer-to-peer communication happens on port 8001; client communication on port 7001.

---

## Tech Stack

- **Language**: C++17
- **Networking**: Raw TCP sockets with 4-byte length-prefixed framing
- **Serialization**: Protocol Buffers v2
- **Concurrency**: POSIX pthreads, mutex/condition variables
- **Build**: Makefile + g++
- **Deployment**: Docker, AWS EC2 (multi-region)

---

## Deployment

The cluster is currently deployed on AWS EC2 across multiple regions. Each node runs the same Docker image built from the [Dockerfile](Dockerfile).

Cluster topology is configured in [Server/servers.json](Server/servers.json) and the mock data distribution (which server holds the primary copy of each key) is in [Server/data.json](Server/data.json).

See [AWS_EC2_FREE_TIER_4NODE_PLAN.md](AWS_EC2_FREE_TIER_4NODE_PLAN.md) for the deployment plan and cost management notes.

---

## Building Locally

```bash
cd Server
make

cd ../Client
make
```

Dependencies: `g++`, `libprotobuf-dev`, `protobuf-compiler`, `uuid-dev`

---

## Running Tests

Test transaction workloads are defined as JSON files under [test/](test/):

- `test/1.json` — Single-server scenario: reads and writes on the same key
- `test/2.json` — Multi-server scenario: cross-server reads and writes

Use the test client in [Client/running_example_test.cpp](Client/running_example_test.cpp) to send workloads and verify that all servers converge on the same merged order.

---

## Known Limitations

This is an early-stage research prototype. Notable gaps:

- No persistence — all state is in-memory and lost on restart
- No crash recovery or fault tolerance
- Does not implement the full Caerus paper — only the replication/sequencing layer
- Active bug in the queue coalescing logic
