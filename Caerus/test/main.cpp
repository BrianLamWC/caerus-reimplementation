#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <iostream>
#include <string>
#include <sstream>
#include <map>
#include <set>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <iomanip>
#include <uuid/uuid.h>
#include <unistd.h>
#include <vector>
#include <thread>
#include <fstream>
#include <random>
#include "../Server/json.hpp"

#include "../proto/request.pb.h"
#include "../proto/graph_snapshot.pb.h"

using json = nlohmann::json;

std::atomic<int32_t> globalTransactionCounter{1};
std::mt19937 rng{std::random_device{}()};
std::atomic<uint64_t> sentTxnLogSeq{0};
std::mutex sentTxnLogMtx;
std::ofstream sentTxnLogFile;

struct DataItem
{
    std::string val;
    int32_t primaryCopyID;

    // (optional) convenience constructor
    DataItem(std::string v, int32_t p, std::string m = "")
        : val(std::move(v)), primaryCopyID(p) {} // use move beccause it just steals the buffer instead of allocating a new buffer

    // equality: all fields must match
    bool operator==(DataItem const &o) const noexcept
    {
        return val == o.val && primaryCopyID == o.primaryCopyID;
    }
};

std::unordered_map<std::string, DataItem> mockDB;

// Record for a transaction and its neighbors
struct TxnNeighbors
{
    std::string tx_id;
    std::set<std::string> out_neighbors;      // neighbor tx ids
    std::set<std::string> incoming_neighbors; // incoming neighbor tx ids

    bool operator<(TxnNeighbors const &o) const noexcept
    {
        return tx_id < o.tx_id;
    }
    // equality compares both id and neighbor set so that two records are equal
    // only when both tx_id and neighbors match
    bool operator==(TxnNeighbors const &o) const noexcept
    {
        return tx_id == o.tx_id && out_neighbors == o.out_neighbors;
    }
};

// Global map: server_id -> set of (txn id + neighbors)
std::map<int32_t, std::set<TxnNeighbors>> host_txn_neighbors_map;
// Global map: server_id -> merged order vector
std::map<int32_t, std::vector<TxnNeighbors>> host_merged_order_map;
// Global map: server_id -> merged order hash
std::map<int32_t, uint64_t> host_hash_map;

struct TxnSpec
{
    int target_id;
    request::Operation::OperationType type;
    std::vector<int> keys;
};

std::map<std::string, int> hostnames_to_id;
std::map<int, int> server_id_to_port;
std::map<int, std::string> server_id_to_ip;
const std::string servers_config_path = "../Server/servers.json";
const std::string local_servers_config_path = "../Server/local_servers.json";

void loadServersConfig(bool local_mode)
{
    const std::string &path = local_mode ? local_servers_config_path : servers_config_path;
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "loadServersConfig: error opening file " << path << "\n";
        exit(1);
    }

    json data = json::parse(file);
    auto servers = data["servers"];
    hostnames_to_id.clear();
    server_id_to_port.clear();
    server_id_to_ip.clear();

    for (const auto &server : servers)
    {
        std::string ip = server["ip"];
        int id = server["id"];
        int port = server.contains("client_port") ? (int)server["client_port"] : 7001;
        hostnames_to_id[ip] = id;
        server_id_to_port[id] = port;
        server_id_to_ip[id] = ip;
    }
    file.close();
}

void setupMockDB()
{

    std::ifstream file("../Server/data.json");

    if (!file.is_open())
    {
        std::cerr << "setupMockDB: error opening file" << std::endl;
        exit(1);
    }

    json data = json::parse(file);

    auto data_items = data["data_items"];

    for (auto data_item : data_items)
    {
        mockDB.insert({data_item["key"], {data_item["value"], (int32_t)data_item["primary_server_id"]}});
    }

    file.close();
}

int setupConnection(const char *host, int port)
{
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[6];
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res))
        return -1;
    int fd = -1;
    for (auto p = res; p; p = p->ai_next)
    {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

void connectToAllServers(std::map<int, int> &server_id_to_fd)
{
    for (const auto &pair : server_id_to_ip)
    {
        int server_id = pair.first;
        const std::string &ip = pair.second;
        auto pit = server_id_to_port.find(server_id);
        if (pit == server_id_to_port.end() || pit->second <= 0)
        {
            std::cerr << "Missing/invalid port for server_id " << server_id << "\n";
            continue;
        }
        int port = pit->second;
        int fd = setupConnection(ip.c_str(), port);
        if (fd < 0)
        {
            std::cerr << "Can't connect to " << ip << ":" << port << "\n";
            continue;
        }
        server_id_to_fd[server_id] = fd;
    }
}

bool writeNBytes(int fd, const void *buf, size_t n)
{
    const char *p = static_cast<const char *>(buf);
    size_t left = n;
    while (left)
    {
        ssize_t w = ::send(fd, p, left, MSG_NOSIGNAL);
        if (w <= 0)
            return false;
        left -= w;
        p += w;
    }
    return true;
}

bool readNBytes(int fd, void *buf, size_t n)
{
    char *p = static_cast<char *>(buf);
    size_t left = n;
    while (left)
    {
        ssize_t r = ::recv(fd, p, left, 0);
        if (r <= 0)
            return false;
        left -= r;
        p += r;
    }
    return true;
}

template <typename Msg>
bool recvProtoFramed(int fd, Msg &msg)
{
    uint32_t len_n;
    if (!readNBytes(fd, &len_n, sizeof(len_n)))
        return false;
    uint32_t len = ntohl(len_n);
    if (len == 0)
        return false;
    std::string buf;
    buf.resize(len);
    if (!readNBytes(fd, &buf[0], len))
        return false;
    return msg.ParseFromString(buf);
}

request::Request createRequest(const TxnSpec &spec)
{
    request::Request req;
    req.set_recipient(request::Request::BATCHER);
    req.set_client_id(getpid());
    req.set_target_server_id(spec.target_id);

    auto *t = req.add_transaction();
    t->set_id(std::to_string(globalTransactionCounter.fetch_add(1)));
    t->set_client_id(getpid());

    for (int k : spec.keys)
    {
        auto *op = t->add_operations();
        op->set_type(spec.type);
        op->set_key(std::to_string(k));
        if (spec.type == request::Operation::WRITE)
        {
            std::uniform_int_distribution<int> value_dist(1000, 9999);
            op->set_value(std::to_string(value_dist(rng)));
        }
    }

    return req;
}

bool sendProtoFramed(int fd, const google::protobuf::Message &msg)
{
    std::string bytes;
    if (!msg.SerializeToString(&bytes))
        return false;

    uint32_t n = static_cast<uint32_t>(bytes.size());
    uint32_t be = htonl(n); // 4-byte big-endian length prefix
    if (!writeNBytes(fd, &be, sizeof(be)))
        return false;
    if (!writeNBytes(fd, bytes.data(), bytes.size()))
        return false;
    return true;
}

std::vector<std::vector<TxnSpec>> parseJsonFile(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open file: " + filename);
    }

    nlohmann::json json;
    file >> json;

    std::vector<std::vector<TxnSpec>> batches;
    for (const auto &batch : json)
    {
        std::vector<TxnSpec> specs;
        for (const auto &item : batch)
        {
            TxnSpec spec;
            spec.target_id = item["target_id"];
            spec.type = item["type"] == "WRITE" ? request::Operation::WRITE : request::Operation::READ;
            spec.keys = item["keys"].get<std::vector<int>>();
            specs.push_back(spec);
        }
        batches.push_back(specs);
    }
    return batches;
}

void requestMergedOrderFromHost(const int server_id, const int fd);
void compareSnapshots();
void verfiyMergedOrderFromHost(int32_t server_id);
void requestMergedHashFromHost(const int server_id, const int fd);
void compareHashes();
void generateRandomTransactions(int num_txns, int max_ops_per_txn);

void initSentTxnLog(const std::string &path)
{
    std::lock_guard<std::mutex> guard(sentTxnLogMtx);
    if (sentTxnLogFile.is_open())
    {
        sentTxnLogFile.close();
    }
    sentTxnLogFile.open(path, std::ios::out | std::ios::trunc);
    if (!sentTxnLogFile.is_open())
    {
        std::cerr << "Failed to open sent transaction log file: " << path << "\n";
    }
}

void logSentTxnOneLine(const request::Request &req)
{
    if (req.transaction_size() <= 0)
    {
        return;
    }

    const auto &txn = req.transaction(0);
    const uint64_t seq = sentTxnLogSeq.fetch_add(1, std::memory_order_relaxed) + 1;
    std::ostringstream ops;
    for (int i = 0; i < txn.operations_size(); ++i)
    {
        const auto &op = txn.operations(i);
        if (i > 0)
        {
            ops << ",";
        }
        ops << (op.type() == request::Operation::WRITE ? "W" : "R") << ":" << op.key();
        if (op.type() == request::Operation::WRITE)
        {
            ops << "=" << op.value();
        }
    }

    std::lock_guard<std::mutex> guard(sentTxnLogMtx);
    if (!sentTxnLogFile.is_open())
    {
        return;
    }
    sentTxnLogFile << '#' << std::setw(8) << std::setfill('0') << seq
                   << " | tx=" << txn.id()
                   << " | target=" << req.target_server_id()
                   << " | ops=[" << ops.str() << "]\n";
    sentTxnLogFile.flush();
}

void handleCommand(const std::string &command)
{
    // fast hash-based consistency check
    if (command == "compare hashes")
    {
        host_hash_map.clear();

        std::map<int,int> server_id_to_fd;
        connectToAllServers(server_id_to_fd);

        for (const auto &p : server_id_to_fd)
        {
            if (p.second <= 0)
            {
                std::cerr << "Skipping server_id " << p.first << " due to invalid fd.\n";
                continue;
            }
            requestMergedHashFromHost(p.first, p.second);
        }

        compareHashes();
        return;
    }

    // get merged orders from all known servers
    if (command == "get merged")
    {

        host_txn_neighbors_map.clear();
        std::cout << "Cleared previously stored snapshots.\n";
        host_merged_order_map.clear();
        std::cout << "Cleared previously stored merged orders.\n";

        std::map<int,int> server_id_to_fd;
        connectToAllServers(server_id_to_fd);

        for (const auto &p : server_id_to_fd)
        {
            if (p.second <= 0)
            {
                std::cerr << "Skipping server_id " << p.first << " due to invalid fd.\n";
                continue;
            }

            requestMergedOrderFromHost(p.first, p.second);
        }

        compareSnapshots();

        for (const auto &p : server_id_to_ip)
        {
            verfiyMergedOrderFromHost(p.first);
        }

        return;
    }

    // send n number of random requests
    if (command.rfind("test ", 0) == 0 && command.size() > 5)
    {
        int n = std::stoi(command.substr(5));

        int max_ops_per_txn = 5; // default max operations per transaction

        generateRandomTransactions(n, max_ops_per_txn);
        return;
    }

    // send <filename> -> send requests from the given JSON file
    if (command.rfind("send ", 0) == 0)
    {
        std::string filename = command.substr(5);
        std::vector<std::vector<TxnSpec>> batches;
        try
        {
            batches = parseJsonFile(filename);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error: " << e.what() << "\n";
            return;
        }

        // Establish connections to all servers first
        std::map<int, int> server_id_to_fd;
        connectToAllServers(server_id_to_fd);

        // Send each batch in order
        for (size_t batch_idx = 0; batch_idx < batches.size(); ++batch_idx)
        {
            std::cout << "Sending batch " << batch_idx + 1 << "...\n";
            std::vector<request::Request> requests;
            for (const auto &spec : batches[batch_idx])
            {
                requests.push_back(createRequest(spec));
            }
            for (const auto &req : requests)
            {
                int sid = req.target_server_id();
                auto it = server_id_to_fd.find(sid);
                if (it == server_id_to_fd.end() || it->second <= 0)
                {
                    std::cerr << "No valid connection for server_id " << sid << "\n";
                    continue;
                }
                int fd = it->second;
                if (!sendProtoFramed(fd, req))
                {
                    std::cerr << "Send failed to server_id " << sid << "\n";
                }
                else
                {
                    std::cout << "  Sent txn " << req.transaction(0).id() << " to server " << sid << "\n";
                    logSentTxnOneLine(req);
                }
            }
        }

        // Close all connections
        for (auto &kv : server_id_to_fd)
        {
            if (kv.second >= 0)
                close(kv.second);
        }

        return;
    }

    std::cerr << "Unknown command: " << command << "\n";

    return;
}

void requestMergedOrderFromHost(const int server_id, const int fd)
{

    request::Request merge_req;
    merge_req.set_client_id(getpid());
    merge_req.set_recipient(request::Request::MERGED);

    if (!sendProtoFramed(fd, merge_req))
    {
        std::cerr << "Failed to MERGED_ORDER to " << server_id << "\n";
        close(fd);
        return;
    }

    request::GraphSnapshot merged_order_proto;
    if (!recvProtoFramed(fd, merged_order_proto))
    {
        std::cerr << "Failed to receive MergedOrder from " << server_id << "\n";
        close(fd);
        return;
    }

    std::cout << "Merged Order and Graph Snap from " << server_id << ": server_id=" << merged_order_proto.node_id() << "\n";
    // Populate global map for this host id with the snapshot contents.

    // Populate global host_txn_neighbors_map
    std::set<TxnNeighbors> tmp_set;
    for (int i = 0; i < merged_order_proto.adj_size(); ++i)
    {
        const auto &va = merged_order_proto.adj(i);
        TxnNeighbors rec;
        rec.tx_id = va.tx_id();
        for (int j = 0; j < va.out_size(); ++j)
            rec.out_neighbors.insert(va.out(j));
        tmp_set.insert(std::move(rec));
    }

    if (server_id != -1)
    {
        // single-threaded test program: directly assign without locking
        host_txn_neighbors_map[server_id] = std::move(tmp_set);
    }

    // populate global host_merged_order_map
    std::vector<TxnNeighbors> merged_order_vec;
    for (int i = 0; i < merged_order_proto.merged_order_size(); ++i)
    {
        const auto &va = merged_order_proto.merged_order(i);
        TxnNeighbors rec;
        rec.tx_id = va.tx_id();
        for (int j = 0; j < va.in_size(); ++j)
            rec.incoming_neighbors.insert(va.in(j));
        merged_order_vec.push_back(std::move(rec));
    }

    if (server_id != -1)
    {
        // single-threaded test program: directly assign without locking
        host_merged_order_map[server_id] = std::move(merged_order_vec);
    }

    // print merged order map to stdout
    if (server_id != -1)
    {
        const auto &stored = host_merged_order_map[server_id];
        std::cout << "Stored Merged Order for server_id=" << server_id << ": " << stored.size() << " txns, INCOMING NEIGHBORS\n";
        // for (const auto &rec : stored)
        // {
        //     std::cout << "  tx=" << rec.tx_id << " <- ";
        //     for (const auto &n : rec.incoming_neighbors)
        //         std::cout << " " << n;
        //     std::cout << "\n";
        // }
    }

    close(fd);
}

void requestMergedHashFromHost(const int server_id, const int fd)
{
    request::Request hash_req;
    hash_req.set_client_id(getpid());
    hash_req.set_recipient(request::Request::MERGED_HASH);

    if (!sendProtoFramed(fd, hash_req))
    {
        std::cerr << "Failed to send MERGED_HASH to " << server_id << "\n";
        close(fd);
        return;
    }

    request::MergedOrderHash hash_proto;
    if (!recvProtoFramed(fd, hash_proto))
    {
        std::cerr << "Failed to receive MergedOrderHash from " << server_id << "\n";
        close(fd);
        return;
    }

    std::cout << "Hash from server_id=" << server_id << ": " << hash_proto.hash() << "\n";

    if (server_id != -1)
        host_hash_map[server_id] = hash_proto.hash();

    close(fd);
}

void compareHashes()
{
    if (host_hash_map.size() < 2)
    {
        std::cout << "Need at least 2 servers to compare hashes.\n";
        return;
    }

    auto it = host_hash_map.begin();
    uint64_t ref_hash = it->second;
    int32_t ref_id = it->first;
    bool all_match = true;

    for (++it; it != host_hash_map.end(); ++it)
    {
        if (it->second != ref_hash)
        {
            std::cout << "HASH MISMATCH: server " << ref_id << " hash=" << ref_hash
                      << " vs server " << it->first << " hash=" << it->second << "\n";
            all_match = false;
        }
    }

    if (all_match)
        std::cout << "All hashes match.\n";
    else
        std::cout << "Hashes differ — run 'get merged' for full comparison.\n";
}

void compareSnapshots()
{
    if (host_txn_neighbors_map.size() < 2)
    {
        std::cout << "Not enough snapshots to compare (need >=2).\n";
        return;
    }

    auto it = host_txn_neighbors_map.begin();
    const auto host = it->first;
    const auto &set = it->second;
    ++it;

    bool all_equal = true;
    constexpr size_t kMaxDiffReport = 20;
    for (; it != host_txn_neighbors_map.end(); ++it)
    {
        int32_t other_host = it->first;
        const auto &other_set = it->second;
        if (!(set == other_set))
        {
            std::cout << "Snapshots differ: host " << host << " != host " << other_host << "\n";
            all_equal = false;

            std::unordered_map<std::string, const TxnNeighbors *> other_by_id;
            other_by_id.reserve(other_set.size());
            for (const auto &txn : other_set)
                other_by_id.emplace(txn.tx_id, &txn);

            // Print differences
            std::cout << "  Transactions only in host " << host << ":\n";
            size_t only_in_host = 0;
            for (const auto &txn : set)
            {
                if (other_by_id.find(txn.tx_id) == other_by_id.end())
                {
                    ++only_in_host;
                    if (only_in_host <= kMaxDiffReport)
                    {
                        std::cout << "    tx_id: " << txn.tx_id << "\n";
                        std::cout << "      out_neighbors: ";
                        for (const auto &n : txn.out_neighbors) std::cout << n << " ";
                        std::cout << "\n";
                    }
                }
            }
            if (only_in_host > kMaxDiffReport)
                std::cout << "    ... (" << only_in_host - kMaxDiffReport << " more)\n";

            std::cout << "  Transactions only in host " << other_host << ":\n";
            size_t only_in_other = 0;
            for (const auto &txn : other_set)
            {
                if (set.find(txn) == set.end())
                {
                    ++only_in_other;
                    if (only_in_other <= kMaxDiffReport)
                    {
                        std::cout << "    tx_id: " << txn.tx_id << "\n";
                        std::cout << "      out_neighbors: ";
                        for (const auto &n : txn.out_neighbors) std::cout << n << " ";
                        std::cout << "\n";
                    }
                }
            }
            if (only_in_other > kMaxDiffReport)
                std::cout << "    ... (" << only_in_other - kMaxDiffReport << " more)\n";

            // Print transactions with same tx_id but different neighbors
            std::cout << "  Transactions with same tx_id but different neighbors:\n";
            size_t different_neighbors = 0;
            for (const auto &txn : set)
            {
                auto it2 = other_by_id.find(txn.tx_id);
                if (it2 != other_by_id.end() && !(txn == *it2->second))
                {
                    ++different_neighbors;
                    if (different_neighbors <= kMaxDiffReport)
                    {
                        std::cout << "    tx_id: " << txn.tx_id << "\n";
                        std::cout << "      host " << host << " out_neighbors: ";
                        for (const auto &n : txn.out_neighbors) std::cout << n << " ";
                        std::cout << "\n";
                        std::cout << "      host " << other_host << " out_neighbors: ";
                        for (const auto &n : it2->second->out_neighbors) std::cout << n << " ";
                        std::cout << "\n";
                    }
                }
            }
            if (different_neighbors > kMaxDiffReport)
                std::cout << "    ... (" << different_neighbors - kMaxDiffReport << " more)\n";
        }
        else
        {
            std::cout << "Snapshots identical: host " << host << " == host " << other_host << "\n";
        }
    }

    if (all_equal)
        std::cout << "All snapshots are identical.\n";

    return;
}

void verfiyMergedOrderFromHost(int32_t server_id)
{
    auto mit = host_merged_order_map.find(server_id);
    if (mit == host_merged_order_map.end())
    {
        std::cerr << "No merged order stored for server_id " << server_id << "\n";
        return;
    }
    const auto &merged_order = mit->second;

    // verify that for each txn in merged order, its incoming neighbors has itself in their outgoing neighbors in the snapshot
    auto sit = host_txn_neighbors_map.find(server_id);
    if (sit == host_txn_neighbors_map.end())
    {
        std::cerr << "No snapshot stored for server_id " << server_id << "\n";
        return;
    }

    const auto &snapshot = sit->second;
    std::cout << "Verifying Merged Order against Snapshot for server_id=" << server_id << "...\n";

    for (const auto &txn : merged_order)
    {
        const std::string &tx_id = txn.tx_id;
        const auto &incoming_nbrs = txn.incoming_neighbors;

        for (const auto &in_nbr_id : incoming_nbrs)
        {
            // find in_nbr_id in snapshot
            auto it = snapshot.find(TxnNeighbors{in_nbr_id, {}});
            if (it == snapshot.end())
            {
                std::cerr << "  ERROR: Incoming neighbor txn " << in_nbr_id << " not found in snapshot for server_id " << server_id << "\n";
                continue;
            }
            const auto &in_nbr_txn = *it;
            // check if in_nbr_txn has tx_id in its outgoing neighbors
            if (in_nbr_txn.out_neighbors.find(tx_id) == in_nbr_txn.out_neighbors.end())
            {
                std::cerr << "  ERROR: Mismatch: txn " << in_nbr_id << " does not have outgoing edge to " << tx_id << " in snapshot for server_id " << server_id << "\n";
            }
        }
    }

    std::cout << "Verification completed for server_id=" << server_id << ".\n";
}

void generateRandomTransactions(int num_txns, int max_ops_per_txn)
{
    std::cout << "[DEBUG] generateRandomTransactions() called with num_txns=" << num_txns << ", max_ops_per_txn=" << max_ops_per_txn << "\n";
    std::cout << "[DEBUG] mockDB.size()=" << mockDB.size() << "\n";

    if (mockDB.empty())
    {
        std::cerr << "[ERROR] mockDB is empty! Cannot generate transactions.\n";
        return;
    }

    // Establish connections to all servers first
    std::map<int, int> server_id_to_fd;
    connectToAllServers(server_id_to_fd);

    std::uniform_int_distribution<int> key_dist(1, mockDB.size()); // pick keys from 1 to size of mockDB
    std::uniform_int_distribution<int> ops_dist(1, max_ops_per_txn); // number of operations per transaction
    std::uniform_int_distribution<int> type_dist(0, 1); // 0: READ, 1: WRITE

    for (int i = 0; i < num_txns; ++i)
    {
        int num_ops = ops_dist(rng);

        TxnSpec spec;
        for (int j = 0; j < num_ops; ++j)
        {
            spec.type = type_dist(rng) == 0 ? request::Operation::READ : request::Operation::WRITE;
            int random_key = key_dist(rng);
            spec.keys.push_back(random_key);
        }

        // randomly choose a key from spec.keys to determine target server
        if (spec.keys.empty())
        {
            std::cerr << "[ERROR] spec.keys is empty!\n";
            continue;
        }

        int key_for_server = spec.keys[rng() % spec.keys.size()];

        // check primary copy from mockDB
        auto db_it = mockDB.find(std::to_string(key_for_server));
        if (db_it != mockDB.end())
        {
            spec.target_id = db_it->second.primaryCopyID;
        }
        else
        {
            // abort if key not found
            std::cerr << "[ERROR] Key " << key_for_server << " not found in mockDB. Aborting transaction generation.\n";
            continue;
        }

        request::Request req = createRequest(spec);
        std::cout << "Generated txn " << req.transaction(0).id() << " for server " << spec.target_id << " with " << num_ops << " ops.\n";

        // Send the transaction
        int sid = req.target_server_id();
        auto it = server_id_to_fd.find(sid);
        if (it == server_id_to_fd.end() || it->second <= 0)
        {
            std::cerr << "No valid connection for server_id " << sid << "\n";
            continue;
        }
        int fd = it->second;
        if (!sendProtoFramed(fd, req))
        {
            std::cerr << "Send failed to server_id " << sid << "\n";
        }
        else
        {
            std::cout << "  Sent txn " << req.transaction(0).id() << " to server " << sid << "\n";
            logSentTxnOneLine(req);
        }
    }

    // Close all connections
    for (auto &kv : server_id_to_fd)
    {
        if (kv.second >= 0)
            close(kv.second);
    }

}
int main(int argc, char *argv[])
{
    bool local_mode = (argc == 2 && std::string(argv[1]) == "local");
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    std::string command;
    loadServersConfig(local_mode);
    setupMockDB();
    initSentTxnLog("./sent_transactions.log");
    while (true)
    {
        std::cout << "Enter command: ";
        std::getline(std::cin, command);
        if (command == "exit")
        {
            break;
        }
        handleCommand(command);
    }
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
