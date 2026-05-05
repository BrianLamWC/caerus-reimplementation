#ifndef UTILS_H
#define UTILS_H

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include <cstddef>
#include <atomic>
#include <chrono>
#include <condition_variable>

#include "transaction.h"
#include "../proto/request.pb.h"


#define SERVERLIST "servers.json"
#define LOCAL_SERVERLIST "local_servers.json"
#define MOCKDB "data.json"

struct ServerInfo
{
    std::string ip;
    int port;
    int32_t id;
    bool isOnline;
    bool isLeader;
    int client_port;
};

// PINGER THREAD

struct PingerThreadArgs
{
    std::vector<ServerInfo>* servers;
    int num_servers;
    int my_port;
};

class Pinger
{
private:
    PingerThreadArgs args;
public:
    Pinger(std::vector<ServerInfo>* servers, int num_servers, int my_port);
    void* pingPeers();
    bool pingAPeer(const std::string &ip, int port);
};


// MOCK DATABASE

struct DataItem
{
    std::string val;
    int32_t primary_copy_id;

    // (optional) convenience constructor
    DataItem(std::string v, int32_t p, std::string m = "")
        : val(std::move(v)), primary_copy_id(p) {} //use move beccause it just steals the buffer instead of allocating a new buffer

    // equality: all fields must match
    bool operator==(DataItem const &o) const noexcept
    {
        return val == o.val && primary_copy_id == o.primary_copy_id;
    }
};

extern std::unordered_map<std::string, DataItem> mock_db;
extern std::unordered_map<std::string, DataItem> mock_db_logging;

// SERVER ID

extern int peer_port;
extern int32_t my_id;

extern std::vector<ServerInfo> servers;

// HASH COMBINE FUNCTION

template <class T>
inline void hashCombine(std::size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v)
          + 0x9e3779b9
          + (seed << 6)
          + (seed >> 2);
}

// specialize std::hash for DataItem struct
namespace std
{
    template <>
    struct hash<DataItem>
    {
        size_t operator()(DataItem const &d) const noexcept
        {
            
            size_t h = 0;
            hashCombine(h, d.val);
            hashCombine(h, d.primary_copy_id);
            return h;

        }
    };

}

//LEADER INFO
extern std::string LEADER_IP;
extern int LEADER_PORT;
extern int32_t LEADER_ID; 

// steady clock
extern std::chrono::steady_clock::time_point LOGICAL_EPOCH;
extern std::atomic<bool> LOGICAL_EPOCH_READY;
extern bool LEADER;

// for the ready‐handshake
extern std::mutex READY_MTX;
extern std::condition_variable READY_CV;
extern std::unordered_set<int> READY_SET;   // which server IDs we’ve seen
extern int EXPECTED_SERVERS_COUNT;

class Coordinator
{
private:

public:
    Coordinator();
    bool sendReadyToLeader(const std::string& leader_ip, int leader_port, int my_id);
};


void error(const char *msg);
int setupListenfd(int my_port);
bool setNonBlocking(int listenfd);
void threadError(const char *msg);
int setupConnection(const std::string& ip, int port);
void setupMockDB();
void getServers(bool local_mode);
std::vector<Operation> getOperationsFromProtoTransaction(const request::Transaction& txn_proto);
ssize_t readNBytes(int fd, void *buf, size_t n);
bool writeNBytes(int fd, const void *buf, size_t n);
#endif
