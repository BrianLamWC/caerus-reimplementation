#include <unistd.h>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <stdlib.h>
#include <thread>
#include <arpa/inet.h>
#include <fstream>

#include "batcher.h"

namespace
{

    constexpr std::chrono::milliseconds ROUND_PERIOD{50};

    static thread_local std::mt19937_64 rng{std::random_device{}()};

    static thread_local std::uniform_int_distribution<int32_t> dist(
        std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::max());
}

void Batcher::batchRequests()
{
    // Wait until logical clock is ready
    while (!LOGICAL_EPOCH_READY.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    using Clock = std::chrono::high_resolution_clock;

    uint64_t total_txns = 0;
    //std::chrono::nanoseconds::rep ns_elapsed_time = 0;

    while (true)
    {
        auto now = std::chrono::steady_clock::now();
        auto since_0 = now - LOGICAL_EPOCH;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(since_0).count();

        current_window = elapsed_ms / ROUND_PERIOD.count();

        auto next_timestamp = LOGICAL_EPOCH + std::chrono::milliseconds((current_window + 1) * ROUND_PERIOD.count());

        // Pull all transactions from the request queue
        batch = request_queue.popAll();

        // Log the received transactions
        if (!batch.empty())
        {
            std::ofstream log_file("./batcher_logs/received_batch_" + std::to_string(my_id) + ".log", std::ios::app);
            if (log_file)
            {
                for (const auto &req : batch)
                {
                    log_file << "round=" << current_window
                             << " tx=" << req.transaction(0).id()
                             << " ops=" << req.transaction(0).operations_size()
                             << "\n";
                }
            }
            else
            {
                std::cerr << "Failed to open log file for batcher " << my_id << "\n";
            }
        }

        if (!batch.empty())
        {
            processBatch();
            total_txns += batch.size();
        }

        batch.clear();

        std::this_thread::sleep_until(next_timestamp);
    }
}

void Batcher::processBatch()
{

    std::vector<request::Request> batch_for_partial_sequencer;
    batch_for_partial_sequencer.reserve(batch.size());

    for (request::Request &req_proto : batch)
    {
        auto *txn = req_proto.mutable_transaction(0);

        txn->set_random_stamp(dist(rng));

        // figure out which servers to send this transaction to
        std::unordered_set<int32_t> target_peers;
        bool validTransaction = true;
        for (const auto &op : txn->operations())
        {
            auto it = mock_db.find(op.key());
            if (it == mock_db.end())
            {
                printf("  Key %s not found in the mock database\n", op.key().c_str());
                validTransaction = false;
                break;
            }

            const DataItem &data_item = it->second;
            target_peers.insert(data_item.primary_copy_id);
        }

        if (!validTransaction)
        {
            printf("BATCHER: Transaction %s for client %d is invalid, skipping\n",
                   txn->id().c_str(), txn->client_id());
            continue;
        }

        for (auto target_id : target_peers)
        {
            // clone the original request
            request::Request req = req_proto;
            req.set_recipient(request::Request::PARTIAL);
            req.set_server_id(my_id);
            req.set_target_server_id(target_id);
            req.set_batcher_round(current_window);

            if (target_id == my_id)
            {
                batch_for_partial_sequencer.push_back(req);
            }
            else
            {
                outbound_queue.push(req);
            }
        }
    }

    batch_cv.notify_all();

    if (!batch_for_partial_sequencer.empty()) // has to have at least one transaction because transactions are always sent to nodes that have the primary copy of one of the keys in
    {
        // Log the local pushes
        std::ofstream log_file("./batcher_logs/local_pushed_" + std::to_string(my_id) + ".log", std::ios::app);
        if (log_file)
        {
            for (const auto &req : batch_for_partial_sequencer)
            {
                log_file << "round=" << req.batcher_round()
                         << " tx=" << req.transaction(0).id()
                         << " ops=" << req.transaction(0).operations_size()
                         << "\n";
            }
        }
        else
        {
            std::cerr << "Failed to open log file for batcher " << my_id << "\n";
        }

        batcher_to_partial_sequencer_queue.pushAll(batch_for_partial_sequencer);
    }
}

void Batcher::sendTransaction(request::Request &req_proto) // send batch actually
{
    int target_id = req_proto.target_server_id();
    int &connfd = partial_sequencer_fds[target_id];

    // (re)connect on-demand if we lost it
    if (connfd < 0)
    {
        ServerInfo target = target_peers[target_id];

        while ((connfd = setupConnection(target.ip, target.port)) < 0)
        {
            std::cerr << "Batcher " << my_id << ": reconnect to peer " << target_id << " failed, retrying in 1s…\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        partial_sequencer_fds[target_id] = connfd; // update the fd in the map
    }

    req_proto.set_recipient(request::Request::PARTIAL);
    req_proto.set_server_id(my_id);

    // Log the transaction details
    std::ofstream log_file("./batcher_logs/sent_batches_" + std::to_string(my_id) + ".log", std::ios::app);
    if (log_file)
    {
        for (const auto &txn : req_proto.transaction())
        {
            log_file << "to=" << target_id
                     << " round=" << req_proto.batcher_round()
                     << " tx=" << txn.id()
                     << " ops=" << txn.operations_size()
                     << "\n";
        }
    }
    else
    {
        std::cerr << "Failed to open log file for batcher " << my_id << "\n";
    }

    std::string serialized_request;
    if (!req_proto.SerializeToString(&serialized_request))
    {
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
}

// Constructor
Batcher::Batcher()
{

    std::ofstream init_local_log("./batcher_logs/received_batch_" + std::to_string(my_id) + ".log", std::ios::out | std::ios::trunc);
    std::ofstream init_sent_log("./batcher_logs/local_pushed_" + std::to_string(my_id) + ".log", std::ios::out | std::ios::trunc);
    std::ofstream init_recv_log("./batcher_logs/sent_batches_" + std::to_string(my_id) + ".log", std::ios::out | std::ios::trunc);

    if (pthread_create(&sender_thread, NULL, [](void *arg) -> void *
                       {
            static_cast<Batcher*>(arg)->Batcher::sendTransactions();
            return nullptr; }, this) != 0)
    {
        threadError("Error creating sender thread");
    }

    pthread_detach(sender_thread);

    if (pthread_create(&batcher_thread, NULL, [](void *arg) -> void *
                       {
            static_cast<Batcher*>(arg)->Batcher::batchRequests();
            return nullptr; }, this) != 0)
    {
        threadError("Error creating batcher thread");
    }

    pthread_detach(batcher_thread);

    for (auto &server : servers)
    {
        if (server.id != my_id)
        {
            target_peers[server.id] = server;
            partial_sequencer_fds[server.id] = -1;
        }
    }

    printf("Batcher initialized with %zu target peers\n", target_peers.size());
}

void Batcher::sendTransactions()
{

    while (true)
    {
        std::unique_lock<std::mutex> lk(batch_mutex);
        batch_cv.wait(lk, [&]
                      { return !outbound_queue.empty(); });
        auto req = outbound_queue.pop(); // thread-safe pop
        lk.unlock();

        sendTransaction(req);
    }
}
