#include <chrono>
#include "partial_sequencer.h"
#include <netinet/in.h>
#include <thread>
#include <fstream>

namespace
{
    // compile-time constant for a 5s window
    constexpr std::chrono::milliseconds ROUND_PERIOD{50};

}

void PartialSequencer::processPartialSequence()
{
    while (!LOGICAL_EPOCH_READY.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    int64_t window = 0;
    while (true)
    {

        auto deadline = LOGICAL_EPOCH + std::chrono::milliseconds((window + 1) * ROUND_PERIOD.count());

        // sleep until that moment
        std::this_thread::sleep_until(deadline);

        // grab all requests for this window (may be empty)
        auto batch = batcher_to_partial_sequencer_queue.popAll();

        if (batch.empty())
        {
            // no transactions received in this window
            window++;
            continue;
        }

        partial_sequence_proto.Clear();
        partial_sequence_proto.set_server_id(my_id);
        partial_sequence_proto.set_recipient(request::Request::MERGER);
        partial_sequence_proto.set_round(static_cast<int32_t>(window));

        for (auto &req : batch)
        {
            // each req.transaction(0) is a local write for your primaries
            partial_sequence_proto.add_transaction()->CopyFrom(req.transaction(0));
        }

        // log if partial sequence is not empty
        if (partial_sequence_proto.transaction_size() > 0)
        {
            std::ofstream logf("partial_sequence_log_" + std::to_string(my_id) + ".log",
                               std::ios::app);
            if (logf)
            {
                for (int i = 0; i < partial_sequence_proto.transaction_size(); ++i)
                {
                    logf << "round=" << window
                         << " tx=" << partial_sequence_proto.transaction(i).id()
                         << " ops=" << partial_sequence_proto.transaction(i).operations_size()
                         << "\n";
                }
            }
        }

        {
            std::lock_guard<std::mutex> lk(partial_sequencer_to_merger_queue_mtx);
            partial_sequencer_to_merger_queue.push(partial_sequence_proto);
        } // unlock first

        partial_sequencer_to_merger_queue_cv.notify_one();

        // broadcast to other regions
        sendPartialSequence();

        window++;
    }
}

void PartialSequencer::sendPartialSequence()
{
    for (auto &target : target_peers)
    {
        int target_id = target.first;
        int &connfd = merger_fds[target_id];

        partial_sequence_proto.set_target_server_id(target_id);

        std::ofstream logf("partial_sequence_sent_" + std::to_string(my_id) + ".log", std::ios::app);
        if (logf)
        {
            for (const auto &txn : partial_sequence_proto.transaction())
            {
                logf << "to=" << target_id
                     << " round=" << partial_sequence_proto.round()
                     << " tx=" << txn.id()
                     << " ops=" << txn.operations_size()
                     << "\n";
            }
        }

        // (re)connect on-demand if we lost it
        if (connfd < 0)
        {

            ServerInfo target = target_peers[target_id];

            while ((connfd = setupConnection(target.ip, target.port)) < 0)
            {
                std::cerr << "Partial Sequencer " << my_id << ": reconnect to peer " << target_id << " failed, retrying in 1s…\n";
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            merger_fds[target_id] = connfd; // update the fd in the map
        }

        std::string serialized_request;
        if (!partial_sequence_proto.SerializeToString(&serialized_request))
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
}

void PartialSequencer::pushReceivedTransactionIntoPartialSequence(const request::Request &req_proto)
{
    std::ofstream logf("partial_sequencer_received_" + std::to_string(my_id) + ".log", std::ios::app);
    if (logf && req_proto.transaction_size() > 0)
    {
        logf << "round=" << req_proto.batcher_round()
             << " tx=" << req_proto.transaction(0).id()
             << " ops=" << req_proto.transaction(0).operations_size()
             << "\n";
    }

    // Push the transaction into the queue
    batcher_to_partial_sequencer_queue.push(req_proto);
}

PartialSequencer::PartialSequencer()
{

    std::ofstream init_log("partial_sequence_log_" + std::to_string(my_id) + ".log", std::ios::out | std::ios::trunc);
    std::ofstream init_recv_log("partial_sequencer_received_" + std::to_string(my_id) + ".log", std::ios::out | std::ios::trunc);
    std::ofstream init_sent_log("partial_sequence_sent_" + std::to_string(my_id) + ".log", std::ios::out | std::ios::trunc);

    if (pthread_create(&partial_sequencer_thread, NULL, [](void *arg) -> void *
                       {
            static_cast<PartialSequencer*>(arg)->PartialSequencer::processPartialSequence();
            return nullptr; }, this) != 0)
    {
        threadError("Error creating batcher thread");
    }

    pthread_detach(partial_sequencer_thread);

    for (auto &server : servers)
    {
        if (server.id != my_id)
        {
            target_peers[server.id] = server;
            merger_fds[server.id] = -1;
        }
    }
}
