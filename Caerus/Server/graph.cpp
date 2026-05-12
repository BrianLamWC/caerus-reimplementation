// graph.cpp

#include <iostream>
#include <queue>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

#include "graph.h"

namespace
{
constexpr const char *kGraphLogPath = "graph.log";
std::mutex graph_log_mtx;
std::atomic<uint64_t> graph_log_seq{0};
std::once_flag graph_log_init_once;

void appendGraphLog(const std::string &event, const std::string &details)
{
    const uint64_t seq = graph_log_seq.fetch_add(1, std::memory_order_relaxed) + 1;

    std::ostringstream line;
    line << '#' << std::setw(8) << std::setfill('0') << seq
         << " | " << event << " | " << details << '\n';

    std::lock_guard<std::mutex> guard(graph_log_mtx);
    std::call_once(graph_log_init_once, []() {
        std::ofstream reset(kGraphLogPath, std::ios::trunc);
    });
    std::ofstream out(kGraphLogPath, std::ios::app);
    if (out.is_open())
    {
        out << line.str();
    }
}
} // namespace

Transaction *Graph::addNode(std::unique_ptr<Transaction> uptr)
{
    const std::string &key = uptr->getID();
    Transaction *ptr = uptr.get();

    nodes[key] = std::move(uptr);

    {
        std::lock_guard<std::mutex> lock(snapshot_mtx);
        nodes_static[key] = std::make_unique<Transaction>(
            ptr->getOrder(),
            ptr->getServerId(),
            ptr->getOperations(),
            ptr->getID()
        );
    }

    //appendGraphLog("add_node", "tx=" + key + " size=" + std::to_string(nodes.size()));

    return ptr;
}

Transaction *Graph::getNode(const std::string &uuid)
{
    auto it = nodes.find(uuid);
    return it == nodes.end() ? nullptr : it->second.get();
}

void Graph::addNeighborOut(Transaction* from, Transaction* to) {
    from->addNeighborOut(to);

    // copy to static graph as well
    {
        std::lock_guard<std::mutex> lock(snapshot_mtx);
        auto it_from = nodes_static.find(from->getID());
        auto it_to = nodes_static.find(to->getID());
        if (it_from != nodes_static.end() && it_to != nodes_static.end()) {
            it_from->second->addNeighborOut(it_to->second.get()); 
        }
    }
    
    const auto &nbr = from->getOutNeighbors();
    appendGraphLog(
        "add_edge",
        "from=" + from->getID() + " to=" + to->getID() + " out_deg=" + std::to_string(nbr.size()));
}

void Graph::addNeighborOutStatic(const std::string& from_id, const std::string& to_id) {
    std::lock_guard<std::mutex> lock(snapshot_mtx);
    auto it_from = nodes_static.find(from_id);
    auto it_to   = nodes_static.find(to_id);
    if (it_from != nodes_static.end() && it_to != nodes_static.end()) {
        it_from->second->addNeighborOut(it_to->second.get());
    }
}

void Graph::printAll() const
{
    std::cout << "Graph contains " << nodes.size() << " node(s):\n";
    for (const auto &kv : nodes)
    {
        const Transaction *t = kv.second.get();
        // Basic info
        std::cout << "- ID: " << t->getID() << "\n";
        // Operations
        // const auto &ops = t->getOperations();
        // std::cout << "    Operations (" << ops.size() << "):\n";
        // for (size_t i = 0; i < ops.size(); ++i)
        // {
        //     const auto &op = ops[i];
        //     std::cout << "      [" << i << "] "
        //               << (op.type == OperationType::READ ? "READ" : "WRITE")
        //               << " key=" << op.key;
        //     if (op.type == OperationType::WRITE)
        //     {
        //         std::cout << ", value=" << op.value;
        //     }
        //     std::cout << "\n";
        // }
        // Neighbors
        const auto &nbr = t->getOutNeighbors();
        std::cout << "    Neighbors (" << nbr.size() << "):";
        for (auto *n : nbr)
        {
            std::cout << " " << n->getID();
        }
        std::cout << "\n\n";

        // Expected regions
        const auto &expected_regions = t->getExpectedRegions();
        std::cout << "    Expected regions (" << expected_regions.size() << "):";
        for (auto region : expected_regions)
        {
            std::cout << " " << region;
        }
        std::cout << "\n";

        // Seen regions
        const auto &seen_regions = t->getSeenRegions();
        std::cout << "    Seen regions (" << seen_regions.size() << "):";
        for (auto region : seen_regions)
        {
            std::cout << " " << region;
        }
        std::cout << "\n";
    }
}

void Graph::clear()
{
    nodes.clear();
    index_map.clear();
    low_link_map.clear();
    tarjan_stack = std::stack<Transaction *>{};
    on_stack.clear();
    sccs.clear();
    current_index = 0;
}

std::unique_ptr<Transaction> Graph::removeTransaction(Transaction *rem)
{
    auto it = nodes.find(rem->getID());
    if (it == nodes.end())
        return nullptr;

    // copy the incoming‐neighbor list and break those edges
    std::vector<Transaction *> preds(
        rem->getIncomingNeighbors().begin(),
        rem->getIncomingNeighbors().end());
    for (Transaction *pred : preds)
    {
        pred->removeOutNeighbor(rem);
        rem->addNeighborInID(pred->getID());
    }

    // copy the outgoing‐neighbor list and break those edges
    std::vector<Transaction *> succs(
        rem->getOutNeighbors().begin(),
        rem->getOutNeighbors().end());
    for (Transaction *succ : succs)
    {
        rem->removeOutNeighbor(succ);
    }

    // 3) now it’s safe to steal and erase
    auto removed = std::move(it->second);
    nodes.erase(it);

    // 4) remove from mrw or mrr maps
    for (const auto &op : removed->getOperations())
    {
        auto db_it = mock_db.find(op.key);

        if (db_it == mock_db.end())
        {
            std::cout << "REMOVE::ReadWriteSet: key " << op.key << " not found" << std::endl;
            continue;
        }

        auto data_item = db_it->second;

        if (op.type == OperationType::READ)
        {
            removeMostRecentReader(data_item, removed->getID());
        }
        else if (op.type == OperationType::WRITE)
        {
            removeMostRecentWriter(data_item);
        }
    }

    // print size of graph after removal
    //std::cout << "Graph size after removal: " << nodes.size() << " nodes remaining." << std::endl;

    // check if this transaction has actually been removed
    if (nodes.find(rem->getID()) != nodes.end())
    {
        std::cerr << "Error: Transaction " << rem->getID() << " was not removed from graph!" << std::endl;
    }
    else
    {
        //std::cout << "Transaction " << rem->getID() << " successfully removed from graph." << std::endl;
    }

    //printAll();

    return removed;
}

void Graph::strongConnect(Transaction *v)
{
    // 1. Set the depth index for v
    index_map[v] = current_index;
    low_link_map[v] = current_index;
    ++current_index;

    tarjan_stack.push(v);
    on_stack.insert(v);

    // 2. Consider successors of v
    for (Transaction *w : v->getOutNeighbors())
    {
        if (index_map.find(w) == index_map.end())
        {
            // w has not yet been visited; recurse on it
            strongConnect(w);
            low_link_map[v] = std::min(low_link_map[v], low_link_map[w]);
        }
        else if (on_stack.count(w))
        {
            // successor w is in stack and hence in the current SCC
            low_link_map[v] = std::min(low_link_map[v], index_map[w]);
        }
    }

    // 3. If v is a root node, pop the stack and generate an SCC
    if (low_link_map[v] == index_map[v])
    {
        std::vector<Transaction *> component;
        Transaction *w = nullptr;
        do
        {
            w = tarjan_stack.top();
            tarjan_stack.pop();
            on_stack.erase(w);
            component.push_back(w);
        } while (w != v);
        sccs.push_back(std::move(component));
    }
}

void Graph::findSCCs()
{
    // reset any old Tarjan state
    index_map.clear();
    low_link_map.clear();
    while (!tarjan_stack.empty())
        tarjan_stack.pop();
    on_stack.clear();
    sccs.clear();
    current_index = 0;

    // run Tarjan on every node
    for (auto &kv : nodes)
    {
        Transaction *v = kv.second.get();
        if (index_map.find(v) == index_map.end())
        {
            strongConnect(v);
        }
    }

    // if (sccs.size() > 0)
    // {
    //     // Print the SCCs
    //     std::cout << "Strongly Connected Components (SCCs):\n";
    //     for (size_t i = 0; i < sccs.size(); ++i)
    //     {
    //         std::cout << "Component " << i << ":";
    //         for (Transaction *t : sccs[i])
    //         {
    //             std::cout << " " << t->getID();
    //         }
    //         std::cout << "\n\n";
    //     }
    // }
}

void Graph::buildTransactionSCCMap()
{

    txn_scc_index_map.clear();

    for (int i = 0; i < (int)sccs.size(); ++i)
    {
        for (Transaction *txn_ptr : sccs[i])
        {
            txn_scc_index_map[txn_ptr] = i;
        }
    }

    // Print the mapping
    // std::cout << "Transaction to SCC mapping:\n";
    // for (const auto& pair : txn_scc_index_map) {
    //     std::cout << "Transaction ID: " << pair.first->getID()
    //               << " is in SCC: " << pair.second << "\n";
    // }
}

void Graph::buildCondensationGraph()
{
    // 1. Resize to one entry per SCC, and clear any old edges
    neighbors_out.assign(sccs.size(), {});
    neighbors_in.assign(sccs.size(), {});

    // 2. Walk *every* original edge u → v in your graph:
    for (auto &kv : nodes)
    {
        Transaction *u = kv.second.get();
        int cu = txn_scc_index_map[u]; // which SCC u belongs to

        for (Transaction *v : u->getOutNeighbors())
        {
            int cv = txn_scc_index_map[v]; // which SCC v belongs to

            // 3. If it crosses SCCs, record it once
            if (cu != cv)
            {
                // neighbors_out[cu].insert(cv).second is true only on the first insertion
                if (neighbors_out[cu].insert(cv).second)
                {
                    // record the reverse link too
                    neighbors_in[cv].push_back(cu);
                }
            }
        }
    }
}

bool Graph::isSCCComplete(const int &scc_index)
{
    // For SCC c to be a sink, no member may have an edge out to a txn in a different comp.
    // And for completeness, each txn must return true from isComplete().
    for (auto *T : sccs[scc_index])
    {
        // SCC‐sink check: scan every outgoing edge of T
        for (auto *nbr : T->getOutNeighbors())
        {
            if (txn_scc_index_map.at(nbr) != scc_index)
            {
                //std::cout << "SCC " << scc_index << " incomplete due to txn " << T->getID() << " having outgoing edge to txn " << nbr->getID() << ".\n";
                return false;
            }
        }

        // Ensure transaction has observed all expected regions.
        if (T->getSeenRegions() != T->getExpectedRegions())
        {
            //std::cout << "SCC " << scc_index << " incomplete due to txn " << T->getID() << " missing regions.\n";
            return false;
        }
    }

    return true;
}

int32_t Graph::getMergedOrders()
{
    // 1) SCC + condensation once
    findSCCs(); // one rep per SCC
    buildTransactionSCCMap();
    buildCondensationGraph();
    int sccs_count = sccs.size();

    // 2) compute out-degrees and seed ready queue
    std::vector<int> out_degrees(sccs_count);
    std::queue<int> Q;

    for (int c = 0; c < sccs_count; ++c)
    {
        out_degrees[c] = neighbors_out[c].size();
    }

    for (int c = 0; c < sccs_count; ++c)
    {
        if (out_degrees[c] == 0 && isSCCComplete(c))
        {
            Q.push(c);
        }
    }

    int32_t transaction_count = 0;

    while (!Q.empty())
    {
        int c = Q.front();
        Q.pop();
        auto &comp = sccs[c];

        if (comp.size() > 1)
        {

            std::sort(comp.begin(), comp.end(),
                      [&](auto *a, auto *b)
                      {
                          int32_t a_rand = a->getOrder();
                          int32_t b_rand = b->getOrder();

                          if (a_rand != b_rand)
                          {
                              return a_rand < b_rand;
                          }

                          return a->getServerId() < b->getServerId();
                      });
        }

        for (Transaction *T : comp)
        {
            if (auto up = removeTransaction(T))
            {
                // push the removed transaction into the merged order queue
                {
                    std::lock_guard<std::mutex> lock(snapshot_mtx);
                    merged.push(*up);
                }
                transaction_count++;
            }
        }

        // // Print every transaction id currently in the merged order queue
        // {
        //     std::lock_guard<std::mutex> lock(snapshot_mtx);
        //     auto snapshot = merged.snapshot();
        //     std::cout << "Merged order queue (" << snapshot.size() << ") ids:";
        //     for (const auto &m : snapshot)
        //     {
        //         std::cout << " " << m.getID();
        //     }
        //     std::cout << std::endl;
        // }

        for (int p : neighbors_in[c])
        {
            if (--out_degrees[p] == 0 && isSCCComplete(p))
            {
                Q.push(p);
            }
        }
    }

    return transaction_count;
}

void Graph::buildSnapshotProto(request::GraphSnapshot &snap) const
{
    std::lock_guard<std::mutex> lock(snapshot_mtx);
    snap.Clear();
    // Use the process/server id if available, fallback to PID
    snap.set_node_id(std::to_string(my_id));

    for (const auto &kv : nodes_static)
    {
        const std::string &tx_id = kv.first;
        Transaction *tx = kv.second.get();

        request::VertexAdj *va = snap.add_adj();
        va->set_tx_id(tx_id);

        for (Transaction *nbr : tx->getOutNeighbors())
        {
            va->add_out(nbr->getID());
        }
    }

    for (const auto &kv : merged.snapshot())
    {
        request::VertexAdj *va = snap.add_merged_order();
        va->set_tx_id(kv.getID());

        for (const std::string &nbr : kv.getIncomingNeighborIDs()) // use incoming neighbor IDs
        {
            va->add_in(nbr);
        }

    }

}

std::vector<Transaction*> Graph::getAllNodes() const
{
    std::vector<Transaction*> result;
    result.reserve(nodes.size());
    for (const auto& kv : nodes)
    {
        result.push_back(kv.second.get());
    }
    return result;
}

void Graph::addMostRecentWriter(DataItem item, Transaction* txn)
{
    // add or update the most recent writer for a data item

    // std::cout << "Graph::addMostRecentWriter: adding most recent writer for data item ("
    //           << item.val << ", " << item.primary_copy_id
    //           << ") to transaction " << txn->getID() << std::endl;

    most_recent_writer[item] = txn;

    // std::cout << "Graph::addMostRecentWriter: set most recent writer for data item ("
    //           << item.val << ", " << item.primary_copy_id
    //           << ") to transaction " << txn->getID() << std::endl;

}

void Graph::removeMostRecentWriter(DataItem item)
{
    // remove the data item from the most recent writer map
    most_recent_writer.erase(item);
    // std::cout << "Graph::removeMostRecentWriter: removed most recent writer for data item ("
    //           << item.val << ", " << item.primary_copy_id
    //           << ")\n";

}

std::string Graph::getMostRecentWriterID(DataItem item)
{
    auto it = most_recent_writer.find(item);
    if (it != most_recent_writer.end() && it->second != nullptr)
    {
        // std::cout << "Graph::getMostRecentWriterID: most recent writer for data item ("
        //           << item.val << ", " << item.primary_copy_id
        //           << ") is transaction " << it->second->getID() << std::endl;
        return it->second->getID();
    }
    // std::cout << "Graph::getMostRecentWriterID: no most recent writer for data item ("
    //           << item.val << ", " << item.primary_copy_id
    //           << ")\n";
    return ""; // return empty string if no writer found
}

void Graph::addMostRecentReader(DataItem item, const std::string& txn_id)
{
    // add the txn_id to the set of most recent readers for the data item if not already present
    // if set does not exist, create it
    
    most_recent_readers[item].emplace(txn_id);

    // std::cout << "Graph::addMostRecentReader: added transaction " << txn_id
    //           << " to most recent readers for data item ("
    //           << item.val << ", " << item.primary_copy_id
    //           << ")\n";

}

void Graph::removeMostRecentReader(DataItem item, const std::string& txn_id)
{
    // remove the txn_id from the set of most recent readers for the data item
    auto it = most_recent_readers.find(item);
    if (it != most_recent_readers.end())
    {
        it->second.erase(txn_id);
        // if the set becomes empty, remove the entry from the map
        if (it->second.empty())
        {
            most_recent_readers.erase(it);
        }
    }

    // std::cout << "Graph::removeMostRecentReader: removed transaction " << txn_id
    //           << " from most recent readers for data item ("
    //           << item.val << ", " << item.primary_copy_id
    //           << ")\n";

}

std::unordered_set<std::string> Graph::getMostRecentReadersIDs(DataItem item)
{
    auto it = most_recent_readers.find(item);
    if (it != most_recent_readers.end())
    {
        // std::cout << "Graph::getMostRecentReadersIDs: most recent readers for data item ("
        //           << item.val << ", " << item.primary_copy_id
        //           << ") are:";
        // for (const auto& reader_id : it->second)
        // {
        //     std::cout << " " << reader_id;
        // }
        // std::cout << std::endl;  

        return it->second;
    }
    return {}; // return empty set if no readers found
}

void Graph::clearMRRIds(DataItem item)
{
    most_recent_readers.erase(item);

    // std::cout << "Graph::clearMRRIds: cleared most recent readers for data item ("
    //           << item.val << ", " << item.primary_copy_id
    //           << ")\n";
}
