#include "merger.h"
#include "utils.h"

#include "logger.h"

#include <arpa/inet.h>
#include <string>
#include <sstream>

void Merger::popFromQueue()
{
    while (true)
    {
        // wait on the local queue’s CV, then pop
        request::Request req_proto;
        {
            std::unique_lock<std::mutex> local_lock(partial_sequencer_to_merger_queue_mtx);
            partial_sequencer_to_merger_queue_cv.wait(local_lock, []
                                                      { return !partial_sequencer_to_merger_queue.empty(); });
            req_proto = partial_sequencer_to_merger_queue.pop();
        }

        processRequest(req_proto);
    }
}

void Merger::processRequest(const request::Request &req_proto)
{

    if (req_proto.transaction_size() > 0)
    {
        // Log the request
        std::ofstream logf("./merger_logs/merger_log" + std::to_string(my_id) + ".jsonl", std::ios::app);
        if (logf)
        {
            logf << "{\"server\":" << req_proto.server_id()
                 << ",\"round\":" << req_proto.round()
                 << ",\"txns\":[";
            for (int i = 0; i < req_proto.transaction_size(); ++i)
            {
                logf << "\"" << req_proto.transaction(i).id() << "\"";
                if (i + 1 < req_proto.transaction_size())
                {
                    logf << ",";
                }
            }
            logf << "]}\n";
        }
        else
        {
            std::cerr << "MERGER: failed to open log file ./merger_logs/merger_log.jsonl\n";
        }
    }

    const int sid = req_proto.server_id();
    auto it = partial_sequences.find(sid);

    if (it == partial_sequences.end())
    {
        // unknown server id
        std::cerr << "MERGER: Unknown server ID " << req_proto.server_id() << " in processRequest" << std::endl;
        return;
    }

    auto &q = it->second; // get the QueueTS<Transaction> for this server
    std::vector<Transaction> transactions;

    for (const auto &txn_proto : req_proto.transaction())
    {
        std::vector<Operation> operations = getOperationsFromProtoTransaction(txn_proto);

        Transaction txn(txn_proto.random_stamp(), req_proto.server_id(), operations, txn_proto.id());

        transactions.push_back(txn);

        // std::cout << "MERGER: pushed txn " << txn.getID() << " from server " << sid << " into its queue" << std::endl;
    }

    q->push(transactions);

    {
        std::lock_guard<std::mutex> g(ready_mtx); // lock ready queue mutex
        if (!enqueued_sids.count(sid))
        {
            enqueued_sids.insert(sid);
            ready_q.push_back(sid);

            ready_cv.notify_one();
        }
    }
}

void Merger::insertAlgorithm()
{
    std::unique_lock<std::mutex> lk(ready_mtx); // initialize lock for ready queue

    while (true)
    {
        ready_cv.wait(lk, [this]() { return !ready_q.empty(); }); // wait until there is work to do

        int sid = ready_q.front();
        ready_q.pop_front();
        enqueued_sids.erase(sid);

        lk.unlock(); // unlock ready queue mutex while working

        auto it = partial_sequences.find(sid);
        auto &inner_map = it->second;

        if (inner_map->empty())
        {
            lk.lock(); // lock before going back to
            continue;
        }

        auto transactions = inner_map->pop();

        std::unordered_set<DataItem> primary_set;

        // setup the primary set for current server
        for (const auto &txn : transactions)
        {
            for (const auto &op : txn.getOperations())
            {
                auto db_it = mock_db.find(op.key);

                if (db_it == mock_db.end())
                {
                    std::cout << "INSERT::PrimarySet: key " << op.key << " not found" << std::endl;
                    continue;
                }

                auto data_item = db_it->second;

                if (data_item.primary_copy_id == sid)
                {
                    primary_set.insert(data_item);
                }
            }
        }

        // process each transaction
        for (auto &txn : transactions)
        {
            // std::cout << "INSERT::Transaction: " << txn.getID() << std::endl;

            std::unordered_set<DataItem> write_set;
            std::unordered_set<DataItem> read_set;
            std::unordered_set<int32_t> expected_regions;

            // setup the read and write set for the current transaction
            for (const auto &op : txn.getOperations())
            {

                auto it = mock_db.find(op.key);

                if (it == mock_db.end())
                {
                    std::cout << "INSERT::ReadWriteSet: key " << op.key << " not found" << std::endl;
                    continue;
                }

                DataItem data_item = it->second;

                if (op.type == OperationType::WRITE)
                {
                    write_set.insert(data_item); // dont use nullptr, need to be valid pointer or just ""
                }
                else if (op.type == OperationType::READ)
                {
                    read_set.insert(data_item);
                }

                expected_regions.insert(data_item.primary_copy_id); // add primary copy id to expected regions

                // pritn read and write set
                // std::cout << "INSERT::ReadWriteSet: key " << op.key << " type " << (op.type == OperationType::READ ? "READ" : "WRITE") << std::endl;
            }

            auto curr_txn = graph.getNode(txn.getID());

            if (curr_txn == nullptr)
            {
                txn.setExpectedRegions(expected_regions);
                txn.addSeenRegion(sid);
                curr_txn = graph.addNode(std::make_unique<Transaction>(txn));
            }
            else
            {
                curr_txn->addSeenRegion(sid);
            }

            // Read Set ∩ Primary Set
            for (const auto &data_item : read_set)
            {
                if (primary_set.find(data_item) == primary_set.end())
                    continue;

                // std::cout << "INSERT::READSET:" << data_item.val << " is in read and primary set" << std::endl;

                auto mrw_id = graph.getMostRecentWriterID(data_item);
                auto mrw = graph.getNode(mrw_id);

                if (mrw_id.empty())
                { // no previous writer
                    // std::cout << "INSERT::READSET: no previous writer for data item ("
                    //           << data_item.val << ", " << data_item.primary_copy_id
                    //           << ")" << std::endl;
                    graph.addMostRecentReader(data_item, curr_txn->getID());
                }
                else if (mrw and mrw->getID() != curr_txn->getID())
                { // previous writer in graph
                    // std::cout << "INSERT::READSET: previous writer for data item ("
                    //           << data_item.val << ", " << data_item.primary_copy_id
                    //           << ") is transaction " << mrw_id << std::endl;
                    graph.addNeighborOut(curr_txn, mrw);
                    graph.addMostRecentReader(data_item, curr_txn->getID());
                }
                else if (!mrw)
                { // previous writer not in graph
                    //std::cout << "INSERT::READSET: previous writer " << mrw_id << " not in graph" << std::endl;
                }
            }

            // Write Set ∩ Primary Set
            for (const auto &data_item : write_set)
            {

                if (primary_set.find(data_item) == primary_set.end())
                    continue;

                auto mrw_id = graph.getMostRecentWriterID(data_item);
                auto mrw = mrw_id.empty() ? nullptr : graph.getNode(mrw_id);

                auto mrr_ids = graph.getMostRecentReadersIDs(data_item);

                if (mrw_id.empty())
                {
                    // true "no previous writer"
                    graph.addMostRecentWriter(data_item, curr_txn);
                    graph.clearMRRIds(data_item);
                    continue;
                }

                if (mrr_ids.empty())
                {
                    if (mrw != nullptr)
                        graph.addNeighborOut(curr_txn, mrw);
                }
                else
                {
                    for (const auto &reader_id : mrr_ids)
                    {
                        auto read_txn = graph.getNode(reader_id);
                        if (read_txn != nullptr)
                            graph.addNeighborOut(curr_txn, read_txn);
                    }
                }

                graph.addMostRecentWriter(data_item, curr_txn);
                graph.clearMRRIds(data_item);
            }
        }

        // If more arrived for this sid while we were working, re-enqueue it
        {
            std::lock_guard<std::mutex> g(ready_mtx);
            // If queue isn’t empty, schedule another turn for this sid.
            auto it2 = partial_sequences.find(sid);
            if (it2 != partial_sequences.end() && !it2->second->empty())
            {
                if (!enqueued_sids.count(sid))
                {
                    enqueued_sids.insert(sid);
                    ready_q.push_back(sid);
                    ready_cv.notify_one();
                }
            }
        }

        //graph.printAll();

        // call graph cleanup for merged orders and log if any removed
        {
            int removed = graph.getMergedOrders();
            if (removed > 0)
            {
                std::cout << "MERGER: removed " << removed << " node from graph" << std::endl;
            }
        }

        lk.lock(); // lock before going back to waiting, IMPORTANT needs to be locked before wait
    }
}

Merger::Merger()
{

    std::ofstream init_log("./merger_logs/merger_log" + std::to_string(my_id) + ".jsonl", std::ios::out | std::ios::trunc);
    // std::ofstream init_tp("throughput_log_" + std::to_string(my_id) + ".log", std::ios::out | std::ios::trunc);

    partial_sequences.reserve(servers.size());
    for (const auto &server : servers)
    {
        partial_sequences.emplace(server.id, std::make_unique<QueueTS<std::vector<Transaction>>>());
        expected_server_ids.push_back(server.id);
    }

    // Create a popper thread that calls the popFromQueue() method.
    if (pthread_create(&popper, nullptr, [](void *arg) -> void *
                       {
            static_cast<Merger*>(arg)->popFromQueue();
            return nullptr; }, this) != 0)
    {
        threadError("Error creating merger thread");
    }

    pthread_detach(popper);

    // Create an insert thread that calls the insertAlgorithm() method.
    if (pthread_create(&insert_thread, nullptr, [](void *arg) -> void *
                       {
            static_cast<Merger*>(arg)->insertAlgorithm();
            return nullptr; }, this) != 0)
    {
        threadError("Error creating insert thread");
    }

    pthread_detach(insert_thread);
}

void Merger::sendMergedOrdersOnFd(int fd)
{
    request::GraphSnapshot snap;

    // build snapshot (Graph::buildSnapshotProto is thread-safe and locks its own mutex)
    graph.buildSnapshotProto(snap);

    std::string payload;
    if (!snap.SerializeToString(&payload))
    {
        std::cerr << "MERGER: failed to serialize GraphSnapshot" << std::endl;
        return;
    }

    uint32_t len = static_cast<uint32_t>(payload.size());
    uint32_t netlen = htonl(len);

    // write length prefix
    if (!writeNBytes(fd, &netlen, sizeof(netlen)))
    {
        std::cerr << "MERGER: failed to write snapshot length to fd " << fd << std::endl;
        return;
    }

    // write payload
    if (!writeNBytes(fd, payload.data(), payload.size()))
    {
        std::cerr << "MERGER: failed to write snapshot payload to fd " << fd << std::endl;
        return;
    }
}
