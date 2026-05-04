#ifndef BATCHER_H
#define BATCHER_H

#include <vector>
#include <cstdint>
#include <array>
#include <chrono>
#include <cstdio>
#include <random>
#include <mutex>
#include <condition_variable>
#include <random>
#include <limits>

#include "utils.h"
#include "transaction.h"
#include "queue_ts.h"
#include "../proto/request.pb.h"

class Batcher
{
private:

    int64_t current_window;
    std::vector<request::Request> batch;
    pthread_t batcher_thread;

    pthread_t sender_thread;
    QueueTS <request::Request> outbound_queue;
    std::mutex batch_mutex;
    std::condition_variable batch_cv;

    std::unordered_map<int, ServerInfo> target_peers;
    std::unordered_map<int,int> partial_sequencer_fds; // id to fd mapping for partial sequencer connections

    int32_t next_round{0};

public:

    Batcher();
    void batchRequests();
    void processBatch();
    void sendTransaction(request::Request& txn);
    void sendTransactions();

};

#endif // BATCHER_H
