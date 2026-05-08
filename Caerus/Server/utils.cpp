#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <pthread.h>
#include <netdb.h>
#include <unistd.h>

#include "json.hpp"
#include "utils.h"

using json = nlohmann::json;

std::unordered_map<std::string, DataItem> mock_db;
std::unordered_map<std::string, DataItem> mock_db_logging;

int peer_port;
int32_t my_id;
std::vector<ServerInfo> servers;


std::string LEADER_IP;
int LEADER_PORT;
int LEADER_ID;

std::chrono::steady_clock::time_point LOGICAL_EPOCH;
std::atomic<bool> LOGICAL_EPOCH_READY{false};
bool LEADER = false;

std::mutex READY_MTX;
std::condition_variable READY_CV;
std::unordered_set<int> READY_SET;
int EXPECTED_SERVERS_COUNT;

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

void threadError(const char *msg)
{
    perror(msg);
    pthread_exit(NULL);  // Terminate only the calling thread
}

int setupConnection(const std::string &ip, int port)
{
 
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;           // IPv4 only
    hints.ai_socktype = SOCK_STREAM;       // TCP

    // Convert port to string
    char portStr[6];
    snprintf(portStr, sizeof(portStr), "%d", port);

    int err = getaddrinfo(ip.c_str(), portStr, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "setupConnection: getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }

    int connfd = -1;
    for (auto p = res; p; p = p->ai_next) {
        connfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (connfd < 0) continue;

        if (connect(connfd, p->ai_addr, p->ai_addrlen) == 0)
            break;  // success

        close(connfd);
        connfd = -1;
    }

    freeaddrinfo(res);

    if (connfd < 0) {
        // print the address of the peer we tried to connect to
        fprintf(stderr, "setupConnection: error connecting to %s:%d\n", ip.c_str(), port);
    }
    
    return connfd;

}

int setupListenfd(int my_port)
{

    int listenfd;
    struct sockaddr_in my_addr;

    // create listening socket
    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    int opt  = 1;

    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        error("setupListenfd: setsockopt failed");
    }

    if (listenfd < 0)
    {
        error("setupListenfd: error creating listening socket");
    }

    // configure server address structure
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(my_port);

    // bind listening socket to server address and port
    if (bind(listenfd, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0)
    {
        error("setupListenfd: error binding my address to listening socket");
    }

    // set listening socket to non blocking
    if (!setNonBlocking(listenfd))
    {
        error("setupListenfd: error setting socket to non-blocking mode");
    }

    return listenfd;
}

bool setNonBlocking(int listenfd)
{

    if (listenfd < 0)
    {
        error("setNonBlocking: error setting non blocking, invalid listening socket");
        return false;
    }

    int flags = fcntl(listenfd, F_GETFL, 0);

    if (flags == -1)
    {
        error("setNonBlocking: error getting file access mode and file status flags");
        return false;
    }

    if (fcntl(listenfd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        error("setNonBlocking: error setting file access mode and file status flags");
        return false;
    }

    return true;
}

void setupMockDB(){
    
    std::ifstream file(MOCKDB);

    if (!file.is_open())
    {
        error("getServers: error opening file");
        exit(1);
    }

    json data = json::parse(file);

    auto data_items = data["data_items"];

    for (auto data_item : data_items)
    {
        mock_db.insert({data_item["key"], {data_item["value"], (int32_t) data_item["primary_server_id"]} });
        mock_db_logging.insert({data_item["key"], {data_item["value"], (int32_t) data_item["primary_server_id"]} });
    }
    
    file.close();

}

void getServers(bool local_mode)
{
    const char* filename = local_mode ? LOCAL_SERVERLIST : SERVERLIST;
    std::ifstream file(filename);

    if (!file.is_open())
    {
        if (local_mode)
        {
            std::cerr << "Error: local_servers.json not found in current directory.\n"
                      << "Create it for local mode. See local_servers.json.example for the template.\n";
        }
        else
        {
            error("getServers: error opening file");
        }
        exit(1);
    }

    json data = json::parse(file);

    auto servers_list = data["servers"];

    for (auto& server : servers_list)
    {
        int cp = server.contains("client_port") ? (int)server["client_port"] : 7001;
        servers.push_back({server["ip"], server["port"], (int32_t) server["id"], false, (bool) server["leader"], cp});


        if ((bool)server["leader"] == true)
        {
            LEADER_IP = server["ip"];
            LEADER_PORT = server["port"];
            LEADER_ID = (int32_t) server["id"];


            if (my_id == LEADER_ID)
            {
                LEADER = true;
            }
             
        }

    }

    file.close();

}

std::vector<Operation> getOperationsFromProtoTransaction(const request::Transaction& txn_proto){

    std::vector<Operation> operations;

    for (const auto &op_proto : txn_proto.operations())
    {
        Operation operation;
        operation.type = (op_proto.type() == request::Operation::WRITE) ? OperationType::WRITE : OperationType::READ;
        operation.key = op_proto.key();

        if (op_proto.has_value() && operation.type == OperationType::WRITE)
        {
            operation.value = op_proto.value();
        }

        operations.push_back(operation);
    }

    return operations;
}

Pinger::Pinger(std::vector<ServerInfo>* servers, int num_servers, int my_port){

    args = {servers, num_servers, my_port};
    pthread_t pinger_thread;
    
    if (pthread_create(&pinger_thread, NULL, [](void* arg) -> void* {
            static_cast<Pinger*>(arg)->pingPeers();
            return nullptr;
        }, this) != 0)
    {
        threadError("error creating thread for pinging servers");
    }

    pthread_detach(pinger_thread);

}

void* Pinger::pingPeers()
{

    while (1)
    {

        // system("clear");

        for (auto& server : *args.servers)
        {
            if (server.port == args.my_port)
            {
                continue;
            }

            server.isOnline = Pinger::pingAPeer(server.ip, server.port);
            // printf("server %s:%d status: %d\n", server.ip.c_str(), server.port, server.isOnline);
        }

        sleep(5);
    }
    
    pthread_exit(NULL);

}

bool Pinger::pingAPeer(const std::string &ip, int port)   
{

    int connfd = setupConnection(ip, port);

    if (connfd < 0)
    {
        return false;
    }

    // create a Request message
    request::Request request;
    request.set_server_id(my_id);

    // Set the recipient
    request.set_recipient(request::Request::PING);

    // // Create the empty Transaction 
    // request::Transaction *transaction = request.add_transaction();

    // Serialize the Request message
    std::string serialized_request;
    if (!request.SerializeToString(&serialized_request))
    {
        error("error serializing request");
    }

    // Send serialized request
    int sent_bytes = write(connfd, serialized_request.c_str(), serialized_request.size());
    if (sent_bytes < 0)
    {
        error("error writing to socket");
    }

    close(connfd);

    return true;
}

// Read exactly n bytes or return –1 on error, 0 on EOF
ssize_t readNBytes(int fd, void *buf, size_t n) {
    char *p   = static_cast<char*>(buf);
    size_t left = n;
    while (left) {
        ssize_t r = ::read(fd, p, left);
        if (r < 0)  return -1;     // real error
        if (r == 0)  return 0;     // peer closed
        left -= r; p += r;
    }
    return n;
}

// Write exactly n bytes or return false on error
bool writeNBytes(int fd, const void *buf, size_t n) {
    const char *p = static_cast<const char*>(buf);
    size_t left = n;
    while (left) {
        ssize_t w = ::send(fd, p, left, MSG_NOSIGNAL);
        if (w <= 0) return false;
        left -= w; p += w;
    }
    return true;
}

Coordinator::Coordinator()
{

    if (LEADER)
    {
        
        printf("Coordinator: I am the leader, setting up the logical epoch.\n");

        EXPECTED_SERVERS_COUNT = (int)servers.size() - 1;

        std::unique_lock<std::mutex> lk(READY_MTX);
        READY_CV.wait(lk, [] {
            return (int)READY_SET.size() >= EXPECTED_SERVERS_COUNT;
        });

        for (auto& peer : servers) {
            printf("Coordinator: peer %s:%d\n", peer.ip.c_str(), peer.port);
            if (peer.id == my_id) continue;
            int connfd = setupConnection(peer.ip, peer.port);
            if (connfd < 0) {
                fprintf(stderr, "Coordinator: cannot connect to %s:%d\n",
                        peer.ip.c_str(), peer.port);
                continue;
            }

            request::Request start_msg;
            start_msg.set_recipient(request::Request::START);
            start_msg.set_server_id(my_id);

            std::string serialized_request;
            if (!start_msg.SerializeToString(&serialized_request)) {
                perror("SerializeToString failed");
                close(connfd);
                return;
            }

            uint32_t netlen = htonl(uint32_t(serialized_request.size()));
            if (!writeNBytes(connfd, &netlen, sizeof(netlen)) ||
                !writeNBytes(connfd, serialized_request.data(), serialized_request.size()))
            {
                perror("writeNBytes failed");
                // connection broken, force reconnect
                close(connfd);
                connfd = -1;
            }  

            close(connfd);
        }

        LOGICAL_EPOCH = std::chrono::steady_clock::now();
        LOGICAL_EPOCH_READY.store(true);

        printf("Coordinator: All peers ready → START broadcast. Epoch set.\n");

    }else{

        printf("Coordinator: I am not the leader, sending READY to leader %s:%d\n", LEADER_IP.c_str(), LEADER_PORT);
        
        while(!Coordinator::sendReadyToLeader(LEADER_IP, LEADER_PORT, my_id)){

            sleep(1);

        }

        

    }
    


}

bool Coordinator::sendReadyToLeader(const std::string &leader_ip, int leader_port, int my_id)
{
    printf("Coordinator: sending READY to leader %s:%d\n", leader_ip.c_str(), leader_port);

    int connfd = setupConnection(leader_ip, leader_port);
    if (connfd < 0) {
        fprintf(stderr, "sendReady: cannot connect to leader %s:%d\n",
                leader_ip.c_str(), leader_port);
        return false;
    }

    request::Request ready_msg;
    ready_msg.set_recipient(request::Request::READY);
    ready_msg.set_server_id(my_id);
    ready_msg.set_target_server_id(LEADER_ID);

    std::string serialized_request;
    if (!ready_msg.SerializeToString(&serialized_request)) {
        perror("SerializeToString failed");
        close(connfd);
        return false;
    }

    uint32_t netlen = htonl(uint32_t(serialized_request.size()));
    if (!writeNBytes(connfd, &netlen, sizeof(netlen)) ||
        !writeNBytes(connfd, serialized_request.data(), serialized_request.size()))
    {
        perror("writeNBytes failed");
        // connection broken, force reconnect
        close(connfd);
        return false;
    }    

    printf("Coordinator: READY sent to leader %s:%d\n", leader_ip.c_str(), leader_port);
    close(connfd);
    return true;
}
