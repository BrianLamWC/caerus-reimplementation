# AWS EC2 4-Node Deployment Plan (Cost-Minimized / Free-First)

## Goal
Run this project on 4 EC2 instances with this placement:
- Node 1 and Node 2 in Region A
- Node 3 in Region B
- Node 4 in Region C

Target outcome: keep monthly cost at `$0` whenever possible by running only short test windows and aggressively stopping resources.

## Important Cost Reality
A 4-instance cluster running 24/7 is generally not free on AWS.

To stay near `$0`, use a **scheduled test model**:
- Keep all instances `stopped` when not testing.
- Start all 4 only for test sessions.
- Stop all 4 immediately after tests.

Legacy-free-tier math (if your account has EC2 750 free instance-hours/month):
- 4 instances consume 4 instance-hours per wall-clock hour.
- Max cluster runtime per month: `750 / 4 = 187.5` hours.
- Approx safe daily runtime: `~6.25 hours/day` before other overhead.

If your account uses the newer credit-based free plan, treat runtime as even tighter and watch Billing/Free Tier usage in console daily.

## Proposed Region Layout
Use low-latency US regions unless you need geographic spread testing:
- Region A: `us-east-1` (Node 1 = leader, Node 2)
- Region B: `us-west-2` (Node 3)
- Region C: `us-central-1` (Node 4)

## Instance Type and Storage
- Instance type: free-eligible micro in your account plan.
- AMI: Ubuntu LTS (or Amazon Linux; Ubuntu matches your Dockerfile/tooling expectations).
- Root volume: small (for example 8 GiB gp3).
- Do not attach extra EBS volumes unless needed.

## Networking Plan
- Each node needs inbound TCP:
- `22` (SSH) from your IP only.
- `7001` (client listener) from trusted sources only.
- `8001` (peer listener) from the other node public IPs.

Use one security group per region and keep rules minimal. Update ingress rules after instances are created and public IPs are known.

## Codebase-Specific Configuration
This repo currently uses `Server/servers.json` for server topology and `Server/main.cpp` uses:
- peer port `8001`
- client port `7001`

You need 4 entries in `Server/servers.json` (one per node) with:
- public IP or reachable private IP
- port `8001`
- IDs `1..4`
- exactly one `leader: true`

Suggested ID mapping:
- Node 1 (us-east-1): id `1`, `leader: true`
- Node 2 (us-east-1): id `2`
- Node 3 (us-west-2): id `3`
- Node 4 (us-central-1): id `4`

Also update `Server/data.json` so key `primary_server_id` values are in `1..4`.

## Build and Run Steps Per Node
From repo root on each instance:

```bash
sudo apt-get update
sudo apt-get install -y build-essential make g++ libprotobuf-dev protobuf-compiler uuid-dev
cd /path/to/Caerus/Server
make clean && make
./build/main <node_id>
```

Run one process per node with corresponding ID.

## Session Workflow (Free-First)
1. Start all 4 instances.
2. Confirm instance public IPs.
3. Push updated `Server/servers.json` to all nodes.
4. Start Node 1, Node 2, Node 3, Node 4 server binaries.
5. Run client workload.
6. Collect logs/artifacts.
7. Stop all server processes.
8. Stop all 4 EC2 instances.

## Guardrails to Prevent Charges
- Create AWS Budgets alerts at `$1`, `$3`, `$5`.
- Enable Free Tier usage alerts.
- Avoid NAT Gateway, ALB/NLB, RDS, and other always-on managed services.
- Keep test traffic bounded; inter-region transfer is billable.
- Delete unused snapshots and unattached volumes.

## Optional Automation (Strongly Recommended)
Automate start/stop so idle runtime never leaks:
- Use EventBridge Scheduler + SSM Automation or Lambda.
- Schedule `start` before test window.
- Schedule `stop` right after test window.

## Risk Notes
- Public IPv4 addressing may incur charges depending on your free-tier/account model.
- Inter-region data transfer is generally not free.
- If you need continuous multi-region uptime, a non-zero bill is expected.

## Minimal Weekly Runtime Budget Example
Example target to reduce charge risk:
- 4 test sessions/week
- 2 hours/session
- Total: 32 instance-hours/week (`4 nodes * 2h * 4 sessions`)
- Approx monthly: 128 instance-hours

This is typically safer than long daily runs.

## Final Checklist
- Confirm your account free-tier model in Billing Console.
- Confirm 4-node `servers.json` is identical on all nodes.
- Confirm SG rules allow 8001 cross-node traffic.
- Confirm all nodes are stopped after each test.
